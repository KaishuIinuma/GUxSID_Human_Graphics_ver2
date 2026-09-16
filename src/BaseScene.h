#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"  
#include "core/HumanContourData.h"

// ============================================
// シーン共通の基底クラス
// ============================================
class BaseScene {
public:
  virtual ~BaseScene() {}
  virtual void setup() {}
  virtual void update(const HumanContourData &humanData) {}
  virtual void draw() = 0;

protected:
  // パレットを一巡するまで同じ色を再抽選しないための状態。
  // 各Sceneが独立して保持するため、シーンを切り替えてもそのSceneの色順は維持される。
  vector<size_t> colorDeck;
  vector<size_t> colorIndices;
  uint64_t nextColorUpdateMs = 0;
  size_t colorDeckPaletteSize = 0;

  void updateColorAssignments(
    size_t objectCount,
    size_t paletteSize,
    uint64_t updateIntervalMs
  ) {
    const uint64_t now = ofGetElapsedTimeMillis();
    const bool objectCountChanged = colorIndices.size() != objectCount;
    const bool updateDue = now >= nextColorUpdateMs;

    if (!objectCountChanged && !updateDue) {
      return;
    }

    colorIndices.clear();
    if (objectCount == 0 || paletteSize == 0) {
      nextColorUpdateMs = now + updateIntervalMs;
      return;
    }

    if (colorDeckPaletteSize != paletteSize) {
      colorDeck.clear();
      colorDeckPaletteSize = paletteSize;
    }

    const size_t uniqueColorCount = std::min(objectCount, paletteSize);
    for (size_t i = 0; i < uniqueColorCount; ++i) {
      if (colorDeck.empty()) {
        refillColorDeck(paletteSize);
      }

      // その更新内で既に使った色を避けてデッキから1色取り出す。
      // これにより、デッキの切り替わり時でも同じ画面内では色が重複しない。
      auto colorIt = std::find_if(
        colorDeck.begin(), colorDeck.end(),
        [this](size_t colorIndex) {
          return std::find(colorIndices.begin(), colorIndices.end(), colorIndex)
                 == colorIndices.end();
        }
      );

      if (colorIt == colorDeck.end()) {
        refillColorDeck(paletteSize);
        colorIt = std::find_if(
          colorDeck.begin(), colorDeck.end(),
          [this](size_t colorIndex) {
            return std::find(colorIndices.begin(), colorIndices.end(), colorIndex)
                   == colorIndices.end();
          }
        );
      }

      colorIndices.push_back(*colorIt);
      colorDeck.erase(colorIt);
    }

    // パレット数を超えるオブジェクトは、6色を一巡した後にのみ重複させる。
    for (size_t i = uniqueColorCount; i < objectCount; ++i) {
      colorIndices.push_back(colorIndices[i % paletteSize]);
    }

    nextColorUpdateMs = now + updateIntervalMs;
  }

  size_t colorIndexForObject(size_t objectIndex) const {
    return objectIndex < colorIndices.size() ? colorIndices[objectIndex] : 0;
  }

private:
  void refillColorDeck(size_t paletteSize) {
    colorDeck.resize(paletteSize);
    for (size_t i = 0; i < paletteSize; ++i) {
      colorDeck[i] = i;
    }

    // Fisher-Yates shuffle。ofRandomを使うことでopenFrameworksの乱数設定に従う。
    for (size_t i = paletteSize; i > 1; --i) {
      const size_t randomIndex = static_cast<size_t>(ofRandom(i));
      std::swap(colorDeck[i - 1], colorDeck[randomIndex]);
    }
  }
};
