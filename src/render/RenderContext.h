#pragma once

#include "core/VisualTypes.h"
#include "ofMain.h"

namespace gux {

struct RenderContext {
  bool enableBase = true;
  bool enableStroke = true;
  float strokeWeight = 10.0f;
  StrokeJoinType strokeJoinType = StrokeJoinType::Round;
  ofColor backgroundColor = ofColor::white;
};

}  // namespace gux
