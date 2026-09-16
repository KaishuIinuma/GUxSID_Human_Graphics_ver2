#pragma once

#include "ofMain.h"
#include <cstdint>

namespace gux {

using ObjectId = std::uint64_t;

// Detection結果に、フレームをまたいで利用できるIDを付けたオブジェクト。
struct TrackedObject {
  ObjectId id = 0;
  ofPolyline contour;
  glm::vec2 centroid{0.0f, 0.0f};
  ofRectangle boundingBox;
  std::uint64_t ageFrames = 0;
};

}  // namespace gux
