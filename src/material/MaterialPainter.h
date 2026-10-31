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
  // reveal (0〜1): 1 未満なら、左下から右上へ色面が入る途中まで描く。
  void drawFill(const ofPolyline& polygon, const MaterialComponent& material,
                float reveal = 1.0f) const {
    if (reveal <= 0.0f || polygon.size() < 3) return;
    if (reveal < 1.0f) {
      drawFillWipe(polygon, material, reveal);
      return;
    }
    if (material.type == MaterialType::Solid) {
      shapePainter.drawFill(polygon, material.primary);
      return;
    }
    drawLinearGradientFill(polygon, material);
  }

  // reveal (0〜1): 1 未満なら、輪郭を一筆書きのように途中までなぞって描く。
  void drawStroke(const ofPolyline& polygon,
                  const MaterialComponent& material, float strokeWeight,
                  StrokeJoinType joinType, float reveal = 1.0f) const {
    if (!std::isfinite(strokeWeight) || strokeWeight <= 0.0f) return;
    if (reveal <= 0.0f || polygon.size() < 3) return;
    if (reveal < 1.0f) {
      const std::vector<glm::vec2> path = ShapePainter::revealPath(polygon, reveal);
      if (material.type == MaterialType::Solid) {
        shapePainter.drawStrokePath(path, false, material.primary, strokeWeight,
                                    joinType);
      } else {
        // グラデーションの向きと範囲は、全体の輪郭から決める (色が途中で動かない)。
        drawLinearGradientStroke(path, false, gradientAxis(polygon, material),
                                 material, strokeWeight, joinType);
      }
      return;
    }
    if (material.type == MaterialType::Solid) {
      shapePainter.drawStroke(polygon, material.primary, strokeWeight, joinType);
      return;
    }
    drawLinearGradientStroke(polygon.getVertices(), true,
                             gradientAxis(polygon, material), material,
                             strokeWeight, joinType);
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

  static void drawLinearGradientFill(const ofPolyline& polygon,
                                     const MaterialComponent& material) {
    if (polygon.size() < 3) return;
    const GradientAxis axis = gradientAxis(polygon, material);
    ofMesh mesh = ShapePainter::fillMesh(polygon);
    mesh.clearColors();
    for (const auto& vertex : mesh.getVertices()) {
      mesh.addColor(colorAt(glm::vec2(vertex.x, vertex.y), axis, material));
    }
    ofSetColor(255);
    mesh.draw();
  }

  // 塗りを、左下からの斜めの境界線で切り取って描く (色はそのまま)。
  static void drawFillWipe(const ofPolyline& polygon,
                           const MaterialComponent& material, float reveal) {
    const ofMesh& full = ShapePainter::fillMesh(polygon);
    if (full.getMode() != OF_PRIMITIVE_TRIANGLES) return;
    ofMesh mesh = full;
    if (material.type != MaterialType::Solid) {
      const GradientAxis axis = gradientAxis(polygon, material);
      mesh.clearColors();
      for (const auto& vertex : mesh.getVertices()) {
        mesh.addColor(colorAt(glm::vec2(vertex.x, vertex.y), axis, material));
      }
      ofSetColor(255);
    } else {
      mesh.clearColors();
      ofSetColor(material.primary);
    }
    ofMesh clipped = ShapePainter::clipMeshForWipe(mesh, reveal, polygon);
    if (!clipped.getVertices().empty()) clipped.draw();
  }

  template <typename Point>
  static void drawLinearGradientStroke(const std::vector<Point>& points, bool closed,
                                       const GradientAxis& axis,
                                       const MaterialComponent& material,
                                       float strokeWeight,
                                       StrokeJoinType joinType) {
    const size_t n = points.size();
    if (n < 2 || (closed && n < 3)) return;
    const auto polygon = [&points](size_t i) { return glm::vec2(points[i].x, points[i].y); };
    const float halfWeight = strokeWeight * 0.5f;
    ofMesh segments;
    ofMesh joints;
    segments.setMode(OF_PRIMITIVE_TRIANGLES);
    joints.setMode(OF_PRIMITIVE_TRIANGLES);
    auto triangle = [](ofMesh& mesh, const glm::vec2& a,
                       const glm::vec2& b, const glm::vec2& c,
                       const ofColor& ca, const ofColor& cb, const ofColor& cc) {
      mesh.addVertex(glm::vec3(a, 0.0f)); mesh.addColor(ca);
      mesh.addVertex(glm::vec3(b, 0.0f)); mesh.addColor(cb);
      mesh.addVertex(glm::vec3(c, 0.0f)); mesh.addColor(cc);
    };
    for (size_t i = 0; i < n; ++i) {
      const glm::vec2 p1 = polygon(i);
      const glm::vec2 p2 = polygon((i + 1) % n);
      const glm::vec2 previous = polygon((i - 1 + n) % n);
      const glm::vec2 direction = p2 - p1;
      const float length = glm::length(direction);
      const ofColor firstColor = colorAt(p1, axis, material);
      const ofColor secondColor = colorAt(p2, axis, material);
      // 途中で止まる線は、最後の点から最初の点へは結ばない。
      const bool hasSegment = closed || i + 1 < n;
      if (length > 0.0f && hasSegment) {
        const glm::vec2 normal = glm::vec2(-direction.y, direction.x) / length * halfWeight;
        triangle(segments, p1 + normal, p1 - normal, p2 + normal,
                 firstColor, firstColor, secondColor);
        triangle(segments, p2 + normal, p1 - normal, p2 - normal,
                 secondColor, firstColor, secondColor);
      }

      if (joinType == StrokeJoinType::Round) {
        constexpr int circleSegments = 16;
        for (int j = 0; j < circleSegments; ++j) {
          const float a0 = TWO_PI * j / circleSegments;
          const float a1 = TWO_PI * (j + 1) / circleSegments;
          triangle(joints, p1,
                   p1 + halfWeight * glm::vec2(std::cos(a0), std::sin(a0)),
                   p1 + halfWeight * glm::vec2(std::cos(a1), std::sin(a1)),
                   firstColor, firstColor, firstColor);
        }
        continue;
      }
      if (!closed && (i == 0 || i + 1 == n)) continue;  // 線の端には角を付けない
      if (length <= 1e-6f || glm::length2(p1 - previous) <= 1e-12f) continue;
      const glm::vec2 incoming = glm::normalize(p1 - previous);
      const glm::vec2 outgoing = glm::normalize(p2 - p1);
      const glm::vec2 normal1(-incoming.y, incoming.x);
      const glm::vec2 normal2(-outgoing.y, outgoing.x);
      if (glm::length2(normal1 + normal2) <= 1e-12f) continue;
      const glm::vec2 miter = glm::normalize(normal1 + normal2);
      const float dot = glm::dot(normal1, miter);
      if (std::abs(dot) <= 0.05f) continue;
      float miterLength = (strokeWeight * 0.5f) / dot;
      if (miterLength > strokeWeight * 4.0f) miterLength = strokeWeight * 4.0f;
      const glm::vec2 outside = p1 + miter * miterLength;
      const glm::vec2 inside = p1 - miter * miterLength;
      triangle(joints, p1, p1 + normal1 * halfWeight, outside,
               firstColor, firstColor, firstColor);
      triangle(joints, p1, outside, p1 + normal2 * halfWeight,
               firstColor, firstColor, firstColor);
      triangle(joints, p1, p1 - normal1 * halfWeight, inside,
               firstColor, firstColor, firstColor);
      triangle(joints, p1, inside, p1 - normal2 * halfWeight,
               firstColor, firstColor, firstColor);
    }
    ofSetColor(255);
    if (!segments.getVertices().empty()) segments.draw();
    if (!joints.getVertices().empty()) joints.draw();
  }

  ShapePainter shapePainter;
};

}  // namespace gux
