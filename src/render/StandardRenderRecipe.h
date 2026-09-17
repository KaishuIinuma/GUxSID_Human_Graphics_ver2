#pragma once

#include "render/BasePass.h"
#include "render/RenderRecipe.h"
#include "render/StrokePass.h"

namespace gux {

// HumanGraphicsSceneの基準となる描画手順。各ObjectをBase -> Strokeの順で描画する。
class StandardRenderRecipe final : public RenderRecipe {
 public:
  static constexpr std::string_view RecipeId = "standard_render";

  std::string_view id() const override { return RecipeId; }

  void draw(const std::vector<SceneObject>& objects,
            const RenderContext& context) const override {
    for (const auto& object : objects) {
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
