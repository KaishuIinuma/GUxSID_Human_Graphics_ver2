#pragma once

#include "render/BasePass.h"
#include "render/RenderRecipe.h"
#include "render/StrokePass.h"

namespace gux {

// 現行Scene4の見た目を、BaseとStrokeの独立Passで再現するRecipe。
class CurrentRenderRecipe final : public RenderRecipe {
 public:
  void draw(const std::vector<SceneObject>& objects,
            const RenderContext& context) const override {
    for (size_t i = 0; i < objects.size(); ++i) {
      const auto& object = objects[i];
      if (object.geometry.size() < 3) continue;
      ofPushMatrix();
      ofTranslate(object.pivot + object.transform.position);
      ofRotateDeg(object.transform.rotationDegrees);
      ofScale(object.transform.scale.x, object.transform.scale.y);
      ofTranslate(-object.pivot);
      if (context.enableBase) basePass.draw(object, context);
      if (context.enableStroke) strokePass.draw(object, context);
      ofPopMatrix();
    }
  }

 private:
  BasePass basePass;
  StrokePass strokePass;
};

}  // namespace gux
