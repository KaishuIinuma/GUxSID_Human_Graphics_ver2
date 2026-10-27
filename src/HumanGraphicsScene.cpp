#include "HumanGraphicsScene.h"
#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>

namespace {

void hashCombine(uint64_t& seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

}  // namespace

void HumanGraphicsScene::setup() {
  objectTracker.reset();
  trackedObjects.clear();
  geometryObjects.clear();
  sceneObjects.clear();
  soundPeopleList.clear();
  materialAssignmentSystem.reset();
  renderRecipe = std::make_shared<gux::StandardRenderRecipe>();
  mergeEvent = std::make_shared<gux::StandardMergeEvent>();
  sceneLayout = std::make_shared<gux::StandardSceneLayout>();
  sceneBehavior = std::make_shared<gux::StandardSceneBehavior>();
  hasDetectionSignature = false;
  hasPipelineSignature = false;
  mergeActive = false;
}

void HumanGraphicsScene::update(const HumanContourData& humanData) {
  update(humanData, ofGetElapsedTimef(), ofGetLastFrameTime());
}

void HumanGraphicsScene::update(const HumanContourData& humanData,
                                float elapsedSeconds, float deltaSeconds) {
  backgroundSeconds = elapsedSeconds;
  // Detectionはsegmentation実行時だけ更新されるため、同じ観測を毎描画フレーム
  // Trackerへ重複投入しない。
  const uint64_t currentDetectionSignature = detectionSignature(humanData);
  if (!hasDetectionSignature ||
      currentDetectionSignature != lastDetectionSignature) {
    trackedObjects = objectTracker.update(humanData);
    lastDetectionSignature = currentDetectionSignature;
    hasDetectionSignature = true;
  }

  const uint64_t currentPipelineSignature = pipelineSignature(currentDetectionSignature);
  bool compositionUpdated = false;
  bool mergeJustStarted = false;
  if (!hasPipelineSignature || currentPipelineSignature != lastPipelineSignature) {
    geometryObjects =
        geometryProcessor.process(trackedObjects, geometrySettings());
    sceneObjects =
        sceneComposer.compose(geometryObjects, compositionSettings());
    updateSoundPeople();

    const bool isMerged = sceneObjects.size() < geometryObjects.size();
    mergeJustStarted = isMerged && !mergeActive;
    mergeActive = isMerged;
    compositionUpdated = true;

    lastPipelineSignature = currentPipelineSignature;
    hasPipelineSignature = true;
  }

  materialAssignmentSystem.update(
      sceneObjects, ofApp::colorPallate, ofApp::colorPaletteSize,
      ofApp::colorUpdateIntervalMs, baseMaterialType, outlineMaterialType,
      lockedPaletteIndices,
      static_cast<uint64_t>(std::max(0.0f, elapsedSeconds) * 1000.0f));

  if (compositionUpdated && sceneLayout) {
    gux::LayoutContext layoutContext;
    layoutContext.canvasWidth = static_cast<float>(activeCanvasWidth());
    layoutContext.canvasHeight = static_cast<float>(activeCanvasHeight());
    sceneLayout->apply(sceneObjects, layoutContext);
  }

  // EventはComposition・Material・Layoutが完了したSceneObjectへ適用する。
  if (compositionUpdated && mergeActive && mergeEvent) {
    gux::MergeEventContext eventContext;
    eventContext.sourceObjectCount = geometryObjects.size();
    eventContext.composedObjectCount = sceneObjects.size();
    eventContext.justStarted = mergeJustStarted;
    mergeEvent->apply(sceneObjects, eventContext);
  }

  if (sceneBehavior) {
    gux::BehaviorContext behaviorContext;
    behaviorContext.elapsedSeconds = elapsedSeconds;
    behaviorContext.deltaSeconds = deltaSeconds;
    behaviorContext.compositionUpdated = compositionUpdated;
    sceneBehavior->update(sceneObjects, behaviorContext);
  }
}

void HumanGraphicsScene::draw() {
  draw(true);
}

void HumanGraphicsScene::draw(bool drawBackground) {
  if (drawBackground) {
    ofBackground(ofApp::background_color);
    backgroundGradientPass.draw(activeCanvasWidth(), activeCanvasHeight(),
                                backgroundSeconds, ofColor(ofApp::background_color),
                                backgroundGradient);
  }
  if (!renderRecipe) return;
  const gux::RenderContext context = renderContext();
  // 紙の影は、紙と同じ Recipe で形を描いてから作る。どの Recipe でも同じように効く。
  // 結ぶストロークはObject本体より前面に描き、紙の影にも含める。
  const auto drawPaper = [&] {
    renderRecipe->draw(sceneObjects, context);
    if (enableBridgeStroke) bridgeStrokePass.draw(sceneObjects, context);
  };
  paperShadowPass.draw(activeCanvasWidth(), activeCanvasHeight(), paperShadow,
                       drawPaper);
  drawPaper();
}

void HumanGraphicsScene::setCanvasSize(int width, int height) {
  canvasWidth = width;
  canvasHeight = height;
  hasPipelineSignature = false;
}

void HumanGraphicsScene::setLockedPaletteIndices(
    std::vector<size_t> paletteIndices) {
  if (lockedPaletteIndices == paletteIndices) return;
  lockedPaletteIndices = std::move(paletteIndices);
  materialAssignmentSystem.reset();
}

int HumanGraphicsScene::activeCanvasWidth() const {
  return canvasWidth > 0 ? canvasWidth : ofGetWidth();
}

int HumanGraphicsScene::activeCanvasHeight() const {
  return canvasHeight > 0 ? canvasHeight : ofGetHeight();
}

void HumanGraphicsScene::setRenderRecipe(const std::string& recipeId) {
  // Recipeの切り替え後は、Compositionからやり直す。
  hasPipelineSignature = false;
  if (recipeId == gux::MixRenderRecipe::RecipeId) {
    renderRecipe = std::make_shared<gux::MixRenderRecipe>();
    return;
  }

  if (recipeId != gux::StandardRenderRecipe::RecipeId) {
    ofLogWarning("HumanGraphicsScene")
        << "Unknown Recipe ID: " << recipeId
        << ". Falling back to " << gux::StandardRenderRecipe::RecipeId;
  }
  renderRecipe = std::make_shared<gux::StandardRenderRecipe>();
}

std::string_view HumanGraphicsScene::renderRecipeId() const {
  return renderRecipe ? renderRecipe->id()
                      : gux::StandardRenderRecipe::RecipeId;
}

void HumanGraphicsScene::setMergeEvent(const std::string& eventId) {
  if (eventId != gux::StandardMergeEvent::EventId) {
    ofLogWarning("HumanGraphicsScene")
        << "Unknown Merge Event ID: " << eventId
        << ". Falling back to " << gux::StandardMergeEvent::EventId;
  }
  mergeEvent = std::make_shared<gux::StandardMergeEvent>();
  hasPipelineSignature = false;
}

std::string_view HumanGraphicsScene::mergeEventId() const {
  return mergeEvent ? mergeEvent->id() : gux::StandardMergeEvent::EventId;
}

void HumanGraphicsScene::setSceneLayout(const std::string& layoutId) {
  if (layoutId != gux::StandardSceneLayout::LayoutId) {
    ofLogWarning("HumanGraphicsScene")
        << "Unknown Layout ID: " << layoutId << ". Falling back to "
        << gux::StandardSceneLayout::LayoutId;
  }
  sceneLayout = std::make_shared<gux::StandardSceneLayout>();
  hasPipelineSignature = false;
}

std::string_view HumanGraphicsScene::sceneLayoutId() const {
  return sceneLayout ? sceneLayout->id() : gux::StandardSceneLayout::LayoutId;
}

void HumanGraphicsScene::setSceneBehavior(const std::string& behaviorId) {
  if (behaviorId != gux::StandardSceneBehavior::BehaviorId) {
    ofLogWarning("HumanGraphicsScene")
        << "Unknown Behavior ID: " << behaviorId << ". Falling back to "
        << gux::StandardSceneBehavior::BehaviorId;
  }
  sceneBehavior = std::make_shared<gux::StandardSceneBehavior>();
  sceneBehavior->reset();
  hasPipelineSignature = false;
}

std::string_view HumanGraphicsScene::sceneBehaviorId() const {
  return sceneBehavior ? sceneBehavior->id()
                       : gux::StandardSceneBehavior::BehaviorId;
}

uint64_t HumanGraphicsScene::detectionSignature(
    const HumanContourData& humanData) const {
  uint64_t signature = 0;
  hashCombine(signature, humanData.contours.size());
  for (const auto& contour : humanData.contours) {
    hashCombine(signature, contour.size());
    for (const auto& point : contour) {
      hashCombine(signature, std::hash<float>{}(point.x));
      hashCombine(signature, std::hash<float>{}(point.y));
    }
  }
  return signature;
}

uint64_t HumanGraphicsScene::pipelineSignature(uint64_t detectionHash) const {
  uint64_t signature = detectionHash;
  hashCombine(signature, std::hash<int>{}(ofApp::vertexCount));
  hashCombine(signature, std::hash<bool>{}(enableBase));
  hashCombine(signature, std::hash<bool>{}(enableOffset));
  hashCombine(signature, std::hash<bool>{}(enableStroke));
  hashCombine(signature, std::hash<bool>{}(enableMerge));
  hashCombine(signature, std::hash<bool>{}(enableLooseContour));
  hashCombine(signature, std::hash<float>{}(looseContourStrength));
  hashCombine(signature, std::hash<float>{}(offsetSize));
  hashCombine(signature, std::hash<float>{}(offsetScale));
  hashCombine(signature,
              std::hash<int>{}(static_cast<int>(offsetJoinType)));
  hashCombine(signature, std::hash<float>{}(strokeWeight));
  hashCombine(signature,
              std::hash<int>{}(static_cast<int>(ofApp::compositionMergeMode)));
  hashCombine(signature,
              std::hash<float>{}(ofApp::compositionCentroidMergeDistance));
  hashCombine(signature, std::hash<float>{}(ofApp::compositionMergeMaskScale));
  hashCombine(signature,
              std::hash<float>{}(ofApp::compositionStrokeMergeMaskMinScale));
  hashCombine(signature,
              std::hash<float>{}(ofApp::compositionStrokeMergeApproximationPx));
  hashCombine(signature,
              std::hash<size_t>{}(ofApp::compositionStrokeMergeMaxVertices));
  hashCombine(signature, std::hash<int>{}(activeCanvasWidth()));
  hashCombine(signature, std::hash<int>{}(activeCanvasHeight()));
  return signature;
}

gux::GeometrySettings HumanGraphicsScene::geometrySettings() const {
  gux::GeometrySettings settings;
  settings.vertexCount = ofApp::vertexCount;
  settings.enableLooseContour = enableLooseContour;
  settings.looseContourStrength = looseContourStrength;
  settings.enableOffset = enableOffset;
  settings.offsetSize = offsetSize;
  settings.offsetScale = offsetScale;
  settings.offsetJoinType = offsetJoinType;
  return settings;
}

gux::CompositionSettings HumanGraphicsScene::compositionSettings() const {
  gux::CompositionSettings settings;
  // 再配置では並び順・個体数を保つ必要があるため、事前結合しない。
  settings.enableMerge = enableMerge &&
      sceneLayoutId() == gux::StandardSceneLayout::LayoutId &&
      sceneBehaviorId() == gux::StandardSceneBehavior::BehaviorId;
  settings.enableBase = enableBase;
  settings.enableOffset = enableOffset;
  settings.enableStroke = enableStroke;
  settings.offsetSize = offsetSize;
  settings.offsetScale = offsetScale;
  settings.strokeWeight = strokeWeight;
  settings.mergeMode =
      ofApp::compositionMergeMode == ofApp::CompositionMergeMode::CentroidDistance
          ? gux::MergeMode::CentroidDistance
          : gux::MergeMode::RenderedGraphic;
  settings.centroidMergeDistance = ofApp::compositionCentroidMergeDistance;
  settings.mergeMaskScale = ofApp::compositionMergeMaskScale;
  settings.strokeMergeMaskMinScale = ofApp::compositionStrokeMergeMaskMinScale;
  settings.strokeMergeApproximationPx =
      ofApp::compositionStrokeMergeApproximationPx;
  settings.strokeMergeMaxVertices = ofApp::compositionStrokeMergeMaxVertices;
  settings.vertexCount = ofApp::vertexCount;
  settings.canvasWidth = activeCanvasWidth();
  settings.canvasHeight = activeCanvasHeight();
  return settings;
}

gux::RenderContext HumanGraphicsScene::renderContext() const {
  gux::RenderContext context;
  context.enableBase = enableBase;
  context.enableStroke = enableStroke;
  context.strokeWeight = strokeWeight;
  context.strokeJoinType = strokeJoinType;
  context.backgroundColor = ofColor(ofApp::background_color);
  return context;
}

// 合体後の形はすべての元IDを持つため、誰と誰が合体したかは形の内外で判定する。
// 並べ替え・レイアウト前に呼び、合体後の形と元の形が同じ座標系にある状態で比べる。
void HumanGraphicsScene::updateSoundPeople() {
  soundPeopleList.clear();
  soundPeopleList.reserve(trackedObjects.size());

  const bool oneToOne = sceneObjects.size() == geometryObjects.size();
  std::unordered_map<gux::ObjectId, size_t> shapeOfObject;
  if (!oneToOne) {
    for (const auto& object : geometryObjects) {
      const glm::vec2 center = object.contour.getCentroid2D();
      int found = -1;
      for (size_t i = 0; i < sceneObjects.size() && found < 0; ++i) {
        if (sceneObjects[i].geometry.inside(center.x, center.y)) {
          found = static_cast<int>(i);
        }
      }
      // 凹んだ形で重心が外に出た場合は、頂点が最も多く入る形を選ぶ。
      if (found < 0) {
        size_t bestCount = 0;
        for (size_t i = 0; i < sceneObjects.size(); ++i) {
          size_t count = 0;
          for (const auto& point : object.contour) {
            if (sceneObjects[i].geometry.inside(point.x, point.y)) ++count;
          }
          if (count > bestCount) {
            bestCount = count;
            found = static_cast<int>(i);
          }
        }
      }
      if (found >= 0) shapeOfObject[object.id] = static_cast<size_t>(found);
    }
  }

  // グループの値は、そのグループ内で最小の人物IDにする（フレームをまたいで安定する）。
  std::unordered_map<size_t, gux::ObjectId> groupOfShape;
  for (const auto& [id, shape] : shapeOfObject) {
    auto it = groupOfShape.find(shape);
    if (it == groupOfShape.end() || id < it->second) groupOfShape[shape] = id;
  }

  // 動きの量は、画面に描かれる輪郭（頂点数を揃えた後）の頂点から測る。
  std::unordered_map<gux::ObjectId, const ofPolyline*> contourOfObject;
  for (const auto& object : geometryObjects) contourOfObject[object.id] = &object.contour;

  for (const auto& tracked : trackedObjects) {
    gux::SoundPerson person;
    person.id = tracked.id;
    person.centroid = tracked.centroid;
    person.area = std::abs(tracked.contour.getArea());
    person.group = tracked.id;
    auto shape = shapeOfObject.find(tracked.id);
    if (shape != shapeOfObject.end()) person.group = groupOfShape[shape->second];
    auto contour = contourOfObject.find(tracked.id);
    const ofPolyline& source =
        contour != contourOfObject.end() ? *contour->second : tracked.contour;
    person.vertices.reserve(source.size());
    for (const auto& point : source) person.vertices.emplace_back(point.x, point.y);
    soundPeopleList.push_back(person);
  }
}
