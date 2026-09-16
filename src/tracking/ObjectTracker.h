#pragma once

#include "core/HumanContourData.h"
#include "tracking/TrackedObject.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace gux {

// 現時点では重心の最近傍で対応付ける軽量Tracker。
// SceneObjectのID契約を先に固定し、将来Kalman/IoU Trackerへ交換できる境界にする。
class ObjectTracker {
 public:
  std::vector<TrackedObject> update(const HumanContourData& detections) {
    struct DetectionView {
      size_t index = 0;
      glm::vec2 centroid{0.0f, 0.0f};
    };
    struct Candidate {
      size_t detectionIndex = 0;
      size_t trackIndex = 0;
      float distance = 0.0f;
    };

    std::vector<DetectionView> views;
    views.reserve(detections.contours.size());
    for (size_t i = 0; i < detections.contours.size(); ++i) {
      glm::vec2 centroid = i < detections.centroids.size()
          ? detections.centroids[i]
          : contourCentroid(detections.contours[i]);
      views.push_back({i, centroid});
    }

    std::vector<Candidate> candidates;
    for (size_t detectionIndex = 0; detectionIndex < views.size(); ++detectionIndex) {
      for (size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const float distance = glm::distance(
            views[detectionIndex].centroid, tracks[trackIndex].centroid);
        if (distance <= maxMatchDistance) {
          candidates.push_back({detectionIndex, trackIndex, distance});
        }
      }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                return a.distance < b.distance;
              });

    std::vector<int> detectionToTrack(views.size(), -1);
    std::vector<bool> trackUsed(tracks.size(), false);
    for (const auto& candidate : candidates) {
      if (detectionToTrack[candidate.detectionIndex] >= 0 ||
          trackUsed[candidate.trackIndex]) {
        continue;
      }
      detectionToTrack[candidate.detectionIndex] =
          static_cast<int>(candidate.trackIndex);
      trackUsed[candidate.trackIndex] = true;
    }

    for (auto& track : tracks) ++track.missedFrames;

    std::vector<TrackedObject> visibleObjects;
    visibleObjects.reserve(views.size());
    for (size_t detectionIndex = 0; detectionIndex < views.size(); ++detectionIndex) {
      TrackState* state = nullptr;
      const int matchedTrack = detectionToTrack[detectionIndex];
      if (matchedTrack >= 0) {
        state = &tracks[static_cast<size_t>(matchedTrack)];
      } else {
        tracks.push_back({nextId++, views[detectionIndex].centroid, 0, 0});
        state = &tracks.back();
      }

      state->centroid = views[detectionIndex].centroid;
      state->missedFrames = 0;
      ++state->ageFrames;

      const size_t sourceIndex = views[detectionIndex].index;
      TrackedObject object;
      object.id = state->id;
      object.contour = detections.contours[sourceIndex];
      object.centroid = state->centroid;
      object.ageFrames = state->ageFrames;
      if (sourceIndex < detections.boundingBoxes.size()) {
        object.boundingBox = detections.boundingBoxes[sourceIndex];
      }
      visibleObjects.push_back(std::move(object));
    }

    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [this](const TrackState& track) {
                         return track.missedFrames > maxMissedFrames;
                       }),
        tracks.end());
    return visibleObjects;
  }

  void reset() {
    tracks.clear();
    nextId = 1;
  }

  float maxMatchDistance = 200.0f;
  std::uint32_t maxMissedFrames = 10;

 private:
  struct TrackState {
    ObjectId id = 0;
    glm::vec2 centroid{0.0f, 0.0f};
    std::uint32_t missedFrames = 0;
    std::uint64_t ageFrames = 0;
  };

  static glm::vec2 contourCentroid(const ofPolyline& contour) {
    glm::vec2 centroid(0.0f);
    if (contour.size() == 0) return centroid;
    for (const auto& point : contour) centroid += glm::vec2(point.x, point.y);
    return centroid / static_cast<float>(contour.size());
  }

  ObjectId nextId = 1;
  std::vector<TrackState> tracks;
};

}  // namespace gux
