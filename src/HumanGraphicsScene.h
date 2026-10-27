#pragma once

#include "BaseScene.h"
#include "core/VisualTypes.h"
#include "event/StandardMergeEvent.h"
#include "geometry/GeometryProcessor.h"
#include "material/MaterialAssignmentSystem.h"
#include "render/BackgroundGradientPass.h"
#include "render/MixRenderRecipe.h"
#include "render/PaperShadowPass.h"
#include "render/BridgeStrokePass.h"
#include "render/StandardRenderRecipe.h"
#include "scene/SceneComposer.h"
#include "scene/StandardSceneBehavior.h"
#include "scene/StandardSceneLayout.h"
#include "sound/SoundOscSender.h"
#include "tracking/ObjectTracker.h"

#include <memory>

// HumanGraphicsSceneは各段階を接続するControllerだけを担当する。
// Detection自体はofApp/PersonSegmenterが担当し、ここではその結果を受け取る。
class HumanGraphicsScene : public BaseScene {
 public:
  void setup() override;
  void update(const HumanContourData& humanData) override;
  void update(const HumanContourData& humanData, float elapsedSeconds,
              float deltaSeconds);
  void draw() override;
  void draw(bool drawBackground);
  void setCanvasSize(int width, int height);
  void setLockedPaletteIndices(std::vector<size_t> paletteIndices);
  void setRenderRecipe(const std::string& recipeId);
  std::string_view renderRecipeId() const;
  void setMergeEvent(const std::string& eventId);
  std::string_view mergeEventId() const;
  void setSceneLayout(const std::string& layoutId);
  std::string_view sceneLayoutId() const;
  void setSceneBehavior(const std::string& behaviorId);
  std::string_view sceneBehaviorId() const;

  // 音の送信用。人ごとの位置・面積と、合体グループを返す（キャンバスのピクセル単位）。
  const std::vector<gux::SoundPerson>& soundPeople() const { return soundPeopleList; }
  // 結ぶストローク用の重心(このSceneのキャンバス座標)。
  std::vector<glm::vec2> bridgeStrokePoints() const {
    return gux::BridgeStrokePass::centroids(sceneObjects);
  }
  // 結ぶストロークで結ぶ点を外から指定する(Dual Outputで2画面をつなげる用)。
  // clear するまで、自分の重心の代わりにこの点を結ぶ。
  void setBridgeStrokeOverride(std::vector<glm::vec2> points) {
    bridgeStrokeOverride = std::move(points);
    hasBridgeStrokeOverride = true;
  }
  void clearBridgeStrokeOverride() {
    bridgeStrokeOverride.clear();
    hasBridgeStrokeOverride = false;
  }
  glm::vec2 canvasSize() const {
    return glm::vec2(activeCanvasWidth(), activeCanvasHeight());
  }

  bool enableBase = true;
  bool enableOffset = true;
  bool enableStroke = true;
  bool enableBridgeStroke = false;  // 全Objectの重心を結ぶストローク
  bool enableMerge = true;
  bool enableLooseContour = false;
  float looseContourStrength = 7.0f;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  OffsetJoinType offsetJoinType = OffsetJoinType::Round;
  float strokeWeight = 10.0f;
  StrokeJoinType strokeJoinType = StrokeJoinType::Round;
  gux::MaterialType baseMaterialType = gux::MaterialType::Solid;
  gux::MaterialType outlineMaterialType = gux::MaterialType::Solid;
  gux::PaperShadowSettings paperShadow;
  gux::BackgroundGradientSettings backgroundGradient;

 private:
  gux::ObjectTracker objectTracker;
  gux::GeometryProcessor geometryProcessor;
  gux::SceneComposer sceneComposer;
  gux::MaterialAssignmentSystem materialAssignmentSystem;
  std::vector<size_t> lockedPaletteIndices;
  // 動画書き出しではHumanGraphicsScene全体をコピーするため、Recipeは共有所有にする。
  // Recipe自体は状態を持たず、通常描画と書き出し描画で安全に共有できる。
  std::shared_ptr<gux::RenderRecipe> renderRecipe;
  std::shared_ptr<gux::MergeEvent> mergeEvent;
  std::shared_ptr<gux::SceneLayout> sceneLayout;
  std::shared_ptr<gux::SceneBehavior> sceneBehavior;
  gux::PaperShadowPass paperShadowPass;
  gux::BridgeStrokePass bridgeStrokePass;
  std::vector<glm::vec2> bridgeStrokeOverride;
  bool hasBridgeStrokeOverride = false;
  gux::BackgroundGradientPass backgroundGradientPass;
  float backgroundSeconds = 0.0f;  // 背景のにじみが漂う時間（書き出しでは動画の時間）

  std::vector<gux::TrackedObject> trackedObjects;
  std::vector<gux::GeometryObject> geometryObjects;
  std::vector<gux::SceneObject> sceneObjects;
  std::vector<gux::SoundPerson> soundPeopleList;

  uint64_t lastDetectionSignature = 0;
  uint64_t lastPipelineSignature = 0;
  bool hasDetectionSignature = false;
  bool hasPipelineSignature = false;
  bool mergeActive = false;
  int canvasWidth = 0;
  int canvasHeight = 0;

  int activeCanvasWidth() const;
  int activeCanvasHeight() const;
  uint64_t detectionSignature(const HumanContourData& humanData) const;
  uint64_t pipelineSignature(uint64_t detectionHash) const;
  gux::GeometrySettings geometrySettings() const;
  gux::CompositionSettings compositionSettings() const;
  gux::RenderContext renderContext() const;
  void updateSoundPeople();
};
