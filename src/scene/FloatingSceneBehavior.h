#pragma once

#include "scene/SceneBehavior.h"

#include <unordered_map>
#include <unordered_set>

namespace gux {

// オブジェクトごとのNoise移動・拡縮に、接続相手のScaleを緩く混ぜる。
class FloatingSceneBehavior final : public SceneBehavior {
 public:
  static constexpr std::string_view BehaviorId = "floating_behavior";
  std::string_view id() const override { return BehaviorId; }

  void reset() override { states.clear(); }

  void update(std::vector<SceneObject>& objects,
              const BehaviorContext& context) override {
    std::unordered_set<ObjectId> visibleIds;
    std::vector<float> noiseScales(objects.size(), 1.0f);
    for (size_t i = 0; i < objects.size(); ++i) {
      auto& object = objects[i];
      visibleIds.insert(object.id);
      auto [iterator, inserted] = states.try_emplace(object.id);
      State& state = iterator->second;
      if (inserted) state.seed = seedFor(object.id);
      if (inserted || context.compositionUpdated) {
        state.anchorPosition = object.transform.position;
        state.anchorScale = object.transform.scale;
      }

      const float t = context.elapsedSeconds * 0.16f;
      const float x = (ofNoise(state.seed, t) - 0.5f) * 100.0f;
      const float y = (ofNoise(state.seed + 41.0f, t * 0.83f) - 0.5f) * 100.0f;
      object.transform.position = state.anchorPosition + glm::vec2(x, y);
      noiseScales[i] = 0.82f + ofNoise(state.seed + 83.0f, t * 0.72f) * 0.36f;
    }

    // 検知順に2つずつ接続し、互いのScaleを22%だけ混ぜる。
    for (size_t i = 0; i + 1 < objects.size(); i += 2) {
      const float first = noiseScales[i];
      const float second = noiseScales[i + 1];
      noiseScales[i] = ofLerp(first, second, 0.22f);
      noiseScales[i + 1] = ofLerp(second, first, 0.22f);
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      const State& state = states.at(objects[i].id);
      objects[i].transform.scale = state.anchorScale * noiseScales[i];
    }

    for (auto iterator = states.begin(); iterator != states.end();) {
      if (visibleIds.count(iterator->first) == 0) {
        iterator = states.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }

 private:
  struct State {
    float seed = 0.0f;
    glm::vec2 anchorPosition{0.0f};
    glm::vec2 anchorScale{1.0f};
  };

  static float seedFor(ObjectId id) {
    return static_cast<float>((id * 2654435761ULL) % 100000ULL) * 0.01f;
  }

  std::unordered_map<ObjectId, State> states;
};

}  // namespace gux
