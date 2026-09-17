#pragma once

#include "scene/SceneObject.h"

#include <string_view>

namespace gux {

struct BehaviorContext {
  float elapsedSeconds = 0.0f;
  float deltaSeconds = 0.0f;
  bool compositionUpdated = false;
};

class SceneBehavior {
 public:
  virtual ~SceneBehavior() = default;
  virtual std::string_view id() const = 0;
  virtual void reset() = 0;
  virtual void update(std::vector<SceneObject>& objects,
                      const BehaviorContext& context) = 0;
};

}  // namespace gux
