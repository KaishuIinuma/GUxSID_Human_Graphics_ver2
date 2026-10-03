#include "PersonSegmenter.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <stdexcept>
#include "../third_party/onnxruntime/include/onnxruntime_c_api.h"

// The official macOS arm64 runtime is loaded from data so both the OF make
// build and the Xcode app bundle use the same, self-contained CoreML backend.
class OrtYoloSession {
public:
  OrtYoloSession(const std::string &modelPath, int inputSize) {
    try {
      const std::string libraryPath = ofToDataPath(
          "onnxruntime/libonnxruntime.1.30.0.dylib", true);
      library = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!library) throw std::runtime_error(dlerror());
      auto getApiBase = reinterpret_cast<const OrtApiBase *(*)()>(
          dlsym(library, "OrtGetApiBase"));
      if (!getApiBase) throw std::runtime_error("OrtGetApiBase is missing");
      api = getApiBase()->GetApi(ORT_API_VERSION);
      if (!api) throw std::runtime_error("ONNX Runtime API version mismatch");

      check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "GUxSID", &env));
      OrtSessionOptions *options = nullptr;
      check(api->CreateSessionOptions(&options));
      try {
        check(api->AddSessionConfigEntry(options,
              "session.disable_cpu_ep_fallback", "1"));
        const std::filesystem::path cachePath =
            std::filesystem::path(ofFilePath::getUserHomeDir()) /
            "Library/Caches/GUxSID_Human_Graphics_ver2/CoreML";
        std::filesystem::create_directories(cachePath);
        const std::string cache = cachePath.string();
        std::vector<const char *> keys = {"ModelFormat", "MLComputeUnits",
                                          "RequireStaticInputShapes", "ModelCacheDirectory"};
        std::vector<const char *> values = {"MLProgram", "CPUAndGPU", "1",
                                            cache.c_str()};
        // Opt in when validating operation placement on a particular Mac.
        if (const char *profile = std::getenv("GUXSID_COREML_PROFILE");
            profile && std::string(profile) == "1") {
          keys.push_back("ProfileComputePlan");
          values.push_back("1");
        }
        check(api->SessionOptionsAppendExecutionProvider(options,
              "CoreML", keys.data(), values.data(), keys.size()));
        check(api->CreateSession(env, modelPath.c_str(), options, &session));
      } catch (...) {
        api->ReleaseSessionOptions(options);
        throw;
      }
      api->ReleaseSessionOptions(options);

      OrtAllocator *allocator = nullptr;
      check(api->GetAllocatorWithDefaultOptions(&allocator));
      size_t inputCount = 0, outputCount = 0;
      check(api->SessionGetInputCount(session, &inputCount));
      check(api->SessionGetOutputCount(session, &outputCount));
      if (inputCount != 1 || outputCount != 2)
        throw std::runtime_error("Expected one YOLO input and two outputs");
      char *name = nullptr;
      check(api->SessionGetInputName(session, 0, allocator, &name));
      inputName = name;
      api->AllocatorFree(allocator, name);
      for (size_t i = 0; i < outputCount; ++i) {
        name = nullptr;
        check(api->SessionGetOutputName(session, i, allocator, &name));
        outputNames.emplace_back(name);
        api->AllocatorFree(allocator, name);
      }
      check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                     &memoryInfo));
      expectedInputSize = inputSize;
    } catch (...) {
      release();
      throw;
    }
  }

  ~OrtYoloSession() { release(); }

  std::vector<cv::Mat> run(cv::Mat &blob) {
    if (blob.type() != CV_32F || !blob.isContinuous() || blob.dims != 4 ||
        blob.size[0] != 1 || blob.size[1] != 3 ||
        blob.size[2] != expectedInputSize || blob.size[3] != expectedInputSize)
      throw std::runtime_error("Unexpected YOLO input tensor");
    const int64_t shape[] = {1, 3, expectedInputSize, expectedInputSize};
    OrtValue *input = nullptr;
    check(api->CreateTensorWithDataAsOrtValue(memoryInfo, blob.data,
          blob.total() * blob.elemSize(), shape, 4,
          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
    OrtValue *outputs[2] = {nullptr, nullptr};
    try {
      const char *inputNames[] = {inputName.c_str()};
      const OrtValue *inputs[] = {input};
      const char *names[] = {outputNames[0].c_str(), outputNames[1].c_str()};
      check(api->Run(session, nullptr, inputNames, inputs, 1, names, 2,
                     outputs));
      std::vector<cv::Mat> result;
      for (auto *output : outputs) {
        OrtTensorTypeAndShapeInfo *info = nullptr;
        check(api->GetTensorTypeAndShape(output, &info));
        try {
          ONNXTensorElementDataType type;
          size_t count = 0;
          check(api->GetTensorElementType(info, &type));
          check(api->GetDimensionsCount(info, &count));
          if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || count < 3 || count > 4)
            throw std::runtime_error("Unexpected YOLO output tensor type");
          std::vector<int64_t> dimensions(count);
          check(api->GetDimensions(info, dimensions.data(), count));
          std::vector<int> cvDimensions;
          for (auto dimension : dimensions) {
            if (dimension <= 0 || dimension > INT_MAX)
              throw std::runtime_error("Invalid YOLO output dimension");
            cvDimensions.push_back(static_cast<int>(dimension));
          }
          void *data = nullptr;
          check(api->GetTensorMutableData(output, &data));
          result.push_back(cv::Mat(static_cast<int>(count), cvDimensions.data(),
                                   CV_32F, data).clone());
        } catch (...) {
          api->ReleaseTensorTypeAndShapeInfo(info);
          throw;
        }
        api->ReleaseTensorTypeAndShapeInfo(info);
      }
      for (auto *output : outputs) api->ReleaseValue(output);
      api->ReleaseValue(input);
      return result;
    } catch (...) {
      for (auto *output : outputs) if (output) api->ReleaseValue(output);
      api->ReleaseValue(input);
      throw;
    }
  }

private:
  void check(OrtStatus *status) const {
    if (!status) return;
    const std::string message = api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    throw std::runtime_error(message);
  }
  void release() {
    if (api) {
      if (memoryInfo) api->ReleaseMemoryInfo(memoryInfo);
      if (session) api->ReleaseSession(session);
      if (env) api->ReleaseEnv(env);
    }
    if (library) dlclose(library);
  }
  void *library = nullptr;
  const OrtApi *api = nullptr;
  OrtEnv *env = nullptr;
  OrtSession *session = nullptr;
  OrtMemoryInfo *memoryInfo = nullptr;
  std::string inputName;
  std::vector<std::string> outputNames;
  int expectedInputSize = 0;
};

PersonSegmenter::PersonSegmenter() = default;
PersonSegmenter::~PersonSegmenter() = default;

//--------------------------------------------------------------
bool PersonSegmenter::loadModel(const std::string &modelPath, int inputSizeArg) {
  inputSize = inputSizeArg;
  try {
    ortSession = std::make_unique<OrtYoloSession>(modelPath, inputSize);
    loaded = true;
    ofLogNotice("PersonSegmenter") << "YOLO CoreML MLProgram (GPU) loaded: " << modelPath;
  } catch (const std::exception &e) {
    ofLogError("PersonSegmenter") << "YOLO CoreML load failed: " << e.what();
    ortSession.reset();
    loaded = false;
  }
  return loaded;
}

//--------------------------------------------------------------
cv::Mat PersonSegmenter::letterbox(const cv::Mat &src, float &scale, int &padX, int &padY) const {
  const int srcW = src.cols;
  const int srcH = src.rows;

  scale = std::min(static_cast<float>(inputSize) / srcW, static_cast<float>(inputSize) / srcH);

  const int newW = std::max(1, static_cast<int>(std::round(srcW * scale)));
  const int newH = std::max(1, static_cast<int>(std::round(srcH * scale)));

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(newW, newH));

  padX = (inputSize - newW) / 2;
  padY = (inputSize - newH) / 2;

  // YOLOの慣例に合わせてグレー(114,114,114)でパディングする
  cv::Mat out(inputSize, inputSize, src.type(), cv::Scalar(114, 114, 114));
  resized.copyTo(out(cv::Rect(padX, padY, newW, newH)));
  return out;
}

//--------------------------------------------------------------
HumanContourData PersonSegmenter::detect(const cv::Mat &rgbFrame, int outputWidth, int outputHeight) {
  HumanContourData result;

  if (!isLoaded() || rgbFrame.empty() || outputWidth <= 0 || outputHeight <= 0) {
    return result;
  }

  const int srcW = rgbFrame.cols;
  const int srcH = rgbFrame.rows;

  float scale = 1.0f;
  int padX = 0, padY = 0;
  // 元のRGBフレームを伸縮してからYOLOへ渡す。頂点数調整・Offset・描画の
  // いずれよりも前に適用する。0%では従来の入力をそのまま使う。
  cv::Mat stretchedFrame;
  const cv::Mat* detectionFrame = &rgbFrame;
  if (aspectRatioPercent != 0.0f) {
    const float ratio =
        1.0f + std::clamp(aspectRatioPercent, -50.0f, 50.0f) * 0.01f;
    const float horizontalScale = std::sqrt(ratio);
    const float verticalScale = 1.0f / horizontalScale;
    const float centerX = 0.5f * static_cast<float>(srcW - 1);
    const float centerY = 0.5f * static_cast<float>(srcH - 1);
    const cv::Matx23f transform(
        horizontalScale, 0.0f, (1.0f - horizontalScale) * centerX,
        0.0f, verticalScale, (1.0f - verticalScale) * centerY);
    cv::warpAffine(rgbFrame, stretchedFrame, transform, rgbFrame.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(114, 114, 114));
    detectionFrame = &stretchedFrame;
  }
  cv::Mat letterboxed = letterbox(*detectionFrame, scale, padX, padY);

  // 0〜1正規化してNCHW形式のblobを作成（入力は既にRGB前提なのでswapRB=false）
  cv::Mat blob = cv::dnn::blobFromImage(
      letterboxed, 1.0 / 255.0, cv::Size(inputSize, inputSize),
      cv::Scalar(0, 0, 0), false, false);
  std::vector<cv::Mat> outs;
  try {
    outs = ortSession->run(blob);
  } catch (const std::exception &e) {
    ofLogError("PersonSegmenter") << "YOLO CoreML inference failed: " << e.what();
    return result;
  }

  if (outs.size() < 2) {
    ofLogError("PersonSegmenter") << "想定外の出力数です(" << outs.size()
                                   << "個)。YOLO-segモデルではない可能性があります。";
    return result;
  }

  // 出力を次元数で判別する
  // (検出テンソル = 3次元[1,C,N] / プロトタイプマスク = 4次元[1,32,H,W])
  cv::Mat detOut, protoOut;
  for (auto &o : outs) {
    if (o.dims == 3 && detOut.empty()) {
      detOut = o;
    } else if (o.dims == 4 && protoOut.empty()) {
      protoOut = o;
    }
  }
  if (detOut.empty() || protoOut.empty()) {
    ofLogError("PersonSegmenter") << "検出テンソル/プロトタイプマスクの判別に失敗しました。"
                                   << "モデルの出力形式を確認してください。";
    return result;
  }

  const int C = detOut.size[1];
  const int N = detOut.size[2];
  const int protoC = protoOut.size[1]; // 通常32
  const int protoH = protoOut.size[2]; // 通常160
  const int protoW = protoOut.size[3]; // 通常160
  const int nc = C - 4 - protoC;       // クラス数を逆算

  if (nc <= 0) {
    ofLogError("PersonSegmenter") << "出力形状からクラス数を算出できませんでした (C=" << C << ")";
    return result;
  }

  const float *data = reinterpret_cast<const float *>(detOut.data);

  // ============================================
  // 1. 候補ボックスの抽出（クラス=person かつ 信頼度がしきい値以上のみ）
  // ============================================
  std::vector<Detection> candidates;
  std::vector<cv::Rect> boxesForNms;
  std::vector<float> scoresForNms;

  for (int n = 0; n < N; n++) {
    const float cx = data[0 * N + n];
    const float cy = data[1 * N + n];
    const float w  = data[2 * N + n];
    const float h  = data[3 * N + n];

    // 人物クラスが閾値未満なら他の79クラスを読む必要がない。
    if (personClassId < 0 || personClassId >= nc) continue;
    const float personScore = data[(4 + personClassId) * N + n];
    if (personScore <= 0.0f || personScore < yoloConfidenceThreshold) continue;
    bool otherClassWins = false;
    for (int c = 0; c < nc; ++c) {
      if (c != personClassId &&
          (data[(4 + c) * N + n] > personScore ||
           (c < personClassId && data[(4 + c) * N + n] == personScore))) {
        otherClassWins = true;
        break;
      }
    }
    if (otherClassWins) {
      continue;
    }

    Detection det;
    det.box = cv::Rect2f(cx - w / 2.0f, cy - h / 2.0f, w, h);
    det.confidence = personScore;
    det.maskCoeffs.resize(protoC);
    for (int k = 0; k < protoC; k++) {
      det.maskCoeffs[k] = data[(4 + nc + k) * N + n];
    }

    boxesForNms.emplace_back(
        static_cast<int>(det.box.x), static_cast<int>(det.box.y),
        static_cast<int>(det.box.width), static_cast<int>(det.box.height));
    scoresForNms.push_back(det.confidence);
    candidates.push_back(std::move(det));
  }

  if (candidates.empty()) {
    return result;
  }

  // ============================================
  // 2. NMSで重複検出を除去
  // ============================================
  std::vector<int> keepIndices;
  cv::dnn::NMSBoxes(boxesForNms, scoresForNms, yoloConfidenceThreshold,
                    nmsThreshold, keepIndices);

  if (static_cast<int>(keepIndices.size()) > maxDetections) {
    std::sort(keepIndices.begin(), keepIndices.end(), [&](int a, int b) {
      return scoresForNms[a] > scoresForNms[b];
    });
    keepIndices.resize(maxDetections);
  }

  // プロトタイプマスクを [protoC, protoH*protoW] の2D行列として扱う
  cv::Mat protoMat(protoC, protoH * protoW, CV_32F, protoOut.data);

  // 640入力空間 -> 元画像(rgbFrame)空間 -> 出力(output)空間への変換スケール
  const float outScaleX = static_cast<float>(outputWidth) / static_cast<float>(srcW);
  const float outScaleY = static_cast<float>(outputHeight) / static_cast<float>(srcH);
  // ============================================
  // 3. Ultralytics process_mask(..., upsample=true) と同じ順序で
  //    プロトタイプを合成 -> 入力サイズへ拡大 -> logit>0で二値化 -> boxで切る。
  //    YOLOプロトタイプマスクを復元する。
  // ============================================
  for (int idx : keepIndices) {
    const Detection &det = candidates[idx];

    // マスク係数 × プロトタイプ -> 低解像度(160x160) logitマスク
    cv::Mat coefMat(1, protoC, CV_32F, const_cast<float *>(det.maskCoeffs.data()));
    cv::Mat maskLowRes = coefMat * protoMat;      // [1, protoH*protoW]
    maskLowRes = maskLowRes.reshape(1, protoH);   // [protoH, protoW]

    cv::Mat maskUpsampled;
    cv::resize(maskLowRes, maskUpsampled, cv::Size(inputSize, inputSize),
               0.0, 0.0, cv::INTER_LINEAR);

    // crop_maskと同様に、x1 <= x < x2 / y1 <= y < y2 の整数画素を残す。
    const int left = std::clamp(static_cast<int>(std::ceil(det.box.x)), 0, inputSize);
    const int top = std::clamp(static_cast<int>(std::ceil(det.box.y)), 0, inputSize);
    const int right = std::clamp(
        static_cast<int>(std::ceil(det.box.x + det.box.width)), 0, inputSize);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(det.box.y + det.box.height)), 0, inputSize);
    if (right <= left || bottom <= top) continue;

    cv::Mat maskBinary = cv::Mat::zeros(inputSize, inputSize, CV_8UC1);
    const cv::Rect maskBox(left, top, right - left, bottom - top);
    cv::compare(maskUpsampled(maskBox), cv::Scalar(0.0),
                maskBinary(maskBox), cv::CMP_GT);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(maskBinary, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) continue;

    // 描画側の1検出=1輪郭という契約に合わせ、最大の連結成分を採用する。
    size_t largestIdx = 0;
    double largestArea = 0.0;
    for (size_t i = 0; i < contours.size(); i++) {
      const double area = cv::contourArea(contours[i]);
      if (area > largestArea) {
        largestArea = area;
        largestIdx = i;
      }
    }

    const auto &contour = contours[largestIdx];
    if (contour.size() < 3) continue;

    // 出力空間での面積換算が小さすぎるものはノイズとして除外
    const double outputAreaApprox = largestArea * (outScaleX / scale) * (outScaleY / scale);
    if (outputAreaApprox < minContourArea) continue;

    // 640入力空間 -> 元画像空間 -> 出力空間 へ変換
    ofPolyline poly;
    vector<glm::vec2> pts;
    pts.reserve(contour.size());

    for (const auto &point : contour) {
      const float xSrc = (point.x - padX) / scale;
      const float ySrc = (point.y - padY) / scale;

      glm::vec2 p(xSrc * outScaleX, ySrc * outScaleY);
      poly.addVertex(p.x, p.y);
      pts.push_back(p);
    }

    poly.close();
    poly = poly.getSmoothed(2); // マスク由来のガタついた輪郭を滑らかにする

    result.contours.push_back(poly);
    result.contourPoints.push_back(pts);

    // 重心（矩形中心を採用。単純な頂点平均より安定する）
    const float cxSrc = (det.box.x + det.box.width / 2.0f - padX) / scale;
    const float cySrc = (det.box.y + det.box.height / 2.0f - padY) / scale;
    result.centroids.push_back(glm::vec2(cxSrc * outScaleX, cySrc * outScaleY));

    // 外接矩形
    const float bx = (det.box.x - padX) / scale * outScaleX;
    const float by = (det.box.y - padY) / scale * outScaleY;
    const float bw = det.box.width / scale * outScaleX;
    const float bh = det.box.height / scale * outScaleY;
    result.boundingBoxes.push_back(ofRectangle(bx, by, bw, bh));
  }

  result.numHumans = static_cast<int>(result.contours.size());
  return result;
}
