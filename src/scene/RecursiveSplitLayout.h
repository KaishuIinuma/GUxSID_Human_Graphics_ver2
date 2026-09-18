#pragma once

#include "scene/SceneLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gux {

// 検知順を保ったまま画面を二分し、各オブジェクトを葉領域へ収める。
class RecursiveSplitLayout final : public SceneLayout {
 public:
  static constexpr std::string_view LayoutId = "recursive_split_layout";
  std::string_view id() const override { return LayoutId; }

  void apply(std::vector<SceneObject>& objects,
             const LayoutContext& context) const override {
    if (objects.empty()) return;
    const float outerMargin = 24.0f;
    const ofRectangle canvas(outerMargin, outerMargin,
        std::max(1.0f, context.canvasWidth - outerMargin * 2.0f),
        std::max(1.0f, context.canvasHeight - outerMargin * 2.0f));
    assign(objects, 0, objects.size(), canvas);
  }

 private:
  static ofRectangle bounds(const ofPolyline& polygon) {
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const auto& point : polygon) {
      minX = std::min(minX, point.x);
      minY = std::min(minY, point.y);
      maxX = std::max(maxX, point.x);
      maxY = std::max(maxY, point.y);
    }
    return ofRectangle(minX, minY, std::max(1.0f, maxX - minX),
                       std::max(1.0f, maxY - minY));
  }

  static void fit(SceneObject& object, const ofRectangle& cell) {
    if (object.geometry.size() < 3) return;
    const float padding = std::min(22.0f,
        std::min(cell.width, cell.height) * 0.08f);
    const float availableWidth = std::max(1.0f, cell.width - padding * 2.0f);
    const float availableHeight = std::max(1.0f, cell.height - padding * 2.0f);
    const ofRectangle box = bounds(object.geometry);
    const float scale = std::min(availableWidth / box.width,
                                 availableHeight / box.height);
    object.transform.scale = glm::vec2(scale);
    const glm::vec2 cellCenter(cell.x + cell.width * 0.5f,
                               cell.y + cell.height * 0.5f);
    const glm::vec2 boxCenter(box.x + box.width * 0.5f,
                              box.y + box.height * 0.5f);
    object.transform.position = cellCenter - object.pivot -
                                (boxCenter - object.pivot) * scale;
  }

  static std::uint64_t splitSeed(const std::vector<SceneObject>& objects,
                                 size_t begin, size_t end) {
    std::uint64_t seed = objects[begin].id;
    seed ^= objects[end - 1].id + 0x9e3779b97f4a7c15ULL + (seed << 6) +
            (seed >> 2);
    seed ^= static_cast<std::uint64_t>(end - begin) *
            0xbf58476d1ce4e5b9ULL;
    seed ^= seed >> 30;
    seed *= 0xbf58476d1ce4e5b9ULL;
    seed ^= seed >> 27;
    seed *= 0x94d049bb133111ebULL;
    return seed ^ (seed >> 31);
  }

  static float goldenSplitRatio(std::uint64_t seed) {
    constexpr float goldenLarge = 0.61803398875f;
    constexpr float maxJitter = 0.035f;
    const float unit = static_cast<float>((seed >> 1) & 0xffffULL) /
                       static_cast<float>(0xffffULL);
    const float largeRatio = goldenLarge + (unit * 2.0f - 1.0f) * maxJitter;
    return (seed & 1ULL) == 0 ? largeRatio : 1.0f - largeRatio;
  }

  static void assign(std::vector<SceneObject>& objects, size_t begin,
                     size_t end, const ofRectangle& cell) {
    const size_t count = end - begin;
    if (count == 1) {
      fit(objects[begin], cell);
      return;
    }

    // 黄金比を芯に少しだけ揺らし、どちら側を大きくするかも変える。
    // Object ID由来にすることで、毎フレーム位置がちらつくランダムさは避ける。
    const float ratio = goldenSplitRatio(splitSeed(objects, begin, end));
    const size_t firstCount = std::clamp(
        static_cast<size_t>(std::lround(static_cast<float>(count) * ratio)),
        size_t{1}, count - 1);
    const size_t middle = begin + firstCount;
    constexpr float gap = 12.0f;
    if (cell.width >= cell.height) {
      const float split = cell.width * ratio;
      assign(objects, begin, middle,
             ofRectangle(cell.x, cell.y, std::max(1.0f, split - gap * 0.5f),
                         cell.height));
      assign(objects, middle, end,
             ofRectangle(cell.x + split + gap * 0.5f, cell.y,
                         std::max(1.0f, cell.width - split - gap * 0.5f),
                         cell.height));
    } else {
      const float split = cell.height * ratio;
      assign(objects, begin, middle,
             ofRectangle(cell.x, cell.y, cell.width,
                         std::max(1.0f, split - gap * 0.5f)));
      assign(objects, middle, end,
             ofRectangle(cell.x, cell.y + split + gap * 0.5f, cell.width,
                         std::max(1.0f, cell.height - split - gap * 0.5f)));
    }
  }
};

}  // namespace gux
