#pragma once

#include "render/RenderPass.h"
#include "material/MaterialPainter.h"

namespace gux {

class StrokePass final : public RenderPass {
 public:
  void draw(const SceneObject& object,
            const RenderContext& context) const override {
    painter.drawStroke(object.geometry, object.appearance.outlineMaterial,
                       context.strokeWeight,
                       context.strokeJoinType);
  }

 private:
  MaterialPainter painter;
};

}  // namespace gux
