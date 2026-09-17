#pragma once

#include "scene/SceneBehavior.h"

namespace gux {

class StandardSceneBehavior final : public SceneBehavior {
 public:
  static constexpr std::string_view BehaviorId = "standard_behavior";
  std::string_view id() const override { return BehaviorId; }
  void reset() override {}
  void update(std::vector<SceneObject>&, const BehaviorContext&) override {}
};

}  // namespace gux
