#pragma once

#include "render/BasePass.h"
#include "render/RenderRecipe.h"
#include "render/StrokePass.h"

namespace gux {

// パレット色のベタ塗りBaseに、背景色のStrokeを重ねるRecipe。
class MixRenderRecipe final : public RenderRecipe {
 public:
  static constexpr std::string_view RecipeId = "mix_render";

  std::string_view id() const override { return RecipeId; }

  void draw(const std::vector<SceneObject>& objects,
            const RenderContext& context) const override {
    for (const auto& object : objects) {
      if (object.geometry.size() < 3) continue;

      SceneObject styledObject = object;
      styledObject.appearance.baseMaterial.type = MaterialType::Solid;
      styledObject.appearance.outlineMaterial.type = MaterialType::Solid;
      styledObject.appearance.outlineMaterial.primary = context.backgroundColor;
      styledObject.appearance.outlineMaterial.secondary = context.backgroundColor;

      ofPushMatrix();
      ofTranslate(object.pivot + object.transform.position);
      ofRotateDeg(object.transform.rotationDegrees);
      ofScale(object.transform.scale.x, object.transform.scale.y);
      ofTranslate(-object.pivot);
      basePass.draw(styledObject, context);
      strokePass.draw(styledObject, context);
      ofPopMatrix();
    }
  }

 private:
  BasePass basePass;
  StrokePass strokePass;
};

}  // namespace gux
