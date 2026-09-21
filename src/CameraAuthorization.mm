#include "CameraAuthorization.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace {
std::string nsErrorDescription(NSError *error) {
    if (!error) return "unknown AVFoundation error";
    const char *description = error.localizedDescription.UTF8String;
    return description ? description : "unknown AVFoundation error";
}

void releasePixelBufferBytes(void *, const void *baseAddress) {
    std::free(const_cast<void *>(baseAddress));
}

CVReturn createTightlyPackedARGBPixelBuffer(
    int width, int height, CVPixelBufferRef *pixelBuffer) {
    const size_t bytesPerRow = static_cast<size_t>(width) * 4;
    void *pixels = std::calloc(static_cast<size_t>(height), bytesPerRow);
    if (!pixels) return kCVReturnAllocationFailed;

    const CVReturn result = CVPixelBufferCreateWithBytes(
        kCFAllocatorDefault, width, height, kCVPixelFormatType_32ARGB, pixels,
        bytesPerRow, releasePixelBufferBytes, nullptr, nullptr, pixelBuffer);
    if (result != kCVReturnSuccess) std::free(pixels);
    return result;
}

void convertPremultipliedToStraightARGB(
    void *baseAddress, size_t bytesPerRow, int width, int height) {
    auto *rows = static_cast<unsigned char *>(baseAddress);
    for (int y = 0; y < height; ++y) {
        unsigned char *pixel = rows + static_cast<size_t>(y) * bytesPerRow;
        for (int x = 0; x < width; ++x, pixel += 4) {
            const unsigned int alpha = pixel[0];
            if (alpha == 0) {
                pixel[1] = pixel[2] = pixel[3] = 0;
            } else if (alpha < 255) {
                pixel[1] = static_cast<unsigned char>(
                    std::min(255u, (pixel[1] * 255u + alpha / 2u) / alpha));
                pixel[2] = static_cast<unsigned char>(
                    std::min(255u, (pixel[2] * 255u + alpha / 2u) / alpha));
                pixel[3] = static_cast<unsigned char>(
                    std::min(255u, (pixel[3] * 255u + alpha / 2u) / alpha));
            }
        }
    }
}
} // namespace

bool ensureCameraAuthorization() {
    const AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];

    if (status == AVAuthorizationStatusAuthorized) {
        return true;
    }

    if (status == AVAuthorizationStatusDenied ||
        status == AVAuthorizationStatusRestricted) {
        return false;
    }

    __block BOOL granted = NO;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                            completionHandler:^(BOOL didGrantAccess) {
        granted = didGrantAccess;
        dispatch_semaphore_signal(semaphore);
    }];

    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    return granted == YES;
}

std::string createLosslessMovieWithAVFoundation(
    const std::string &pngDirectory,
    const std::string &moviePath,
    int width,
    int height,
    int frameCount,
    double frameRate) {
    @autoreleasepool {
        NSString *moviePathString =
            [NSString stringWithUTF8String:moviePath.c_str()];
        if (!moviePathString) return "invalid MOV output path";

        NSURL *movieURL = [NSURL fileURLWithPath:moviePathString];
        if ([[NSFileManager defaultManager] fileExistsAtPath:moviePathString]) {
            return "MOV output already exists";
        }

        NSError *writerError = nil;
        AVAssetWriter *writer = [[AVAssetWriter alloc]
            initWithURL:movieURL
            fileType:AVFileTypeQuickTimeMovie
            error:&writerError];
        if (!writer) return nsErrorDescription(writerError);

        CVPixelBufferRef formatBuffer = nullptr;
        const CVReturn formatBufferResult = createTightlyPackedARGBPixelBuffer(
            width, height, &formatBuffer);
        if (formatBufferResult != kCVReturnSuccess || !formatBuffer) {
            return "could not create the lossless MOV format buffer";
        }
        CMVideoFormatDescriptionRef formatDescription = nullptr;
        const OSStatus formatResult = CMVideoFormatDescriptionCreateForImageBuffer(
            kCFAllocatorDefault, formatBuffer, &formatDescription);
        CVPixelBufferRelease(formatBuffer);
        if (formatResult != noErr || !formatDescription) {
            return "could not create the lossless MOV format description";
        }

        // outputSettings=nilで既に生成したARGBフレームを再圧縮せずMOVへ格納する。
        // 外部コーデック不要で、PNGの画質とアルファを完全に維持する。
        AVAssetWriterInput *videoInput = [[AVAssetWriterInput alloc]
            initWithMediaType:AVMediaTypeVideo
            outputSettings:nil
            sourceFormatHint:formatDescription];
        CFRelease(formatDescription);
        videoInput.expectsMediaDataInRealTime = NO;

        if (![writer canAddInput:videoInput]) {
            return "AVFoundation cannot add the lossless ARGB video input";
        }
        [writer addInput:videoInput];
        if (![writer startWriting]) {
            return nsErrorDescription(writer.error);
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        if (!colorSpace) {
            [writer cancelWriting];
            return "could not create RGB color space";
        }

        NSString *directory =
            [NSString stringWithUTF8String:pngDirectory.c_str()];
        std::string failure;
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            @autoreleasepool {
                while (!videoInput.readyForMoreMediaData &&
                       writer.status == AVAssetWriterStatusWriting) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (writer.status != AVAssetWriterStatusWriting) {
                    failure = nsErrorDescription(writer.error);
                    break;
                }

                NSString *frameName = [NSString
                    stringWithFormat:@"frame_%06d.png", frameIndex + 1];
                NSString *framePath =
                    [directory stringByAppendingPathComponent:frameName];
                NSURL *frameURL = [NSURL fileURLWithPath:framePath];
                CGImageSourceRef imageSource = CGImageSourceCreateWithURL(
                    (__bridge CFURLRef)frameURL, nullptr);
                CGImageRef image = imageSource
                    ? CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr)
                    : nullptr;
                if (imageSource) CFRelease(imageSource);
                if (!image) {
                    failure = "could not read PNG frame " +
                        std::to_string(frameIndex + 1);
                    break;
                }

                CVPixelBufferRef pixelBuffer = nullptr;
                const CVReturn bufferResult = createTightlyPackedARGBPixelBuffer(
                    width, height, &pixelBuffer);
                if (bufferResult != kCVReturnSuccess || !pixelBuffer) {
                    CGImageRelease(image);
                    failure = "could not allocate video pixel buffer";
                    break;
                }

                CVPixelBufferLockBaseAddress(pixelBuffer, 0);
                void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
                const size_t bytesPerRow =
                    CVPixelBufferGetBytesPerRow(pixelBuffer);
                CGContextRef context = CGBitmapContextCreate(
                    baseAddress, width, height, 8, bytesPerRow, colorSpace,
                    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Big);
                if (!context) {
                    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
                    CVPixelBufferRelease(pixelBuffer);
                    CGImageRelease(image);
                    failure = "could not create video bitmap context";
                    break;
                }

                CGContextClearRect(context, CGRectMake(0, 0, width, height));
                CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
                CGContextRelease(context);
                CGImageRelease(image);
                convertPremultipliedToStraightARGB(
                    baseAddress, bytesPerRow, width, height);
                CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

                CMSampleTimingInfo timing = {
                    CMTimeMakeWithSeconds(1.0 / frameRate, 60000),
                    CMTimeMakeWithSeconds(
                        static_cast<double>(frameIndex) / frameRate, 60000),
                    kCMTimeInvalid};
                CMVideoFormatDescriptionRef frameFormat = nullptr;
                const OSStatus frameFormatResult =
                    CMVideoFormatDescriptionCreateForImageBuffer(
                        kCFAllocatorDefault, pixelBuffer, &frameFormat);
                CMSampleBufferRef sampleBuffer = nullptr;
                const OSStatus sampleResult = frameFormatResult == noErr
                    ? CMSampleBufferCreateReadyWithImageBuffer(
                          kCFAllocatorDefault, pixelBuffer, frameFormat, &timing,
                          &sampleBuffer)
                    : frameFormatResult;
                const bool appended = sampleResult == noErr && sampleBuffer &&
                    [videoInput appendSampleBuffer:sampleBuffer];
                if (sampleBuffer) CFRelease(sampleBuffer);
                if (frameFormat) CFRelease(frameFormat);
                if (!appended) {
                    failure = nsErrorDescription(writer.error);
                    CVPixelBufferRelease(pixelBuffer);
                    break;
                }
                CVPixelBufferRelease(pixelBuffer);
            }
        }
        CGColorSpaceRelease(colorSpace);

        if (!failure.empty()) {
            [videoInput markAsFinished];
            [writer cancelWriting];
            return failure;
        }

        [videoInput markAsFinished];
        dispatch_semaphore_t completionSemaphore = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(completionSemaphore);
        }];
        dispatch_semaphore_wait(completionSemaphore, DISPATCH_TIME_FOREVER);
        if (writer.status != AVAssetWriterStatusCompleted) {
            return nsErrorDescription(writer.error);
        }
        return "";
    }
}
