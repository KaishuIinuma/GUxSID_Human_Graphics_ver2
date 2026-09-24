#pragma once

#include "tracking/TrackedObject.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gux {

// 検出輪郭を低解像度のマスクに戻し、人物IDで固定した緩やかな距離場の
// 変形を加える。頂点を直接揺らさないため、頂点数や並び順に依存しない。
class LooseContourProcessor {
 public:
  ofPolyline process(const TrackedObject& object,
                     float strengthPercent) const {
    const ofPolyline& source = object.contour;
    if (source.size() < 3 || strengthPercent <= 0.0f) return source;

    float minX = source[0].x;
    float minY = source[0].y;
    float maxX = minX;
    float maxY = minY;
    for (const auto& point : source) {
      minX = std::min(minX, point.x);
      minY = std::min(minY, point.y);
      maxX = std::max(maxX, point.x);
      maxY = std::max(maxY, point.y);
    }
    const float width = maxX - minX;
    const float height = maxY - minY;
    if (width < 8.0f || height < 8.0f) return source;

    // 解像度を制限し、入力動画と書き出し解像度で同じ形状を使う。
    const float scale = std::min(1.0f, 256.0f / std::max(width, height));
    const float amplitude =
        0.01f * std::clamp(strengthPercent, 0.0f, 30.0f) *
        std::min(width, height) * scale;
    const int padding = std::max(12, static_cast<int>(std::ceil(amplitude * 2.0f + 8.0f)));
    const int cols = static_cast<int>(std::ceil(width * scale)) + 1 + padding * 2;
    const int rows = static_cast<int>(std::ceil(height * scale)) + 1 + padding * 2;

    std::vector<cv::Point> polygon;
    polygon.reserve(source.size());
    for (const auto& point : source) {
      polygon.emplace_back(
          static_cast<int>(std::lround((point.x - minX) * scale)) + padding,
          static_cast<int>(std::lround((point.y - minY) * scale)) + padding);
    }

    cv::Mat mask(rows, cols, CV_8UC1, cv::Scalar(0));
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));

    cv::Mat insideDistance;
    cv::Mat outsideDistance;
    cv::distanceTransform(mask, insideDistance, cv::DIST_L2, 3);
    cv::Mat inverseMask;
    cv::bitwise_not(mask, inverseMask);
    cv::distanceTransform(inverseMask, outsideDistance, cv::DIST_L2, 3);
    cv::Mat signedDistance = insideDistance - outsideDistance;
    cv::GaussianBlur(signedDistance, signedDistance, cv::Size(), 2.0);

    const float phase0 = phase(object.id, 0);
    const float phase1 = phase(object.id, 1);
    const float phase2 = phase(object.id, 2);
    constexpr float tau = 6.28318530718f;
    cv::Mat looseMask(rows, cols, CV_8UC1, cv::Scalar(0));
    for (int y = padding; y < rows - padding; ++y) {
      const float v = (static_cast<float>(y - padding) / scale) / height;
      const float* distanceRow = signedDistance.ptr<float>(y);
      uint8_t* targetRow = looseMask.ptr<uint8_t>(y);
      for (int x = padding; x < cols - padding; ++x) {
        const float u = (static_cast<float>(x - padding) / scale) / width;
        const float wave =
            0.52f * std::sin(tau * (1.10f * u + 0.35f * v) + phase0) +
            0.32f * std::sin(tau * (-0.27f * u + 1.45f * v) + phase1) +
            0.16f * std::sin(tau * (1.70f * u - 0.80f * v) + phase2);
        targetRow[x] = distanceRow[x] + amplitude * wave > 0.0f ? 255 : 0;
      }
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(looseMask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return source;
    const auto largest = std::max_element(
        contours.begin(), contours.end(),
        [](const auto& a, const auto& b) {
          return cv::contourArea(a) < cv::contourArea(b);
        });
    if (largest->size() < 3) return source;

    ofPolyline result;
    for (const auto& point : *largest) {
      result.addVertex(minX + (point.x - padding) / scale,
                       minY + (point.y - padding) / scale);
    }
    result.setClosed(true);
    return result;
  }

 private:
  static float phase(ObjectId id, uint32_t channel) {
    uint64_t value = id + 0x9e3779b97f4a7c15ULL * (channel + 1);
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return static_cast<float>(value & 0xffff) *
           (6.28318530718f / 65536.0f);
  }
};

}  // namespace gux
