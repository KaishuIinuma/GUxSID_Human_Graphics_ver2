#pragma once

#include "scene/SceneObject.h"

#include <string_view>

namespace gux {

struct LayoutContext {
  float canvasWidth = 1.0f;
  float canvasHeight = 1.0f;
};

class SceneLayout {
 public:
  virtual ~SceneLayout() = default;
  virtual std::string_view id() const = 0;
  virtual void apply(std::vector<SceneObject>& objects,
                     const LayoutContext& context) const = 0;
};

}  // namespace gux
