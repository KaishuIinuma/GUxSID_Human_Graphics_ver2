#pragma once

#include "core/VisualTypes.h"
#include "ofMain.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

  // ---- 現れる演出 (SceneObject::reveal) 用 ----

  // 左下から右上へ進む向き (画面は y が下向き)。
  static glm::vec2 wipeDirection() { return glm::normalize(glm::vec2(1.0f, -1.0f)); }

  // 塗りのメッシュ (三角形) を、wipeDirection への射影が limit 以下の側だけ残して切り取る。
  // 色が付いていれば切り口の色も補間する。
  static ofMesh clipMeshForWipe(const ofMesh& source, float reveal,
                                const ofPolyline& polygon) {
    const glm::vec2 direction = wipeDirection();
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const auto& point : polygon) {
      const float d = glm::dot(glm::vec2(point.x, point.y), direction);
      minimum = std::min(minimum, d);
      maximum = std::max(maximum, d);
    }
    const float limit = minimum + (maximum - minimum) * reveal;

    ofMesh result;
    result.setMode(OF_PRIMITIVE_TRIANGLES);
    const auto& vertices = source.getVertices();
    const auto& colors = source.getColors();
    const bool hasColors = colors.size() == vertices.size();
    const auto& indices = source.getIndices();
    const size_t count = source.hasIndices() ? indices.size() : vertices.size();
    const auto indexAt = [&](size_t i) {
      return source.hasIndices() ? static_cast<size_t>(indices[i]) : i;
    };
    struct Corner { glm::vec3 position; ofFloatColor color; float distance; };
    const auto add = [&](const Corner& c) {
      result.addVertex(c.position);
      if (hasColors) result.addColor(c.color);
    };
    for (size_t t = 0; t + 2 < count; t += 3) {
      Corner corners[3];
      for (int k = 0; k < 3; ++k) {
        const size_t index = indexAt(t + k);
        corners[k].position = vertices[index];
        corners[k].color = hasColors ? colors[index] : ofFloatColor::white;
        corners[k].distance =
            glm::dot(glm::vec2(vertices[index].x, vertices[index].y), direction) - limit;
      }
      // Sutherland-Hodgman で1つの半平面に対して切る (結果は最大4角形)。
      Corner kept[4];
      int keptCount = 0;
      for (int k = 0; k < 3; ++k) {
        const Corner& a = corners[k];
        const Corner& b = corners[(k + 1) % 3];
        const bool aIn = a.distance <= 0.0f;
        const bool bIn = b.distance <= 0.0f;
        if (aIn) kept[keptCount++] = a;
        if (aIn != bIn) {
          const float u = a.distance / (a.distance - b.distance);
          Corner cut;
          cut.position = a.position + (b.position - a.position) * u;
          cut.color = a.color.getLerped(b.color, u);
          cut.distance = 0.0f;
          kept[keptCount++] = cut;
        }
      }
      for (int k = 1; k + 1 < keptCount; ++k) {
        add(kept[0]);
        add(kept[k]);
        add(kept[k + 1]);
      }
    }
    return result;
  }

  // 閉じた輪郭を、左下に一番近い頂点から一周なぞった時の、reveal (0〜1) までの道のり。
  static std::vector<glm::vec2> revealPath(const ofPolyline& polygon, float reveal) {
    std::vector<glm::vec2> path;
    const size_t n = polygon.size();
    if (n < 2) return path;
    const glm::vec2 direction = wipeDirection();
    size_t start = 0;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < n; ++i) {
      const float d = glm::dot(glm::vec2(polygon[i].x, polygon[i].y), direction);
      if (d < best) { best = d; start = i; }
    }
    float total = 0.0f;
    for (size_t i = 0; i < n; ++i) {
      total += glm::distance(glm::vec2(polygon[i]), glm::vec2(polygon[(i + 1) % n]));
    }
    float remaining = total * std::clamp(reveal, 0.0f, 1.0f);
    glm::vec2 current = polygon[start];
    path.push_back(current);
    for (size_t step = 0; step < n && remaining > 0.0f; ++step) {
      const glm::vec2 next = polygon[(start + step + 1) % n];
      const float length = glm::distance(current, next);
      if (length >= remaining) {
        path.push_back(current + (next - current) * (remaining / std::max(length, 1e-6f)));
        break;
      }
      path.push_back(next);
      remaining -= length;
      current = next;
    }
    return path;
  }

  void drawStroke(const ofPolyline& polygon, const ofColor& color,
                  float strokeWeight, StrokeJoinType joinType) const {
    if (polygon.size() < 3) return;
    drawStrokePath(polygon.getVertices(), true, color, strokeWeight, joinType);
  }

  // 閉じた輪郭 (closed=true) か、途中で止まる線 (closed=false) を描く。
  // 途中で止まる線の端は、Round なら丸く、Straight なら切りっぱなしにする。
  template <typename Point>
  void drawStrokePath(const std::vector<Point>& points, bool closed,
                      const ofColor& color, float strokeWeight,
                      StrokeJoinType joinType) const {
    const size_t n = points.size();
    if (n < 2 || (closed && n < 3) || !std::isfinite(strokeWeight) ||
        strokeWeight <= 0.0f) {
      return;
    }
    const auto polygon = [&points](size_t i) { return glm::vec2(points[i].x, points[i].y); };
    ofSetColor(color);
    ofFill();

    const float halfWeight = strokeWeight * 0.5f;
    const size_t segmentCount = closed ? n : n - 1;

    // セグメント矩形を三角形メッシュにまとめる。
    ofMesh segmentMesh;
    segmentMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    segmentMesh.getVertices().reserve(n * 6); // 各セグメント = 2三角形 = 6頂点

    for (size_t i = 0; i < segmentCount; ++i) {
      const glm::vec2 p1 = polygon(i);
      const glm::vec2 p2 = polygon((i + 1) % n);
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
        const glm::vec2 center = polygon(i);
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
        if (!closed && (i == 0 || i + 1 == n)) continue;  // 線の端には角を付けない
        const glm::vec2 p1 = polygon(i);
        const glm::vec2 p2 = polygon((i + 1) % n);
        const glm::vec2 previous = polygon((i - 1 + n) % n);
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
