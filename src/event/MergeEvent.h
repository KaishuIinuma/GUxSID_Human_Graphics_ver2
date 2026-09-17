#pragma once

#include "scene/SceneObject.h"

#include <string_view>

namespace gux {

struct MergeEventContext {
  size_t sourceObjectCount = 0;
  size_t composedObjectCount = 0;
  bool justStarted = false;
};

// SceneComposerが結合を成立させた後に、SceneObjectへ反応を適用する境界。
class MergeEvent {
 public:
  virtual ~MergeEvent() = default;
  virtual std::string_view id() const = 0;
  virtual void apply(std::vector<SceneObject>& objects,
                     const MergeEventContext& context) const = 0;
};

}  // namespace gux
