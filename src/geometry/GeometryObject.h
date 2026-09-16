#pragma once

#include "tracking/TrackedObject.h"

namespace gux {

// Tracking済みオブジェクトから作られた、描画前の形状データ。
struct GeometryObject {
  ObjectId id = 0;
  ofPolyline contour;
  glm::vec2 sourceCentroid{0.0f, 0.0f};
};

}  // namespace gux
