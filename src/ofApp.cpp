#include "ofApp.h"
#include "CameraAuthorization.h"
#include "StableCameraGrabber.h"
#include "SourceCrop.h"
#include <GLFW/glfw3.h> // guiWindow/mainWindowの表示切り替え・リサイズ用
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace {
const std::string kSettingsDirectoryName =
    "Library/Application Support/GUxSID_Human_Graphics_ver2";
const std::string kControlsFileName = "controls.json";
// コード内の初期値 (setupGuiParameters) の版。上げると、保存済みの controls.json が
// これより古いMacでは次の起動時に一度だけ初期値で強制的に上書きする
// (元のファイルは controls.backup.json に残す)。上書き後は通常どおり保存・復元する。
const std::string kControlsDefaultsVersion = "20261031";
const std::string kControlsDefaultsVersionKey = "Defaults_Version";

std::string settingsDirectoryPath() {
  return ofFilePath::join(ofFilePath::getUserHomeDir(), kSettingsDirectoryName);
}

void migrateLegacyScaleKey(ofJson& settings) {
  if (!settings.is_object()) return;
  auto controls = settings.find("ControlsSettings");
  if (controls == settings.end() || !controls->is_object()) return;
  const auto legacy = controls->find("Graphics_Offset_Scale");
  if (legacy != controls->end() && controls->find("Scale") == controls->end()) {
    (*controls)["Scale"] = *legacy;
  }
}

void migrateLegacyPersonConfidenceKey(ofJson& settings) {
  if (!settings.is_object()) return;
  auto controls = settings.find("ControlsSettings");
  if (controls == settings.end() || !controls->is_object()) return;
  const auto legacy = controls->find("Person_Confidence");
  if (legacy == controls->end()) return;
  if (controls->find("YOLO_Confidence") == controls->end()) {
    (*controls)["YOLO_Confidence"] = *legacy;
  }
}

// プリセット廃止に伴い Auto Cycle 関連の名前から "Preset" を外した。
void migrateLegacyAutoCycleKeys(ofJson& settings) {
  if (!settings.is_object()) return;
  auto controls = settings.find("ControlsSettings");
  if (controls == settings.end() || !controls->is_object()) return;
  const std::pair<const char *, const char *> renames[] = {
      {"Preset_Auto_Cycle", "Auto_Cycle"},
      {"Preset_Cycle_Interval__s_", "Cycle_Interval__s_"},
      {"Preset_Transition__s_", "Transition__s_"},
      {"Preset_Transition_Window__s_", "Transition_Window__s_"},
  };
  for (const auto &[oldKey, newKey] : renames) {
    const auto legacy = controls->find(oldKey);
    if (legacy != controls->end() && controls->find(newKey) == controls->end()) {
      (*controls)[newKey] = *legacy;
    }
  }
}

void configureBundledDataPath() {
#ifdef TARGET_OSX
  const of::filesystem::path executablePath =
      ofFilePath::getCurrentExePathFS();
  const of::filesystem::path bundledDataPath =
      executablePath.parent_path().parent_path() / "Resources" / "data";

  if (of::filesystem::is_directory(bundledDataPath)) {
    ofSetDataPathRoot(bundledDataPath);
    ofLogNotice("DataPath")
        << "Using bundled data directory: "
        << ofPathToString(bundledDataPath);
  }
#endif
}

// 黒背景つきの文字表示。ofDrawBitmapStringHighlight は MODEL モードだと
// 背景を上下反転して描いてしまうため、背景の矩形を自前で描く。
// (寸法は ofDrawBitmapStringHighlight と同じ: 1文字 8px、行間 1.7、余白 4px)
void drawTextWithBackground(const std::string &text, float x, float y) {
  const std::vector<std::string> lines = ofSplitString(text, "\n");
  size_t maxLineLength = 0;
  for (const std::string &line : lines) {
    maxLineLength = std::max(maxLineLength, line.size());
  }
  const float padding = 4.0f;
  const float fontSize = 8.0f;
  const float width = maxLineLength * fontSize + 2.0f * padding;
  const float height = lines.size() * fontSize * 1.7f - 1.0f + 2.0f * padding;
  ofPushStyle();
  ofFill();
  ofSetColor(0);
  ofDrawRectangle(x - padding, y - (padding + fontSize + 2.0f), width, height);
  ofSetColor(255);
  ofDrawBitmapString(text, x, y);
  ofPopStyle();
}

void drawSourceCropOverview(const ofTexture &texture, int sourceWidth,
                            int sourceHeight, const cv::Rect &crop,
                            int canvasWidth, int canvasHeight) {
  if (!texture.isAllocated() || crop.empty()) return;

  const float margin = 20.0f;
  const float availableWidth = std::min(360.0f, canvasWidth * 0.35f);
  const float availableHeight = std::min(240.0f, canvasHeight * 0.32f);
  const float scale = std::min(availableWidth / sourceWidth,
                               availableHeight / sourceHeight);
  if (scale <= 0.0f) return;

  const float imageWidth = sourceWidth * scale;
  const float imageHeight = sourceHeight * scale;
  const float imageX = canvasWidth - margin - imageWidth;
  const float imageY = canvasHeight - margin - imageHeight;
  if (imageX < 0.0f || imageY < 25.0f) return;

  ofPushStyle();
  ofSetColor(255);
  texture.draw(imageX, imageY, imageWidth, imageHeight);
  ofNoFill();
  ofSetLineWidth(2.0f);
  ofSetColor(255);
  ofDrawRectangle(imageX, imageY, imageWidth, imageHeight);
  ofSetLineWidth(3.0f);
  ofSetColor(0, 255, 255);
  ofDrawRectangle(imageX + crop.x * scale, imageY + crop.y * scale,
                  crop.width * scale, crop.height * scale);
  ofSetColor(255);
  drawTextWithBackground("SOURCE / CYAN: VISIBLE CROP", imageX, imageY - 8.0f);
  ofPopStyle();
}
// 推論スレッドの結果を受け取る。applyResult=falseなら完了を待って捨てる。
void takeDetectionResult(std::future<HumanContourData> &future,
                         HumanContourData &target, bool applyResult) {
  if (!future.valid()) return;
  try {
    HumanContourData result = future.get();
    if (applyResult) target = std::move(result);
  } catch (const std::exception &e) {
    ofLogError("ofApp") << "Realtime detection failed: " << e.what();
  }
}

bool isFutureReady(const std::future<HumanContourData> &future) {
  return future.valid() &&
         future.wait_for(std::chrono::milliseconds(0)) ==
             std::future_status::ready;
}
} // namespace

//--------------------------------------------------------------
void ofApp::setup() {
  ofSetFrameRate(60);

  // 配布.appでは、モデル・フォント・プリセットを
  // Contents/Resources/dataから読む。開発ビルドでは従来のbin/dataを使う。
  configureBundledDataPath();

  // ============================================
  // ★追加/修正: カメラデバイスの取得と初期化
  // ============================================
  // 同じ名前のカメラが複数あっても番号と実機がずれないよう、並び順を固定した
  // 取り込みを使う (StableCameraGrabber.h)。
  cam.setGrabber(std::make_shared<StableCameraGrabber>());
  cam2.setGrabber(std::make_shared<StableCameraGrabber>());
  if (ensureCameraAuthorization()) {
    cameraDevices = cam.listDevices();
    for (size_t i = 0; i < cameraDevices.size(); ++i) {
      ofLogNotice() << "Camera " << i << ": " << cameraDevices[i].deviceName;
    }

    // 通常起動時も、復旧後も内蔵カメラを優先する。
    if (!cameraDevices.empty()) {
      cam.setDeviceID(getDefaultCameraIndex());
    } else {
      ofLogError("ofApp") << "カメラへのアクセスは許可されていますが、利用可能なカメラが見つかりません。";
    }
  } else {
    ofLogError("ofApp")
        << "カメラへのアクセスが許可されていません。システム設定 > プライバシーとセキュリティ > カメラで、このアプリを許可してください。";
  }

  // メモリ領域を確保
  colorImg.allocate(W, H);
  colorImg2.allocate(W, H);

  // ============================================
  // ★変更: YOLO11-seg (person専用) ONNXモデルの読み込み
  //   Ultralyticsの `yolo export model=yolo11n-seg.pt format=onnx` 等で
  //   書き出したモデルを data/ 以下に配置しておくこと。
  // ============================================
  string modelPath = ofToDataPath("yolo11n-seg.onnx", true);
  bool modelOk = personSegmenter.loadModel(modelPath, 640);
  // 系統2(下段)用のモデルは、Dual Output がオンの時だけ読み込む
  // (loadSecondSegmenterIfNeeded。設定の読み込み後に呼ぶ)。
  if (!modelOk) {
    ofLogError() << "YOLOモデルの読み込みに失敗しました。data/に"
                     "yolo11n-seg.onnx を配置しているか確認してください: " << modelPath;
  }

  // シーンの初期化（まずはscene1をデフォルトに設定）
  
  humanGraphicsScene = std::make_shared<HumanGraphicsScene>();
  humanGraphicsScene->setup();
  currentScene = humanGraphicsScene;
  humanGraphicsScene2 = std::make_shared<HumanGraphicsScene>();
  humanGraphicsScene2->setup();

  // デバッグ画面は基本「非表示」
  showDebug = false;

  // ============================================
  // 動画処理クラスにも同じPersonSegmenterインスタンスを共有させる
  // （モデルはofApp側で一度だけ読み込み、videoProcessor側は
  //   ポインタで参照するだけでよい）
  // ============================================
  videoProcessor.setup(&personSegmenter);

  // ============================================
  // 調整用GUIの初期化
  // ============================================
  setupGuiParameters();
}

//--------------------------------------------------------------
// 1. setupGuiParameters() をスッキリさせます
void ofApp::setupGuiParameters() {
  // フォントと実際の操作領域を拡大する（描画だけの拡大はしない）。
  const std::string guiFontPath = ofToDataPath("mono.ttf", true);
  if (ofFile(guiFontPath).exists()) {
    ofxBaseGui::loadFont(guiFontPath, 18, true, true, 72);
  } else {
    ofLogWarning("ofApp") << "GUIフォントが見つからないため、ビットマップフォントを使用します: "
                          << guiFontPath;
    ofxBaseGui::setUseTTF(false);
  }
  ofxBaseGui::setDefaultWidth(controlWindowWidth - 40);
  ofxBaseGui::setDefaultHeight(26);
  ofxBaseGui::setDefaultTextPadding(8);

  pRealtime.set("Realtime", true);
  pMainWindowResolution.set("Main Window Resolution", "-- x --");
  pMainWindowFps.set("Main Window FPS", "--");
  pRunTime.set("Run Time", "00:00:00");
  pMainWindowTargetFps.set("Main Window Target FPS", 24, 1, 120);
  // ★追加: カメラ選択用パラメーターの初期化
  int maxCameraIndex = std::max(0, (int)cameraDevices.size() - 1);
  // 展示機の初期値: カメラ1 = 1番, カメラ2 = 0番 (台数が足りなければ範囲内に収める)。
  int defaultCameraIndex = std::min(1, maxCameraIndex);
  pCameraIndex.set("Camera ID", defaultCameraIndex, 0, maxCameraIndex);
  
  string defaultCamName = cameraDevices.empty() ? "No Camera Found" : cameraDevices[defaultCameraIndex].deviceName;
  pCameraName.set("Camera Name", defaultCamName);
  pCamera2Index.set("Camera 2 ID", 0, 0, maxCameraIndex);
  setupCameraChoices();

  // Auto Cycle: 一定の間隔で Fill (Base) と Stroke のトグルを入れ替える。
  pAutoCycle.set("Auto Cycle", true);
  // 本番は3分(180秒)くらいを想定。確認用の初期値は30秒。
  pCycleSeconds.set("Cycle Interval (s)", 6.0625f, 5.0f, 600.0f);
  // 一人分の切り替えの演出の長さ。0なら演出なしで切り替える。
  pTransitionSeconds.set("Transition (s)", 0.5f, 0.0f, 10.0f);
  // 全員の変化が収まる時間。一人ひとりはこの中のランダムな時刻に変わる。
  pTransitionWindowSeconds.set("Transition Window (s)", 1.98214f, 0.0f, 30.0f);

  // ★追加: 左右反転トグル（デフォルトOFF）
  pFlipHorizontal.set("Flip Horizontal", true);
  pProjectionSafeArea.set("Projection Margin", false);
  pDualOutput.set("Dual Output (2 Screens)", true);

  personSegmenter.yoloConfidenceThreshold = 0.214286f;
  pYoloConfidence.set("YOLO Confidence",
                      personSegmenter.yoloConfidenceThreshold, 0.0f, 1.0f);
  pYoloInput320.set("YOLO Input 320", false);

  vertexCount = 17;
  pVertexCount.set("Vertex Count", vertexCount, 4, 100);
  pLooseContour.set("Loose Contour", false);
  pLooseContourStrength.set("Loose Contour Strength", 0.0f, 0.0f, 30.0f);
  pMinPersonSize.set("Min Person Size", 0.196429f, 0.0f, 1.0f);
  pAspectRatio.set("Aspect Ratio", 0.17857f, -50.0f, 50.0f);
  pVideoPeopleCount.set("Video People", 1, 1, 20);
  pVideoSoloColor.set("Solo Color", 1, 1,
                      static_cast<int>(colorPaletteSize));
  pVideoColorLock.set("Video Color Lock", false);
  pVideoPlaybackSpeed.set("Video Playback Speed", 1.0f, 0.1f, 2.0f);
 

  pGraphicsEnableBase.set("Graphics Base Enable", false);
  pGraphicsEnableOffset.set("Graphics Offset Enable", true);
  pGraphicsEnableStroke.set("Graphics Stroke Enable", true);
  pGraphicsEnableBridgeStroke.set("Graphics Bridge Stroke Enable", true);
  pGraphicsMergeEnable.set("Graphics Merge Enable", true);
  pGraphicsOffsetSize.set("Graphics Offset Size", 40.7143f, 0.0f, 600.0f);
  pGraphicsOffsetScale.set("Scale", 0.971964f, 0.1f, 2.0f);
  pGraphicsOffsetRound.set("Graphics Offset Round Mode", true);
  pGraphicsStrokeWeight.set("Graphics Stroke Weight", 10.0f, 0.1f, 200.0f);
  pGraphicsStrokeRound.set("Graphics Stroke Round Mode", true);
  pGraphicsBaseMaterial.set("Graphics Base 0:ベタ 1:グラデ", 0, 0, 1);
  pGraphicsStrokeMaterial.set("Graphics Stroke 0:ベタ 1:グラデ", 0, 0, 1);
  pRenderRecipeId.set("Recipe ID", "standard_render");
  pMergeEventId.set("Event ID", "standard_event");
  pSceneLayoutId.set("Layout ID", "standard_layout");
  pSceneBehaviorId.set("Behavior ID", "standard_behavior");
  pPaperShadowEnable.set("Paper Shadow", true);
  pPaperShadowLift.set("Paper Shadow Lift", 1.62857f, 0.0f, 4.0f);
  pPaperShadowAngle.set("Paper Shadow Angle", 64.2857f, 0.0f, 360.0f);
  pPaperShadowOpacity.set("Paper Shadow Opacity", 0.969643f, 0.0f, 3.0f);
  pExportAlpha.set("alpha", false);
  pSoundOscEnable.set("Sound OSC Out", true);
  pSoundOscSlots.set("OSC Slots (TouchDesigner)", false);
  pSoundOscTarget.set("Sound OSC Target",
                      "127.0.0.1:" + ofToString(kSoundOscPort));

  // ★追加: リスナー紐付け
  pGraphicsEnableBase.addListener(this, &ofApp::onGraphicsEnableBaseChanged);
  pGraphicsEnableOffset.addListener(this, &ofApp::onGraphicsEnableOffsetChanged);
  pGraphicsEnableStroke.addListener(this, &ofApp::onGraphicsEnableStrokeChanged);
  pGraphicsEnableBridgeStroke.addListener(this, &ofApp::onGraphicsEnableBridgeStrokeChanged);
  pGraphicsMergeEnable.addListener(this, &ofApp::onGraphicsMergeEnableChanged);
  pGraphicsOffsetSize.addListener(this, &ofApp::onGraphicsOffsetSizeChanged);
  pGraphicsOffsetScale.addListener(this, &ofApp::onGraphicsOffsetScaleChanged); // ★追加
  pGraphicsOffsetRound.addListener(this, &ofApp::onGraphicsOffsetRoundChanged);
  pGraphicsStrokeWeight.addListener(this, &ofApp::onGraphicsStrokeWeightChanged);
  pGraphicsStrokeRound.addListener(this, &ofApp::onGraphicsStrokeRoundChanged);
  pGraphicsBaseMaterial.addListener(this, &ofApp::onGraphicsBaseMaterialChanged);
  pGraphicsStrokeMaterial.addListener(this, &ofApp::onGraphicsStrokeMaterialChanged);
  pRenderRecipeId.addListener(this, &ofApp::onRenderRecipeIdChanged);
  pMergeEventId.addListener(this, &ofApp::onMergeEventIdChanged);
  pSceneLayoutId.addListener(this, &ofApp::onSceneLayoutIdChanged);
  pSceneBehaviorId.addListener(this, &ofApp::onSceneBehaviorIdChanged);
  pPaperShadowEnable.addListener(this, &ofApp::onLookEnableChanged);
  pPaperShadowLift.addListener(this, &ofApp::onLookValueChanged);
  pPaperShadowAngle.addListener(this, &ofApp::onLookValueChanged);
  pPaperShadowOpacity.addListener(this, &ofApp::onLookValueChanged);
  pSoundOscEnable.addListener(this, &ofApp::onSoundOscEnableChanged);
  pSoundOscSlots.addListener(this, &ofApp::onSoundOscSlotsChanged);

  pVideoStatusText.set("Video Status", "VideoモードでOpen Videoまたはドラッグから動画を読み込めます");

  // 表示状態にかかわらず、すべての操作可能なパラメータを保存対象にする。
  setupGuiPersistence();
  loadGuiSettings();

  // 保存済みモードを先に確定し、Video起動時にカメラを開かないようにする。
  // ofParameterのリスナーはこの後で登録するため、ここでは明示的に反映する。
  realtimeMode = pRealtime.get();
  applyTargetFrameRate(pMainWindowTargetFps.get());
  soundOscSender.setTarget("127.0.0.1", kSoundOscPort);
  soundOscSender.setSlotMessagesEnabled(pSoundOscSlots.get());
  soundOscSender.setEnabled(pSoundOscEnable.get());
  videoProcessor.processFps = static_cast<float>(pMainWindowTargetFps.get());
  personSegmenter.aspectRatioPercent = pAspectRatio.get();
  if (pYoloInput320.get()) {
    bool enabled = true;
    onYoloInput320Changed(enabled);
  }
  loadSecondSegmenterIfNeeded();

  pCameraIndex.addListener(this, &ofApp::onCameraIndexChanged);
  pCamera2Index.addListener(this, &ofApp::onCamera2IndexChanged);
  pRealtime.addListener(this, &ofApp::onRealtimeChanged);
  pFlipHorizontal.addListener(this, &ofApp::onFlipHorizontalChanged);
  pProjectionSafeArea.addListener(this, &ofApp::onProjectionSafeAreaChanged);
  pDualOutput.addListener(this, &ofApp::onDualOutputChanged);
  // 保存済みの投影余白設定に合わせて、起動時のMain Window比率を決める。
  applyMainWindowAspectLock();
  pYoloConfidence.addListener(this, &ofApp::onYoloConfidenceChanged);
  pYoloInput320.addListener(this, &ofApp::onYoloInput320Changed);
  pVertexCount.addListener(this, &ofApp::onVertexCountChanged);
  pLooseContour.addListener(this, &ofApp::onLooseContourChanged);
  pLooseContourStrength.addListener(
      this, &ofApp::onLooseContourStrengthChanged);
  pMinPersonSize.addListener(this, &ofApp::onMinPersonSizeChanged);
  pAspectRatio.addListener(this, &ofApp::onAspectRatioChanged);
  pVideoPeopleCount.addListener(this, &ofApp::onVideoPeopleCountChanged);
  pVideoSoloColor.addListener(this, &ofApp::onVideoSoloColorChanged);
  pVideoColorLock.addListener(this, &ofApp::onVideoColorLockChanged);
  pVideoPlaybackSpeed.addListener(this, &ofApp::onVideoPlaybackSpeedChanged);

  pMainWindowTargetFps.addListener(
      this, &ofApp::onMainWindowTargetFpsChanged);
  pExportAlpha.addListener(this, &ofApp::onExportAlphaChanged);
  pAutoCycle.addListener(this, &ofApp::onAutoCycleChanged);
  pCycleSeconds.addListener(this, &ofApp::onCycleSecondsChanged);
  pTransitionSeconds.addListener(this, &ofApp::onTransitionSecondsChanged);
  pTransitionWindowSeconds.addListener(this, &ofApp::onTransitionSecondsChanged);

  playButton.setup("Play");
  pauseButton.setup("Pause");
  restartButton.setup("Restart from beginning");
  openVideoButton.setup("Open Video / Image...");
  exportImageSequenceButton.setup("Export Image Sequence");
  playButton.addListener(this, &ofApp::onPlayPressed);
  pauseButton.addListener(this, &ofApp::onPausePressed);
  restartButton.addListener(this, &ofApp::onRestartPressed);
  openVideoButton.addListener(this, &ofApp::onOpenVideoPressed);
  exportImageSequenceButton.addListener(this, &ofApp::onExportImageSequencePressed);

  // 保存済みの値を、GUI以外の実行状態にも反映する。
  int savedCameraIndex = pCameraIndex.get();
  onCameraIndexChanged(savedCameraIndex);
  personSegmenter.yoloConfidenceThreshold = pYoloConfidence.get();
  vertexCount = pVertexCount.get();
  videoProcessor.setPlaybackSpeed(pVideoPlaybackSpeed.get());
  if (humanGraphicsScene) {
    humanGraphicsScene->enableBase = pGraphicsEnableBase.get();
    humanGraphicsScene->enableOffset = pGraphicsEnableOffset.get();
    humanGraphicsScene->enableStroke = pGraphicsEnableStroke.get();
    humanGraphicsScene->enableBridgeStroke = pGraphicsEnableBridgeStroke.get();
    humanGraphicsScene->enableMerge = pGraphicsMergeEnable.get();
    humanGraphicsScene->enableLooseContour = pLooseContour.get();
    humanGraphicsScene->looseContourStrength = pLooseContourStrength.get();
    humanGraphicsScene->minPersonSize = pMinPersonSize.get();
    humanGraphicsScene->offsetSize = pGraphicsOffsetSize.get();
    humanGraphicsScene->offsetScale = pGraphicsOffsetScale.get();
    humanGraphicsScene->offsetJoinType = pGraphicsOffsetRound.get()
        ? OffsetJoinType::Round : OffsetJoinType::Straight;
    humanGraphicsScene->strokeWeight = pGraphicsStrokeWeight.get();
    humanGraphicsScene->strokeJoinType = pGraphicsStrokeRound.get()
        ? StrokeJoinType::Round : StrokeJoinType::Straight;
    humanGraphicsScene->baseMaterialType = pGraphicsBaseMaterial.get() == 1
        ? gux::MaterialType::LinearGradient : gux::MaterialType::Solid;
    humanGraphicsScene->outlineMaterialType = pGraphicsStrokeMaterial.get() == 1
        ? gux::MaterialType::LinearGradient : gux::MaterialType::Solid;
    // 廃止した Recipe (mix_render など) が保存されていたら standard_render に戻す。
    if (pRenderRecipeId.get() != gux::StandardRenderRecipe::RecipeId) {
      pRenderRecipeId.setWithoutEventNotifications(
          std::string(gux::StandardRenderRecipe::RecipeId));
    }
    humanGraphicsScene->setRenderRecipe(pRenderRecipeId.get());
    humanGraphicsScene->setMergeEvent(pMergeEventId.get());
    humanGraphicsScene->setSceneLayout(pSceneLayoutId.get());
    humanGraphicsScene->setSceneBehavior(pSceneBehaviorId.get());
  }
  applyLookSettings();
  applyVideoColorMode();
  rebuildGuiPanel();
  showGuiWindow();
}

//--------------------------------------------------------------
void ofApp::setupGuiPersistence() {
  guiParams.clear();
  guiParams.setName("ControlsSettings");
  guiParams.add(pRealtime);
  guiParams.add(pCameraIndex);
  guiParams.add(pCamera2Index);
  guiParams.add(pFlipHorizontal);
  guiParams.add(pProjectionSafeArea);
  guiParams.add(pDualOutput);
  guiParams.add(pYoloConfidence);
  guiParams.add(pYoloInput320);
  guiParams.add(pVertexCount);
  guiParams.add(pGraphicsOffsetScale);
  guiParams.add(pAspectRatio);
  guiParams.add(pLooseContour);
  guiParams.add(pLooseContourStrength);
  guiParams.add(pMinPersonSize);
  guiParams.add(pVideoPeopleCount);
  guiParams.add(pVideoSoloColor);
  guiParams.add(pVideoColorLock);
  guiParams.add(pVideoPlaybackSpeed);
  guiParams.add(pGraphicsEnableBase);
  guiParams.add(pGraphicsEnableOffset);
  guiParams.add(pGraphicsEnableStroke);
  guiParams.add(pGraphicsEnableBridgeStroke);
  guiParams.add(pGraphicsMergeEnable);
  guiParams.add(pGraphicsOffsetSize);
  guiParams.add(pGraphicsOffsetRound);
  guiParams.add(pGraphicsStrokeWeight);
  guiParams.add(pGraphicsStrokeRound);
  guiParams.add(pGraphicsBaseMaterial);
  guiParams.add(pGraphicsStrokeMaterial);
  guiParams.add(pRenderRecipeId);
  guiParams.add(pMergeEventId);
  guiParams.add(pSceneLayoutId);
  guiParams.add(pSceneBehaviorId);
  guiParams.add(pPaperShadowEnable);
  guiParams.add(pPaperShadowLift);
  guiParams.add(pPaperShadowAngle);
  guiParams.add(pPaperShadowOpacity);
  guiParams.add(pMainWindowTargetFps);
  guiParams.add(pExportAlpha);
  guiParams.add(pSoundOscEnable);
  guiParams.add(pSoundOscSlots);
  guiParams.add(pAutoCycle);
  guiParams.add(pCycleSeconds);
  guiParams.add(pTransitionSeconds);
  guiParams.add(pTransitionWindowSeconds);
}


//--------------------------------------------------------------
void ofApp::loadGuiSettings() {
  const auto settingsPath = ofFilePath::join(settingsDirectoryPath(), kControlsFileName);
  if (!ofFile(settingsPath, ofFile::Reference).exists()) return;

  ofJson settings = ofLoadJson(settingsPath);
  std::string savedVersion;
  if (settings.is_object() && settings.contains("ControlsSettings") &&
      settings["ControlsSettings"].is_object()) {
    const auto &controls = settings["ControlsSettings"];
    const auto found = controls.find(kControlsDefaultsVersionKey);
    if (found != controls.end() && found->is_string()) savedVersion = *found;
  }
  if (savedVersion != kControlsDefaultsVersion) {
    // 古い初期値の設定は読み込まず、コード内の初期値で上書きする。
    const auto backupPath =
        ofFilePath::join(settingsDirectoryPath(), "controls.backup.json");
    ofFile::copyFromTo(settingsPath, backupPath, false, true);
    saveGuiSettings();
    ofLogNotice("ofApp") << "Controlsの設定を初期値 (" << kControlsDefaultsVersion
                         << ") で上書きしました。元の設定: " << backupPath;
    return;
  }
  if (settings.is_object()) {
    migrateLegacyScaleKey(settings);
    migrateLegacyPersonConfidenceKey(settings);
    migrateLegacyAutoCycleKeys(settings);
    isLoadingGuiSettings = true;
    ofDeserialize(settings, guiParams);
    isLoadingGuiSettings = false;
  }
}

//--------------------------------------------------------------
void ofApp::saveGuiSettings() const {
  if (isLoadingGuiSettings) return;

  const auto settingsDirectory = settingsDirectoryPath();
  ofDirectory::createDirectory(settingsDirectory, false, true);

  ofJson settings;
  ofSerialize(settings, guiParams);
  settings["ControlsSettings"][kControlsDefaultsVersionKey] = kControlsDefaultsVersion;
  const auto settingsPath = ofFilePath::join(settingsDirectory, kControlsFileName);
  if (!ofSavePrettyJson(settingsPath, settings)) {
    ofLogError("ofApp") << "GUI設定を保存できませんでした: " << settingsPath;
  }
}

//--------------------------------------------------------------
void ofApp::exit() {
  finishRealtimeDetection(false);
  soundOscSender.stop();
  saveGuiSettings();
}

//--------------------------------------------------------------
int ofApp::getDefaultCameraIndex() const {
  // macOSでは内蔵カメラ名に FaceTime が含まれる。ほかの環境での表記も
  // 受け入れ、見つからない場合だけ従来通り先頭デバイスを使う。
  for (size_t i = 0; i < cameraDevices.size(); ++i) {
    const string name = ofToLower(cameraDevices[i].deviceName);
    if (name.find("facetime") != string::npos ||
        name.find("built-in") != string::npos ||
        name.find("internal") != string::npos ||
        name.find("内蔵") != string::npos) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

//--------------------------------------------------------------
// Realtime/動画モードの切り替えに応じて、Control Windowに
// 表示するウィジェットを組み直す。
//
// ofParameterに紐づくリスナーはウィジェットの有無に関係なく
// 生き続けるため、gui.clear() -> gui.setup() をしても
// キー操作やパラメータ変更の反映（onXxxChanged系）は失われない。
//--------------------------------------------------------------
// 2. rebuildGuiPanel() で、パネルをゼロから構築し直すようにします
void ofApp::rebuildGuiPanel() {
  gui.clear();
  
  gui.setup("Controls"); // guiParamsを使わず、文字列で直接初期化

  // 共通のUIを追加
  gui.add(pMainWindowResolution);
  gui.add(pRunTime);
  gui.add(pMainWindowTargetFps);
  gui.add(pMainWindowFps);
  gui.add(pProjectionSafeArea);
  gui.add(pRealtime);
  if (!realtimeMode) gui.add(&openVideoButton);
  gui.add(pAutoCycle);
  gui.add<float>(pCycleSeconds);
  gui.add<float>(pTransitionSeconds);
  gui.add<float>(pTransitionWindowSeconds);

  // ★追加: Realtime(カメラ)モードの時だけカメラ選択UI・左右反転トグルを表示
  if (realtimeMode) {
      gui.add(pDualOutput);
      gui.add(cameraChoiceGroups[0]);
      if (dualOutputActive()) gui.add(cameraChoiceGroups[1]);
      gui.add(pFlipHorizontal);
  }


  gui.add<float>(pYoloConfidence);
  if (ofFile(ofToDataPath("yolo11n-seg-320.onnx", true)).exists()) {
    gui.add(pYoloInput320);
  }
  gui.add(pVertexCount);
  gui.add(pGraphicsOffsetScale);
  gui.add(pAspectRatio);
  if (!realtimeMode) gui.add(pVideoPlaybackSpeed);
  gui.add(pLooseContour);
  gui.add(pLooseContourStrength);
  gui.add<float>(pMinPersonSize);
  if (!realtimeMode) {
    gui.add(pVideoColorLock);
    gui.add(pVideoPeopleCount);
    if (pVideoPeopleCount.get() == 1) {
      gui.add(pVideoSoloColor);
    }
  }


  gui.add(pGraphicsEnableBase);
  gui.add(pGraphicsEnableOffset);
  gui.add(pGraphicsEnableStroke);
  gui.add(pGraphicsEnableBridgeStroke);
  gui.add(pGraphicsMergeEnable);
  gui.add<float>(pGraphicsOffsetSize);
  gui.add(pGraphicsOffsetRound);
  gui.add<float>(pGraphicsStrokeWeight);
  gui.add(pGraphicsStrokeRound);
  gui.add(pPaperShadowEnable);
  gui.add<float>(pPaperShadowLift);
  gui.add<float>(pPaperShadowAngle);
  gui.add<float>(pPaperShadowOpacity);
  gui.add(pSoundOscEnable);
  gui.add(pSoundOscSlots);
  gui.add(pSoundOscTarget);

  // 動画モード時のみUIを追加
  if (!realtimeMode) {
    gui.add(&playButton);
    gui.add(&pauseButton);
    gui.add(&restartButton);
    gui.add(pExportAlpha);
    gui.add(&exportImageSequenceButton);
    gui.add(pVideoStatusText);
  }
}

//--------------------------------------------------------------
void ofApp::updateControlsMetrics() {
  const uint64_t now = ofGetElapsedTimeMillis();
  if (now - lastControlsMetricsUpdateMs < 250) return;
  lastControlsMetricsUpdateMs = now;

  if (mainWindow && mainWindow->getGLFWWindow()) {
    const glm::vec2 size = mainWindow->getWindowSize();
    pMainWindowResolution =
        ofToString(static_cast<int>(std::lround(size.x))) + " x " +
        ofToString(static_cast<int>(std::lround(size.y)));
    pMainWindowFps = ofToString(mainWindow->events().getFrameRate(), 1) + " / " +
                     ofToString(pMainWindowTargetFps.get()) + " fps";
  } else {
    pMainWindowResolution = "Unavailable";
    pMainWindowFps = "--";
  }

  const uint64_t totalSeconds = now / 1000;
  const uint64_t hours = totalSeconds / 3600;
  const uint64_t minutes = (totalSeconds / 60) % 60;
  const uint64_t seconds = totalSeconds % 60;
  std::ostringstream runTime;
  runTime << std::setfill('0') << std::setw(2) << hours << ':'
          << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
  pRunTime = runTime.str();
}

//--------------------------------------------------------------
void ofApp::onMainWindowTargetFpsChanged(int &value) {
  applyTargetFrameRate(value);
  videoProcessor.processFps = static_cast<float>(value);
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

void ofApp::applyTargetFrameRate(int value) {
  // ofSetFrameRate は現在のウィンドウだけを変更する。GUI操作中の
  // currentWindow は Controls なので、両ウィンドウを明示して設定する。
  if (mainWindow) mainWindow->events().setFrameRate(value);
  if (guiWindow) guiWindow->events().setFrameRate(value);
}

void ofApp::finishRealtimeDetection(bool applyResult) {
  // 設定変更の前に呼ばれるので、2系統とも推論の完了を待つ。
  takeDetectionResult(detectionFuture, humanData, applyResult);
  takeDetectionResult(detectionFuture2, humanData2, applyResult);
}

//--------------------------------------------------------------
// 系統2のモデルは Dual Output を使う時だけ必要なので、起動時には読まず、
// オンの時に系統1と同じモデルを読み込む。
void ofApp::loadSecondSegmenterIfNeeded() {
  if (!pDualOutput.get()) return;
  const bool use320 = pYoloInput320.get();
  const std::string path =
      ofToDataPath(use320 ? "yolo11n-seg-320.onnx" : "yolo11n-seg.onnx", true);
  if (personSegmenter2.isLoaded() && secondSegmenterModelPath == path) return;
  takeDetectionResult(detectionFuture2, humanData2, false);
  secondSegmenterModelPath.clear();
  if (personSegmenter2.loadModel(path, use320 ? 320 : 640)) {
    secondSegmenterModelPath = path;
  } else {
    ofLogError("ofApp") << "Camera 2 YOLO model failed: " << path;
  }
}

//--------------------------------------------------------------
void ofApp::onExportAlphaChanged(bool &value) {
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onYoloConfidenceChanged(float &value) {
  finishRealtimeDetection(false);
  personSegmenter.yoloConfidenceThreshold = value;
  videoProcessor.requestReprocess();
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onYoloInput320Changed(bool &value) {
  finishRealtimeDetection(false);
  const std::string selectedPath = ofToDataPath(
      value ? "yolo11n-seg-320.onnx" : "yolo11n-seg.onnx", true);
  if (value && !ofFile(selectedPath).exists()) {
    ofLogError("ofApp") << "320px ONNX model is missing: " << selectedPath;
    pYoloInput320.setWithoutEventNotifications(false);
    saveGuiSettings();
    return;
  }
  if (!personSegmenter.loadModel(selectedPath, value ? 320 : 640)) {
    ofLogError("ofApp") << "YOLO model failed; restoring 640px model";
    personSegmenter.loadModel(ofToDataPath("yolo11n-seg.onnx", true), 640);
    pYoloInput320.setWithoutEventNotifications(false);
  }
  // 系統2も同じモデルに揃える (Dual Output がオフなら、オンにした時に読む)。
  loadSecondSegmenterIfNeeded();
  humanData = HumanContourData();
  humanData2 = HumanContourData();
  lastRealtimeProcessMs2 = 0;
  videoProcessor.requestReprocess();
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVertexCountChanged(int &value) {
  vertexCount = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onLooseContourChanged(bool &value) {
  if (humanGraphicsScene) humanGraphicsScene->enableLooseContour = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onLooseContourStrengthChanged(float &value) {
  if (humanGraphicsScene) humanGraphicsScene->looseContourStrength = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onMinPersonSizeChanged(float &value) {
  if (humanGraphicsScene) humanGraphicsScene->minPersonSize = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onAspectRatioChanged(float &value) {
  finishRealtimeDetection(false);
  personSegmenter.aspectRatioPercent = value;
  // Videoが一時停止中でも、次のupdateで保持中の元フレームから検出し直す。
  videoProcessor.requestReprocess();
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoPeopleCountChanged(int &) {
  if (isLoadingGuiSettings) return;
  if (pVideoColorLock.get() && pVideoPeopleCount.get() > 1) {
    selectVideoPaletteColors();
  }
  applyVideoColorMode();
  rebuildGuiPanel();
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoSoloColorChanged(int &) {
  if (isLoadingGuiSettings) return;
  applyVideoColorMode();
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoColorLockChanged(bool &enabled) {
  if (enabled && pVideoPeopleCount.get() > 1) {
    selectVideoPaletteColors();
  }
  if (isLoadingGuiSettings) return;
  applyVideoColorMode();
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoPlaybackSpeedChanged(float &value) {
  videoProcessor.setPlaybackSpeed(value);
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::selectVideoPaletteColors() {
  const std::vector<size_t> previousColors = videoPaletteIndices;
  videoPaletteIndices.clear();

  std::vector<size_t> deck(colorPaletteSize);
  const size_t peopleCount = static_cast<size_t>(pVideoPeopleCount.get());
  while (videoPaletteIndices.size() < peopleCount) {
    std::iota(deck.begin(), deck.end(), 0);
    for (size_t i = deck.size(); i > 1; --i) {
      std::swap(deck[i - 1], deck[static_cast<size_t>(ofRandom(i))]);
    }
    for (size_t color : deck) {
      if (videoPaletteIndices.size() == peopleCount) break;
      videoPaletteIndices.push_back(color);
    }
  }
  // トグルを再びONにしたとき、偶然同じ配色を引いても見た目が変わるようにする。
  if (videoPaletteIndices == previousColors && videoPaletteIndices.size() > 1) {
    std::swap(videoPaletteIndices[0], videoPaletteIndices[1]);
  }
}

//--------------------------------------------------------------
void ofApp::applyVideoColorMode() {
  if (!humanGraphicsScene) return;
  if (realtimeMode || !videoProcessor.isLoaded() || !pVideoColorLock.get()) {
    humanGraphicsScene->setLockedPaletteIndices({});
    return;
  }

  if (pVideoPeopleCount.get() == 1) {
    humanGraphicsScene->setLockedPaletteIndices(
        {static_cast<size_t>(pVideoSoloColor.get() - 1)});
  } else {
    if (videoPaletteIndices.size() !=
        static_cast<size_t>(pVideoPeopleCount.get())) {
      selectVideoPaletteColors();
    }
    humanGraphicsScene->setLockedPaletteIndices(videoPaletteIndices);
  }
}

//--------------------------------------------------------------


void ofApp::onGraphicsEnableBaseChanged(bool &value) {
    if (humanGraphicsScene) humanGraphicsScene->enableBase = value;
    saveGuiSettings();
}
void ofApp::onGraphicsEnableOffsetChanged(bool &value) {
    if (humanGraphicsScene) humanGraphicsScene->enableOffset = value;
    saveGuiSettings();
}
void ofApp::onGraphicsOffsetScaleChanged(float &value) {
    if (humanGraphicsScene) humanGraphicsScene->offsetScale = value;
    saveGuiSettings();
}
void ofApp::onGraphicsOffsetRoundChanged(bool &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->offsetJoinType = value ? OffsetJoinType::Round
                                       : OffsetJoinType::Straight;
    }
    saveGuiSettings();
}
void ofApp::onGraphicsEnableStrokeChanged(bool &value) {
    if (humanGraphicsScene) humanGraphicsScene->enableStroke = value;
    saveGuiSettings();
}
void ofApp::onGraphicsEnableBridgeStrokeChanged(bool &value) {
    if (humanGraphicsScene) humanGraphicsScene->enableBridgeStroke = value;
    saveGuiSettings();
}
void ofApp::onSoundOscEnableChanged(bool &value) {
    soundOscSender.setEnabled(value);
    saveGuiSettings();
}
void ofApp::onSoundOscSlotsChanged(bool &value) {
    soundOscSender.setSlotMessagesEnabled(value);
    saveGuiSettings();
}
void ofApp::onGraphicsMergeEnableChanged(bool &value) {
    if (humanGraphicsScene) humanGraphicsScene->enableMerge = value;
    saveGuiSettings();
}
void ofApp::onGraphicsOffsetSizeChanged(float &value) {
    if (humanGraphicsScene) humanGraphicsScene->offsetSize = value;
    saveGuiSettings();
}
void ofApp::onGraphicsStrokeWeightChanged(float &value) {
    if (humanGraphicsScene) humanGraphicsScene->strokeWeight = value;
    saveGuiSettings();
}
void ofApp::onLookEnableChanged(bool &) {
    applyLookSettings();
    saveGuiSettings();
}
void ofApp::onLookValueChanged(float &) {
    applyLookSettings();
    saveGuiSettings();
}
void ofApp::applyLookSettings() {
    if (!humanGraphicsScene) return;
    humanGraphicsScene->paperShadow.enabled = pPaperShadowEnable.get();
    humanGraphicsScene->paperShadow.lift = pPaperShadowLift.get();
    humanGraphicsScene->paperShadow.angleDegrees = pPaperShadowAngle.get();
    humanGraphicsScene->paperShadow.opacity = pPaperShadowOpacity.get();
}
void ofApp::onGraphicsStrokeRoundChanged(bool &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->strokeJoinType = value ? StrokeJoinType::Round : StrokeJoinType::Straight;
    }
    saveGuiSettings();
}
void ofApp::onGraphicsBaseMaterialChanged(int &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->baseMaterialType = value == 1
            ? gux::MaterialType::LinearGradient : gux::MaterialType::Solid;
    }
    saveGuiSettings();
}
void ofApp::onGraphicsStrokeMaterialChanged(int &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->outlineMaterialType = value == 1
            ? gux::MaterialType::LinearGradient : gux::MaterialType::Solid;
    }
    saveGuiSettings();
}

void ofApp::onRenderRecipeIdChanged(string &value) {
    const bool isKnownRecipe =
        value == gux::StandardRenderRecipe::RecipeId;
    const std::string activeId = isKnownRecipe
        ? value : std::string(gux::StandardRenderRecipe::RecipeId);
    if (!isKnownRecipe) {
        pRenderRecipeId.setWithoutEventNotifications(activeId);
    }
    if (humanGraphicsScene) {
        humanGraphicsScene->setRenderRecipe(activeId);
    }
    saveGuiSettings();
}

void ofApp::onMergeEventIdChanged(string &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->setMergeEvent(value);
        const std::string activeId(humanGraphicsScene->mergeEventId());
        if (activeId != value) {
            ofLogWarning("ofApp") << "Unknown Event ID: " << value
                                  << ". Active Event: " << activeId;
        }
    }
    saveGuiSettings();
}

void ofApp::onSceneLayoutIdChanged(string &value) {
    const std::string standardId(gux::StandardSceneLayout::LayoutId);
    if (value != standardId) {
        pSceneLayoutId.setWithoutEventNotifications(standardId);
    }
    if (humanGraphicsScene) {
        humanGraphicsScene->setSceneLayout(standardId);
    }
    saveGuiSettings();
}

void ofApp::onSceneBehaviorIdChanged(string &value) {
    if (humanGraphicsScene) {
        humanGraphicsScene->setSceneBehavior(value);
        const std::string activeId(humanGraphicsScene->sceneBehaviorId());
        if (activeId != value) {
            ofLogWarning("ofApp") << "Unknown Behavior ID: " << value
                                  << ". Active Behavior: " << activeId;
            pSceneBehaviorId.setWithoutEventNotifications(activeId);
        }
    }
    saveGuiSettings();
}


//--------------------------------------------------------------
//--------------------------------------------------------------
void ofApp::onRealtimeChanged(bool &value) {
  finishRealtimeDetection(false);
  realtimeMode = value;
  applyVideoColorMode();
  // モードを切り替えた直後は、次に届いたフレームをすぐ処理できるようにする。
  lastRealtimeProcessMs = 0;

  if (realtimeMode) {
    int cameraIndex = pCameraIndex.get();
    onCameraIndexChanged(cameraIndex);
  } else {
    // VideoモードではCMIOのカメラ転送スレッドを残さない。
    // AVFoundation動画再生との併存によるメモリ破壊を防ぐ。
    applyCameraSelection(); // realtimeMode=false なので両方閉じる
    ofLogNotice("ofApp") << "Videoモードへ移行したためカメラを閉じました。";
    // 読み込み済みの動画があれば、その比率でMain Windowを固定し直す。
    applyMainWindowAspectLock();
  }

  rebuildGuiPanel();

  // Realtimeへ戻る際は既定サイズにする。比率はユーザーが変更できる。
  if (realtimeMode && mainWindow && mainWindow->getGLFWWindow()) {
    GLFWwindow *handle = mainWindow->getGLFWWindow();
    glfwSetWindowAspectRatio(handle, GLFW_DONT_CARE, GLFW_DONT_CARE);
    // setWindowShape() を通し、Retinaの論理サイズと物理ピクセルの
    // 変換をopenFrameworks側に任せる。
    mainWindow->setWindowShape(W, H);
    applyMainWindowAspectLock();
  }
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onPlayPressed() {
  videoProcessor.play();
}

//--------------------------------------------------------------
void ofApp::onPausePressed() {
  videoProcessor.pause();
}

//-------------------------------------------------------------
void ofApp::onRestartPressed() {
  videoProcessor.restart();
}

//--------------------------------------------------------------
void ofApp::onOpenVideoPressed() {
  if (realtimeMode) return;
  ofFileDialogResult selection = ofSystemLoadDialog("Open Video / Image");
  if (!selection.bSuccess) return;
  loadVideoFile(selection.getPath());
}

//--------------------------------------------------------------
void ofApp::onExportImageSequencePressed() {
  startImageSequenceExport();
}

//--------------------------------------------------------------
void ofApp::startImageSequenceExport() {
  if (isExportingImageSequence || isExportingMovie) {
    pVideoStatusText = "Export is already running";
    return;
  }
  if (realtimeMode || !videoProcessor.isLoaded() || !humanGraphicsScene) {
    pVideoStatusText = "Export requires a loaded video or image in Video mode";
    return;
  }
  if (!mainWindow || !mainWindow->getGLFWWindow()) {
    pVideoStatusText = "Export requires an open Main Window";
    return;
  }

  // Mainの描画座標をここで固定する。Controls側から押されても
  // ofGetWidth()/ofGetHeight() はControlsのサイズを返す可能性がある。
  // 投影余白がONの場合は、余白を除いた描画範囲だけを書き出す。
  exportCanvasWidth = mainCanvasWidth();
  exportCanvasHeight = mainCanvasHeight();
  exportCropPositionX = cropPositionX;
  exportCropPositionY = cropPositionY;
  if (exportCanvasWidth <= 0 || exportCanvasHeight <= 0) {
    pVideoStatusText = "Export failed: invalid Main Window size";
    return;
  }

  exportFrameRate = static_cast<double>(pMainWindowTargetFps.get());
  if (!std::isfinite(exportFrameRate) || exportFrameRate <= 0.0) {
    exportFrameRate = 30.0;
  }

  exportFromStillImage = videoProcessor.isStillImage();
  if (exportFromStillImage) {
    // 静止画は1フレームのPNGだけを書き出し、MOVは作らない。
    if (!ofLoadImage(exportStillPixels, videoProcessor.getLoadedFileName())) {
      pVideoStatusText = "Export failed: could not load source image";
      exportFromStillImage = false;
      return;
    }
    exportSourceWidth = static_cast<int>(exportStillPixels.getWidth());
    exportSourceHeight = static_cast<int>(exportStillPixels.getHeight());
    exportSourceTotalFrames = 1;
    exportSourceFrameRate = exportFrameRate;
    exportTotalFrames = 1;
  } else {
    if (!exportVideoPlayer.load(videoProcessor.getLoadedFileName())) {
      pVideoStatusText = "Export failed: could not load source video";
      return;
    }

    exportSourceWidth = static_cast<int>(exportVideoPlayer.getWidth());
    exportSourceHeight = static_cast<int>(exportVideoPlayer.getHeight());
    exportSourceTotalFrames = exportVideoPlayer.getTotalNumFrames();
    const double sourceDurationSeconds = exportVideoPlayer.getDuration();
    exportSourceFrameRate = sourceDurationSeconds > 0.0
        ? static_cast<double>(exportSourceTotalFrames) / sourceDurationSeconds
        : 30.0;
    if (!std::isfinite(exportSourceFrameRate) || exportSourceFrameRate <= 0.0) {
      exportSourceFrameRate = 30.0;
    }
    exportTotalFrames = sourceDurationSeconds > 0.0
        ? std::max(1, static_cast<int>(std::llround(
              sourceDurationSeconds * exportFrameRate)))
        : exportSourceTotalFrames;
  }
  if (exportSourceWidth <= 0 || exportSourceHeight <= 0 ||
      exportSourceTotalFrames <= 0 || exportTotalFrames <= 0) {
    pVideoStatusText = "Export failed: source video has no frames";
    cancelImageSequenceExport();
    return;
  }

  // Mainの構図・線幅・結合判定はそのままに、描画だけを高解像度化する。
  // ProRes用に偶数寸法へ切り上げ、幅の端数は左右に均等に置く。
  exportHeight = std::max(3000, exportCanvasHeight);
  if (exportHeight % 2 != 0) ++exportHeight;
  exportScale = static_cast<float>(exportHeight) / exportCanvasHeight;
  const double evenWidth = std::ceil(
      static_cast<double>(exportCanvasWidth) * exportScale / 2.0) * 2.0;
  GLint maxTextureSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  if (exportHeight > maxTextureSize || evenWidth > maxTextureSize) {
    pVideoStatusText = "Export failed: 3000px canvas exceeds GPU texture limit";
    cancelImageSequenceExport();
    return;
  }
  exportWidth = static_cast<int>(evenWidth);
  exportOffsetX = 0.5f *
      (exportWidth - exportCanvasWidth * exportScale);

  exportFbo.allocate(exportWidth, exportHeight, GL_RGBA);
  if (!exportFbo.isAllocated()) {
    pVideoStatusText = "Export failed: could not allocate 3000px canvas";
    cancelImageSequenceExport();
    return;
  }
  exportPixels.allocate(exportWidth, exportHeight, OF_PIXELS_RGBA);

  const auto exportRoot = ofFilePath::join(
      ofFilePath::getUserHomeDir(), "Movies/GUxSID_Human_Graphics");
  const auto exportFolder = ofFilePath::getBaseName(
      videoProcessor.getLoadedFileName()) + "_" +
      ofGetTimestampString("%Y%m%d_%H%M%S");
  exportOutputDirectory = ofFilePath::join(exportRoot, exportFolder);
  exportPngDirectory = ofFilePath::join(exportOutputDirectory, "png");
  exportMovieDirectory = ofFilePath::join(exportOutputDirectory, "mov");
  if (!ofDirectory::createDirectory(exportPngDirectory, false, true) ||
      (!exportFromStillImage &&
       !ofDirectory::createDirectory(exportMovieDirectory, false, true))) {
    pVideoStatusText = "Export failed: could not create output folder";
    cancelImageSequenceExport();
    return;
  }

  exportAlpha = pExportAlpha.get();

  // Mainの描画用Sceneとは別インスタンスを使うため、書き出し中も
  // Main Windowのループ再生と色・輪郭の状態を維持できる。
  exportScene = std::make_shared<HumanGraphicsScene>(*humanGraphicsScene);
  exportScene->setCanvasSize(exportCanvasWidth, exportCanvasHeight);
  // Behaviorは時系列状態を持ちうるため、Main側とは独立した状態で書き出す。
  exportScene->setSceneBehavior(
      std::string(humanGraphicsScene->sceneBehaviorId()));

  if (!exportFromStillImage) {
    exportVideoPlayer.setLoopState(OF_LOOP_NONE);
    // AVFoundationでは停止中のfirstFrame()が新規フレームを通知しない場合が
    // あるため、最初の1フレームだけ再生状態で確実にデコードする。
    exportVideoPlayer.play();
    exportVideoPlayer.firstFrame();
  }
  exportFrameIndex = 0;
  exportDecodedSourceFrame = -1;
  exportDecodedData = HumanContourData();
  exportTimelineStartSeconds = ofGetElapsedTimef();
  exportAwaitingFirstFrame = true;
  exportFrameWaitStartedAtMillis = ofGetElapsedTimeMillis();
  isExportingImageSequence = true;
  pVideoStatusText = "Exporting: 0 / " + ofToString(exportTotalFrames);
}

//--------------------------------------------------------------
void ofApp::updateImageSequenceExport() {
  if (!isExportingImageSequence) return;

  const int desiredSourceFrame = std::min(
      exportSourceTotalFrames - 1,
      static_cast<int>(std::floor(
          static_cast<double>(exportFrameIndex) * exportSourceFrameRate /
          exportFrameRate)));
  // AVFoundation can stop reporting isFrameNew() near the physical end of a
  // file even though one output frame remains on the resampled timeline.
  // Hold the last successfully decoded contours for that final output frame
  // so the PNG sequence always reaches its declared length and MOV creation
  // can start.
  const bool holdDecodedDataForFinalOutput =
      exportFrameIndex == exportTotalFrames - 1 &&
      exportDecodedSourceFrame >= 0;

  // Target FPSがソースFPSより高い場合は同じデコード結果を複数回使い、
  // 低い場合もAVFoundationへの連続シークを避けて1フレームずつ進める。
  bool decodedNewFrame = false;
  if (exportDecodedSourceFrame != desiredSourceFrame &&
      !holdDecodedDataForFinalOutput && exportFromStillImage) {
    // 静止画はデコード待ちがなく、唯一のフレームをそのまま使う。
    exportDecodedSourceFrame = desiredSourceFrame;
    decodedNewFrame = true;
  } else if (exportDecodedSourceFrame != desiredSourceFrame &&
      !holdDecodedDataForFinalOutput) {
    exportVideoPlayer.update();
    if (!exportVideoPlayer.isFrameNew()) {
      constexpr uint64_t kFrameDecodeTimeoutMillis = 10000;
      if (ofGetElapsedTimeMillis() - exportFrameWaitStartedAtMillis >=
          kFrameDecodeTimeoutMillis) {
        pVideoStatusText = "Export failed: timed out while decoding frame " +
            ofToString(exportFrameIndex + 1) + " / " +
            ofToString(exportTotalFrames);
        cancelImageSequenceExport();
      }
      return;
    }

    if (exportAwaitingFirstFrame) {
      exportVideoPlayer.setPaused(true);
      exportAwaitingFirstFrame = false;
    }

    ++exportDecodedSourceFrame;
    if (exportDecodedSourceFrame < desiredSourceFrame) {
      if (exportDecodedSourceFrame + 1 == exportSourceTotalFrames - 1) {
        const float finalFrameSeekPosition = std::max(
            0.0f, (static_cast<float>(exportSourceTotalFrames - 1) - 0.5f) /
                      static_cast<float>(exportSourceTotalFrames));
        exportVideoPlayer.setPosition(finalFrameSeekPosition);
      } else {
        exportVideoPlayer.nextFrame();
      }
      exportFrameWaitStartedAtMillis = ofGetElapsedTimeMillis();
      return;
    }
    decodedNewFrame = true;
  }

  if (decodedNewFrame) {
    const ofPixels& framePixels = exportFromStillImage
        ? exportStillPixels : exportVideoPlayer.getPixels();
    if (!framePixels.isAllocated()) return;

    cv::Mat rgbMat;
    const int channels = framePixels.getNumChannels();
    if (channels == 4) {
      cv::Mat rgbaMat(exportSourceHeight, exportSourceWidth, CV_8UC4,
                      const_cast<unsigned char*>(framePixels.getData()));
      cv::cvtColor(rgbaMat, rgbMat, cv::COLOR_RGBA2RGB);
    } else if (channels == 3) {
      rgbMat = cv::Mat(exportSourceHeight, exportSourceWidth, CV_8UC3,
                       const_cast<unsigned char*>(framePixels.getData()));
    } else if (channels == 1) {
      cv::Mat grayMat(exportSourceHeight, exportSourceWidth, CV_8UC1,
                      const_cast<unsigned char*>(framePixels.getData()));
      cv::cvtColor(grayMat, rgbMat, cv::COLOR_GRAY2RGB);
    } else {
      pVideoStatusText = "Export failed: unsupported pixel format";
      cancelImageSequenceExport();
      return;
    }
    const cv::Rect crop = sourceCrop(exportSourceWidth, exportSourceHeight,
                                      exportCanvasWidth, exportCanvasHeight,
                                      exportCropPositionX, exportCropPositionY);
    if (crop.empty()) return;
    exportDecodedData = personSegmenter.detect(
        rgbMat(crop), exportCanvasWidth, exportCanvasHeight);
  }

  const float exportElapsedSeconds = exportTimelineStartSeconds +
      static_cast<float>(exportFrameIndex / exportFrameRate);
  exportScene->update(exportDecodedData, exportElapsedSeconds,
                      static_cast<float>(1.0 / exportFrameRate));
  exportFbo.begin();
  if (exportAlpha) {
    ofClear(0, 0, 0, 0);
  } else {
    ofClear(ofApp::background_color, ofApp::background_color,
            ofApp::background_color, 255);
  }
  ofPushMatrix();
  ofTranslate(exportOffsetX, 0.0f);
  ofScale(exportScale, exportScale);
  // 透明書き出しでは背景を描かない。それ以外は背景のグラデーションも書き出す。
  exportScene->draw(!exportAlpha);
  ofPopMatrix();
  exportFbo.end();

  exportFbo.readToPixels(exportPixels);
  // FBOから読むピクセルはOpenGL座標系なので、PNG用に上下を反転する。
  exportPixels.mirror(true, false);

  const std::string fileName = "frame_" +
      ofToString(exportFrameIndex + 1, 6, '0') + ".png";
  const auto outputPath = ofFilePath::join(exportPngDirectory, fileName);
  if (!ofSaveImage(exportPixels, outputPath)) {
    pVideoStatusText = "Export failed while saving: " + outputPath;
    cancelImageSequenceExport();
    return;
  }

  ++exportFrameIndex;
  if (exportFrameIndex >= exportTotalFrames) {
    const bool fromStillImage = exportFromStillImage;
    cancelImageSequenceExport();
    if (fromStillImage) {
      pVideoStatusText = "Export finished:\n" + outputPath;
    } else {
      startMovieExport();
    }
    return;
  }

  pVideoStatusText = "Exporting: " + ofToString(exportFrameIndex) +
      " / " + ofToString(exportTotalFrames);
  const int nextSourceFrame = std::min(
      exportSourceTotalFrames - 1,
      static_cast<int>(std::floor(
          static_cast<double>(exportFrameIndex) * exportSourceFrameRate /
          exportFrameRate)));
  if (nextSourceFrame == exportDecodedSourceFrame) {
    return;
  }
  if (exportDecodedSourceFrame + 1 == exportSourceTotalFrames - 1) {
    // AVFoundationで末尾へnextFrame()すると、再生時刻がdurationに
    // 吸着して最終フレームがisFrameNew()にならない場合がある。
    // 最終フレームの直前へシークし、AssetReaderに末尾のサンプルを
    // 選ばせることで180/180などの末尾待ちを防ぐ。
    const float finalFrameSeekPosition = std::max(
        0.0f, (static_cast<float>(exportSourceTotalFrames - 1) - 0.5f) /
                  static_cast<float>(exportSourceTotalFrames));
    exportVideoPlayer.setPosition(finalFrameSeekPosition);
  } else {
    exportVideoPlayer.nextFrame();
  }
  exportFrameWaitStartedAtMillis = ofGetElapsedTimeMillis();
}

//--------------------------------------------------------------
void ofApp::cancelImageSequenceExport() {
  isExportingImageSequence = false;
  exportAwaitingFirstFrame = false;
  exportDecodedSourceFrame = -1;
  exportFrameWaitStartedAtMillis = 0;
  exportVideoPlayer.close();
  exportFromStillImage = false;
  exportStillPixels.clear();
  exportScene.reset();
}

//--------------------------------------------------------------
void ofApp::startMovieExport() {
  const std::string folderName =
      ofFilePath::getFileName(exportOutputDirectory);
  exportMoviePath = ofFilePath::join(
      exportMovieDirectory, folderName + ".mov");

#ifdef TARGET_OSX
  exportMovieCodecName = "Apple ProRes 4444 (alpha)";
#else
  exportMovieCodecName = "unsupported";
#endif

  isExportingMovie = true;
  pVideoStatusText = "Creating highest-quality MOV (" +
      exportMovieCodecName + ", AVFoundation)...\n" + exportMoviePath;
  const std::string outputDirectory = exportPngDirectory;
  const std::string outputMoviePath = exportMoviePath;
  const int width = exportWidth;
  const int height = exportHeight;
  const int frameCount = exportFrameIndex;
  const double frameRate = exportFrameRate;
  exportMovieFuture = std::async(std::launch::async, [=]() {
#ifdef TARGET_OSX
    return createProRes4444MovieWithAVFoundation(
        outputDirectory, outputMoviePath, width, height, frameCount,
        frameRate);
#else
    return std::string("AVFoundation is available only on macOS");
#endif
  });
}

//--------------------------------------------------------------
void ofApp::updateMovieExport() {
  if (!isExportingMovie || !exportMovieFuture.valid()) return;
  if (exportMovieFuture.wait_for(std::chrono::seconds(0)) !=
      std::future_status::ready) {
    return;
  }

  const std::string error = exportMovieFuture.get();
  isExportingMovie = false;
  if (error.empty() && ofFile::doesFileExist(exportMoviePath, false)) {
    pVideoStatusText = "Finished: " + ofToString(exportFrameIndex) +
        " PNG frames + MOV (" + exportMovieCodecName +
        ", no audio, AVFoundation)\n" +
        exportMoviePath;
  } else {
    pVideoStatusText = "PNG export finished, but MOV creation failed\n" +
        error;
  }
}

//--------------------------------------------------------------
// 動画を開いた時のMain Windowサイズを入力動画に合わせる
//--------------------------------------------------------------
void ofApp::resizeMainWindowToVideo() {
  if (!mainWindow) return;

  if (!mainWindow->getGLFWWindow()) return;
  // 投影余白がONの間はMain Windowを4Kの比率で保つため、動画サイズへ合わせない。
  if (pProjectionSafeArea.get()) {
    applyMainWindowAspectLock();
    return;
  }

  int w = videoProcessor.getVideoWidth();
  int h = videoProcessor.getVideoHeight();
  if (w <= 0 || h <= 0) return;

  GLFWwindow *handle = mainWindow->getGLFWWindow();
  // 別の比率の動画に読み替えた場合でも、旧い制約が
  // サイズ変更を妨げないようにする。
  glfwSetWindowAspectRatio(handle, GLFW_DONT_CARE, GLFW_DONT_CARE);

  // 動画の幅・高さは物理ピクセル単位。直接
  // glfwSetWindowSize() へ渡すとRetina上で論理サイズとして
  // 扱われるため、openFrameworksの倍率変換を通す。
  mainWindow->setWindowShape(w, h);
  applyMainWindowAspectLock();
}

//--------------------------------------------------------------
// 投影余白がONの間はMain Windowを4K(16:9)の比率に固定し、
// OFFの間は比率の制約を外して自由にリサイズできるようにする。
void ofApp::applyMainWindowAspectLock() {
  if (!mainWindow || !mainWindow->getGLFWWindow()) return;
  GLFWwindow *handle = mainWindow->getGLFWWindow();

  // 全画面中は画面サイズに合わせるため、比率の制約を外す。
  if (mainWindow->getWindowMode() == OF_FULLSCREEN) {
    glfwSetWindowAspectRatio(handle, GLFW_DONT_CARE, GLFW_DONT_CARE);
    return;
  }

  // 投影余白がONの間と2出力中は、4K(16:9)の比率に固定して配置を保つ。
  if (pProjectionSafeArea.get() || dualOutputActive()) {
    glfwSetWindowAspectRatio(handle, kProjectionFrameWidth,
                             kProjectionFrameHeight);
    return;
  }

  // 投影余白がOFFの間は、比率を固定せず自由にリサイズできるようにする。
  glfwSetWindowAspectRatio(handle, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

//--------------------------------------------------------------
// メインウィンドウにファイルがドロップされたときに呼ばれる
// (ofBaseAppの仮想関数。ofRunAppにより自動的に紐付けられているため、
//  main.cpp側で明示的にofAddListerする必要はない)
//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
  handleDroppedFile(dragInfo);
}

//--------------------------------------------------------------
// Control Window(guiWindow)にファイルがドロップされたときに呼ばれる。
// main.cpp側で ofAddListener(guiWindow->events().fileDragEvent, ...) により
// 紐付けている。ofAddListenerの要件に合わせて参照渡しのシグネチャにしている。
//--------------------------------------------------------------
void ofApp::onGuiWindowFileDragged(ofDragInfo &dragInfo) {
  handleDroppedFile(dragInfo);
}

//--------------------------------------------------------------
void ofApp::onGuiWindowKeyPressed(ofKeyEventArgs &args) {
  // Controlsにフォーカスがある時も画角移動とデバッグ切り替えを受け付ける。
  if (args.key == 'd' || args.key == 'D' ||
      args.key == 'f' || args.key == 'F' ||
      args.key == OF_KEY_LEFT || args.key == OF_KEY_RIGHT ||
      args.key == OF_KEY_UP || args.key == OF_KEY_DOWN) {
    keyPressed(args.key);
  }
  // Main Windowが閉じられていてもControls Windowは残るため、
  // Controls側でAキーを受けてMain Windowを復帰できるようにする。
  else if (args.key == 'a' || args.key == 'A') {
    showMainWindow();
  }
}

//--------------------------------------------------------------
// dragEvent / onGuiWindowFileDragged 共通のドロップ処理本体
//--------------------------------------------------------------
void ofApp::handleDroppedFile(const ofDragInfo &dragInfo) {
  // Realtimeモード中はドロップを無視する
  if (realtimeMode) {
    return;
  }

  if (dragInfo.files.empty()) {
    return;
  }

  // 複数ファイルがドロップされても、先頭の1件のみを対象にする
  loadVideoFile(dragInfo.files[0]);
}

//--------------------------------------------------------------
void ofApp::loadVideoFile(const std::string &path) {
  pVideoStatusText = "読み込み中: " + path;

  bool ok = videoProcessor.loadVideo(path);
  if (ok) {
    applyVideoColorMode();
    // 動画の解像度・縦横比にメインウィンドウを合わせる
    resizeMainWindowToVideo();

    pVideoStatusText = string(videoProcessor.isStillImage() ? "表示中: "
                                                            : "再生中: ") +
        path +
        " (" + ofToString(videoProcessor.getVideoWidth()) + "x" +
        ofToString(videoProcessor.getVideoHeight()) + ")";
  } else {
    pVideoStatusText = "読み込みに失敗しました: " + path;
  }
}

//--------------------------------------------------------------
void ofApp::showGuiWindow() {
  // まだ生きているControls Windowは前面に出すだけでよい。
  if (guiWindow) {
    GLFWwindow *handle = guiWindow->getGLFWWindow();
    if (handle) {
      // 閉じるボタン直後にAキーが押された場合は、破棄前なら復帰できる。
      glfwSetWindowShouldClose(handle, GLFW_FALSE);
      glfwShowWindow(handle);
      glfwRestoreWindow(handle);
      glfwFocusWindow(handle);
      guiVisible = true;
      return;
    }
  }

  // 閉じるボタンで破棄済みの場合は、Main Windowとコンテキストを共有して再生成する。
  if (!mainWindow) return;

  ofGLFWWindowSettings guiSettings;
  guiSettings.setSize(controlWindowWidth, controlWindowHeight);
  guiSettings.title = "Controls";
  guiSettings.shareContextWith = mainWindow;

  auto newGuiWindow = ofCreateWindow(guiSettings);
  guiWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(newGuiWindow);
  if (!guiWindow) return;
  guiWindow->events().setFrameRate(pMainWindowTargetFps.get());

  ofAddListener(guiWindow->events().draw, this, &ofApp::drawGui);
  ofAddListener(guiWindow->events().fileDragEvent, this,
                &ofApp::onGuiWindowFileDragged);
  ofAddListener(guiWindow->events().keyPressed, this,
                &ofApp::onGuiWindowKeyPressed);
  guiVisible = true;
}

//--------------------------------------------------------------
void ofApp::showMainWindow() {
  // Main Windowが生きている場合は、再生成せず前面へ出す。
  if (mainWindow) {
    GLFWwindow *handle = mainWindow->getGLFWWindow();
    if (handle) {
      glfwSetWindowShouldClose(handle, GLFW_FALSE);
      glfwShowWindow(handle);
      glfwRestoreWindow(handle);
      glfwFocusWindow(handle);
      return;
    }
  }

  // Main Windowが破棄済みなら、Controls Windowとコンテキストを共有して復帰する。
  ofGLFWWindowSettings mainSettings;
  if (!realtimeMode && videoProcessor.isLoaded()) {
    // Videoモード中の復帰は、起動時の16:9ではなく
    // 読み込み済み動画の縦横比で作り直す。
    mainSettings.setSize(videoProcessor.getVideoWidth(),
                         videoProcessor.getVideoHeight());
  } else {
    mainSettings.setSize(W, H);
  }
  mainSettings.title = "Main Window";
  mainSettings.windowMode = OF_WINDOW;
  if (guiWindow && guiWindow->getGLFWWindow()) {
    mainSettings.shareContextWith = guiWindow;
  }

  auto newMainWindow = ofCreateWindow(mainSettings);
  mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(newMainWindow);
  if (!mainWindow) return;
  mainWindow->events().setFrameRate(pMainWindowTargetFps.get());
  applyMainWindowAspectLock();

  // ofRunAppはsetup()を再度呼ぶため、既に動作中のアプリを初期化し直さないよう
  // 必要なイベントだけを新しいMain Windowへ接続する。
  ofAddListener(mainWindow->events().update, this,
                &ofApp::onRecreatedMainWindowUpdate);
  ofAddListener(mainWindow->events().draw, this,
                &ofApp::onRecreatedMainWindowDraw);
  ofAddListener(mainWindow->events().keyPressed, this,
                &ofApp::onRecreatedMainWindowKeyPressed);
  ofAddListener(mainWindow->events().fileDragEvent, this,
                &ofApp::onGuiWindowFileDragged);
  ofAddListener(mainWindow->events().exit, this,
                &ofApp::onRecreatedMainWindowExit);
}

//--------------------------------------------------------------
void ofApp::onRecreatedMainWindowUpdate(ofEventArgs &args) {
  update();
}

//--------------------------------------------------------------
void ofApp::onRecreatedMainWindowDraw(ofEventArgs &args) {
  draw();
}

//--------------------------------------------------------------
void ofApp::onRecreatedMainWindowKeyPressed(ofKeyEventArgs &args) {
  keyPressed(args.key);
}

//--------------------------------------------------------------
void ofApp::onRecreatedMainWindowExit(ofEventArgs &args) {
  exit();
}


// ============================================
// ★追加: カメラデバイス切り替え用リスナー
// ============================================
void ofApp::onCameraIndexChanged(int &index) {
  if (index < 0 || index >= static_cast<int>(cameraDevices.size())) return;
  // カメラの名前表示を更新
  pCameraName = cameraDevices[index].deviceName;
  syncCameraChoices();
  applyCameraSelection();
  saveGuiSettings();
}

void ofApp::onCamera2IndexChanged(int &index) {
  if (index < 0 || index >= static_cast<int>(cameraDevices.size())) return;
  syncCameraChoices();
  applyCameraSelection();
  saveGuiSettings();
}

// 「番号: 名前」。macOSの名前の先頭に付く方向制御文字(U+200E)は取り除く。
std::string ofApp::cameraDisplayName(const ofVideoDevice &device) {
  std::string name = device.deviceName;
  const std::string lrm = "\xE2\x80\x8E";
  for (size_t at = name.find(lrm); at != std::string::npos; at = name.find(lrm)) {
    name.erase(at, lrm.size());
  }
  return ofToString(device.id) + ": " + name;
}

// 系統ごとに、認識したカメラを1台ずつトグルにする (起動時のカメラ一覧)。
void ofApp::setupCameraChoices() {
  cameraChoiceListeners.unsubscribeAll();
  const char *groupNames[2] = {"Camera 1 (Upper)", "Camera 2 (Lower)"};
  for (int input = 0; input < 2; ++input) {
    cameraChoiceGroups[input].clear();
    cameraChoiceGroups[input].setName(groupNames[input]);
    cameraChoices[input].clear();
    for (size_t device = 0; device < cameraDevices.size(); ++device) {
      ofParameter<bool> choice;
      choice.set(cameraDisplayName(cameraDevices[device]), false);
      cameraChoices[input].push_back(choice);
      cameraChoiceGroups[input].add(cameraChoices[input].back());
      cameraChoiceListeners.push(cameraChoices[input].back().newListener(
          [this, input, device](bool &value) {
            onCameraChoiceChanged(input, static_cast<int>(device), value);
          }));
    }
  }
  syncCameraChoices();
}

// 現在のカメラIDに合わせて、トグルのチェックを付け直す。
void ofApp::syncCameraChoices() {
  const int selected[2] = {pCameraIndex.get(), pCamera2Index.get()};
  for (int input = 0; input < 2; ++input) {
    for (size_t device = 0; device < cameraChoices[input].size(); ++device) {
      cameraChoices[input][device].setWithoutEventNotifications(
          static_cast<int>(device) == selected[input]);
    }
  }
}

void ofApp::onCameraChoiceChanged(int input, int device, bool value) {
  // 選択中のものを外した場合は、そのまま選択状態に戻す (必ず1台選ばれている)。
  ofParameter<int> &index = input == 0 ? pCameraIndex : pCamera2Index;
  if (!value || index.get() == device) {
    syncCameraChoices();
    return;
  }
  index = device; // onCameraIndexChanged / onCamera2IndexChanged が開き直す
}

// 選択に合わせてカメラを開き直す。変わった系統だけを閉じてから開く
// (2系統で機器を入れ替える場合に、同じ機器を二重に開かないため)。
void ofApp::applyCameraSelection() {
  int wanted[2] = {-1, -1};
  if (realtimeMode && !cameraDevices.empty()) {
    wanted[0] = pCameraIndex.get();
    if (dualOutputActive() && pCamera2Index.get() != wanted[0]) {
      wanted[1] = pCamera2Index.get();
    }
  }
  ofVideoGrabber *grabbers[2] = {&cam, &cam2};
  bool changed[2] = {false, false};
  for (int i = 0; i < 2; ++i) {
    if (openedCameraDevice[i] == wanted[i]) continue;
    changed[i] = true;
    if (i == 1 && detectionFuture2.valid()) {
      takeDetectionResult(detectionFuture2, humanData2, false);
    }
    grabbers[i]->close();
    openedCameraDevice[i] = -1;
  }
  for (int i = 0; i < 2; ++i) {
    if (!changed[i] || wanted[i] < 0) continue;
    grabbers[i]->setDeviceID(wanted[i]);
    grabbers[i]->setup(W, H); // W, H は ofApp.h で定義されている解像度
    openedCameraDevice[i] = wanted[i];
    ofLogNotice() << "Camera " << (i + 1) << " -> device " << wanted[i] << ": "
                  << cameraDevices[wanted[i]].deviceName;
  }
  if (realtimeMode && dualOutputActive() && wanted[1] < 0 && changed[1]) {
    ofLogNotice() << "Camera 2 shares Camera 1 (same device)";
  }
}

//--------------------------------------------------------------
void ofApp::onAutoCycleChanged(bool &value) {
  lastCycleSwitchMs = ofGetElapsedTimeMillis();
  saveGuiSettings();
}
void ofApp::onCycleSecondsChanged(float &value) {
  saveGuiSettings();
}
void ofApp::onTransitionSecondsChanged(float &value) {
  saveGuiSettings();
}

// Fill (Base) と Stroke のトグルを入れ替える。
// Fill表示中なら Stroke だけに、それ以外なら Fill だけにする。
// ほかの設定はControlsの値のまま。
void ofApp::applyNextCycleLook() {
  const bool showFill = !pGraphicsEnableBase.get();
  pGraphicsEnableBase = showFill;
  pGraphicsEnableStroke = !showFill;
}

// Auto Cycle がオンの間、Cycle Interval ごとに Fill と Stroke を切り替える。
void ofApp::updateAutoCycle() {
  if (cycleTransitionActive) return;  // 演出中は次の切り替えを始めない
  if (!pAutoCycle.get()) return;
  if (isExportingMovie || isExportingImageSequence) return;
  const uint64_t now = ofGetElapsedTimeMillis();
  const uint64_t intervalMs = static_cast<uint64_t>(
      std::max(1.0f, pCycleSeconds.get()) * 1000.0f);
  if (now - lastCycleSwitchMs < intervalMs) return;

  lastCycleSwitchMs = now;
  if (pTransitionSeconds.get() > 0.0f) {
    startCycleTransition();
  } else {
    applyNextCycleLook();
  }
}

// 切り替えの演出を始める。今の見た目を「前の見た目」の Scene として複製し、
// 本体のトグルはすぐ切り替える。今いる人それぞれに、
// Transition Window の中でランダムな開始時刻を割り振る。
void ofApp::startCycleTransition() {
  if (!humanGraphicsScene) {
    applyNextCycleLook();
    return;
  }
  const float duration = std::max(0.01f, pTransitionSeconds.get());
  const float latestStart =
      std::max(0.0f, pTransitionWindowSeconds.get() - duration);

  std::shared_ptr<HumanGraphicsScene> mains[2] = {humanGraphicsScene,
                                                  humanGraphicsScene2};
  cycleTransitionEndSeconds = 0.0f;
  for (int band = 0; band < 2; ++band) {
    transitionStartSeconds[band].clear();
    outgoingSceneActive[band] = false;
    if (!mains[band] || (band == 1 && !dualOutputActive())) continue;
    // 2回目以降は代入で中身だけ写す (影のFBOなどは作り直さず使い回す)。
    if (!outgoingScenes[band]) {
      outgoingScenes[band] = std::make_shared<HumanGraphicsScene>(*mains[band]);
    } else {
      *outgoingScenes[band] = *mains[band];
    }
    outgoingScenes[band]->offsetScale = 0.0f;  // 割り振りのない人(新しく来た人)は出さない
    outgoingScenes[band]->setHoldColors(true);
    outgoingSceneActive[band] = true;
    mains[band]->setHoldColors(true);
    mains[band]->setSoundShapesFrozen(true);
    mains[band]->setMergeSuppressed(true);
    for (const gux::ObjectId id : mains[band]->trackedIds()) {
      const float start = ofRandom(0.0f, latestStart);
      transitionStartSeconds[band][id] = start;
      cycleTransitionEndSeconds =
          std::max(cycleTransitionEndSeconds, start + duration);
    }
  }

  cycleTransitionActive = true;
  cycleTransitionStartMs = ofGetElapsedTimeMillis();
  applyNextCycleLook();  // 本体は新しい見た目になる
  updateCycleTransition();    // 最初のフレームから割り振りを反映する
}

namespace {
// 0→1 をゆっくり始めてゆっくり終える (cubic in-out)。
float easeInOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t < 0.5f ? 4.0f * t * t * t
                  : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
}  // namespace

// 人ごとの開始時刻からの経過で、前と新しい見た目の Scene に割り振る。
//   開始前 : 前の見た目の形のまま (新しい見た目では隠す)
//   開始後 : 前の見た目の形はすぐ消し、新しい見た目の形が現れていく
//            (塗りは左下から色面が入り、線は一筆書きのようになぞる)。
//            現れる長さは Transition (s)。
void ofApp::updateCycleTransition() {
  if (!cycleTransitionActive) return;
  const float elapsed =
      (ofGetElapsedTimeMillis() - cycleTransitionStartMs) / 1000.0f;
  if (elapsed >= cycleTransitionEndSeconds) {
    finishCycleTransition();
    return;
  }

  const float duration = std::max(0.01f, pTransitionSeconds.get());
  const int baseVertexCount = pVertexCount.get();
  const float baseScale = pGraphicsOffsetScale.get();
  std::shared_ptr<HumanGraphicsScene> mains[2] = {humanGraphicsScene,
                                                  humanGraphicsScene2};
  for (int band = 0; band < 2; ++band) {
    if (!outgoingSceneActive[band] || !mains[band] || !outgoingScenes[band]) continue;
    std::unordered_map<gux::ObjectId, gux::ShapeOverride> hiddenInNew;
    std::unordered_map<gux::ObjectId, gux::ShapeOverride> shownInOld;
    std::unordered_map<gux::ObjectId, float> reveal;
    for (const auto &[id, start] : transitionStartSeconds[band]) {
      const float p = (elapsed - start) / duration;
      if (p < 0.0f) {
        shownInOld[id] = {baseVertexCount, baseScale};
        hiddenInNew[id] = {baseVertexCount, 0.0f};
      } else if (p < 1.0f) {
        reveal[id] = easeInOutCubic(p);
      }
    }
    // 隠す人の組み合わせは開始時刻にしか変わらないので、形の作り直しはその時だけ起きる。
    mains[band]->setShapeOverrides(std::move(hiddenInNew));
    mains[band]->setRevealProgress(std::move(reveal));
    outgoingScenes[band]->setShapeOverrides(std::move(shownInOld));
  }
}

void ofApp::finishCycleTransition() {
  cycleTransitionActive = false;
  std::shared_ptr<HumanGraphicsScene> mains[2] = {humanGraphicsScene,
                                                  humanGraphicsScene2};
  for (int band = 0; band < 2; ++band) {
    outgoingSceneActive[band] = false;
    transitionStartSeconds[band].clear();
    if (!mains[band]) continue;
    mains[band]->clearShapeOverrides();
    mains[band]->clearRevealProgress();
    mains[band]->setMergeSuppressed(false);
    mains[band]->setHoldColors(false);
    mains[band]->setSoundShapesFrozen(false);
  }
  // 新しい見た目の形への変化を、人の動きとして音へ送らない。
  soundOscSender.resetMotionBaseline();
}

//--------------------------------------------------------------
void ofApp::onFlipHorizontalChanged(bool &value) {
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::update() {
  updateAutoCycle();
  updateCycleTransition();
  if (guiRebuildPending) {
    guiRebuildPending = false;
    rebuildGuiPanel();
  }
  // MOV生成はバックグラウンドで進め、完了だけをMainスレッドで反映する。
  if (isExportingMovie) {
    updateMovieExport();
  }

  // 書き出し中は通常のMain表示用処理を止め、書き出し専用プレイヤーの
  // 1フレームずつの処理だけを進める。
  if (isExportingImageSequence) {
    updateImageSequenceExport();
    return;
  }

  if (realtimeMode) {
    // ============================================
    // Realtime=true: Webカメラ映像を処理する
    // YOLO推論を別スレッドで実行し、完了した結果をメインスレッドで反映する。
    // ============================================
    cam.update();
    const bool camera2Shared = openedCameraDevice[1] < 0;
    if (!camera2Shared) cam2.update();

    if (isFutureReady(detectionFuture)) {
      takeDetectionResult(detectionFuture, humanData, true);
    }
    if (isFutureReady(detectionFuture2)) {
      takeDetectionResult(detectionFuture2, humanData2, true);
    }
    // 推論していない間に、系統1の検出設定を系統2へ写す。
    syncSecondPipelineSettings();

    const int outW = mainCanvasWidth();
    const int outH = mainCanvasHeight();
    // 同じセッションに同時にアクセスしないよう、系統ごとに
    // 前の推論が終わるまで新しいフレームの推論を起動しない。
    const uint64_t now = ofGetElapsedTimeMillis();
    const uint64_t intervalMs = 1000 / std::max(1, pMainWindowTargetFps.get());
    const auto due = [&](const std::future<HumanContourData> &future,
                         const PersonSegmenter &segmenter, uint64_t lastMs) {
      return segmenter.isLoaded() && !future.valid() &&
             (lastMs == 0 || now - lastMs >= intervalMs);
    };
    // 新しいフレームが来たかを調べる。プレビュー用バッファ (colorImg) への
    // 全面コピーと左右反転は、デバッグ表示を出している時だけ行う。
    const auto takeFrame = [&](ofVideoGrabber &grabber, ofxCvColorImage &image) {
      if (!grabber.isFrameNew()) return false;
      const ofPixels &pixels = grabber.getPixels();
      if (pixels.getWidth() <= 0 || pixels.getHeight() <= 0) return false;
      if (showDebug) {
        image.setFromPixels(pixels);
        if (pFlipHorizontal) image.mirror(false, true);
      }
      return true;
    };
    // カメラの画素バッファは次の更新で変わるため、推論用に切り抜いて複製する。
    // カメラの画素から使う範囲だけを1回で切り出す (左右反転もその範囲だけ)。
    const auto cropForDetection = [&](ofVideoGrabber &grabber) {
      const ofPixels &pixels = grabber.getPixels();
      const int frameW = static_cast<int>(pixels.getWidth());
      const int frameH = static_cast<int>(pixels.getHeight());
      if (frameW <= 0 || frameH <= 0 || pixels.getNumChannels() != 3) {
        return cv::Mat();
      }
      // 切り抜き位置は反転後の画像に対するものなので、反転前の座標に直す。
      cv::Rect crop = sourceCrop(frameW, frameH, outW, outH,
                                 cropPositionX, cropPositionY);
      if (crop.empty()) return cv::Mat();
      if (pFlipHorizontal) crop.x = frameW - crop.x - crop.width;
      const cv::Mat source(frameH, frameW, CV_8UC3,
                           const_cast<unsigned char *>(pixels.getData()),
                           pixels.getBytesStride());
      cv::Mat frame;
      if (pFlipHorizontal) {
        cv::flip(source(crop), frame, 1);  // 新しい画像に書き出すので複製を兼ねる
      } else {
        frame = source(crop).clone();
      }
      return frame;
    };
    const auto launch2 = [&](const cv::Mat &frame) {
      detectionFuture2 = std::async(
          std::launch::async,
          [this, frame, outW, outH]() -> HumanContourData {
            return personSegmenter2.detect(frame, outW, outH);
          });
      lastRealtimeProcessMs2 = now;
    };

    // カメラ1 (共有中はカメラ2の推論もこのフレームで行う)
    if (takeFrame(cam, colorImg)) {
      const bool run1 = due(detectionFuture, personSegmenter, lastRealtimeProcessMs);
      const bool run2 = dualOutputActive() && camera2Shared &&
          due(detectionFuture2, personSegmenter2, lastRealtimeProcessMs2);
      if (run1 || run2) {
        // 2系統は同じ複製を読み取り専用で共有する。
        const cv::Mat frame = cropForDetection(cam);
        if (!frame.empty()) {
          if (run1) {
            detectionFuture = std::async(
                std::launch::async,
                [this, frame, outW, outH]() -> HumanContourData {
                  return personSegmenter.detect(frame, outW, outH);
                });
            lastRealtimeProcessMs = now;
          }
          if (run2) launch2(frame);
        }
      }
    }

    // カメラ2 (カメラ1と別の機器を選んでいる時)
    if (!camera2Shared && takeFrame(cam2, colorImg2) && dualOutputActive() &&
        due(detectionFuture2, personSegmenter2, lastRealtimeProcessMs2)) {
      const cv::Mat frame = cropForDetection(cam2);
      if (!frame.empty()) launch2(frame);
    }
  } else {
    // ============================================
    // Realtime=false: ドロップされた動画ファイルを処理する
    // (処理の実体はVideoProcessingクラスに分離している)
    // ============================================
    if (mainWindow) {
      videoProcessor.setOutputView(mainCanvasWidth(),
                                   mainCanvasHeight(),
                                   cropPositionX, cropPositionY);
    }
    videoProcessor.update();
  }

  // Sceneのレイアウトを描画範囲（投影余白を除いた範囲）に合わせる。
  // setCanvasSize()はレイアウトを作り直すため、サイズが変わった時だけ呼ぶ。
  if (humanGraphicsScene) {
    const int canvasW = mainCanvasWidth();
    const int canvasH = mainCanvasHeight();
    if (canvasW != lastSceneCanvasWidth || canvasH != lastSceneCanvasHeight) {
      humanGraphicsScene->setCanvasSize(canvasW, canvasH);
      if (humanGraphicsScene2) humanGraphicsScene2->setCanvasSize(canvasW, canvasH);
      for (auto &scene : outgoingScenes) {
        if (scene) scene->setCanvasSize(canvasW, canvasH);
      }
      lastSceneCanvasWidth = canvasW;
      lastSceneCanvasHeight = canvasH;
    }
  }

  // 現在のアクティブなシーンへ座標データを渡す
  // (Realtimeの状態に応じて、渡すデータの出所だけを切り替える)
  if (currentScene) {
    const HumanContourData &activeData = realtimeMode ? humanData : videoProcessor.humanData;
    currentScene->update(activeData);
    // 切り替えの演出中は、前のプリセットの Scene にも同じ検出結果を渡す
    // (同じ状態から同じ入力でトラッキングするので、人のIDも揃う)。
    if (cycleTransitionActive && outgoingSceneActive[0] && outgoingScenes[0]) {
      outgoingScenes[0]->update(activeData);
    }
  }
  if (dualOutputActive() && humanGraphicsScene2) {
    humanGraphicsScene2->update(humanData2);
    if (cycleTransitionActive && outgoingSceneActive[1] && outgoingScenes[1]) {
      outgoingScenes[1]->update(humanData2);
    }
  }

  // 人物グラフィックのシーンを表示している間だけ、音へ人物の情報を送る。
  if (humanGraphicsScene && currentScene == humanGraphicsScene) {
    const glm::vec2 canvas = humanGraphicsScene->canvasSize();
    if (dualOutputActive() && humanGraphicsScene2) {
      // Dual Outputでは2系統の人をまとめて送る。結ぶストロークと同じく、
      // 上段(左の投影)の右端と下段(右の投影)の左端がつながった横長の1画面として扱い、
      // 下段の人は1段分だけ右へずらす。x は 0〜0.5 が上段、0.5〜1 が下段になる。
      // 2系統のTrackerはどちらもIDを1から振るため、下段のIDはずらして重ならないようにする。
      // (ずらしたIDどうしは合体しないので、上段と下段の人がmerge扱いになることもない)
      const float bandOffset = canvas.x;
      dualSoundPeople = humanGraphicsScene->soundPeople();
      for (gux::SoundPerson person : humanGraphicsScene2->soundPeople()) {
        person.id += kSecondPipelineSoundIdOffset;
        person.group += kSecondPipelineSoundIdOffset;
        person.centroid.x += bandOffset;
        for (glm::vec2 &vertex : person.vertices) vertex.x += bandOffset;
        dualSoundPeople.push_back(std::move(person));
      }
      soundOscSender.update(dualSoundPeople, canvas.x * 2.0f, canvas.y,
                            ofGetElapsedTimef());
    } else {
      soundOscSender.update(humanGraphicsScene->soundPeople(), canvas.x,
                            canvas.y, ofGetElapsedTimef());
    }
  }
}


//--------------------------------------------------------------
void ofApp::draw() {
  // Exportボタンを押した後はMain Windowの通常描画を停止する。
  if (isExportingImageSequence) return;

  if (dualOutputActive()) {
    drawDualOutput();
    return;
  }

  // 現在のシーンのグラフィックを描画
  // (Realtimeでもオフラインでも、Scene側は同じ描画ロジックのまま)
  // 投影余白がONの場合は、中央の描画範囲へ原点を移して描く。
  const ofRectangle canvasRect = mainCanvasRect();
  ofPushMatrix();
  ofTranslate(canvasRect.x, canvasRect.y);

  if (currentScene) {
    currentScene->draw();
    // 切り替えの演出中は、まだ変わっていない人を前のプリセットで重ねて描く。
    if (cycleTransitionActive && outgoingSceneActive[0] && outgoingScenes[0] &&
        currentScene == humanGraphicsScene) {
      outgoingScenes[0]->draw(false);
    }
  }

  // Dキーが押されている時のみ、デバッグ用HUDを表示
  if (showDebug) {
    drawDebug();
  }
  ofPopMatrix();

  // 描画範囲からはみ出した分も含めて、外側を白で塗りつぶす。
  drawProjectionMargins();
}

//--------------------------------------------------------------
ofRectangle ofApp::mainCanvasRect() const {
  const float windowW = static_cast<float>(
      mainWindow ? mainWindow->getWidth() : ofGetWidth());
  const float windowH = static_cast<float>(
      mainWindow ? mainWindow->getHeight() : ofGetHeight());
  if (!pProjectionSafeArea.get() || windowW <= 0.0f || windowH <= 0.0f) {
    return ofRectangle(0.0f, 0.0f, windowW, windowH);
  }
  // 4K画面に対する投影範囲の割合を、現在のウィンドウサイズに当てはめる。
  // (4Kフルスクリーンでは、ちょうど中央の3840x990になる)
  const float w = std::round(windowW * kProjectionSafeWidth /
                             static_cast<float>(kProjectionFrameWidth));
  const float h = std::round(windowH * kProjectionSafeHeight /
                             static_cast<float>(kProjectionFrameHeight));
  return ofRectangle(std::round((windowW - w) * 0.5f),
                     std::round((windowH - h) * 0.5f), w, h);
}

int ofApp::mainCanvasWidth() const {
  // 2出力中は、ウィンドウの大きさに関係なく1段分の解像度で描く。
  if (dualOutputActive()) return kDualBandWidth;
  return static_cast<int>(mainCanvasRect().width);
}

int ofApp::mainCanvasHeight() const {
  if (dualOutputActive()) return kDualBandHeight;
  return static_cast<int>(mainCanvasRect().height);
}

//--------------------------------------------------------------
void ofApp::syncSecondPipelineSettings() {
  if (!humanGraphicsScene || !humanGraphicsScene2) return;
  HumanGraphicsScene &a = *humanGraphicsScene;
  HumanGraphicsScene &b = *humanGraphicsScene2;
  b.enableBase = a.enableBase;
  b.enableOffset = a.enableOffset;
  b.enableStroke = a.enableStroke;
  b.enableBridgeStroke = a.enableBridgeStroke;
  b.enableMerge = a.enableMerge;
  b.enableLooseContour = a.enableLooseContour;
  b.looseContourStrength = a.looseContourStrength;
  b.minPersonSize = a.minPersonSize;
  b.offsetSize = a.offsetSize;
  b.offsetScale = a.offsetScale;
  b.offsetJoinType = a.offsetJoinType;
  b.strokeWeight = a.strokeWeight;
  b.strokeJoinType = a.strokeJoinType;
  b.baseMaterialType = a.baseMaterialType;
  b.outlineMaterialType = a.outlineMaterialType;
  b.paperShadow = a.paperShadow;
  if (b.renderRecipeId() != a.renderRecipeId()) {
    b.setRenderRecipe(std::string(a.renderRecipeId()));
  }
  if (b.mergeEventId() != a.mergeEventId()) {
    b.setMergeEvent(std::string(a.mergeEventId()));
  }
  if (b.sceneLayoutId() != a.sceneLayoutId()) {
    b.setSceneLayout(std::string(a.sceneLayoutId()));
  }
  if (b.sceneBehaviorId() != a.sceneBehaviorId()) {
    b.setSceneBehavior(std::string(a.sceneBehaviorId()));
  }

  // 推論中の値は書き換えない (別スレッドが読んでいるため)。
  if (!detectionFuture2.valid()) {
    personSegmenter2.yoloConfidenceThreshold = personSegmenter.yoloConfidenceThreshold;
    personSegmenter2.aspectRatioPercent = personSegmenter.aspectRatioPercent;
    personSegmenter2.nmsThreshold = personSegmenter.nmsThreshold;
    personSegmenter2.personClassId = personSegmenter.personClassId;
    personSegmenter2.maxDetections = personSegmenter.maxDetections;
    personSegmenter2.minContourArea = personSegmenter.minContourArea;
  }
}

//--------------------------------------------------------------
void ofApp::allocateDualBandFbos() {
  for (ofFbo &fbo : dualBandFbo) {
    if (fbo.isAllocated()) continue;
    ofFboSettings settings;
    settings.width = kDualBandWidth;
    settings.height = kDualBandHeight;
    settings.internalformat = GL_RGBA;
    settings.numSamples = 4; // Main Windowと同じくエッジを滑らかにする
    fbo.allocate(settings);
  }
}

//--------------------------------------------------------------
// 各系統を3550x950のFBOへ描き、3840x2160のキャンバスへ上下2段で置く。
// (Sceneは描画のたびに背景を塗るので、FBOに分けて互いに消さないようにする)
// ウィンドウが4Kでない場合は、キャンバス全体を縮小して中央に表示する。
void ofApp::drawDualOutput() {
  allocateDualBandFbos();
  const std::shared_ptr<HumanGraphicsScene> scenes[2] = {humanGraphicsScene,
                                                         humanGraphicsScene2};
  // 結ぶストロークは、上段の右端と下段の左端が接した1本の横長画面として結ぶ。
  // 両段の重心を下段だけ右へ1段分ずらして集め、各段にはその段の座標で渡す。
  // 段の外へ出る部分は FBO の端で切れるので、境目でつながって見える。
  const bool joinBridge = scenes[0] && scenes[1] &&
                          scenes[0]->enableBridgeStroke &&
                          scenes[1]->enableBridgeStroke;
  if (joinBridge) {
    const float bandOffset = static_cast<float>(kDualBandWidth);
    std::vector<glm::vec2> joined = scenes[0]->bridgeStrokePoints();
    for (const glm::vec2 &p : scenes[1]->bridgeStrokePoints()) {
      joined.push_back(p + glm::vec2(bandOffset, 0.0f));
    }
    std::vector<glm::vec2> lower = joined;
    for (glm::vec2 &p : lower) p.x -= bandOffset;
    scenes[0]->setBridgeStrokeOverride(std::move(joined));
    scenes[1]->setBridgeStrokeOverride(std::move(lower));
  }
  for (int i = 0; i < 2; ++i) {
    dualBandFbo[i].begin();
    ofClear(background_color, background_color, background_color, 255);
    if (scenes[i]) scenes[i]->draw();
    if (cycleTransitionActive && outgoingSceneActive[i] && outgoingScenes[i]) {
      outgoingScenes[i]->draw(false);
    }
    if (showDebug) drawDebug(i);
    dualBandFbo[i].end();
  }
  if (joinBridge) {
    scenes[0]->clearBridgeStrokeOverride();
    scenes[1]->clearBridgeStrokeOverride();
  }

  const float windowW = static_cast<float>(
      mainWindow ? mainWindow->getWidth() : ofGetWidth());
  const float windowH = static_cast<float>(
      mainWindow ? mainWindow->getHeight() : ofGetHeight());
  const float scale = std::min(windowW / kDualCanvasWidth,
                               windowH / kDualCanvasHeight);
  const float offsetX = std::round((windowW - kDualCanvasWidth * scale) * 0.5f);
  const float offsetY = std::round((windowH - kDualCanvasHeight * scale) * 0.5f);

  ofBackground(0);
  ofPushMatrix();
  ofPushStyle();
  ofTranslate(offsetX, offsetY);
  ofScale(scale, scale);
  ofSetColor(255);
  dualBandFbo[0].draw(0, kDualUpperBandY);
  dualBandFbo[1].draw(0, kDualLowerBandY);
  ofPopStyle();
  ofPopMatrix();
}

//--------------------------------------------------------------
void ofApp::drawProjectionMargins() {
  if (!pProjectionSafeArea.get() || dualOutputActive()) return;
  const float windowW = static_cast<float>(
      mainWindow ? mainWindow->getWidth() : ofGetWidth());
  const float windowH = static_cast<float>(
      mainWindow ? mainWindow->getHeight() : ofGetHeight());
  const ofRectangle r = mainCanvasRect();

  ofPushStyle();
  ofFill();
  ofSetColor(255);
  ofDrawRectangle(0.0f, 0.0f, windowW, r.y);                      // 上
  ofDrawRectangle(0.0f, r.getBottom(), windowW, windowH - r.getBottom()); // 下
  ofDrawRectangle(0.0f, r.y, r.x, r.height);                      // 左
  ofDrawRectangle(r.getRight(), r.y, windowW - r.getRight(), r.height);  // 右
  ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::toggleMainWindowFullscreen() {
  if (!mainWindow || !mainWindow->getGLFWWindow()) return;
  const bool goFullscreen = mainWindow->getWindowMode() != OF_FULLSCREEN;
  if (goFullscreen) {
    // 比率の制約が残っていると画面いっぱいに広がらないため、先に外す。
    glfwSetWindowAspectRatio(mainWindow->getGLFWWindow(),
                             GLFW_DONT_CARE, GLFW_DONT_CARE);
  }
  mainWindow->setFullscreen(goFullscreen);
  // ウィンドウ表示へ戻った時は、元の比率の制約を掛け直す。
  if (!goFullscreen) applyMainWindowAspectLock();
}

//--------------------------------------------------------------
void ofApp::onProjectionSafeAreaChanged(bool &value) {
  applyMainWindowAspectLock();
  // 描画範囲が変わるので、推論結果もすぐ作り直す。
  if (realtimeMode) {
    finishRealtimeDetection(false);
    lastRealtimeProcessMs = 0;
  }
  saveGuiSettings();
}

//--------------------------------------------------------------
// 2画面モードの切り替え。カメラ2の開閉、描画サイズ、ウィンドウ比率を合わせ直す。
void ofApp::onDualOutputChanged(bool &value) {
  if (realtimeMode) {
    finishRealtimeDetection(false);
    lastRealtimeProcessMs = 0;
    lastRealtimeProcessMs2 = 0;
    humanData2 = HumanContourData();
  }
  loadSecondSegmenterIfNeeded();
  applyCameraSelection();
  applyMainWindowAspectLock();
  // パネルの表示 (カメラ2の選択欄) を切り替える。クリック処理中にパネルを
  // 作り直すと新しいトグルが反応しなくなるため、次の update() で作り直す。
  guiRebuildPending = true;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::drawDebug(int band) {
  ofPushStyle();
  // 既定の文字描画(BILLBOARD)はウィンドウの投影を前提に位置を計算するため、
  // Dual Output の FBO 内では文字が反転したり範囲外に出たりする。
  // 座標系どおりに描く MODEL モードにして、FBO 内でも正しく表示させる。
  ofSetDrawBitmapMode(OF_BITMAPMODE_MODEL);
  // Dual Output の下段はカメラ2の映像と検出結果を使う。
  // カメラ2がカメラ1と同じ機器の時は、映像はカメラ1のものを共有している。
  const bool lowerBand = band == 1 && dualOutputActive();
  const bool camera2Shared = openedCameraDevice[1] < 0;
  ofxCvColorImage &debugImg =
      (lowerBand && !camera2Shared) ? colorImg2 : colorImg;
  ofVideoGrabber &debugCam = (lowerBand && !camera2Shared) ? cam2 : cam;
  const HumanContourData &debugData = lowerBand ? humanData2 : humanData;
  PersonSegmenter &debugSegmenter =
      lowerBand ? personSegmenter2 : personSegmenter;
  const int canvasWidth = mainCanvasWidth();
  const int canvasHeight = mainCanvasHeight();
  cv::Rect visibleCrop;
  const ofTexture *sourceTexture = nullptr;
  int sourceWidth = 0;
  int sourceHeight = 0;

  // 背景に半透明の黒幕を敷く（グラフィックの上に見やすく重ねるため）
  ofSetColor(0, 0, 0, 210);
  ofFill();
  ofDrawRectangle(0, 0, canvasWidth, canvasHeight);

  ofSetColor(255);

  if (realtimeMode) {
    // カメラ実写を表示
    const cv::Rect crop = sourceCrop(debugImg.getWidth(), debugImg.getHeight(),
                                      canvasWidth, canvasHeight,
                                      cropPositionX, cropPositionY);
    if (!crop.empty()) {
      // ofxCvImage は通常 draw() 時に CPU 画素をテクスチャへ転送する。
      // drawSubsection() で直接描く場合は明示的な更新が必要。
      debugImg.updateTexture();
      debugImg.getTexture().drawSubsection(
          0, 0, canvasWidth, canvasHeight,
          crop.x, crop.y, crop.width, crop.height);
      visibleCrop = crop;
      sourceTexture = &debugImg.getTexture();
      sourceWidth = debugImg.getWidth();
      sourceHeight = debugImg.getHeight();
    }

    // ★変更: contourFinderのブロブではなく、
    //   PersonSegmenterが出したhumanData(人物ごとの輪郭)をオーバーレイする
    ofNoFill();
    ofSetLineWidth(4);
    for (size_t i = 0; i < debugData.contours.size(); i++) {
      ofSetColor(0, 255, 0);
      debugData.contours[i].draw();

      if (i < debugData.boundingBoxes.size()) {
        ofSetColor(255, 255, 0);
        ofDrawRectangle(debugData.boundingBoxes[i]);
      }
    }
  } else {
    // 動画モード時は、読み込んでいる動画のプレビューを表示する
    if (videoProcessor.isLoaded()) {
      const cv::Rect crop = sourceCrop(
          videoProcessor.getVideoWidth(), videoProcessor.getVideoHeight(),
          canvasWidth, canvasHeight, cropPositionX, cropPositionY);
      if (!crop.empty()) {
        videoProcessor.getTexture().drawSubsection(
            0, 0, canvasWidth, canvasHeight,
            crop.x, crop.y, crop.width, crop.height);
        visibleCrop = crop;
        sourceTexture = &videoProcessor.getTexture();
        sourceWidth = videoProcessor.getVideoWidth();
        sourceHeight = videoProcessor.getVideoHeight();
      }

      ofNoFill();
      ofSetLineWidth(4);
      for (size_t i = 0; i < videoProcessor.humanData.contours.size(); i++) {
        ofSetColor(0, 255, 0);
        videoProcessor.humanData.contours[i].draw();

        if (i < videoProcessor.humanData.boundingBoxes.size()) {
          ofSetColor(255, 255, 0);
          ofDrawRectangle(videoProcessor.humanData.boundingBoxes[i]);
        }
      }
    }
  }

  if (sourceTexture) {
    drawSourceCropOverview(*sourceTexture, sourceWidth, sourceHeight,
                           visibleCrop, canvasWidth, canvasHeight);
  }

  // ステータスと操作ガイド
  string info = "=== [DEBUG MODE] Person Detection ===\n";
  info += "Model: YOLO11-seg " + debugSegmenter.backendDescription() + "\n";
  const float activeThreshold = debugSegmenter.yoloConfidenceThreshold;
  if (dualOutputActive()) {
    info += "Band: " + string(lowerBand ? "Lower (Camera 2)" : "Upper (Camera 1)") + "\n";
  }
  info += "Mode: " + string(realtimeMode ? "Realtime (Camera)" : "Video File") + "\n";
  if (realtimeMode) {
    info += "Camera initialized: " + string(debugCam.isInitialized() ? "Yes" : "No") + "\n";
    info += "Camera frame: " + ofToString(debugImg.getWidth()) + " x " +
            ofToString(debugImg.getHeight()) + "\n";
    info += "AI model loaded: " + string(debugSegmenter.isLoaded() ? "Yes" : "No") + "\n";
    info += "AI backend: " + debugSegmenter.backendDescription() + "\n";
    info += "Detected People: " + ofToString(debugData.numHumans) + "\n";
    info += "Threshold: " + ofToString(activeThreshold, 2) + "\n";
  } else {
    info += "Detected People: " + ofToString(videoProcessor.humanData.numHumans) + "\n";
    info += "Threshold: " + ofToString(activeThreshold, 2) + "\n";
    info += "Target FPS: " + ofToString(pMainWindowTargetFps.get()) + "\n";
    info += "Playing: " + string(videoProcessor.isStillImage()
                                     ? "Still Image"
                                     : videoProcessor.isVideoPlaying()
                                           ? "Yes" : "No (Paused)") + "\n";
  }
  info += "Scene Selection: '1' -> Scene 1\n";
  info += "Crop position: Arrow keys\n";
  info += "Fullscreen: 'F'\n";
  if (!visibleCrop.empty()) {
    info += "Visible source: (" + ofToString(visibleCrop.x) + ", " +
            ofToString(visibleCrop.y) + ") " +
            ofToString(visibleCrop.width) + " x " +
            ofToString(visibleCrop.height) + "\n";
    info += "Movable: X " +
            string(visibleCrop.width < sourceWidth ? "Yes" : "No") +
            " / Y " +
            string(visibleCrop.height < sourceHeight ? "Yes" : "No") + "\n";
  }
  info += "Press 'D' to CLOSE this debug overlay.";

  drawTextWithBackground(info, 20, 25);

  ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
  // 'D' または 'd' キーでデバッグ画面の表示/非表示を切り替え
  if (key == 'd' || key == 'D') {
    showDebug = !showDebug;
  }
  // 'A' または 'a' キーでControls Windowを前面表示／再生成する
  else if (key == 'a' || key == 'A') {
    showGuiWindow();
  }
  // 'F' または 'f' キーでMain Windowの全画面表示を切り替える
  else if (key == 'f' || key == 'F') {
    toggleMainWindowFullscreen();
  }

  else if (key == '4') {
    currentScene = humanGraphicsScene;
  }
  // ============================================
  // 全体設定のキー操作
  // （GUIのスライダーと同じofParameterを操作するので、
  //   キーで動かすとバー側の表示にも即座に反映される）
  // ============================================
  // 余白内の画角を5%ずつ移動する。切り抜きがない方向では動かさない。
  else if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT ||
           key == OF_KEY_UP || key == OF_KEY_DOWN) {
    const int sourceWidth = realtimeMode ? colorImg.getWidth()
                                         : videoProcessor.getVideoWidth();
    const int sourceHeight = realtimeMode ? colorImg.getHeight()
                                          : videoProcessor.getVideoHeight();
    const int canvasWidth = mainWindow ? mainCanvasWidth() : 0;
    const int canvasHeight = mainWindow ? mainCanvasHeight() : 0;
    const cv::Rect crop = sourceCrop(sourceWidth, sourceHeight,
                                      canvasWidth, canvasHeight,
                                      cropPositionX, cropPositionY);
    if (crop.empty()) return;

    constexpr float step = 0.05f;
    const float oldX = cropPositionX;
    const float oldY = cropPositionY;
    if (crop.width < sourceWidth && key == OF_KEY_LEFT) {
      cropPositionX = std::max(0.0f, cropPositionX - step);
    } else if (crop.width < sourceWidth && key == OF_KEY_RIGHT) {
      cropPositionX = std::min(1.0f, cropPositionX + step);
    } else if (crop.height < sourceHeight && key == OF_KEY_UP) {
      cropPositionY = std::max(0.0f, cropPositionY - step);
    } else if (crop.height < sourceHeight && key == OF_KEY_DOWN) {
      cropPositionY = std::min(1.0f, cropPositionY + step);
    }
    if (cropPositionX != oldX || cropPositionY != oldY) {
      if (realtimeMode) {
        finishRealtimeDetection(false);
        lastRealtimeProcessMs = 0;
      } else {
        videoProcessor.setOutputView(canvasWidth, canvasHeight,
                                     cropPositionX, cropPositionY);
      }
    }
  }
  // 頂点数 ('[' / ']', 4〜100)
  else if (key == '[') {
    pVertexCount = std::max(4, pVertexCount.get() - 1);
  } else if (key == ']') {
    pVertexCount = std::min(100, pVertexCount.get() + 1);
  }
}
void ofApp::drawGui(ofEventArgs & args) {
  // GUIウィンドウが表示状態の時だけ描画処理を行う
  if (guiVisible) {
    updateControlsMetrics();
    ofBackground(40); // GUIウィンドウの背景色（暗いグレー）
    gui.draw();
  }
}
