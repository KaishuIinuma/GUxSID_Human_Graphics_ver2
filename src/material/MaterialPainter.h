#pragma once

#include "render/ShapePainter.h"
#include "scene/AppearanceComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gux {

// MaterialComponentを実際の塗り・線描画へ変換する唯一の描画窓口。
class MaterialPainter {
 public:
  void drawFill(const ofPolyline& polygon,
                const MaterialComponent& material) const {
    if (material.type == MaterialType::Solid) {
      shapePainter.drawFill(polygon, material.primary);
      return;
    }
    drawLinearGradientFill(polygon, material);
  }

  void drawStroke(const ofPolyline& polygon,
                  const MaterialComponent& material, float strokeWeight,
                  StrokeJoinType joinType) const {
    if (material.type == MaterialType::Solid) {
      shapePainter.drawStroke(polygon, material.primary, strokeWeight, joinType);
      return;
    }
    drawLinearGradientStroke(polygon, material, strokeWeight, joinType);
  }

 private:
  struct GradientAxis {
    glm::vec2 direction{0.0f, 1.0f};
    float minimum = 0.0f;
    float maximum = 1.0f;
  };

  static GradientAxis gradientAxis(const ofPolyline& polygon,
                                   const MaterialComponent& material) {
    GradientAxis axis;
    axis.direction = glm::length(material.gradientDirection) > 0.0001f
        ? glm::normalize(material.gradientDirection)
        : glm::vec2(0.0f, 1.0f);
    axis.minimum = std::numeric_limits<float>::max();
    axis.maximum = std::numeric_limits<float>::lowest();
    for (const auto& point : polygon) {
      const float projection = glm::dot(glm::vec2(point.x, point.y), axis.direction);
      axis.minimum = std::min(axis.minimum, projection);
      axis.maximum = std::max(axis.maximum, projection);
    }
    if (axis.maximum - axis.minimum < 0.0001f) axis.maximum = axis.minimum + 1.0f;
    return axis;
  }

  static ofColor colorAt(const glm::vec2& point, const GradientAxis& axis,
                         const MaterialComponent& material) {
    const float projection = glm::dot(point, axis.direction);
    const float amount = ofClamp((projection - axis.minimum) /
                                 (axis.maximum - axis.minimum),
                                 0.0f, 1.0f);
    return material.primary.getLerped(material.secondary, amount);
  }

  static ofPath makePath(const ofPolyline& polygon) {
    ofPath path;
    path.setFilled(true);
    path.moveTo(polygon[0]);
    for (size_t i = 1; i < polygon.size(); ++i) path.lineTo(polygon[i]);
    path.close();
    return path;
  }

  static void drawLinearGradientFill(const ofPolyline& polygon,
                                     const MaterialComponent& material) {
    if (polygon.size() < 3) return;
    const GradientAxis axis = gradientAxis(polygon, material);
    ofMesh mesh = makePath(polygon).getTessellation();
    mesh.clearColors();
    for (const auto& vertex : mesh.getVertices()) {
      mesh.addColor(colorAt(glm::vec2(vertex.x, vertex.y), axis, material));
    }
    mesh.draw();
  }

  static void drawLinearGradientStroke(const ofPolyline& polygon,
                                       const MaterialComponent& material,
                                       float strokeWeight,
                                       StrokeJoinType joinType) {
    if (polygon.size() < 3) return;
    const GradientAxis axis = gradientAxis(polygon, material);
    for (size_t i = 0; i < polygon.size(); ++i) {
      const glm::vec2 p1 = polygon[i];
      const glm::vec2 p2 = polygon[(i + 1) % polygon.size()];
      const glm::vec2 previous = polygon[(i - 1 + polygon.size()) % polygon.size()];
      const glm::vec2 direction = p2 - p1;
      const float length = glm::length(direction);
      const ofColor firstColor = colorAt(p1, axis, material);
      const ofColor secondColor = colorAt(p2, axis, material);
      if (length > 0.0f) {
        const glm::vec2 normal = glm::vec2(-direction.y, direction.x) / length *
            (strokeWeight * 0.5f);
        ofMesh segment;
        segment.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
        segment.addVertex(glm::vec3(p1 + normal, 0));
        segment.addColor(firstColor);
        segment.addVertex(glm::vec3(p1 - normal, 0));
        segment.addColor(firstColor);
        segment.addVertex(glm::vec3(p2 + normal, 0));
        segment.addColor(secondColor);
        segment.addVertex(glm::vec3(p2 - normal, 0));
        segment.addColor(secondColor);
        segment.draw();
      }

      ofSetColor(firstColor);
      if (joinType == StrokeJoinType::Round) {
        ofDrawCircle(p1, strokeWeight * 0.5f);
        continue;
      }
      const glm::vec2 incoming = glm::normalize(p1 - previous);
      const glm::vec2 outgoing = glm::normalize(p2 - p1);
      const glm::vec2 normal1(-incoming.y, incoming.x);
      const glm::vec2 normal2(-outgoing.y, outgoing.x);
      const glm::vec2 miter = glm::normalize(normal1 + normal2);
      const float dot = glm::dot(normal1, miter);
      if (std::abs(dot) <= 0.05f) continue;
      float miterLength = (strokeWeight * 0.5f) / dot;
      if (miterLength > strokeWeight * 4.0f) miterLength = strokeWeight * 4.0f;
      drawMiterJoint(p1, normal1, normal2, miter, miterLength, strokeWeight);
    }
  }

  static void drawMiterJoint(const glm::vec2& point, const glm::vec2& normal1,
                             const glm::vec2& normal2, const glm::vec2& miter,
                             float miterLength, float strokeWeight) {
    const glm::vec2 outside = point + miter * miterLength;
    const glm::vec2 inside = point - miter * miterLength;
    ofMesh outsideJoint;
    outsideJoint.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    outsideJoint.addVertex(glm::vec3(point, 0));
    outsideJoint.addVertex(glm::vec3(point + normal1 * (strokeWeight * 0.5f), 0));
    outsideJoint.addVertex(glm::vec3(outside, 0));
    outsideJoint.addVertex(glm::vec3(point + normal2 * (strokeWeight * 0.5f), 0));
    outsideJoint.draw();
    ofMesh insideJoint;
    insideJoint.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    insideJoint.addVertex(glm::vec3(point, 0));
    insideJoint.addVertex(glm::vec3(point - normal1 * (strokeWeight * 0.5f), 0));
    insideJoint.addVertex(glm::vec3(inside, 0));
    insideJoint.addVertex(glm::vec3(point - normal2 * (strokeWeight * 0.5f), 0));
    insideJoint.draw();
  }

  ShapePainter shapePainter;
};

}  // namespace gux
