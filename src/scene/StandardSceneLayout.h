#pragma once

#include "scene/SceneLayout.h"

namespace gux {

class StandardSceneLayout final : public SceneLayout {
 public:
  static constexpr std::string_view LayoutId = "standard_layout";
  std::string_view id() const override { return LayoutId; }
  void apply(std::vector<SceneObject>&, const LayoutContext&) const override {}
};

}  // namespace gux
