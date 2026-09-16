#pragma once

#include "geometry/GeometryObject.h"
#include "scene/AppearanceComponent.h"

namespace gux {

struct Transform2D {
  glm::vec2 position{0.0f, 0.0f};
  float rotationDegrees = 0.0f;
  glm::vec2 scale{1.0f, 1.0f};
};

// 検出結果から独立して存在できる、作品内の描画オブジェクト。
struct SceneObject {
  ObjectId id = 0;
  std::vector<ObjectId> sourceObjectIds;
  ofPolyline geometry;
  glm::vec2 pivot{0.0f, 0.0f};
  Transform2D transform;
  AppearanceComponent appearance;
  std::uint32_t instanceIndex = 0;

  SceneObject clone(ObjectId newId, std::uint32_t newInstanceIndex) const {
    SceneObject copy = *this;
    copy.id = newId;
    copy.instanceIndex = newInstanceIndex;
    return copy;
  }
};

}  // namespace gux
