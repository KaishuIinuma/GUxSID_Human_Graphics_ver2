#pragma once

#include "scene/AppearanceComponent.h"

namespace gux {

// デバッグ用の仮マテリアル。Scene4から外せば既存のパレット割り当てへ戻る。
class SampleMaterial_Gradient {
 public:
  static MaterialComponent create() {
    MaterialComponent material;
    material.type = MaterialType::LinearGradient;
    material.primary = ofColor(255, 64, 160);
    material.secondary = ofColor(32, 224, 255);
    material.gradientDirection = glm::vec2(1.0f, 1.0f);
    return material;
  }
};

}  // namespace gux
