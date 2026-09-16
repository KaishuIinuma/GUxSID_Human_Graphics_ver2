#pragma once

#include "render/RenderPass.h"
#include "material/MaterialPainter.h"

namespace gux {

class BasePass final : public RenderPass {
 public:
  void draw(const SceneObject& object,
            const RenderContext&) const override {
    painter.drawFill(object.geometry, object.appearance.baseMaterial);
  }

 private:
  MaterialPainter painter;
};

}  // namespace gux
