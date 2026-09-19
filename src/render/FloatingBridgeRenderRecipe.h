#pragma once

#include "render/BasePass.h"
#include "render/RenderRecipe.h"
#include "render/ShapePainter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gux {

// 検知順の2Objectを、向かい合う輪郭部分から四角形で接続する。
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

    for (const auto& object : objects) {
      if (object.geometry.size() < 3) continue;
      SceneObject solidObject = object;
      solidObject.appearance.baseMaterial.type = MaterialType::Solid;
      ofPushMatrix();
      ofTranslate(object.pivot + object.transform.position);
      ofRotateDeg(object.transform.rotationDegrees);
      ofScale(object.transform.scale.x, object.transform.scale.y);
      ofTranslate(-object.pivot);
      basePass.draw(solidObject, context);
      ofPopMatrix();
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

  void drawBridge(const SceneObject& first, const SceneObject& second,
                  const RenderContext& context) const {
    const ofPolyline firstGeometry = transformedGeometry(first);
    const ofPolyline secondGeometry = transformedGeometry(second);
    const glm::vec2 firstCenter =
        first.pivot + first.transform.position;
    const glm::vec2 secondCenter =
        second.pivot + second.transform.position;
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
    shapePainter.drawStroke(bridge, ofColor::black,
                            std::max(2.0f, context.strokeWeight * 0.2f),
                            StrokeJoinType::Straight);
  }

  BasePass basePass;
  ShapePainter shapePainter;
};

}  // namespace gux
