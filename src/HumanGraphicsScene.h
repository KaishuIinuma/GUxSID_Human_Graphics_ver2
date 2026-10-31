#pragma once

#include <unordered_set>

#include "BaseScene.h"
#include "core/VisualTypes.h"
#include "event/StandardMergeEvent.h"
#include "geometry/GeometryProcessor.h"
#include "material/MaterialAssignmentSystem.h"
#include "render/PaperShadowPass.h"
#include "render/BridgeStrokePass.h"
#include "render/StandardRenderRecipe.h"
#include "scene/SceneComposer.h"
#include "scene/StandardSceneBehavior.h"
#include "scene/StandardSceneLayout.h"
#include "sound/SoundOscSender.h"
#include "tracking/ObjectTracker.h"

#include <memory>
#include <unordered_map>

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
  // プリセット切り替えの演出中は true にする。形が縮む動きを人の動きとして
  // 音へ送らないよう、音に渡す輪郭とグループを演出前のまま保つ
  // (輪郭は人の重心の移動に合わせて平行移動する)。
  void setSoundShapesFrozen(bool frozen) {
    if (soundShapesFrozen == frozen) return;
    soundShapesFrozen = frozen;
    hasPipelineSignature = false;
  }
  // プリセット切り替えの演出用: 人ごとに頂点数とScaleを上書きする。
  // 載っていない人は通常の Vertex Count / offsetScale を使う。
  void setShapeOverrides(std::unordered_map<gux::ObjectId, gux::ShapeOverride> overrides) {
    shapeOverrides = std::move(overrides);
  }
  void clearShapeOverrides() { shapeOverrides.clear(); }
  // 人ごとの現れる進み具合 (0〜1)。載っていない人は 1 (全体を描く)。
  void setRevealProgress(std::unordered_map<gux::ObjectId, float> progress) {
    revealProgress = std::move(progress);
  }
  void clearRevealProgress() { revealProgress.clear(); }
  // 演出中は合体させない (現れる途中の形どうしが合体すると、描き始めがずれるため)。
  void setMergeSuppressed(bool suppressed) { mergeSuppressed = suppressed; }
  // 演出中は、人が順番に消えたり現れたりしても一人ひとりの色を保つ。
  void setHoldColors(bool hold) { materialAssignmentSystem.setHoldColors(hold); }
  // 今トラッキングしている人のID。
  std::vector<gux::ObjectId> trackedIds() const {
    std::vector<gux::ObjectId> ids;
    ids.reserve(trackedObjects.size());
    for (const auto& object : trackedObjects) ids.push_back(object.id);
    return ids;
  }

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
  // これより小さい人は描かない (音にも送らない)。輪郭の外接矩形の長い辺が
  // キャンバスの高さに占める割合。0 なら全員を描く。
  float minPersonSize = 0.0f;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  OffsetJoinType offsetJoinType = OffsetJoinType::Round;
  float strokeWeight = 10.0f;
  StrokeJoinType strokeJoinType = StrokeJoinType::Round;
  gux::MaterialType baseMaterialType = gux::MaterialType::Solid;
  gux::MaterialType outlineMaterialType = gux::MaterialType::Solid;
  gux::PaperShadowSettings paperShadow;

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

  std::vector<gux::TrackedObject> allTrackedObjects;  // Trackerの結果 (小さい人も含む)
  std::vector<gux::TrackedObject> trackedObjects;     // 描く人だけ
  std::unordered_set<gux::ObjectId> shownIds;         // 大きさの判定で表示中の人
  float lastMinPersonSize = -1.0f;
  int lastFilterCanvasHeight = -1;
  std::vector<gux::GeometryObject> geometryObjects;
  std::vector<gux::SceneObject> sceneObjects;
  std::vector<gux::SoundPerson> soundPeopleList;
  bool soundShapesFrozen = false;
  std::unordered_map<gux::ObjectId, gux::ShapeOverride> shapeOverrides;
  std::unordered_map<gux::ObjectId, float> revealProgress;
  bool mergeSuppressed = false;

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
  void filterSmallPeople();
};
