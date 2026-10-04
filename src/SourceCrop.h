#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <opencv2/core.hpp>

// 入力画素を引き伸ばさず、出力の比率に収まる最大の矩形を選ぶ。
// position は切り抜ける余白内の位置 (0=左/上、1=右/下)。
inline cv::Rect sourceCrop(int sourceWidth, int sourceHeight,
                           int outputWidth, int outputHeight,
                           float positionX, float positionY) {
  if (sourceWidth <= 0 || sourceHeight <= 0 ||
      outputWidth <= 0 || outputHeight <= 0) {
    return {};
  }

  int width = sourceWidth;
  int height = sourceHeight;
  if (static_cast<int64_t>(sourceWidth) * outputHeight >
      static_cast<int64_t>(sourceHeight) * outputWidth) {
    width = std::max(1, static_cast<int>(
        static_cast<int64_t>(sourceHeight) * outputWidth / outputHeight));
  } else {
    height = std::max(1, static_cast<int>(
        static_cast<int64_t>(sourceWidth) * outputHeight / outputWidth));
  }

  const int x = static_cast<int>(std::lround(
      (sourceWidth - width) * std::clamp(positionX, 0.0f, 1.0f)));
  const int y = static_cast<int>(std::lround(
      (sourceHeight - height) * std::clamp(positionY, 0.0f, 1.0f)));
  return {x, y, width, height};
}
