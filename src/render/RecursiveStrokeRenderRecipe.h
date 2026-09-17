#pragma once

#include "render/RenderRecipe.h"
#include "render/StrokePass.h"

#include <algorithm>
#include <cmath>

namespace gux {

// RecursiveSplitLayoutで配置されたObjectをStrokeだけで描画する。
class RecursiveStrokeRenderRecipe final : public RenderRecipe {
 public:
  static constexpr std::string_view RecipeId = "recursive_stroke_render";
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
      RenderContext fixedStrokeContext = context;
      const float scale = std::max(0.0001f, std::abs(object.transform.scale.x));
      fixedStrokeContext.strokeWeight = context.strokeWeight / scale;
      strokePass.draw(object, fixedStrokeContext);
      ofPopMatrix();
    }
  }

 private:
  StrokePass strokePass;
};

}  // namespace gux
