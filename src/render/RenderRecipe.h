#pragma once

#include "render/RenderContext.h"
#include "scene/SceneObject.h"

#include <string_view>

namespace gux {

class RenderRecipe {
 public:
  virtual ~RenderRecipe() = default;
  // Presetや将来のMIDI/同期制御から参照するための安定した識別子。
  virtual std::string_view id() const = 0;
  virtual void draw(const std::vector<SceneObject>& objects,
                    const RenderContext& context) const = 0;
};

}  // namespace gux
