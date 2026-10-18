#pragma once

#include "render/RenderRecipe.h"
#include "render/ShapePainter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gux {

// 近接順に並んだ2Objectを、向かい合う輪郭部分から四角形で接続する。
// 各Objectはペアを結ぶ線に垂直な線で二分し、ペアの2人で同じ配色にする。
// 左のObjectの色を外側(相手と反対側)、右のObjectの色を内側(相手側)に使う。
class FloatingBridgeRenderRecipe final : public RenderRecipe {
 public:
  static constexpr std::string_view RecipeId = "floating_bridge_render";
  std::string_view id() const override { return RecipeId; }

  void draw(const std::vector<SceneObject>& objects,
            const RenderContext& context) const override {
    // 接続面を先に描き、端部をObject本体で隠す。
    for (size_t i = 0; i + 1 < objects.size(); i += 2) {
      drawBridge(objects[i], objects[i + 1], context);
    }

    for (size_t i = 0; i + 1 < objects.size(); i += 2) {
      const SceneObject& first = objects[i];
      const SceneObject& second = objects[i + 1];
      // 浮遊の揺れで左右が入れ替わらないよう、輪郭由来のpivotで左右を決める。
      const bool firstIsLeft = first.pivot.x <= second.pivot.x;
      const ofColor& outerColor = firstIsLeft
          ? first.appearance.baseMaterial.primary
          : second.appearance.baseMaterial.primary;
      const ofColor& innerColor = firstIsLeft
          ? second.appearance.baseMaterial.primary
          : first.appearance.baseMaterial.primary;
      drawObject(first, &second, outerColor, innerColor, context);
      drawObject(second, &first, outerColor, innerColor, context);
    }

    // 接続相手のいない最後のObjectは元の色で描く。
    if (objects.size() % 2 != 0) {
      const ofColor& color = objects.back().appearance.baseMaterial.primary;
      drawObject(objects.back(), nullptr, color, color, context);
    }
  }

 private:
  struct EdgePair {
    glm::vec2 low{0.0f};
    glm::vec2 high{0.0f};
    bool valid = false;
  };

  static glm::vec2 transformPoint(const SceneObject& object,
                                  const glm::vec2& point) {
    glm::vec2 local = point - object.pivot;
    local *= object.transform.scale;
    const float radians = glm::radians(object.transform.rotationDegrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const glm::vec2 rotated(local.x * cosine - local.y * sine,
                            local.x * sine + local.y * cosine);
    return object.pivot + object.transform.position + rotated;
  }

  static ofPolyline transformedGeometry(const SceneObject& object) {
    ofPolyline result;
    for (const auto& point : object.geometry) {
      const glm::vec2 transformed =
          transformPoint(object, glm::vec2(point.x, point.y));
      result.addVertex(transformed.x, transformed.y);
    }
    result.setClosed(true);
    return result;
  }

  static EdgePair perpendicularExtremes(const ofPolyline& polygon,
                                        const glm::vec2& direction) {
    EdgePair result;
    if (polygon.size() < 2) return result;
    const glm::vec2 perpendicular(-direction.y, direction.x);
    float lowProjection = std::numeric_limits<float>::max();
    float highProjection = std::numeric_limits<float>::lowest();
    for (const auto& point3 : polygon) {
      const glm::vec2 point(point3.x, point3.y);
      const float across = glm::dot(point, perpendicular);
      if (across < lowProjection) {
        lowProjection = across;
        result.low = point;
      }
      if (across > highProjection) {
        highProjection = across;
        result.high = point;
      }
      result.valid = true;
    }
    return result;
  }

  static glm::vec2 worldCenter(const SceneObject& object) {
    return object.pivot + object.transform.position;
  }

  // 半平面 dot(p - origin, normal) >= 0 で多角形を切り取る(Sutherland-Hodgman)。
  static ofPolyline clipHalfPlane(const ofPolyline& polygon,
                                  const glm::vec2& origin,
                                  const glm::vec2& normal) {
    ofPolyline result;
    const size_t count = polygon.size();
    for (size_t i = 0; i < count; ++i) {
      const glm::vec2 current(polygon[i].x, polygon[i].y);
      const glm::vec2 next(polygon[(i + 1) % count].x,
                           polygon[(i + 1) % count].y);
      const float currentSide = glm::dot(current - origin, normal);
      const float nextSide = glm::dot(next - origin, normal);
      if (currentSide >= 0.0f) result.addVertex(current.x, current.y);
      if ((currentSide >= 0.0f) != (nextSide >= 0.0f)) {
        const float t = currentSide / (currentSide - nextSide);
        const glm::vec2 crossing = glm::mix(current, next, t);
        result.addVertex(crossing.x, crossing.y);
      }
    }
    result.setClosed(true);
    return result;
  }

  void drawObject(const SceneObject& object, const SceneObject* partner,
                  const ofColor& outerColor, const ofColor& innerColor,
                  const RenderContext&) const {
    if (object.geometry.size() < 3) return;
    ofPushMatrix();
    ofTranslate(object.pivot + object.transform.position);
    ofRotateDeg(object.transform.rotationDegrees);
    ofScale(object.transform.scale.x, object.transform.scale.y);
    ofTranslate(-object.pivot);
    shapePainter.drawFill(object.geometry, outerColor);

    if (partner != nullptr && innerColor != outerColor) {
      const glm::vec2 difference = worldCenter(*partner) - worldCenter(object);
      if (glm::length(difference) >= 1.0f) {
        // ワールド方向をObjectローカルへ写す。中心はローカルではpivotになり、
        // 半平面の法線は S * R^T * direction になる。
        const glm::vec2 direction = glm::normalize(difference);
        const float radians = glm::radians(object.transform.rotationDegrees);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const glm::vec2 unrotated(direction.x * cosine + direction.y * sine,
                                  -direction.x * sine + direction.y * cosine);
        const glm::vec2 localNormal = unrotated * object.transform.scale;
        if (glm::length(localNormal) > 0.0001f) {
          const ofPolyline facingHalf =
              clipHalfPlane(object.geometry, object.pivot, localNormal);
          shapePainter.drawFill(facingHalf, innerColor);
        }
      }
    }
    ofPopMatrix();
  }

  void drawBridge(const SceneObject& first, const SceneObject& second,
                  const RenderContext& context) const {
    const ofPolyline firstGeometry = transformedGeometry(first);
    const ofPolyline secondGeometry = transformedGeometry(second);
    const glm::vec2 firstCenter = worldCenter(first);
    const glm::vec2 secondCenter = worldCenter(second);
    const glm::vec2 difference = secondCenter - firstCenter;
    if (glm::length(difference) < 1.0f) return;
    const glm::vec2 direction = glm::normalize(difference);
    const EdgePair firstEdge =
        perpendicularExtremes(firstGeometry, direction);
    const EdgePair secondEdge =
        perpendicularExtremes(secondGeometry, direction);
    if (!firstEdge.valid || !secondEdge.valid) return;

    ofPolyline bridge;
    bridge.addVertex(firstEdge.low.x, firstEdge.low.y);
    bridge.addVertex(firstEdge.high.x, firstEdge.high.y);
    bridge.addVertex(secondEdge.high.x, secondEdge.high.y);
    bridge.addVertex(secondEdge.low.x, secondEdge.low.y);
    bridge.setClosed(true);
    shapePainter.drawFill(bridge, context.backgroundColor);
    shapePainter.drawStroke(bridge, ofColor(100, 100, 100),
                            std::max(2.0f, context.strokeWeight * 0.2f),
                            StrokeJoinType::Straight);
  }

  ShapePainter shapePainter;
};

}  // namespace gux
