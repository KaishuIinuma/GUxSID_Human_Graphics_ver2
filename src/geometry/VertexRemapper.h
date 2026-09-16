#pragma once

#include "ofMain.h"

namespace gux {

class VertexRemapper {
 public:
  ofPolyline process(const ofPolyline& source, int vertexCount) const {
    if (source.size() < 3 || vertexCount < 3) return source;
    ofPolyline result = source.getResampledByCount(vertexCount);
    result.setClosed(true);
    return result;
  }
};

}  // namespace gux
