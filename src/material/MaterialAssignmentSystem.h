#pragma once

#include "scene/SceneObject.h"

#include <algorithm>

namespace gux {

// パレットをMaterialへ変換してSceneObjectへ割り当てる。
// 色だけでなくMaterial種別もここで決定するため、Rendererは色デッキを知らない。
class MaterialAssignmentSystem {
 public:
  void update(std::vector<SceneObject>& objects, const ofColor* palette,
              size_t paletteSize, uint64_t updateIntervalMs,
              MaterialType baseMaterialType,
              MaterialType outlineMaterialType,
              uint64_t now = ofGetElapsedTimeMillis()) {
    const bool objectCountChanged =
        assignedAppearances.size() != objects.size();
    const bool materialTypeChanged =
        baseMaterialType != lastBaseMaterialType ||
        outlineMaterialType != lastOutlineMaterialType;
    const bool updateDue = now >= nextUpdateMs;
    const bool shouldReassign =
        objectCountChanged || materialTypeChanged || updateDue;

    // SceneObjectは輪郭更新のたびに再生成されるため、再抽選しないフレームでも
    // 前回のAppearanceを戻す。IDの揺れでは色を更新せず、従来どおり
    // 人数変化または指定間隔の到達時だけ新しい色を割り当てる。
    if (!shouldReassign) {
      for (size_t i = 0; i < objects.size(); ++i) {
        objects[i].appearance = assignedAppearances[i];
      }
      return;
    }

    if (paletteSize == 0) {
      assignedAppearances.clear();
      nextUpdateMs = now + updateIntervalMs;
      return;
    }
    if (colorDeckPaletteSize != paletteSize) {
      colorDeck.clear();
      colorDeckPaletteSize = paletteSize;
    }

    assignedAppearances.resize(objects.size());
    std::vector<size_t> assignedIndices;
    assignedIndices.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
      if (colorDeck.empty()) refillColorDeck(paletteSize);
      auto colorIt = colorDeck.begin();

      // 同一画面内では、パレットを一巡するまで重複を避ける。
      // オブジェクト数がパレット数を超えた後は重複を許可する。
      if (assignedIndices.size() < paletteSize) {
        colorIt = std::find_if(
            colorDeck.begin(), colorDeck.end(),
            [&assignedIndices](size_t colorIndex) {
              return std::find(assignedIndices.begin(), assignedIndices.end(),
                               colorIndex) == assignedIndices.end();
            });
        if (colorIt == colorDeck.end()) {
          refillColorDeck(paletteSize);
          colorIt = std::find_if(
              colorDeck.begin(), colorDeck.end(),
              [&assignedIndices](size_t colorIndex) {
                return std::find(assignedIndices.begin(), assignedIndices.end(),
                                 colorIndex) == assignedIndices.end();
              });
        }
      }

      const size_t primaryIndex = *colorIt;
      assignedIndices.push_back(primaryIndex);
      colorDeck.erase(colorIt);

      // グラデーションの終点は次のパレット色を使う。開始色の重複なし契約は維持する。
      const size_t secondaryIndex = (primaryIndex + 1) % paletteSize;
      auto& appearance = objects[i].appearance;
      appearance.baseMaterial = {baseMaterialType, palette[primaryIndex],
                                 palette[secondaryIndex], {0.0f, 1.0f}};
      appearance.outlineMaterial = {outlineMaterialType, palette[primaryIndex],
                                    palette[secondaryIndex], {0.0f, 1.0f}};
      assignedAppearances[i] = appearance;
    }

    lastBaseMaterialType = baseMaterialType;
    lastOutlineMaterialType = outlineMaterialType;
    nextUpdateMs = now + updateIntervalMs;
  }

  void reset() {
    colorDeck.clear();
    assignedAppearances.clear();
    nextUpdateMs = 0;
    colorDeckPaletteSize = 0;
  }

 private:
  void refillColorDeck(size_t paletteSize) {
    colorDeck.resize(paletteSize);
    for (size_t i = 0; i < paletteSize; ++i) colorDeck[i] = i;
    for (size_t i = paletteSize; i > 1; --i) {
      const size_t randomIndex = static_cast<size_t>(ofRandom(i));
      std::swap(colorDeck[i - 1], colorDeck[randomIndex]);
    }
  }

  std::vector<size_t> colorDeck;
  std::vector<AppearanceComponent> assignedAppearances;
  uint64_t nextUpdateMs = 0;
  size_t colorDeckPaletteSize = 0;
  MaterialType lastBaseMaterialType = MaterialType::Solid;
  MaterialType lastOutlineMaterialType = MaterialType::Solid;
};

}  // namespace gux
