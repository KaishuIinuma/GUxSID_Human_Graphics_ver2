#pragma once

#include "BaseScene.h"
#include "core/VisualTypes.h"
#include "geometry/GeometryProcessor.h"
#include "material/MaterialAssignmentSystem.h"
#include "render/StandardRenderRecipe.h"
#include "scene/SceneComposer.h"
#include "tracking/ObjectTracker.h"

#include <memory>

// HumanGraphicsSceneは各段階を接続するControllerだけを担当する。
// Detection自体はofApp/PersonSegmenterが担当し、ここではその結果を受け取る。
class HumanGraphicsScene : public BaseScene {
 public:
  void setup() override;
  void update(const HumanContourData& humanData) override;
  void draw() override;

  bool enableBase = true;
  bool enableOffset = true;
  bool enableStroke = true;
  float offsetSize = 200.0f;
  float offsetScale = 1.0f;
  OffsetJoinType offsetJoinType = OffsetJoinType::Round;
  float strokeWeight = 10.0f;
  StrokeJoinType strokeJoinType = StrokeJoinType::Round;
  gux::MaterialType baseMaterialType = gux::MaterialType::Solid;
  gux::MaterialType outlineMaterialType = gux::MaterialType::Solid;

 private:
  gux::ObjectTracker objectTracker;
  gux::GeometryProcessor geometryProcessor;
  gux::SceneComposer sceneComposer;
  gux::MaterialAssignmentSystem materialAssignmentSystem;
  // 動画書き出しではHumanGraphicsScene全体をコピーするため、Recipeは共有所有にする。
  // Recipe自体は状態を持たず、通常描画と書き出し描画で安全に共有できる。
  std::shared_ptr<gux::RenderRecipe> renderRecipe;

  std::vector<gux::TrackedObject> trackedObjects;
  std::vector<gux::GeometryObject> geometryObjects;
  std::vector<gux::SceneObject> sceneObjects;

  uint64_t lastDetectionSignature = 0;
  uint64_t lastPipelineSignature = 0;
  bool hasDetectionSignature = false;
  bool hasPipelineSignature = false;

  uint64_t detectionSignature(const HumanContourData& humanData) const;
  uint64_t pipelineSignature(const HumanContourData& humanData) const;
  gux::GeometrySettings geometrySettings() const;
  gux::CompositionSettings compositionSettings() const;
  gux::RenderContext renderContext() const;
};
