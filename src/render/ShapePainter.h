#pragma once

#include "core/VisualTypes.h"
#include "ofMain.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gux {

// RenderRecipeから再利用する、状態を持たない低レベル描画機能。
// ストロークのセグメントとジョイントをそれぞれメッシュに集約する。
class ShapePainter {
 public:
  void drawFill(const ofPolyline& polygon, const ofColor& color) const {
    if (polygon.size() < 3) return;
    ofSetColor(color);
    fillMesh(polygon).draw();
  }

  // SceneObject の幾何が同じ間は、塗りのテッセレーションを再利用する。
  static const ofMesh& fillMesh(const ofPolyline& polygon) {
    struct CachedFill { uint64_t hash; ofPolyline polygon; ofMesh mesh; };
    static std::vector<CachedFill> cache;
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](float value) {
      uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      hash = (hash ^ bits) * 1099511628211ULL;
    };
    for (const auto& point : polygon) { mix(point.x); mix(point.y); mix(point.z); }
    for (const auto& entry : cache) {
      if (entry.hash != hash) continue;
      if (entry.polygon.size() != polygon.size()) continue;
      bool equal = true;
      for (size_t i = 0; i < polygon.size(); ++i) {
        if (entry.polygon[i] != polygon[i]) { equal = false; break; }
      }
      if (equal) return entry.mesh;
    }
    ofPath path;
    path.setFilled(true);
    path.moveTo(polygon[0]);
    for (size_t i = 1; i < polygon.size(); ++i) path.lineTo(polygon[i]);
    path.close();
    ofMesh mesh = path.getTessellation();
    if (cache.size() >= 256) cache.clear();
    cache.push_back({hash, polygon, std::move(mesh)});
    return cache.back().mesh;
  }

  void drawStroke(const ofPolyline& polygon, const ofColor& color,
                  float strokeWeight, StrokeJoinType joinType) const {
    if (polygon.size() < 3 || !std::isfinite(strokeWeight) || strokeWeight <= 0.0f) return;
    ofSetColor(color);
    ofFill();

    const size_t n = polygon.size();
    const float halfWeight = strokeWeight * 0.5f;

    // セグメント矩形を三角形メッシュにまとめる。
    ofMesh segmentMesh;
    segmentMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    segmentMesh.getVertices().reserve(n * 6); // 各セグメント = 2三角形 = 6頂点

    for (size_t i = 0; i < n; ++i) {
      const glm::vec2 p1 = polygon[i];
      const glm::vec2 p2 = polygon[(i + 1) % n];
      const glm::vec2 direction = p2 - p1;
      const float length = glm::length(direction);
      if (length <= 0.0f) continue;

      const glm::vec2 normal = glm::vec2(-direction.y, direction.x) / length * halfWeight;

      // quad を TRIANGLE で追加 (p1+n, p1-n, p2+n, p2-n)
      const glm::vec3 v0(p1 + normal, 0.0f);
      const glm::vec3 v1(p1 - normal, 0.0f);
      const glm::vec3 v2(p2 + normal, 0.0f);
      const glm::vec3 v3(p2 - normal, 0.0f);
      segmentMesh.addVertex(v0); segmentMesh.addVertex(v1); segmentMesh.addVertex(v2);
      segmentMesh.addVertex(v2); segmentMesh.addVertex(v1); segmentMesh.addVertex(v3);
    }
    if (!segmentMesh.getVertices().empty()) {
      segmentMesh.draw();
    }

    // ジョイントも一つのメッシュにまとめる。
    if (joinType == StrokeJoinType::Round) {
      ofMesh jointMesh;
      jointMesh.setMode(OF_PRIMITIVE_TRIANGLES);
      constexpr int circleSegments = 16;
      jointMesh.getVertices().reserve(n * circleSegments * 3);
      for (size_t i = 0; i < n; ++i) {
        const glm::vec2 center = polygon[i];
        for (int j = 0; j < circleSegments; ++j) {
          const float a0 = TWO_PI * j / circleSegments;
          const float a1 = TWO_PI * (j + 1) / circleSegments;
          jointMesh.addVertex(glm::vec3(center, 0.0f));
          jointMesh.addVertex(glm::vec3(center + halfWeight * glm::vec2(std::cos(a0), std::sin(a0)), 0.0f));
          jointMesh.addVertex(glm::vec3(center + halfWeight * glm::vec2(std::cos(a1), std::sin(a1)), 0.0f));
        }
      }
      jointMesh.draw();
    } else {
      // Straight (Miter) ジョイントを1メッシュに集約
      ofMesh miterMesh;
      miterMesh.setMode(OF_PRIMITIVE_TRIANGLES);
      miterMesh.getVertices().reserve(n * 12);

      for (size_t i = 0; i < n; ++i) {
        const glm::vec2 p1 = polygon[i];
        const glm::vec2 p2 = polygon[(i + 1) % n];
        const glm::vec2 previous = polygon[(i - 1 + n) % n];
        const glm::vec2 direction = p2 - p1;
        const float length = glm::length(direction);

        if (length <= 1e-6f || glm::length2(p1 - previous) <= 1e-12f) continue;
        const glm::vec2 incoming = glm::normalize(p1 - previous);
        const glm::vec2 outgoing = glm::normalize(p2 - p1);
        const glm::vec2 normal1(-incoming.y, incoming.x);
        const glm::vec2 normal2(-outgoing.y, outgoing.x);
        if (glm::length2(normal1 + normal2) <= 1e-12f) continue;
        const glm::vec2 miter = glm::normalize(normal1 + normal2);
        const float dot = glm::dot(normal1, miter);
        if (std::abs(dot) <= 0.05f) continue;

        float miterLength = halfWeight / dot;
        if (miterLength > strokeWeight * 4.0f) miterLength = strokeWeight * 4.0f;

        const glm::vec2 outside = p1 + miter * miterLength;
        const glm::vec2 inside  = p1 - miter * miterLength;

        // outside joint (triangle fan → 2 triangles)
        const glm::vec3 c(p1, 0.0f);
        const glm::vec3 o(outside, 0.0f);
        const glm::vec3 n1a(p1 + normal1 * halfWeight, 0.0f);
        const glm::vec3 n2a(p1 + normal2 * halfWeight, 0.0f);
        miterMesh.addVertex(c); miterMesh.addVertex(n1a); miterMesh.addVertex(o);
        miterMesh.addVertex(c); miterMesh.addVertex(o);   miterMesh.addVertex(n2a);

        // inside joint
        const glm::vec3 in(inside, 0.0f);
        const glm::vec3 n1b(p1 - normal1 * halfWeight, 0.0f);
        const glm::vec3 n2b(p1 - normal2 * halfWeight, 0.0f);
        miterMesh.addVertex(c); miterMesh.addVertex(n1b); miterMesh.addVertex(in);
        miterMesh.addVertex(c); miterMesh.addVertex(in);  miterMesh.addVertex(n2b);
      }
      if (!miterMesh.getVertices().empty()) {
        miterMesh.draw();
      }
    }
  }
};

}  // namespace gux
