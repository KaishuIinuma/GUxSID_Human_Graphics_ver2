#pragma once

#include "scene/SceneObject.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace gux {

// 近いObject同士を2個ずつ組にし、組が連続するようobjectsを並べ替える。
// FloatingSceneBehaviorとFloatingBridgeRenderRecipeは(0,1),(2,3)...の並びを
// ペアとして扱うため、ここで並びを決めれば両者の組み合わせが一致する。
class ProximityPairing {
 public:
  void reset() {
    previousPairs.clear();
  }

  void reorder(std::vector<SceneObject>& objects) {
    const size_t count = objects.size();
    if (count < 2) {
      previousPairs.clear();
      return;
    }

    struct Candidate {
      float cost;
      size_t first;
      size_t second;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(count * (count - 1) / 2);
    for (size_t i = 0; i < count; ++i) {
      for (size_t j = i + 1; j < count; ++j) {
        float cost = glm::distance(center(objects[i]), center(objects[j]));
        // 直前と同じ組は少し有利にして、距離が拮抗した時の組み替わりを抑える。
        if (previousPairs.count(pairKey(objects[i].id, objects[j].id)) > 0) {
          cost *= kKeepPairBias;
        }
        candidates.push_back({cost, i, j});
      }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                if (a.cost != b.cost) return a.cost < b.cost;
                if (a.first != b.first) return a.first < b.first;
                return a.second < b.second;
              });

    std::vector<bool> used(count, false);
    std::vector<std::pair<size_t, size_t>> pairs;
    for (const auto& candidate : candidates) {
      if (used[candidate.first] || used[candidate.second]) continue;
      used[candidate.first] = used[candidate.second] = true;
      pairs.emplace_back(candidate.first, candidate.second);
    }
    // 元の並び(検知順)が早いペアから並べ、色の割り当て順を安定させる。
    std::sort(pairs.begin(), pairs.end());

    std::vector<SceneObject> ordered;
    ordered.reserve(count);
    previousPairs.clear();
    for (const auto& [first, second] : pairs) {
      previousPairs.insert(pairKey(objects[first].id, objects[second].id));
      ordered.push_back(std::move(objects[first]));
      ordered.push_back(std::move(objects[second]));
    }
    for (size_t i = 0; i < count; ++i) {
      if (!used[i]) ordered.push_back(std::move(objects[i]));
    }
    objects = std::move(ordered);
  }

 private:
  static constexpr float kKeepPairBias = 0.75f;

  static glm::vec2 center(const SceneObject& object) {
    return object.pivot + object.transform.position;
  }

  static std::pair<ObjectId, ObjectId> pairKey(ObjectId a, ObjectId b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
  }

  std::set<std::pair<ObjectId, ObjectId>> previousPairs;
};

}  // namespace gux
