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
  // nowSeconds: 経過時間。検出結果が変わらない間も expireStale() で古い記録を消す。
  std::vector<TrackedObject> update(const HumanContourData& detections,
                                    float nowSeconds) {
    expireStale(nowSeconds);
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
        tracks.push_back({nextId++, views[detectionIndex].centroid, 0, 0,
                          nowSeconds});
        state = &tracks.back();
      }

      state->centroid = views[detectionIndex].centroid;
      state->missedFrames = 0;
      state->lastSeenSeconds = nowSeconds;
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

  // 一定時間見えていない記録を消す。検出が0人のまま変わらない間は update() が
  // 呼ばれないため、毎フレームこちらを呼んで、前の人のIDが別人に再利用されるのを防ぐ。
  void expireStale(float nowSeconds) {
    // 時間が巻き戻った（動画のループ等）ときは基準を今に合わせる。
    for (auto& track : tracks) {
      if (nowSeconds < track.lastSeenSeconds) track.lastSeenSeconds = nowSeconds;
    }
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [this, nowSeconds](const TrackState& track) {
                         return nowSeconds - track.lastSeenSeconds >
                                maxMissedSeconds;
                       }),
        tracks.end());
  }

  // 検出が変わらず update() を呼ばないフレームで、今も見えている人の記録を保つ。
  void markSeen(const std::vector<TrackedObject>& visibleObjects,
                float nowSeconds) {
    for (const auto& object : visibleObjects) {
      for (auto& track : tracks) {
        if (track.id == object.id) {
          track.lastSeenSeconds = nowSeconds;
          break;
        }
      }
    }
  }

  void reset() {
    tracks.clear();
    nextId = 1;
  }

  // 対応付けの最大距離（px）。HumanGraphicsScene が画面の高さに合わせて設定する。
  float maxMatchDistance = 200.0f;
  std::uint32_t maxMissedFrames = 10;
  float maxMissedSeconds = 1.0f;

 private:
  struct TrackState {
    ObjectId id = 0;
    glm::vec2 centroid{0.0f, 0.0f};
    std::uint32_t missedFrames = 0;
    std::uint64_t ageFrames = 0;
    float lastSeenSeconds = 0.0f;
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
