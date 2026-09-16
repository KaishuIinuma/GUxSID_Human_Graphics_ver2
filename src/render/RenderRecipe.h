#pragma once

#include "render/RenderContext.h"
#include "scene/SceneObject.h"

namespace gux {

class RenderRecipe {
 public:
  virtual ~RenderRecipe() = default;
  virtual void draw(const std::vector<SceneObject>& objects,
                    const RenderContext& context) const = 0;
};

}  // namespace gux
