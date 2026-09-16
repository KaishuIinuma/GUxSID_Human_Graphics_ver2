#pragma once

#include "render/RenderContext.h"
#include "scene/SceneObject.h"

namespace gux {

class RenderPass {
 public:
  virtual ~RenderPass() = default;
  virtual void draw(const SceneObject& object,
                    const RenderContext& context) const = 0;
};

}  // namespace gux
