#pragma once

#include "core/VisualTypes.h"
#include "geometry/GeometryObject.h"
#include "geometry/LooseContourProcessor.h"
#include "geometry/OffsetProcessor.h"
#include "geometry/VertexRemapper.h"

#include <algorithm>
#include <unordered_map>

namespace gux {

// 人ごとに頂点数とScaleを上書きする (プリセット切り替えの演出用)。
struct ShapeOverride {
  int vertexCount = 16;
  float scale = 1.0f;
};

struct GeometrySettings {
  int vertexCount = 16;
  bool enableLooseContour = false;
  float looseContourStrength = 7.0f;
  bool enableOffset = true;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  OffsetJoinType offsetJoinType = OffsetJoinType::Round;
  // ここに載っている人は vertexCount / offsetScale の代わりにこの値を使う。
  const std::unordered_map<ObjectId, ShapeOverride>* shapeOverrides = nullptr;
};

class GeometryProcessor {
 public:
  static constexpr float kMinVisibleScale = 0.001f;

  std::vector<GeometryObject> process(
      const std::vector<TrackedObject>& trackedObjects,
      const GeometrySettings& settings) const {
    std::vector<GeometryObject> result;
    result.reserve(trackedObjects.size());

    for (const auto& trackedObject : trackedObjects) {
      int vertexCount = settings.vertexCount;
      float scale = settings.offsetScale;
      if (settings.shapeOverrides) {
        const auto found = settings.shapeOverrides->find(trackedObject.id);
        if (found != settings.shapeOverrides->end()) {
          vertexCount = found->second.vertexCount;
          scale = found->second.scale;
        }
      }
      // Scaleがほぼ0の時は形が点に潰れるので描かない (切り替えの演出で0まで縮める)。
      if (scale <= kMinVisibleScale) continue;

      const ofPolyline contour = settings.enableLooseContour
          ? looseContourProcessor.process(trackedObject,
                                          settings.looseContourStrength)
          : trackedObject.contour;
      ofPolyline remapped = vertexRemapper.process(contour, vertexCount);
      if (remapped.size() < 3) continue;

      ofPolyline target = remapped;
      if (settings.enableOffset) {
        target = offsetProcessor.process(target, settings.offsetSize,
                                         settings.offsetJoinType);
      }
      if (target.size() < 3) continue;

      if (scale != 1.0f) {
        const glm::vec3 centroid = polygonCentroid(remapped);
        for (auto& point : target) {
          point = centroid + (point - centroid) * scale;
        }
      }
      target.setClosed(true);
      result.push_back(
          {trackedObject.id, std::move(target), trackedObject.centroid});
    }
    return result;
  }

 private:
  static glm::vec3 polygonCentroid(const ofPolyline& polygon) {
    glm::vec3 centroid(0.0f);
    for (const auto& point : polygon) centroid += point;
    return polygon.size() == 0
        ? centroid
        : centroid / static_cast<float>(polygon.size());
  }

  VertexRemapper vertexRemapper;
  LooseContourProcessor looseContourProcessor;
  OffsetProcessor offsetProcessor;
};

}  // namespace gux
