#include "HumanGraphicsScene.h"
#include "ofApp.h"

#include <functional>

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
  // Detectionはsegmentation実行時だけ更新されるため、同じ観測を毎描画フレーム
  // Trackerへ重複投入しない。
  const uint64_t currentDetectionSignature = detectionSignature(humanData);
  if (!hasDetectionSignature ||
      currentDetectionSignature != lastDetectionSignature) {
    trackedObjects = objectTracker.update(humanData);
    lastDetectionSignature = currentDetectionSignature;
    hasDetectionSignature = true;
  }

  const uint64_t currentPipelineSignature = pipelineSignature(humanData);
  bool compositionUpdated = false;
  bool mergeJustStarted = false;
  if (!hasPipelineSignature || currentPipelineSignature != lastPipelineSignature) {
    geometryObjects =
        geometryProcessor.process(trackedObjects, geometrySettings());
    sceneObjects =
        sceneComposer.compose(geometryObjects, compositionSettings());

    const bool isMerged = sceneObjects.size() < geometryObjects.size();
    mergeJustStarted = isMerged && !mergeActive;
    mergeActive = isMerged;
    compositionUpdated = true;

    lastPipelineSignature = currentPipelineSignature;
    hasPipelineSignature = true;
  }

  materialAssignmentSystem.update(
      sceneObjects, ofApp::colorPallate, ofApp::colorPaletteSize,
      ofApp::colorUpdateIntervalMs, baseMaterialType, outlineMaterialType);

  if (compositionUpdated && sceneLayout) {
    gux::LayoutContext layoutContext;
    layoutContext.canvasWidth = static_cast<float>(ofGetWidth());
    layoutContext.canvasHeight = static_cast<float>(ofGetHeight());
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
    behaviorContext.elapsedSeconds = ofGetElapsedTimef();
    behaviorContext.deltaSeconds = ofGetLastFrameTime();
    behaviorContext.compositionUpdated = compositionUpdated;
    sceneBehavior->update(sceneObjects, behaviorContext);
  }
}

void HumanGraphicsScene::draw() {
  ofBackground(ofApp::background_color);
  if (renderRecipe) renderRecipe->draw(sceneObjects, renderContext());
}

void HumanGraphicsScene::setRenderRecipe(const std::string& recipeId) {
  if (recipeId == gux::MixRenderRecipe::RecipeId) {
    renderRecipe = std::make_shared<gux::MixRenderRecipe>();
    return;
  }
  if (recipeId == gux::RecursiveStrokeRenderRecipe::RecipeId) {
    renderRecipe = std::make_shared<gux::RecursiveStrokeRenderRecipe>();
    return;
  }
  if (recipeId == gux::FloatingBridgeRenderRecipe::RecipeId) {
    renderRecipe = std::make_shared<gux::FloatingBridgeRenderRecipe>();
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
  if (layoutId == gux::RecursiveSplitLayout::LayoutId) {
    sceneLayout = std::make_shared<gux::RecursiveSplitLayout>();
  } else {
    if (layoutId != gux::StandardSceneLayout::LayoutId) {
      ofLogWarning("HumanGraphicsScene")
          << "Unknown Layout ID: " << layoutId << ". Falling back to "
          << gux::StandardSceneLayout::LayoutId;
    }
    sceneLayout = std::make_shared<gux::StandardSceneLayout>();
  }
  hasPipelineSignature = false;
}

std::string_view HumanGraphicsScene::sceneLayoutId() const {
  return sceneLayout ? sceneLayout->id() : gux::StandardSceneLayout::LayoutId;
}

void HumanGraphicsScene::setSceneBehavior(const std::string& behaviorId) {
  if (behaviorId == gux::FloatingSceneBehavior::BehaviorId) {
    sceneBehavior = std::make_shared<gux::FloatingSceneBehavior>();
  } else {
    if (behaviorId != gux::StandardSceneBehavior::BehaviorId) {
      ofLogWarning("HumanGraphicsScene")
          << "Unknown Behavior ID: " << behaviorId << ". Falling back to "
          << gux::StandardSceneBehavior::BehaviorId;
    }
    sceneBehavior = std::make_shared<gux::StandardSceneBehavior>();
  }
  sceneBehavior->reset();
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

uint64_t HumanGraphicsScene::pipelineSignature(
    const HumanContourData& humanData) const {
  uint64_t signature = detectionSignature(humanData);
  hashCombine(signature, std::hash<int>{}(ofApp::vertexCount));
  hashCombine(signature, std::hash<bool>{}(enableBase));
  hashCombine(signature, std::hash<bool>{}(enableOffset));
  hashCombine(signature, std::hash<bool>{}(enableStroke));
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
  hashCombine(signature, std::hash<int>{}(ofGetWidth()));
  hashCombine(signature, std::hash<int>{}(ofGetHeight()));
  return signature;
}

gux::GeometrySettings HumanGraphicsScene::geometrySettings() const {
  gux::GeometrySettings settings;
  settings.vertexCount = ofApp::vertexCount;
  settings.enableOffset = enableOffset;
  settings.offsetSize = offsetSize;
  settings.offsetScale = offsetScale;
  settings.offsetJoinType = offsetJoinType;
  return settings;
}

gux::CompositionSettings HumanGraphicsScene::compositionSettings() const {
  gux::CompositionSettings settings;
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
  settings.canvasWidth = ofGetWidth();
  settings.canvasHeight = ofGetHeight();
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
