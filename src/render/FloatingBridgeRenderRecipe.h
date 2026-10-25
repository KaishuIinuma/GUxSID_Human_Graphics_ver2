#pragma once

#include "render/BasePass.h"
#include "render/RenderRecipe.h"
#include "render/StrokePass.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gux {

// 画面内の全Objectの重心を、1本のストロークで結ぶ。
// 結ぶ順番は左下端から右へ進み、X座標が戻らない順(X昇順、同じXなら下から)にする。
// Object本体の描画は通常のRecipeと同じく、Base/Strokeトグルの設定に従う。
class FloatingBridgeRenderRecipe final : public RenderRecipe {
 public:
  static constexpr std::string_view RecipeId = "floating_bridge_render";
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

    // 接続線はObject本体より前面に描く。
    drawConnection(objects, context);
  }

 private:
  static glm::vec2 worldCenter(const SceneObject& object) {
    return object.pivot + object.transform.position;
  }

  void drawConnection(const std::vector<SceneObject>& objects,
                      const RenderContext& context) const {
    std::vector<glm::vec2> points;
    points.reserve(objects.size());
    for (const auto& object : objects) {
      if (object.geometry.size() < 3) continue;
      points.push_back(worldCenter(object));
    }
    if (points.size() < 2) return;

    // 左から右へ。Xが同じ場合は下(画面座標でYが大きい方)から結ぶ。
    std::sort(points.begin(), points.end(),
              [](const glm::vec2& a, const glm::vec2& b) {
                if (a.x != b.x) return a.x < b.x;
                return a.y > b.y;
              });

    const float weight = std::max(2.0f, context.strokeWeight * 0.2f);
    drawOpenStroke(points, ofColor(100, 100, 100), weight);
  }

  // 始点と終点をつながない、角を丸めた1本のストロークを描く。
  static void drawOpenStroke(const std::vector<glm::vec2>& points,
                             const ofColor& color, float weight) {
    const float half = weight * 0.5f;
    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLES);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
      const glm::vec2 p1 = points[i];
      const glm::vec2 p2 = points[i + 1];
      const glm::vec2 direction = p2 - p1;
      const float length = glm::length(direction);
      if (length <= 0.0f) continue;
      const glm::vec2 normal =
          glm::vec2(-direction.y, direction.x) / length * half;
      const glm::vec3 v0(p1 + normal, 0.0f);
      const glm::vec3 v1(p1 - normal, 0.0f);
      const glm::vec3 v2(p2 + normal, 0.0f);
      const glm::vec3 v3(p2 - normal, 0.0f);
      mesh.addVertex(v0); mesh.addVertex(v1); mesh.addVertex(v2);
      mesh.addVertex(v2); mesh.addVertex(v1); mesh.addVertex(v3);
    }
    // 折れ目と両端を丸める。
    constexpr int circleSegments = 16;
    for (const auto& center : points) {
      for (int j = 0; j < circleSegments; ++j) {
        const float a0 = TWO_PI * j / circleSegments;
        const float a1 = TWO_PI * (j + 1) / circleSegments;
        mesh.addVertex(glm::vec3(center, 0.0f));
        mesh.addVertex(glm::vec3(
            center + half * glm::vec2(std::cos(a0), std::sin(a0)), 0.0f));
        mesh.addVertex(glm::vec3(
            center + half * glm::vec2(std::cos(a1), std::sin(a1)), 0.0f));
      }
    }
    ofPushStyle();
    ofFill();
    ofSetColor(color);
    mesh.draw();
    ofPopStyle();
  }

  BasePass basePass;
  StrokePass strokePass;
};

}  // namespace gux
