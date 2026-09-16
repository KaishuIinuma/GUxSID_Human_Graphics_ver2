#pragma once

#include "core/VisualTypes.h"
#include "ofMain.h"

#include <cmath>
#include <vector>

namespace gux {
namespace offset_detail {

inline bool lineIntersection(const glm::vec3& p1, const glm::vec3& dir1,
                             const glm::vec3& p2, const glm::vec3& dir2,
                             glm::vec3& outPoint) {
  const float denominator = dir1.x * dir2.y - dir1.y * dir2.x;
  if (std::fabs(denominator) < 1e-6f) return false;
  const glm::vec3 difference = p2 - p1;
  const float t =
      (difference.x * dir2.y - difference.y * dir2.x) / denominator;
  outPoint = p1 + dir1 * t;
  return true;
}

inline bool segmentIntersection(const glm::vec3& p1, const glm::vec3& p2,
                                const glm::vec3& p3, const glm::vec3& p4,
                                glm::vec3& outPoint) {
  const float d1x = p2.x - p1.x;
  const float d1y = p2.y - p1.y;
  const float d2x = p4.x - p3.x;
  const float d2y = p4.y - p3.y;
  const float denominator = d1x * d2y - d1y * d2x;
  if (std::fabs(denominator) < 1e-6f) return false;
  const float dx = p3.x - p1.x;
  const float dy = p3.y - p1.y;
  const float t = (dx * d2y - dy * d2x) / denominator;
  const float u = (dx * d1y - dy * d1x) / denominator;
  constexpr float epsilon = 1e-4f;
  if (t <= epsilon || t >= 1.0f - epsilon ||
      u <= epsilon || u >= 1.0f - epsilon) {
    return false;
  }
  outPoint = glm::vec3(p1.x + d1x * t, p1.y + d1y * t, p1.z);
  return true;
}

inline void removeUnwantedIntersections(std::vector<glm::vec3>& points) {
  constexpr int maxIterations = 100;
  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    const int count = static_cast<int>(points.size());
    if (count < 5) return;
    bool cut = false;
    for (int i = 0; i < count && !cut; ++i) {
      const int nextI = (i + 1) % count;
      for (int j = i + 2; j < count; ++j) {
        const int nextJ = (j + 1) % count;
        if (nextJ <= i) continue;
        glm::vec3 intersection;
        if (!segmentIntersection(points[i], points[nextI], points[j],
                                 points[nextJ], intersection)) {
          continue;
        }
        std::vector<glm::vec3> reduced;
        reduced.reserve(count - (j - i) + 1);
        for (int k = 0; k <= i; ++k) reduced.push_back(points[k]);
        reduced.push_back(intersection);
        for (int k = nextJ; k < count; ++k) reduced.push_back(points[k]);
        if (reduced.size() >= static_cast<size_t>(count)) continue;
        points = std::move(reduced);
        cut = true;
        break;
      }
    }
    if (!cut) return;
  }
}

inline void removeCoincidentPoints(std::vector<glm::vec3>& points) {
  if (points.size() < 2) return;
  constexpr float minimumDistanceSquared = 0.01f;
  std::vector<glm::vec3> cleaned;
  cleaned.reserve(points.size());
  cleaned.push_back(points.front());
  for (size_t i = 1; i < points.size(); ++i) {
    if (glm::length2(points[i] - cleaned.back()) > minimumDistanceSquared) {
      cleaned.push_back(points[i]);
    }
  }
  if (cleaned.size() > 2 &&
      glm::length2(cleaned.front() - cleaned.back()) <=
          minimumDistanceSquared) {
    cleaned.pop_back();
  }
  points = std::move(cleaned);
}

}  // namespace offset_detail

class OffsetProcessor {
 public:
  ofPolyline process(const ofPolyline& polyline, float offset,
                     Scene4OffsetJoinType joinType, float arcRadius = -1.0f,
                     int arcResolution = 20) const {
    if (polyline.size() < 3) return polyline;
    if (arcResolution < 1) arcResolution = 1;
    const int count = static_cast<int>(polyline.size());

    float area = 0.0f;
    for (int i = 0; i < count; ++i) {
      const glm::vec3& p1 = polyline[i];
      const glm::vec3& p2 = polyline[(i + 1) % count];
      area += p1.x * p2.y - p2.x * p1.y;
    }
    const float direction = area > 0.0f ? -1.0f : 1.0f;
    const float orientationSign = area > 0.0f ? 1.0f : -1.0f;

    struct OffsetEdge {
      glm::vec3 start;
      glm::vec3 end;
      glm::vec3 direction;
      glm::vec3 normal;
    };
    std::vector<OffsetEdge> edges(count);
    glm::vec3 lastValidDirection(1.0f, 0.0f, 0.0f);
    glm::vec3 lastValidNormal(0.0f, -1.0f, 0.0f);

    for (int i = 0; i < count; ++i) {
      const glm::vec3 p1 = polyline[i];
      const glm::vec3 p2 = polyline[(i + 1) % count];
      glm::vec3 edgeDirection = p2 - p1;
      const float length = glm::length(edgeDirection);
      glm::vec3 normal;
      if (length < 0.0001f) {
        edgeDirection = lastValidDirection;
        normal = lastValidNormal;
      } else {
        edgeDirection /= length;
        normal = glm::vec3(-edgeDirection.y, edgeDirection.x, 0.0f) * direction;
        lastValidDirection = edgeDirection;
        lastValidNormal = normal;
      }
      edges[i] = {p1 + normal * offset, p2 + normal * offset,
                  edgeDirection, normal};
    }

    const float miterLimit = offset * 4.0f;
    std::vector<glm::vec3> points;
    points.reserve(count * (arcResolution + 2));
    for (int i = 0; i < count; ++i) {
      const int previousIndex = (i - 1 + count) % count;
      const glm::vec3 center = polyline[i];
      const glm::vec3& previousDirection = edges[previousIndex].direction;
      const glm::vec3& currentDirection = edges[i].direction;
      const float crossZ = previousDirection.x * currentDirection.y -
                           previousDirection.y * currentDirection.x;
      const bool isConvex = crossZ * orientationSign > 0.0f;

      if (isConvex && joinType == Scene4OffsetJoinType::Round) {
        const glm::vec3 startVector = edges[previousIndex].end - center;
        const glm::vec3 endVector = edges[i].start - center;
        const float baseRadius = glm::length(startVector);
        const float radius = arcRadius > 0.0f ? arcRadius : baseRadius;
        if (baseRadius < 0.0001f) {
          points.push_back(edges[previousIndex].end);
        } else {
          const float startAngle = std::atan2(startVector.y, startVector.x);
          const float endAngle = std::atan2(endVector.y, endVector.x);
          float angleDifference = endAngle - startAngle;
          while (angleDifference > PI) angleDifference -= TWO_PI;
          while (angleDifference < -PI) angleDifference += TWO_PI;
          for (int step = 0; step <= arcResolution; ++step) {
            const float t =
                static_cast<float>(step) / static_cast<float>(arcResolution);
            const float angle = startAngle + angleDifference * t;
            points.emplace_back(center.x + std::cos(angle) * radius,
                                center.y + std::sin(angle) * radius, center.z);
          }
        }
      } else {
        glm::vec3 intersection;
        if (offset_detail::lineIntersection(
                edges[previousIndex].start, previousDirection,
                edges[i].start, currentDirection, intersection) &&
            glm::length(intersection - center) <= miterLimit) {
          points.push_back(intersection);
        } else {
          points.push_back(edges[previousIndex].end);
          points.push_back(edges[i].start);
        }
      }
      points.push_back(edges[i].end);
    }

    offset_detail::removeUnwantedIntersections(points);
    offset_detail::removeCoincidentPoints(points);

    constexpr size_t maximumPoints = 2000;
    if (points.size() > maximumPoints) {
      std::vector<glm::vec3> reduced;
      reduced.reserve(maximumPoints);
      const float step =
          static_cast<float>(points.size()) / static_cast<float>(maximumPoints);
      for (size_t i = 0; i < maximumPoints; ++i) {
        reduced.push_back(points[static_cast<size_t>(i * step)]);
      }
      points = std::move(reduced);
    }

    ofPolyline result;
    for (const auto& point : points) result.addVertex(point);
    result.setClosed(true);
    return result;
  }
};

}  // namespace gux
