#include "CoreMLYoloSession.h"

#import <Accelerate/Accelerate.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <mutex>
#include <stdexcept>

#if !__has_feature(objc_arc)
#error "CoreMLYoloSession.mm requires ARC"
#endif

namespace {

std::string toStd(NSError *error) {
  return error ? std::string(error.localizedDescription.UTF8String) : "unknown error";
}

MLComputeUnits parseComputeUnits(const std::string &name) {
  if (name == "CPUOnly") return MLComputeUnitsCPUOnly;
  if (name == "CPUAndGPU") return MLComputeUnitsCPUAndGPU;
  if (name == "CPUAndNeuralEngine") {
    if (@available(macOS 13.0, *)) return MLComputeUnitsCPUAndNeuralEngine;
  }
  return MLComputeUnitsAll;
}

// MLMultiArray (FP16 / FP32、ストライド付き) を連続した float32 の cv::Mat に写す。
// ANE の出力は行末がパディングされることがあるので、strides を見てコピーする。
cv::Mat toFloatMat(MLMultiArray *array, const void *bytes) {
  const int rank = static_cast<int>(array.shape.count);
  std::vector<int> sizes(rank);
  std::vector<NSInteger> strides(rank);
  for (int i = 0; i < rank; ++i) {
    sizes[i] = array.shape[i].intValue;
    strides[i] = array.strides[i].integerValue;
  }
  cv::Mat out(rank, sizes.data(), CV_32F);
  float *dst = reinterpret_cast<float *>(out.data);
  const int inner = sizes[rank - 1];
  const NSInteger innerStride = strides[rank - 1];
  const size_t outer = out.total() / inner;
  const bool half = array.dataType == MLMultiArrayDataTypeFloat16;
  if (!half && array.dataType != MLMultiArrayDataTypeFloat32)
    throw std::runtime_error("Unexpected Core ML output data type");

  std::vector<uint16_t> gathered;
  for (size_t row = 0; row < outer; ++row) {
    // row を (先頭 rank-1 次元の) 多次元インデックスに分解して要素オフセットを求める。
    NSInteger offset = 0;
    size_t rest = row;
    for (int d = rank - 2; d >= 0; --d) {
      offset += static_cast<NSInteger>(rest % sizes[d]) * strides[d];
      rest /= sizes[d];
    }
    float *dstRow = dst + row * inner;
    if (half) {
      const uint16_t *srcRow = reinterpret_cast<const uint16_t *>(bytes) + offset;
      if (innerStride != 1) {
        gathered.resize(inner);
        for (int i = 0; i < inner; ++i) gathered[i] = srcRow[i * innerStride];
        srcRow = gathered.data();
      }
      vImage_Buffer src = {const_cast<uint16_t *>(srcRow), 1,
                           static_cast<vImagePixelCount>(inner),
                           inner * sizeof(uint16_t)};
      vImage_Buffer dstBuf = {dstRow, 1, static_cast<vImagePixelCount>(inner),
                              inner * sizeof(float)};
      vImageConvert_Planar16FtoPlanarF(&src, &dstBuf, kvImageNoFlags);
    } else {
      const float *srcRow = reinterpret_cast<const float *>(bytes) + offset;
      for (int i = 0; i < inner; ++i) dstRow[i] = srcRow[i * innerStride];
    }
  }
  return out;
}

} // namespace

struct CoreMLYoloSession::Impl {
  MLModel *model = nil;
  NSString *inputName = nil;
  NSString *detectionName = nil; // [1, C, N]
  NSString *protoName = nil;     // [1, 32, H, W]
  int inputSize = 0;
  CVPixelBufferPoolRef pool = nullptr;
  std::mutex predictMutex;

  ~Impl() {
    if (pool) CVPixelBufferPoolRelease(pool);
  }
};

CoreMLYoloSession::CoreMLYoloSession(const std::string &modelPath, int inputSize,
                                     const std::string &computeUnits)
    : impl(std::make_unique<Impl>()) {
  @autoreleasepool {
    impl->inputSize = inputSize;
    NSURL *url = [NSURL fileURLWithPath:@(modelPath.c_str())];
    NSError *error = nil;

    // .mlmodelc (コンパイル済み) をそのまま読む。.mlpackage / .mlmodel なら
    // 起動時にコンパイルする（1秒前後かかる）。
    if (![url.pathExtension isEqualToString:@"mlmodelc"]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      url = [MLModel compileModelAtURL:url error:&error];
#pragma clang diagnostic pop
      if (!url) throw std::runtime_error("Core ML compile failed: " + toStd(error));
    }

    MLModelConfiguration *config = [[MLModelConfiguration alloc] init];
    config.computeUnits = parseComputeUnits(computeUnits);
    impl->model = [MLModel modelWithContentsOfURL:url configuration:config error:&error];
    if (!impl->model) throw std::runtime_error("Core ML load failed: " + toStd(error));

    MLModelDescription *desc = impl->model.modelDescription;
    if (desc.inputDescriptionsByName.count != 1)
      throw std::runtime_error("Expected one Core ML YOLO input");
    MLFeatureDescription *input = desc.inputDescriptionsByName.allValues.firstObject;
    if (input.type != MLFeatureTypeImage)
      throw std::runtime_error("Core ML YOLO input must be an image");
    MLImageConstraint *image = input.imageConstraint;
    if (static_cast<int>(image.pixelsWide) != inputSize ||
        static_cast<int>(image.pixelsHigh) != inputSize)
      throw std::runtime_error("Core ML YOLO input size does not match " +
                               std::to_string(inputSize));
    impl->inputName = input.name;

    for (NSString *name in desc.outputDescriptionsByName) {
      MLFeatureDescription *output = desc.outputDescriptionsByName[name];
      if (output.type != MLFeatureTypeMultiArray) continue;
      const NSUInteger rank = output.multiArrayConstraint.shape.count;
      if (rank == 3) impl->detectionName = name;
      else if (rank == 4) impl->protoName = name;
    }
    if (!impl->detectionName || !impl->protoName)
      throw std::runtime_error("Core ML YOLO outputs must be [1,C,N] and [1,32,H,W]");

    NSDictionary *attrs = @{
      (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (id)kCVPixelBufferWidthKey : @(inputSize),
      (id)kCVPixelBufferHeightKey : @(inputSize),
      (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
    };
    if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr,
                                (__bridge CFDictionaryRef)attrs, &impl->pool) !=
        kCVReturnSuccess)
      throw std::runtime_error("CVPixelBufferPool creation failed");
  }
}

CoreMLYoloSession::~CoreMLYoloSession() = default;

std::vector<cv::Mat> CoreMLYoloSession::run(const cv::Mat &rgb) {
  if (rgb.type() != CV_8UC3 || rgb.cols != impl->inputSize || rgb.rows != impl->inputSize)
    throw std::runtime_error("Unexpected Core ML YOLO input image");

  @autoreleasepool {
    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, impl->pool, &buffer) !=
        kCVReturnSuccess)
      throw std::runtime_error("CVPixelBuffer allocation failed");

    CVPixelBufferLockBaseAddress(buffer, 0);
    cv::Mat bgra(impl->inputSize, impl->inputSize, CV_8UC4,
                 CVPixelBufferGetBaseAddress(buffer),
                 CVPixelBufferGetBytesPerRow(buffer));
    cv::cvtColor(rgb, bgra, cv::COLOR_RGB2BGRA);
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    NSError *error = nil;
    MLDictionaryFeatureProvider *features = [[MLDictionaryFeatureProvider alloc]
        initWithDictionary:@{impl->inputName : [MLFeatureValue featureValueWithPixelBuffer:buffer]}
                     error:&error];
    CVPixelBufferRelease(buffer);
    if (!features) throw std::runtime_error("Core ML input failed: " + toStd(error));

    id<MLFeatureProvider> result = nil;
    {
      std::lock_guard<std::mutex> lock(impl->predictMutex);
      result = [impl->model predictionFromFeatures:features error:&error];
    }
    if (!result) throw std::runtime_error("Core ML prediction failed: " + toStd(error));

    std::vector<cv::Mat> outs;
    for (NSString *name in @[impl->detectionName, impl->protoName]) {
      MLMultiArray *array = [result featureValueForName:name].multiArrayValue;
      if (!array) throw std::runtime_error("Core ML output is missing");
      __block cv::Mat mat;
      __block std::string failure;
      auto convert = ^(const void *bytes) {
        try {
          mat = toFloatMat(array, bytes);
        } catch (const std::exception &e) {
          failure = e.what();
        }
      };
      if (@available(macOS 12.3, *)) {
        [array getBytesWithHandler:^(const void *bytes, NSInteger) { convert(bytes); }];
      } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        convert(array.dataPointer);
#pragma clang diagnostic pop
      }
      if (!failure.empty()) throw std::runtime_error(failure);
      outs.push_back(mat);
    }
    return outs;
  }
}
