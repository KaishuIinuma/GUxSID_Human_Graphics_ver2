#pragma once

#include "scene/SceneObject.h"
#include "ofMain.h"
#include "render/RenderContext.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gux {

// 画面内の全Objectの重心を、1本のストロークで結ぶ。
// 結ぶ順番は左下端から右へ進み、X座標が戻らない順(X昇順、同じXなら下から)にする。
// どのRecipeの上にも重ねられるよう、Object本体の描画とは独立している。
class BridgeStrokePass {
 public:
  // 結ぶ対象になる重心を集める(並べ替えはしない)。
  static std::vector<glm::vec2> centroids(
      const std::vector<SceneObject>& objects) {
    std::vector<glm::vec2> points;
    points.reserve(objects.size());
    for (const auto& object : objects) {
      if (object.geometry.size() < 3) continue;
      points.push_back(object.pivot + object.transform.position);
    }
    return points;
  }

  void draw(const std::vector<SceneObject>& objects,
            const RenderContext& context) const {
    draw(centroids(objects), context);
  }

  // 重心を直接渡して結ぶ。Dual Output で2画面を1本につなげる時に使う
  // (画面外の点も含めてよい。はみ出した部分は描画先で切れる)。
  void draw(std::vector<glm::vec2> points, const RenderContext& context) const {
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

 private:
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
};

}  // namespace gux
