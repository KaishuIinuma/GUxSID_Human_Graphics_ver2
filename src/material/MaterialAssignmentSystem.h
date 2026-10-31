#pragma once

#include "scene/SceneObject.h"

#include <algorithm>

namespace gux {

// パレットをMaterialへ変換してSceneObjectへ割り当てる。
// 色だけでなくMaterial種別もここで決定するため、Rendererは色デッキを知らない。
class MaterialAssignmentSystem {
 public:
  void update(std::vector<SceneObject>& objects, const ofColor* palette,
              size_t paletteSize,
              MaterialType baseMaterialType,
              MaterialType outlineMaterialType,
              const std::vector<size_t>& lockedPaletteIndices) {
    if (!lockedPaletteIndices.empty() && paletteSize > 0) {
      std::vector<ObjectId> sourceIds;
      sourceIds.reserve(objects.size());
      for (const auto& object : objects) {
        sourceIds.push_back(object.sourceObjectIds.empty()
                                ? object.id : object.sourceObjectIds.front());
      }
      std::sort(sourceIds.begin(), sourceIds.end());
      sourceIds.erase(std::unique(sourceIds.begin(), sourceIds.end()),
                      sourceIds.end());

      for (auto& object : objects) {
        const ObjectId sourceId = object.sourceObjectIds.empty()
                                      ? object.id : object.sourceObjectIds.front();
        const size_t personIndex = static_cast<size_t>(
            std::lower_bound(sourceIds.begin(), sourceIds.end(), sourceId) -
            sourceIds.begin());
        const size_t paletteIndex =
            lockedPaletteIndices[personIndex % lockedPaletteIndices.size()] %
            paletteSize;
        const ofColor color = palette[paletteIndex];
        object.appearance.baseMaterial =
            {baseMaterialType, color, color, {0.0f, 1.0f}};
        object.appearance.outlineMaterial =
            {outlineMaterialType, color, color, {0.0f, 1.0f}};
      }
      return;
    }

    // 演出が終わった直後は、その時の人数を基準にする (ここで全員の色を引き直さない)。
    if (adoptObjectCount) {
      adoptObjectCount = false;
      if (paletteSize > 0) {
        fillMissingColors(objects.size(), palette, paletteSize, baseMaterialType,
                          outlineMaterialType);
        assignedAppearances.resize(objects.size());
    assignedPaletteIndices.resize(objects.size());
        assignedPaletteIndices.resize(objects.size());
      }
    }
    const bool objectCountChanged =
        assignedAppearances.size() != objects.size();
    const bool materialTypeChanged =
        baseMaterialType != lastBaseMaterialType ||
        outlineMaterialType != lastOutlineMaterialType;
    // 色を引き直すのは、初回と、人数・Material種別が変わった時だけ
    // (時間の経過では変えない)。
    const bool shouldReassign = objectCountChanged || materialTypeChanged;

    // プリセット切り替えの演出中は、人数が変わっても色を引き直さない。
    // 増えた分だけ新しい色を引き、Material種別が変わったら色はそのまま種別だけ変える。
    if (holdColors && paletteSize > 0) {
      if (colorDeckPaletteSize != paletteSize) {
        colorDeck.clear();
        colorDeckPaletteSize = paletteSize;
      }
      if (materialTypeChanged) {
        for (size_t i = 0; i < assignedAppearances.size(); ++i) {
          assignedAppearances[i] =
              makeAppearance(assignedPaletteIndices[i] % paletteSize, palette,
                             paletteSize, baseMaterialType, outlineMaterialType);
        }
        lastBaseMaterialType = baseMaterialType;
        lastOutlineMaterialType = outlineMaterialType;
      }
      fillMissingColors(objects.size(), palette, paletteSize, baseMaterialType,
                        outlineMaterialType);
      for (size_t i = 0; i < objects.size(); ++i) {
        objects[i].appearance = assignedAppearances[i];
      }
      return;
    }

    // SceneObjectは輪郭更新のたびに再生成されるため、再抽選しないフレームでも
    // 前回のAppearanceを戻す。
    if (!shouldReassign) {
      for (size_t i = 0; i < objects.size(); ++i) {
        objects[i].appearance = assignedAppearances[i];
      }
      return;
    }

    if (paletteSize == 0) {
      assignedAppearances.clear();
      return;
    }
    if (colorDeckPaletteSize != paletteSize) {
      colorDeck.clear();
      colorDeckPaletteSize = paletteSize;
    }

    assignedAppearances.resize(objects.size());
    assignedPaletteIndices.resize(objects.size());
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
      assignedPaletteIndices[i] = primaryIndex;
    }

    lastBaseMaterialType = baseMaterialType;
    lastOutlineMaterialType = outlineMaterialType;
  }

  // プリセット切り替えの演出中は true にする。人が順番に消えたり現れたりしても、
  // 色を引き直さない。false に戻した時の人数を基準に、以後の変化を見る。
  void setHoldColors(bool hold) {
    if (holdColors && !hold) adoptObjectCount = true;
    holdColors = hold;
  }

  void reset() {
    colorDeck.clear();
    assignedAppearances.clear();
    assignedPaletteIndices.clear();
    colorDeckPaletteSize = 0;
  }

 private:
  // グラデーションの終点は次のパレット色を使う。
  static AppearanceComponent makeAppearance(size_t primaryIndex, const ofColor* palette,
                                            size_t paletteSize,
                                            MaterialType baseMaterialType,
                                            MaterialType outlineMaterialType) {
    const size_t secondaryIndex = (primaryIndex + 1) % paletteSize;
    AppearanceComponent appearance;
    appearance.baseMaterial = {baseMaterialType, palette[primaryIndex],
                               palette[secondaryIndex], {0.0f, 1.0f}};
    appearance.outlineMaterial = {outlineMaterialType, palette[primaryIndex],
                                  palette[secondaryIndex], {0.0f, 1.0f}};
    return appearance;
  }

  // 割り当て済みの色が count 人分に足りない時だけ、使っていない色を足す。
  void fillMissingColors(size_t count, const ofColor* palette, size_t paletteSize,
                         MaterialType baseMaterialType,
                         MaterialType outlineMaterialType) {
    while (assignedAppearances.size() < count) {
      if (colorDeck.empty()) refillColorDeck(paletteSize);
      const auto unused = [this](size_t colorIndex) {
        return std::find(assignedPaletteIndices.begin(), assignedPaletteIndices.end(),
                         colorIndex) == assignedPaletteIndices.end();
      };
      auto colorIt = colorDeck.begin();
      if (assignedPaletteIndices.size() < paletteSize) {
        colorIt = std::find_if(colorDeck.begin(), colorDeck.end(), unused);
        if (colorIt == colorDeck.end()) {
          refillColorDeck(paletteSize);
          colorIt = std::find_if(colorDeck.begin(), colorDeck.end(), unused);
          if (colorIt == colorDeck.end()) colorIt = colorDeck.begin();
        }
      }
      const size_t primaryIndex = *colorIt;
      colorDeck.erase(colorIt);
      assignedPaletteIndices.push_back(primaryIndex);
      assignedAppearances.push_back(makeAppearance(
          primaryIndex, palette, paletteSize, baseMaterialType, outlineMaterialType));
    }
  }

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
  std::vector<size_t> assignedPaletteIndices;
  bool holdColors = false;
  bool adoptObjectCount = false;
  size_t colorDeckPaletteSize = 0;
  MaterialType lastBaseMaterialType = MaterialType::Solid;
  MaterialType lastOutlineMaterialType = MaterialType::Solid;
};

}  // namespace gux
