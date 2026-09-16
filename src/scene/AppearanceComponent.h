#pragma once

#include "ofMain.h"

namespace gux {

// Materialの種類はRendererではなくSceneObjectの見た目として保持する。
enum class MaterialType {
  Solid = 0,
  LinearGradient = 1,
};

struct MaterialComponent {
  MaterialType type = MaterialType::Solid;
  ofColor primary = ofColor::white;
  ofColor secondary = ofColor::white;

  // Objectローカル座標でのグラデーション方向。移動・複製後も形状と一緒に動く。
  glm::vec2 gradientDirection{0.0f, 1.0f};
};

// UnityのMaterial slotに相当する、SceneObjectの外観コンポーネント。
struct AppearanceComponent {
  MaterialComponent baseMaterial;
  MaterialComponent outlineMaterial;
};

}  // namespace gux
