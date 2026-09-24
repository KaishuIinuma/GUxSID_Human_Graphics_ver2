#pragma once

#include "core/VisualTypes.h"
#include "geometry/GeometryObject.h"
#include "geometry/LooseContourProcessor.h"
#include "geometry/OffsetProcessor.h"
#include "geometry/VertexRemapper.h"

#include <algorithm>
#include <cmath>

namespace gux {

struct GeometrySettings {
  int vertexCount = 16;
  bool enableLooseContour = false;
  float looseContourStrength = 7.0f;
  bool enableOffset = true;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  float aspectRatioPercent = 0.0f;
  OffsetJoinType offsetJoinType = OffsetJoinType::Round;
};

class GeometryProcessor {
 public:
  std::vector<GeometryObject> process(
      const std::vector<TrackedObject>& trackedObjects,
      const GeometrySettings& settings) const {
    std::vector<GeometryObject> result;
    result.reserve(trackedObjects.size());

    for (const auto& trackedObject : trackedObjects) {
      const ofPolyline contour = settings.enableLooseContour
          ? looseContourProcessor.process(trackedObject,
                                          settings.looseContourStrength)
          : trackedObject.contour;
      ofPolyline remapped = vertexRemapper.process(contour, settings.vertexCount);
      if (remapped.size() < 3) continue;

      ofPolyline target = remapped;
      if (settings.enableOffset) {
        target = offsetProcessor.process(target, settings.offsetSize,
                                         settings.offsetJoinType);
      }
      if (target.size() < 3) continue;

      if (settings.offsetScale != 1.0f) {
        const glm::vec3 centroid = polygonCentroid(remapped);
        for (auto& point : target) {
          point = centroid + (point - centroid) * settings.offsetScale;
        }
      }
      if (settings.aspectRatioPercent != 0.0f) {
        // 縦横比だけを変え、面積は維持する。正で横長、負で縦長。
        const float ratio =
            1.0f + std::clamp(settings.aspectRatioPercent, -50.0f, 50.0f) *
                       0.01f;
        const float horizontalScale = std::sqrt(ratio);
        const float verticalScale = 1.0f / horizontalScale;
        const glm::vec3 centroid = polygonCentroid(remapped);
        for (auto& point : target) {
          point.x = centroid.x + (point.x - centroid.x) * horizontalScale;
          point.y = centroid.y + (point.y - centroid.y) * verticalScale;
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
