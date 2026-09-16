#include "scene4.h"
#include "ofApp.h"

#include <functional>

namespace {

void hashCombine(uint64_t& seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

}  // namespace

void Scene4::setup() {
  objectTracker.reset();
  trackedObjects.clear();
  geometryObjects.clear();
  sceneObjects.clear();
  materialAssignmentSystem.reset();
  renderRecipe = std::make_shared<gux::CurrentRenderRecipe>();
  hasDetectionSignature = false;
  hasPipelineSignature = false;
}

void Scene4::update(const HumanContourData& humanData) {
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
  if (!hasPipelineSignature || currentPipelineSignature != lastPipelineSignature) {
    geometryObjects =
        geometryProcessor.process(trackedObjects, geometrySettings());
    sceneObjects =
        sceneComposer.compose(geometryObjects, compositionSettings());
    lastPipelineSignature = currentPipelineSignature;
    hasPipelineSignature = true;
  }

  materialAssignmentSystem.update(
      sceneObjects, ofApp::colorPallate, ofApp::colorPaletteSize,
      ofApp::colorUpdateIntervalMs, baseMaterialType, outlineMaterialType);
}

void Scene4::draw() {
  ofBackground(ofApp::background_color);
  if (renderRecipe) renderRecipe->draw(sceneObjects, renderContext());
}

uint64_t Scene4::detectionSignature(
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

uint64_t Scene4::pipelineSignature(
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
              std::hash<int>{}(static_cast<int>(ofApp::scene4MergeMode)));
  hashCombine(signature,
              std::hash<float>{}(ofApp::scene4CentroidMergeDistance));
  hashCombine(signature, std::hash<float>{}(ofApp::scene4MergeMaskScale));
  hashCombine(signature,
              std::hash<float>{}(ofApp::scene4StrokeMergeMaskMinScale));
  hashCombine(signature,
              std::hash<float>{}(ofApp::scene4StrokeMergeApproximationPx));
  hashCombine(signature,
              std::hash<size_t>{}(ofApp::scene4StrokeMergeMaxVertices));
  hashCombine(signature, std::hash<int>{}(ofGetWidth()));
  hashCombine(signature, std::hash<int>{}(ofGetHeight()));
  return signature;
}

gux::GeometrySettings Scene4::geometrySettings() const {
  gux::GeometrySettings settings;
  settings.vertexCount = ofApp::vertexCount;
  settings.enableOffset = enableOffset;
  settings.offsetSize = offsetSize;
  settings.offsetScale = offsetScale;
  settings.offsetJoinType = offsetJoinType;
  return settings;
}

gux::CompositionSettings Scene4::compositionSettings() const {
  gux::CompositionSettings settings;
  settings.enableBase = enableBase;
  settings.enableOffset = enableOffset;
  settings.enableStroke = enableStroke;
  settings.offsetSize = offsetSize;
  settings.offsetScale = offsetScale;
  settings.strokeWeight = strokeWeight;
  settings.mergeMode =
      ofApp::scene4MergeMode == ofApp::Scene4MergeMode::CentroidDistance
          ? gux::MergeMode::CentroidDistance
          : gux::MergeMode::RenderedGraphic;
  settings.centroidMergeDistance = ofApp::scene4CentroidMergeDistance;
  settings.mergeMaskScale = ofApp::scene4MergeMaskScale;
  settings.strokeMergeMaskMinScale = ofApp::scene4StrokeMergeMaskMinScale;
  settings.strokeMergeApproximationPx =
      ofApp::scene4StrokeMergeApproximationPx;
  settings.strokeMergeMaxVertices = ofApp::scene4StrokeMergeMaxVertices;
  settings.vertexCount = ofApp::vertexCount;
  settings.canvasWidth = ofGetWidth();
  settings.canvasHeight = ofGetHeight();
  return settings;
}

gux::RenderContext Scene4::renderContext() const {
  gux::RenderContext context;
  context.enableBase = enableBase;
  context.enableStroke = enableStroke;
  context.strokeWeight = strokeWeight;
  context.strokeJoinType = strokeJoinType;
  return context;
}
