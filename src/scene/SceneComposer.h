#pragma once

#include "geometry/GeometryObject.h"
#include "scene/SceneObject.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace gux {

enum class MergeMode {
  RenderedGraphic,
  CentroidDistance
};

struct CompositionSettings {
  bool enableMerge = true;
  bool enableBase = true;
  bool enableOffset = true;
  bool enableStroke = true;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  float strokeWeight = 10.0f;
  MergeMode mergeMode = MergeMode::RenderedGraphic;
  float centroidMergeDistance = 50.0f;
  float mergeMaskScale = 0.1f;
  float strokeMergeMaskMinScale = 0.25f;
  float strokeMergeApproximationPx = 6.0f;
  size_t strokeMergeMaxVertices = 64;
  int vertexCount = 16;
  int canvasWidth = 1;
  int canvasHeight = 1;
};

namespace composition_detail {

inline bool segmentIntersection(const glm::vec3& p1, const glm::vec3& p2,
                                const glm::vec3& p3, const glm::vec3& p4,
                                glm::vec3& result) {
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
  result = glm::vec3(p1.x + d1x * t, p1.y + d1y * t, p1.z);
  return true;
}

inline void removeUnwantedIntersections(std::vector<glm::vec3>& points) {
  constexpr int maximumIterations = 100;
  for (int iteration = 0; iteration < maximumIterations; ++iteration) {
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

inline float signedArea(const ofPolyline& polygon) {
  float area = 0.0f;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const auto& a = polygon[i];
    const auto& b = polygon[(i + 1) % polygon.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area * 0.5f;
}

inline bool pointInside(const glm::vec3& point, const ofPolyline& polygon) {
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const auto& a = polygon[i];
    const auto& b = polygon[j];
    const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
    if (crosses) inside = !inside;
  }
  return inside;
}

inline bool segmentIntersectionParameters(
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
    const glm::vec3& d, float& t, float& u) {
  const glm::vec3 ab = b - a;
  const glm::vec3 cd = d - c;
  const float denominator = ab.x * cd.y - ab.y * cd.x;
  if (std::fabs(denominator) < 1e-6f) return false;
  const glm::vec3 ac = c - a;
  t = (ac.x * cd.y - ac.y * cd.x) / denominator;
  u = (ac.x * ab.y - ac.y * ab.x) / denominator;
  constexpr float epsilon = 1e-5f;
  return t >= -epsilon && t <= 1.0f + epsilon &&
         u >= -epsilon && u <= 1.0f + epsilon;
}

inline bool mergeGroupPreservingVertices(
    const std::vector<ofPolyline>& inputPolygons, ofPolyline& merged) {
  if (inputPolygons.size() < 2) return false;
  std::vector<ofPolyline> polygons = inputPolygons;
  const float referenceArea = signedArea(polygons.front());
  for (auto& polygon : polygons) {
    if (polygon.size() < 3) return false;
    if (referenceArea * signedArea(polygon) < 0.0f) {
      std::reverse(polygon.getVertices().begin(),
                   polygon.getVertices().end());
    }
    polygon.setClosed(true);
  }

  struct BoundarySegment {
    glm::vec3 start;
    glm::vec3 end;
    bool used = false;
  };
  std::vector<BoundarySegment> boundarySegments;
  for (size_t polygonIndex = 0; polygonIndex < polygons.size(); ++polygonIndex) {
    const auto& polygon = polygons[polygonIndex];
    for (size_t edgeIndex = 0; edgeIndex < polygon.size(); ++edgeIndex) {
      const glm::vec3 start = polygon[edgeIndex];
      const glm::vec3 end = polygon[(edgeIndex + 1) % polygon.size()];
      std::vector<float> cuts{0.0f, 1.0f};
      for (size_t otherIndex = 0; otherIndex < polygons.size(); ++otherIndex) {
        if (otherIndex == polygonIndex) continue;
        const auto& other = polygons[otherIndex];
        for (size_t otherEdge = 0; otherEdge < other.size(); ++otherEdge) {
          float t = 0.0f;
          float u = 0.0f;
          if (segmentIntersectionParameters(
                  start, end, other[otherEdge],
                  other[(otherEdge + 1) % other.size()], t, u)) {
            cuts.push_back(ofClamp(t, 0.0f, 1.0f));
          }
        }
      }

      std::sort(cuts.begin(), cuts.end());
      cuts.erase(std::unique(cuts.begin(), cuts.end(),
                             [](float a, float b) {
                               return std::fabs(a - b) < 1e-5f;
                             }),
                 cuts.end());
      for (size_t cutIndex = 0; cutIndex + 1 < cuts.size(); ++cutIndex) {
        const float t0 = cuts[cutIndex];
        const float t1 = cuts[cutIndex + 1];
        if (t1 - t0 < 1e-5f) continue;
        const glm::vec3 segmentStart = start + (end - start) * t0;
        const glm::vec3 segmentEnd = start + (end - start) * t1;
        const glm::vec3 midpoint = (segmentStart + segmentEnd) * 0.5f;
        bool insideAnotherPolygon = false;
        for (size_t otherIndex = 0; otherIndex < polygons.size(); ++otherIndex) {
          if (otherIndex != polygonIndex &&
              pointInside(midpoint, polygons[otherIndex])) {
            insideAnotherPolygon = true;
            break;
          }
        }
        if (!insideAnotherPolygon) {
          boundarySegments.push_back({segmentStart, segmentEnd, false});
        }
      }
    }
  }
  if (boundarySegments.size() < 3) return false;

  constexpr float joinDistanceSquared = 0.25f;
  std::vector<std::vector<glm::vec3>> closedLoops;
  for (size_t firstIndex = 0; firstIndex < boundarySegments.size(); ++firstIndex) {
    if (boundarySegments[firstIndex].used) continue;
    std::vector<glm::vec3> loop;
    auto& first = boundarySegments[firstIndex];
    first.used = true;
    loop.push_back(first.start);
    glm::vec3 current = first.end;
    bool closed = false;
    for (size_t step = 0; step <= boundarySegments.size(); ++step) {
      if (glm::length2(current - loop.front()) <= joinDistanceSquared) {
        closed = true;
        break;
      }
      loop.push_back(current);
      size_t nextIndex = boundarySegments.size();
      bool reverseNext = false;
      for (size_t candidate = 0; candidate < boundarySegments.size(); ++candidate) {
        if (boundarySegments[candidate].used) continue;
        if (glm::length2(boundarySegments[candidate].start - current) <=
            joinDistanceSquared) {
          nextIndex = candidate;
          break;
        }
        if (glm::length2(boundarySegments[candidate].end - current) <=
            joinDistanceSquared) {
          nextIndex = candidate;
          reverseNext = true;
        }
      }
      if (nextIndex == boundarySegments.size()) break;
      auto& next = boundarySegments[nextIndex];
      next.used = true;
      current = reverseNext ? next.start : next.end;
    }
    if (closed && loop.size() >= 3) closedLoops.push_back(std::move(loop));
  }
  if (closedLoops.size() != 1) return false;

  auto points = std::move(closedLoops.front());
  removeUnwantedIntersections(points);
  removeCoincidentPoints(points);
  if (points.size() < 3) return false;
  merged.clear();
  for (const auto& point : points) merged.addVertex(point);
  merged.setClosed(true);
  return true;
}

inline glm::vec3 centroid(const ofPolyline& polygon) {
  glm::vec3 result(0.0f);
  for (const auto& point : polygon) result += point;
  return polygon.size() == 0
      ? result
      : result / static_cast<float>(polygon.size());
}

inline std::vector<cv::Point> toCvContour(const ofPolyline& polygon,
                                          float scale, int padding) {
  std::vector<cv::Point> result;
  result.reserve(polygon.size());
  for (const auto& point : polygon) {
    result.emplace_back(
        static_cast<int>(std::lround(point.x * scale)) + padding,
        static_cast<int>(std::lround(point.y * scale)) + padding);
  }
  return result;
}

inline ObjectId combineId(ObjectId seed, ObjectId value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

}  // namespace composition_detail

class SceneComposer {
 public:
  std::vector<SceneObject> compose(
      const std::vector<GeometryObject>& geometryObjects,
      const CompositionSettings& settings) const {
    std::vector<ofPolyline> inputContours;
    inputContours.reserve(geometryObjects.size());
    for (const auto& object : geometryObjects) {
      inputContours.push_back(object.contour);
    }
    std::vector<ofPolyline> composedContours =
        mergeContours(inputContours, settings);

    std::vector<ObjectId> allSourceIds;
    allSourceIds.reserve(geometryObjects.size());
    ObjectId combinedId = 0;
    for (const auto& object : geometryObjects) {
      allSourceIds.push_back(object.id);
      combinedId = composition_detail::combineId(combinedId, object.id);
    }

    const bool oneToOne = composedContours.size() == geometryObjects.size();
    std::vector<SceneObject> result;
    result.reserve(composedContours.size());
    for (size_t i = 0; i < composedContours.size(); ++i) {
      SceneObject object;
      object.id = oneToOne
          ? geometryObjects[i].id
          : composition_detail::combineId(combinedId, i + 1);
      object.sourceObjectIds = oneToOne
          ? std::vector<ObjectId>{geometryObjects[i].id}
          : allSourceIds;
      object.geometry = std::move(composedContours[i]);
      const glm::vec3 center = composition_detail::centroid(object.geometry);
      object.pivot = glm::vec2(center.x, center.y);
      result.push_back(std::move(object));
    }
    return result;
  }

 private:
  static std::vector<ofPolyline> mergeContours(
      const std::vector<ofPolyline>& targetPolygons,
      const CompositionSettings& settings) {
    if (!settings.enableMerge || targetPolygons.size() < 2 ||
        (!settings.enableBase && !settings.enableStroke)) {
      return targetPolygons;
    }

    const bool strokeOnly = settings.enableStroke && !settings.enableBase;
    const float scale = strokeOnly
        ? std::max(settings.strokeMergeMaskMinScale, settings.mergeMaskScale)
        : std::max(0.05f, settings.mergeMaskScale);
    const float effectReach =
        (settings.enableOffset
             ? std::abs(settings.offsetSize) * std::abs(settings.offsetScale)
             : 0.0f) +
        (settings.enableStroke ? settings.strokeWeight * 0.5f : 0.0f) + 2.0f;
    const int padding = std::max(
        2, static_cast<int>(std::ceil(effectReach * scale)));
    const int maskWidth = std::max(
        1, static_cast<int>(std::lround(settings.canvasWidth * scale)) +
               padding * 2);
    const int maskHeight = std::max(
        1, static_cast<int>(std::lround(settings.canvasHeight * scale)) +
               padding * 2);

    std::vector<std::vector<cv::Point>> cvContours;
    cvContours.reserve(targetPolygons.size());
    for (const auto& polygon : targetPolygons) {
      cvContours.push_back(
          composition_detail::toCvContour(polygon, scale, padding));
    }

    if (settings.enableOffset) {
      std::vector<size_t> parents(targetPolygons.size());
      std::iota(parents.begin(), parents.end(), 0);
      const auto findRoot = [&parents](size_t index) {
        while (parents[index] != index) {
          parents[index] = parents[parents[index]];
          index = parents[index];
        }
        return index;
      };
      const auto unite = [&parents, &findRoot](size_t a, size_t b) {
        const size_t rootA = findRoot(a);
        const size_t rootB = findRoot(b);
        if (rootA != rootB) parents[rootB] = rootA;
      };

      std::vector<cv::Mat> graphicMasks;
      graphicMasks.reserve(targetPolygons.size());
      for (const auto& contour : cvContours) {
        cv::Mat mask = cv::Mat::zeros(maskHeight, maskWidth, CV_8UC1);
        const std::vector<std::vector<cv::Point>> oneContour{contour};
        if (settings.enableBase) {
          cv::fillPoly(mask, oneContour, cv::Scalar(255), cv::LINE_8);
        }
        if (settings.enableStroke) {
          const int thickness = std::max(
              1, static_cast<int>(std::lround(settings.strokeWeight * scale)));
          cv::polylines(mask, oneContour, true, cv::Scalar(255), thickness,
                        cv::LINE_8);
        }
        graphicMasks.push_back(std::move(mask));
      }

      cv::Mat overlapMask;
      for (size_t i = 0; i < targetPolygons.size(); ++i) {
        const glm::vec3 a = composition_detail::centroid(targetPolygons[i]);
        for (size_t j = i + 1; j < targetPolygons.size(); ++j) {
          cv::bitwise_and(graphicMasks[i], graphicMasks[j], overlapMask);
          const bool graphicsTouch = cv::countNonZero(overlapMask) > 0;
          const bool centroidsAreClose =
              settings.mergeMode == MergeMode::CentroidDistance &&
              glm::distance(a, composition_detail::centroid(targetPolygons[j])) <=
                  settings.centroidMergeDistance;
          if (graphicsTouch || centroidsAreClose) unite(i, j);
        }
      }

      std::vector<size_t> roots;
      std::vector<size_t> groupIndices(targetPolygons.size());
      for (size_t i = 0; i < targetPolygons.size(); ++i) {
        const size_t root = findRoot(i);
        const auto rootIterator = std::find(roots.begin(), roots.end(), root);
        if (rootIterator == roots.end()) {
          roots.push_back(root);
          groupIndices[i] = roots.size() - 1;
        } else {
          groupIndices[i] = static_cast<size_t>(
              std::distance(roots.begin(), rootIterator));
        }
      }

      std::vector<std::vector<ofPolyline>> groups(roots.size());
      for (size_t i = 0; i < targetPolygons.size(); ++i) {
        groups[groupIndices[i]].push_back(targetPolygons[i]);
      }

      std::vector<ofPolyline> mergedPolygons;
      mergedPolygons.reserve(targetPolygons.size());
      for (const auto& group : groups) {
        if (group.size() == 1) {
          mergedPolygons.push_back(group.front());
          continue;
        }
        ofPolyline mergedPolygon;
        if (!composition_detail::mergeGroupPreservingVertices(
                group, mergedPolygon)) {
          // Offset固有の食い込み線と頂点密度を失うラスタ輪郭へ退避しない。
          mergedPolygons.insert(mergedPolygons.end(), group.begin(), group.end());
        } else {
          mergedPolygons.push_back(std::move(mergedPolygon));
        }
      }
      return mergedPolygons;
    }

    cv::Mat mergeMask = cv::Mat::zeros(maskHeight, maskWidth, CV_8UC1);
    if (settings.enableBase) {
      cv::fillPoly(mergeMask, cvContours, cv::Scalar(255), cv::LINE_8);
    }
    if (settings.enableStroke) {
      const int thickness = std::max(
          1, static_cast<int>(std::lround(settings.strokeWeight * scale)));
      cv::polylines(mergeMask, cvContours, true, cv::Scalar(255), thickness,
                    cv::LINE_8);
    }

    if (settings.mergeMode == MergeMode::CentroidDistance) {
      const int bridgeThickness = std::max(
          1, static_cast<int>(std::lround(
                 std::max(settings.strokeWeight, 10.0f) * scale)));
      for (size_t i = 0; i < targetPolygons.size(); ++i) {
        const glm::vec3 a = composition_detail::centroid(targetPolygons[i]);
        for (size_t j = i + 1; j < targetPolygons.size(); ++j) {
          const glm::vec3 b = composition_detail::centroid(targetPolygons[j]);
          if (glm::distance(a, b) <= settings.centroidMergeDistance) {
            cv::line(mergeMask,
                     cv::Point(std::lround(a.x * scale) + padding,
                               std::lround(a.y * scale) + padding),
                     cv::Point(std::lround(b.x * scale) + padding,
                               std::lround(b.y * scale) + padding),
                     cv::Scalar(255), bridgeThickness, cv::LINE_8);
          }
        }
      }
    }

    std::vector<std::vector<cv::Point>> mergedContours;
    cv::findContours(mergeMask, mergedContours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_TC89_KCOS);
    if (mergedContours.size() >= targetPolygons.size()) return targetPolygons;

    std::vector<ofPolyline> mergedPolygons;
    mergedPolygons.reserve(mergedContours.size());
    for (const auto& cvContour : mergedContours) {
      if (cvContour.size() < 3) continue;
      std::vector<cv::Point> simplified;
      float approximation = strokeOnly
          ? std::max(0.5f, settings.strokeMergeApproximationPx * scale)
          : 1.0f;
      cv::approxPolyDP(cvContour, simplified, approximation, true);
      while (strokeOnly &&
             simplified.size() > settings.strokeMergeMaxVertices) {
        approximation *= 1.5f;
        cv::approxPolyDP(cvContour, simplified, approximation, true);
      }
      if (simplified.size() < 3) continue;

      ofPolyline polygon;
      for (const auto& point : simplified) {
        polygon.addVertex((point.x - padding) / scale,
                          (point.y - padding) / scale);
      }
      polygon.setClosed(true);
      if (strokeOnly) {
        mergedPolygons.push_back(std::move(polygon));
      } else {
        mergedPolygons.push_back(
            polygon.getResampledByCount(settings.vertexCount));
        mergedPolygons.back().setClosed(true);
      }
    }
    return mergedPolygons;
  }
};

}  // namespace gux
