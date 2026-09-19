#pragma once

#include "scene/SceneBehavior.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace gux {

// 接続された2Objectを、共通の周期と相反する変位・Scaleで連動させる。
class FloatingSceneBehavior final : public SceneBehavior {
 public:
  static constexpr std::string_view BehaviorId = "floating_behavior";
  std::string_view id() const override { return BehaviorId; }

  void reset() override { states.clear(); }

  void update(std::vector<SceneObject>& objects,
              const BehaviorContext& context) override {
    std::unordered_set<ObjectId> visibleIds;
    for (size_t i = 0; i < objects.size(); ++i) {
      auto& object = objects[i];
      visibleIds.insert(object.id);
      auto [iterator, inserted] = states.try_emplace(object.id);
      State& state = iterator->second;
      if (inserted || context.compositionUpdated) {
        state.anchorPosition = object.transform.position;
        state.anchorScale = object.transform.scale;
      }
    }

    const float deltaSeconds = std::clamp(context.deltaSeconds, 0.0f, 0.1f);
    const float response = 1.0f - std::exp(-4.0f * deltaSeconds);

    // 検知順のペアごとに一つの相互作用値を共有する。
    // 位置は鏡像的に応答し、Scaleは一方が大きくなるほど他方が小さくなる。
    for (size_t i = 0; i + 1 < objects.size(); i += 2) {
      auto& first = objects[i];
      auto& second = objects[i + 1];
      State& firstState = states.at(first.id);
      State& secondState = states.at(second.id);

      const glm::vec2 firstCenter = first.pivot + firstState.anchorPosition;
      const glm::vec2 secondCenter = second.pivot + secondState.anchorPosition;
      const glm::vec2 difference = secondCenter - firstCenter;
      const glm::vec2 direction = glm::length(difference) > 0.001f
          ? glm::normalize(difference) : glm::vec2(1.0f, 0.0f);
      const glm::vec2 perpendicular(-direction.y, direction.x);

      const float phase = pairPhase(first.id, second.id);
      const float interaction =
          std::sin(context.elapsedSeconds * 0.72f + phase);
      const float sharedAlong =
          std::sin(context.elapsedSeconds * 0.31f + phase * 0.67f) * 14.0f;
      const float sharedAcross =
          std::cos(context.elapsedSeconds * 0.27f + phase * 1.21f) * 10.0f;
      const glm::vec2 sharedMotion =
          direction * sharedAlong + perpendicular * sharedAcross;
      const glm::vec2 coupledMotion =
          perpendicular * (interaction * 28.0f) +
          direction * (interaction * 12.0f);

      const glm::vec2 firstTarget = sharedMotion + coupledMotion;
      const glm::vec2 secondTarget = sharedMotion - coupledMotion;
      firstState.motionOffset = glm::mix(
          firstState.motionOffset, firstTarget, response);
      secondState.motionOffset = glm::mix(
          secondState.motionOffset, secondTarget, response);

      constexpr float scaleInfluence = 0.14f;
      firstState.scaleFactor = ofLerp(
          firstState.scaleFactor, 1.0f + interaction * scaleInfluence,
          response);
      secondState.scaleFactor = ofLerp(
          secondState.scaleFactor, 1.0f - interaction * scaleInfluence,
          response);

      first.transform.position =
          firstState.anchorPosition + firstState.motionOffset;
      second.transform.position =
          secondState.anchorPosition + secondState.motionOffset;
      first.transform.scale =
          firstState.anchorScale * firstState.scaleFactor;
      second.transform.scale =
          secondState.anchorScale * secondState.scaleFactor;
    }

    // 接続相手のいない最後のObjectは、基準位置・Scaleを維持する。
    if (objects.size() % 2 != 0) {
      auto& object = objects.back();
      State& state = states.at(object.id);
      state.motionOffset = glm::mix(
          state.motionOffset, glm::vec2(0.0f), response);
      state.scaleFactor = ofLerp(state.scaleFactor, 1.0f, response);
      object.transform.position = state.anchorPosition + state.motionOffset;
      object.transform.scale = state.anchorScale * state.scaleFactor;
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
    glm::vec2 anchorPosition{0.0f};
    glm::vec2 anchorScale{1.0f};
    glm::vec2 motionOffset{0.0f};
    float scaleFactor = 1.0f;
  };

  static float pairPhase(ObjectId first, ObjectId second) {
    std::uint64_t seed = first;
    seed ^= second + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    constexpr float twoPi = 6.28318530718f;
    return static_cast<float>(seed % 10000ULL) / 10000.0f * twoPi;
  }

  std::unordered_map<ObjectId, State> states;
};

}  // namespace gux
