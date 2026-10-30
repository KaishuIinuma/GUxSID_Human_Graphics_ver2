#pragma once

#include "scene/SceneObject.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace gux {

// パレットをMaterialへ変換してSceneObjectへ割り当てる。
// 色だけでなくMaterial種別もここで決定するため、Rendererは色デッキを知らない。
class MaterialAssignmentSystem {
 public:
  void update(std::vector<SceneObject>& objects, const ofColor* palette,
              size_t paletteSize, uint64_t updateIntervalMs,
              MaterialType baseMaterialType,
              MaterialType outlineMaterialType,
              const std::vector<size_t>& lockedPaletteIndices,
              uint64_t now = ofGetElapsedTimeMillis()) {
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

    const bool objectCountChanged = objects.size() != lastObjectCount;
    const bool materialTypeChanged =
        baseMaterialType != lastBaseMaterialType ||
        outlineMaterialType != lastOutlineMaterialType;
    // Color Update Intervalの最大値(20s)では時間経過による再抽選をしない。
    // 初回割り当てや人数・Material種別の変更には引き続き対応する。
    const bool updateDue = updateIntervalMs < 20000 && now >= nextUpdateMs;
    const bool reassignAll =
        objectCountChanged || materialTypeChanged || updateDue;

    if (paletteSize == 0) {
      assignedByKey.clear();
      lastObjectCount = objects.size();
      nextUpdateMs = now + updateIntervalMs;
      return;
    }
    if (colorDeckPaletteSize != paletteSize) {
      colorDeck.clear();
      colorDeckPaletteSize = paletteSize;
      assignedByKey.clear();
    }

    // 色はSceneObjectのIDごとに覚える。SceneObjectの並び順は検出の信頼度順で
    // フレームごとに入れ替わるため、並び順で覚えると人どうしの色が入れ替わる。
    // 人数変化または指定間隔の到達時だけ、全員の色を引き直す（従来どおり）。
    if (reassignAll) assignedByKey.clear();

    // 引き継いだ色を先に確定し、同じ画面で重ならないようにする。
    std::vector<size_t> usedIndices;
    usedIndices.reserve(objects.size());
    for (const auto& object : objects) {
      const auto found = assignedByKey.find(object.id);
      if (found != assignedByKey.end()) usedIndices.push_back(found->second.paletteIndex);
    }

    std::unordered_map<ObjectId, Assigned> next;
    next.reserve(objects.size());
    for (auto& object : objects) {
      auto found = assignedByKey.find(object.id);
      if (found == assignedByKey.end()) {
        // 新しく現れたID（またはTrackerのIDの付け直し）だけ新しい色を引く。
        const size_t primaryIndex = drawColor(paletteSize, usedIndices);
        usedIndices.push_back(primaryIndex);
        // グラデーションの終点は次のパレット色を使う。開始色の重複なし契約は維持する。
        const size_t secondaryIndex = (primaryIndex + 1) % paletteSize;
        Assigned assigned;
        assigned.paletteIndex = primaryIndex;
        assigned.appearance.baseMaterial = {baseMaterialType, palette[primaryIndex],
                                            palette[secondaryIndex], {0.0f, 1.0f}};
        assigned.appearance.outlineMaterial = {outlineMaterialType, palette[primaryIndex],
                                               palette[secondaryIndex], {0.0f, 1.0f}};
        found = assignedByKey.emplace(object.id, assigned).first;
      }
      object.appearance = found->second.appearance;
      next[object.id] = found->second;
    }
    // 画面から消えたIDは忘れる（覚え続けると増え続けるため）。
    assignedByKey = std::move(next);

    lastObjectCount = objects.size();
    if (reassignAll) {
      lastBaseMaterialType = baseMaterialType;
      lastOutlineMaterialType = outlineMaterialType;
      nextUpdateMs = now + updateIntervalMs;
    }
  }

  void reset() {
    colorDeck.clear();
    assignedByKey.clear();
    lastObjectCount = std::numeric_limits<size_t>::max();
    nextUpdateMs = 0;
    colorDeckPaletteSize = 0;
  }

 private:
  struct Assigned {
    AppearanceComponent appearance;
    size_t paletteIndex = 0;
  };

  // 山札から1色引く。パレットを一巡するまでは、同じ画面で使用中の色を避ける。
  // 使用中の色がパレット数以上になった後は重複を許可する。
  size_t drawColor(size_t paletteSize, const std::vector<size_t>& usedIndices) {
    if (colorDeck.empty()) refillColorDeck(paletteSize);
    auto colorIt = colorDeck.begin();
    if (usedIndices.size() < paletteSize) {
      const auto unused = [&usedIndices](size_t colorIndex) {
        return std::find(usedIndices.begin(), usedIndices.end(), colorIndex) ==
               usedIndices.end();
      };
      colorIt = std::find_if(colorDeck.begin(), colorDeck.end(), unused);
      if (colorIt == colorDeck.end()) {
        refillColorDeck(paletteSize);
        colorIt = std::find_if(colorDeck.begin(), colorDeck.end(), unused);
        if (colorIt == colorDeck.end()) colorIt = colorDeck.begin();
      }
    }
    const size_t index = *colorIt;
    colorDeck.erase(colorIt);
    return index;
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
  std::unordered_map<ObjectId, Assigned> assignedByKey;
  size_t lastObjectCount = std::numeric_limits<size_t>::max();
  uint64_t nextUpdateMs = 0;
  size_t colorDeckPaletteSize = 0;
  MaterialType lastBaseMaterialType = MaterialType::Solid;
  MaterialType lastOutlineMaterialType = MaterialType::Solid;
};

}  // namespace gux
