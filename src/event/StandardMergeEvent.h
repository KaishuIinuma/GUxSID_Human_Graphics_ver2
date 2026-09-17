#pragma once

#include "event/MergeEvent.h"

namespace gux {

// 従来互換: 結合後のSceneObjectを変更せず、そのまま描画へ渡す。
class StandardMergeEvent final : public MergeEvent {
 public:
  static constexpr std::string_view EventId = "standard_event";

  std::string_view id() const override { return EventId; }

  void apply(std::vector<SceneObject>&,
             const MergeEventContext&) const override {}
};

}  // namespace gux
