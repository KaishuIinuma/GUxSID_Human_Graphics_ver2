#pragma once

#include "core/VisualTypes.h"
#include "geometry/GeometryObject.h"
#include "geometry/OffsetProcessor.h"
#include "geometry/VertexRemapper.h"

namespace gux {

struct GeometrySettings {
  int vertexCount = 16;
  bool enableOffset = true;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  Scene4OffsetJoinType offsetJoinType = Scene4OffsetJoinType::Round;
};

class GeometryProcessor {
 public:
  std::vector<GeometryObject> process(
      const std::vector<TrackedObject>& trackedObjects,
      const GeometrySettings& settings) const {
    std::vector<GeometryObject> result;
    result.reserve(trackedObjects.size());

    for (const auto& trackedObject : trackedObjects) {
      ofPolyline remapped =
          vertexRemapper.process(trackedObject.contour, settings.vertexCount);
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
  OffsetProcessor offsetProcessor;
};

}  // namespace gux
