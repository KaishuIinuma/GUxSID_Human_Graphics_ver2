#include "ofApp.h"
#include "CameraAuthorization.h"
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
const std::string kPresetDirectoryName = "presets";

std::string settingsDirectoryPath() {
  return ofFilePath::join(ofFilePath::getUserHomeDir(), kSettingsDirectoryName);
}

std::string userPresetDirectoryPath() {
  return ofFilePath::join(settingsDirectoryPath(), kPresetDirectoryName);
}

std::string lowerCase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

bool normalizePresetFileName(const std::string &input,
                             std::string &fileName,
                             std::string &errorMessage) {
  fileName = ofTrim(input);
  if (fileName.empty()) {
    errorMessage = "Enter a preset name";
    return false;
  }
  if (fileName == "." || fileName == ".." ||
      fileName.find_first_of("/\\:") != std::string::npos) {
    errorMessage = "Preset name cannot contain /, \\ or :";
    return false;
  }

  const std::string jsonExtension = ".json";
  if (fileName.size() < jsonExtension.size() ||
      lowerCase(fileName.substr(fileName.size() - jsonExtension.size())) !=
          jsonExtension) {
    fileName += jsonExtension;
  }
  if (fileName.size() == jsonExtension.size()) {
    errorMessage = "Enter a name before .json";
    return false;
  }
  return true;
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
  if (controls->find("Classic_Mask_Threshold") == controls->end()) {
    (*controls)["Classic_Mask_Threshold"] = *legacy;
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

  // ============================================
  // ★変更: YOLO11-seg (person専用) ONNXモデルの読み込み
  //   Ultralyticsの `yolo export model=yolo11n-seg.pt format=onnx` 等で
  //   書き出したモデルを data/ 以下に配置しておくこと。
  // ============================================
  string modelPath = ofToDataPath("yolo11n-seg.onnx", true);
  bool modelOk = personSegmenter.loadModel(modelPath, 640);
  if (!modelOk) {
    ofLogError() << "YOLOモデルの読み込みに失敗しました。data/に"
                     "yolo11n-seg.onnx を配置しているか確認してください: " << modelPath;
  }
  personSegmenter.loadClassicModel(
      ofToDataPath("selfie_segmentation.onnx", true));

  // シーンの初期化（まずはscene1をデフォルトに設定）
  
  humanGraphicsScene = std::make_shared<HumanGraphicsScene>();
  humanGraphicsScene->setup();
  currentScene = humanGraphicsScene;

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
  pMainWindowTargetFps.set("Main Window Target FPS", 60, 1, 120);
  // ★追加: カメラ選択用パラメーターの初期化
  int maxCameraIndex = std::max(0, (int)cameraDevices.size() - 1);
  int defaultCameraIndex = getDefaultCameraIndex();
  pCameraIndex.set("Camera ID", defaultCameraIndex, 0, maxCameraIndex);
  
  string defaultCamName = cameraDevices.empty() ? "No Camera Found" : cameraDevices[defaultCameraIndex].deviceName;
  pCameraName.set("Camera Name", defaultCamName);

  discoverPresetFiles();
  pPresetIndex.set("Preset ID", static_cast<int>(presetPaths.size()), 0,
                   static_cast<int>(presetPaths.size()));
  pPresetName.set("Preset Name", "");
  pPresetStatus.set("Preset Status", "No preset selected");

  // ★追加: 左右反転トグル（デフォルトOFF）
  pFlipHorizontal.set("Flip Horizontal", false);

  pYoloConfidence.set("YOLO Confidence",
                      personSegmenter.yoloConfidenceThreshold, 0.0f, 1.0f);
  pClassicMaskThreshold.set("Classic Mask Threshold",
                            personSegmenter.classicMaskThreshold, 0.0f, 1.0f);
  pClassic.set("Classic", false);

  pVertexCount.set("Vertex Count", vertexCount, 4, 100);
  pLooseContour.set("Loose Contour", false);
  pLooseContourStrength.set("Loose Contour Strength", 7.0f, 0.0f, 30.0f);
  pAspectRatio.set("Aspect Ratio", 0.0f, -50.0f, 50.0f);
  pColorUpdateIntervalSec.set("Color Update Interval (s)", static_cast<float>(colorUpdateIntervalMs) / 1000.0f, 0.0f, 20.0f);
  pVideoPeopleCount.set("Video People", 1, 1, 20);
  pVideoSoloColor.set("Solo Color", 1, 1,
                      static_cast<int>(colorPaletteSize));
  pVideoColorLock.set("Video Color Lock", false);
 

  pGraphicsEnableBase.set("Graphics Base Enable", true);
  pGraphicsEnableOffset.set("Graphics Offset Enable", true);
  pGraphicsEnableStroke.set("Graphics Stroke Enable", true);
  pGraphicsOffsetSize.set("Graphics Offset Size", 200.0f, 0.0f, 600.0f);
  pGraphicsOffsetScale.set("Scale", 1.0f, 0.1f, 2.0f);
  pGraphicsOffsetRound.set("Graphics Offset Round Mode", true);
  pGraphicsStrokeWeight.set("Graphics Stroke Weight", 10.0f, 0.1f, 200.0f);
  pGraphicsStrokeRound.set("Graphics Stroke Round Mode", true);
  pGraphicsBaseMaterial.set("Graphics Base 0:ベタ 1:グラデ", 0, 0, 1);
  pGraphicsStrokeMaterial.set("Graphics Stroke 0:ベタ 1:グラデ", 0, 0, 1);
  pRenderRecipeId.set("Recipe ID", "standard_render");
  pMergeEventId.set("Event ID", "standard_event");
  pSceneLayoutId.set("Layout ID", "standard_layout");
  pSceneBehaviorId.set("Behavior ID", "standard_behavior");
  pExportAlpha.set("alpha", false);

  // ★追加: リスナー紐付け
  pGraphicsEnableBase.addListener(this, &ofApp::onGraphicsEnableBaseChanged);
  pGraphicsEnableOffset.addListener(this, &ofApp::onGraphicsEnableOffsetChanged);
  pGraphicsEnableStroke.addListener(this, &ofApp::onGraphicsEnableStrokeChanged);
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

  pVideoStatusText.set("Video Status", "VideoモードでOpen Videoまたはドラッグから動画を読み込めます");

  // 表示状態にかかわらず、すべての操作可能なパラメータを保存対象にする。
  setupGuiPersistence();
  loadGuiSettings();

  // 保存済みモードを先に確定し、Video起動時にカメラを開かないようにする。
  // ofParameterのリスナーはこの後で登録するため、ここでは明示的に反映する。
  realtimeMode = pRealtime.get();
  ofSetFrameRate(pMainWindowTargetFps.get());
  videoProcessor.processFps = static_cast<float>(pMainWindowTargetFps.get());
  personSegmenter.aspectRatioPercent = pAspectRatio.get();
  personSegmenter.setClassic(pClassic.get());

  pCameraIndex.addListener(this, &ofApp::onCameraIndexChanged);
  pRealtime.addListener(this, &ofApp::onRealtimeChanged);
  pFlipHorizontal.addListener(this, &ofApp::onFlipHorizontalChanged);
  pYoloConfidence.addListener(this, &ofApp::onYoloConfidenceChanged);
  pClassicMaskThreshold.addListener(this, &ofApp::onClassicMaskThresholdChanged);
  pClassic.addListener(this, &ofApp::onClassicChanged);
  pVertexCount.addListener(this, &ofApp::onVertexCountChanged);
  pLooseContour.addListener(this, &ofApp::onLooseContourChanged);
  pLooseContourStrength.addListener(
      this, &ofApp::onLooseContourStrengthChanged);
  pAspectRatio.addListener(this, &ofApp::onAspectRatioChanged);
  pColorUpdateIntervalSec.addListener(this, &ofApp::onColorUpdateIntervalChanged);
  pVideoPeopleCount.addListener(this, &ofApp::onVideoPeopleCountChanged);
  pVideoSoloColor.addListener(this, &ofApp::onVideoSoloColorChanged);
  pVideoColorLock.addListener(this, &ofApp::onVideoColorLockChanged);

  pMainWindowTargetFps.addListener(
      this, &ofApp::onMainWindowTargetFpsChanged);
  pExportAlpha.addListener(this, &ofApp::onExportAlphaChanged);
  pPresetIndex.addListener(this, &ofApp::onPresetIndexChanged);

  playButton.setup("Play");
  pauseButton.setup("Pause");
  restartButton.setup("Restart from beginning");
  openVideoButton.setup("Open Video...");
  exportImageSequenceButton.setup("Export Image Sequence");
  savePresetButton.setup("Preset-Save");
  revertPresetButton.setup("Preset-Revert");
  playButton.addListener(this, &ofApp::onPlayPressed);
  pauseButton.addListener(this, &ofApp::onPausePressed);
  restartButton.addListener(this, &ofApp::onRestartPressed);
  openVideoButton.addListener(this, &ofApp::onOpenVideoPressed);
  exportImageSequenceButton.addListener(this, &ofApp::onExportImageSequencePressed);
  savePresetButton.addListener(this, &ofApp::onSavePresetPressed);
  revertPresetButton.addListener(this, &ofApp::onRevertPresetPressed);

  // 保存済みの値を、GUI以外の実行状態にも反映する。
  int savedCameraIndex = pCameraIndex.get();
  onCameraIndexChanged(savedCameraIndex);
  personSegmenter.yoloConfidenceThreshold = pYoloConfidence.get();
  personSegmenter.classicMaskThreshold = pClassicMaskThreshold.get();
  vertexCount = pVertexCount.get();
  colorUpdateIntervalMs = static_cast<uint64_t>(pColorUpdateIntervalSec.get() * 1000.0f);
  if (humanGraphicsScene) {
    humanGraphicsScene->enableBase = pGraphicsEnableBase.get();
    humanGraphicsScene->enableOffset = pGraphicsEnableOffset.get();
    humanGraphicsScene->enableStroke = pGraphicsEnableStroke.get();
    humanGraphicsScene->enableLooseContour = pLooseContour.get();
    humanGraphicsScene->looseContourStrength = pLooseContourStrength.get();
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
    humanGraphicsScene->setRenderRecipe(pRenderRecipeId.get());
    humanGraphicsScene->setMergeEvent(pMergeEventId.get());
    humanGraphicsScene->setSceneLayout(pSceneLayoutId.get());
    humanGraphicsScene->setSceneBehavior(pSceneBehaviorId.get());
  }
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
  guiParams.add(pFlipHorizontal);
  guiParams.add(pClassic);
  guiParams.add(pYoloConfidence);
  guiParams.add(pClassicMaskThreshold);
  guiParams.add(pVertexCount);
  guiParams.add(pGraphicsOffsetScale);
  guiParams.add(pAspectRatio);
  guiParams.add(pLooseContour);
  guiParams.add(pLooseContourStrength);
  guiParams.add(pColorUpdateIntervalSec);
  guiParams.add(pVideoPeopleCount);
  guiParams.add(pVideoSoloColor);
  guiParams.add(pVideoColorLock);
  guiParams.add(pGraphicsEnableBase);
  guiParams.add(pGraphicsEnableOffset);
  guiParams.add(pGraphicsEnableStroke);
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
  guiParams.add(pMainWindowTargetFps);
  guiParams.add(pExportAlpha);

  // プリセットは見た目・検出設定だけを変更し、現在の入力モード
  // （RealtimeかVideoか）は変更しない。root名はcontrols.jsonと同じにして、
  // 同じJSON形式のうち該当する項目だけを読み込めるようにする。
  presetParams.clear();
  presetParams.setName("ControlsSettings");
  presetParams.add(pCameraIndex);
  presetParams.add(pFlipHorizontal);
  presetParams.add(pClassic);
  presetParams.add(pYoloConfidence);
  presetParams.add(pClassicMaskThreshold);
  presetParams.add(pVertexCount);
  presetParams.add(pGraphicsOffsetScale);
  presetParams.add(pAspectRatio);
  presetParams.add(pLooseContour);
  presetParams.add(pLooseContourStrength);
  presetParams.add(pColorUpdateIntervalSec);
  presetParams.add(pVideoPeopleCount);
  presetParams.add(pVideoSoloColor);
  presetParams.add(pVideoColorLock);
  presetParams.add(pGraphicsEnableBase);
  presetParams.add(pGraphicsEnableOffset);
  presetParams.add(pGraphicsEnableStroke);
  presetParams.add(pGraphicsOffsetSize);
  presetParams.add(pGraphicsOffsetRound);
  presetParams.add(pGraphicsStrokeWeight);
  presetParams.add(pGraphicsStrokeRound);
  presetParams.add(pGraphicsBaseMaterial);
  presetParams.add(pGraphicsStrokeMaterial);
  presetParams.add(pRenderRecipeId);
  presetParams.add(pMergeEventId);
  presetParams.add(pSceneLayoutId);
  presetParams.add(pSceneBehaviorId);

  // 読み込んだプリセットとの差分だけを監視する。Preset Nameや入力モードは
  // presetParamsに含まれないため、保存名の編集中やRealtime切り替えでは
  // Modified扱いにならない。
  presetParameterChangedListener =
      presetParams.parameterChangedE().newListener(
          [this](ofAbstractParameter &) { updatePresetDirtyState(); });
}

//--------------------------------------------------------------
void ofApp::loadGuiSettings() {
  const auto settingsPath = ofFilePath::join(settingsDirectoryPath(), kControlsFileName);
  if (!ofFile(settingsPath, ofFile::Reference).exists()) return;

  ofJson settings = ofLoadJson(settingsPath);
  if (settings.is_object()) {
    migrateLegacyScaleKey(settings);
    migrateLegacyPersonConfidenceKey(settings);
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
  const auto settingsPath = ofFilePath::join(settingsDirectory, kControlsFileName);
  if (!ofSavePrettyJson(settingsPath, settings)) {
    ofLogError("ofApp") << "GUI設定を保存できませんでした: " << settingsPath;
  }
}

//--------------------------------------------------------------
void ofApp::exit() {
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
void ofApp::discoverPresetFiles() {
  presetPaths.clear();

  // 同名の場合は後から走査するユーザー保存版を優先する。
  // 配布.app内のプリセットは読み取り専用として扱い、GUIから作成したものは
  // Application Support/presets に保存してアプリ更新後も残す。
  const vector<string> directories = {
      ofToDataPath("presets", true),
      ofToDataPath("", true),
      userPresetDirectoryPath(),
  };
  std::map<std::string, std::string> pathsByLowerFileName;
  for (const auto &directoryPath : directories) {
    ofDirectory directory(directoryPath);
    if (!directory.exists()) continue;
    directory.allowExt("json");
    directory.listDir();
    for (const auto &file : directory.getFiles()) {
      pathsByLowerFileName[lowerCase(file.getFileName())] =
          file.getAbsolutePath();
    }
  }
  for (const auto &entry : pathsByLowerFileName) {
    presetPaths.push_back(entry.second);
  }
  std::sort(presetPaths.begin(), presetPaths.end());
}

//--------------------------------------------------------------
int ofApp::findPresetIndexByFileName(const string &fileName) const {
  const string targetName = lowerCase(fileName);
  for (size_t index = 0; index < presetPaths.size(); ++index) {
    if (lowerCase(ofFilePath::getFileName(presetPaths[index])) == targetName) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

//--------------------------------------------------------------
void ofApp::updatePresetDirtyState() {
  if (isLoadingGuiSettings || !hasLoadedPresetSnapshot) return;

  ofJson currentPreset;
  ofSerialize(currentPreset, presetParams);
  const bool isDirtyNow = currentPreset != loadedPresetSnapshot;
  if (isDirtyNow == presetIsDirty) return;

  presetIsDirty = isDirtyNow;
  pPresetStatus = presetIsDirty ? "Modified *" : "Preset loaded";
}

//--------------------------------------------------------------
void ofApp::captureLoadedPresetSnapshot() {
  loadedPresetSnapshot = ofJson();
  ofSerialize(loadedPresetSnapshot, presetParams);
  hasLoadedPresetSnapshot = true;
  presetIsDirty = false;
}

//--------------------------------------------------------------
void ofApp::clearLoadedPresetSnapshot() {
  loadedPresetSnapshot = ofJson();
  hasLoadedPresetSnapshot = false;
  presetIsDirty = false;
}

//--------------------------------------------------------------
void ofApp::onRevertPresetPressed() {
  if (!hasLoadedPresetSnapshot || loadedPresetFileName.empty()) {
    pPresetStatus = "No preset to revert";
    return;
  }

  try {
    isLoadingGuiSettings = true;
    ofDeserialize(loadedPresetSnapshot, presetParams);
    isLoadingGuiSettings = false;
    applyVideoColorMode();
    rebuildGuiPanel();
    presetIsDirty = false;
    pPresetName = loadedPresetFileName;
    pPresetStatus = "Preset reverted";
    saveGuiSettings();
    ofLogNotice("ofApp") << "プリセット読込時の値へ戻しました: "
                          << loadedPresetFileName;
  } catch (const std::exception &error) {
    isLoadingGuiSettings = false;
    pPresetStatus = "Preset revert failed";
    ofLogError("ofApp") << "プリセットを元に戻せませんでした: "
                         << loadedPresetFileName << " (" << error.what()
                         << ")";
  }
}

//--------------------------------------------------------------
void ofApp::onSavePresetPressed() {
  string fileName;
  string validationError;
  if (!normalizePresetFileName(pPresetName.get(), fileName, validationError)) {
    pPresetStatus = validationError;
    ofLogWarning("ofApp") << "プリセットを保存できません: " << validationError;
    return;
  }

  const int existingIndex = findPresetIndexByFileName(fileName);
  const bool overwritingLoadedPreset =
      !loadedPresetFileName.empty() &&
      lowerCase(fileName) == lowerCase(loadedPresetFileName);
  if (existingIndex >= 0 && !overwritingLoadedPreset) {
    pPresetStatus = "Name already exists - load it before overwrite";
    ofLogWarning("ofApp")
        << "同名プリセットが既にあります。上書きするには先に読み込んでください: "
        << fileName;
    return;
  }

  const string presetDirectory = userPresetDirectoryPath();
  if (!ofDirectory::createDirectory(presetDirectory, false, true)) {
    pPresetStatus = "Could not create preset folder";
    ofLogError("ofApp") << "プリセット保存先を作成できませんでした: "
                         << presetDirectory;
    return;
  }

  ofJson preset;
  ofSerialize(preset, presetParams);
  const string presetPath = ofFilePath::join(presetDirectory, fileName);
  if (!ofSavePrettyJson(presetPath, preset)) {
    pPresetStatus = "Preset save failed";
    ofLogError("ofApp") << "プリセットを保存できませんでした: " << presetPath;
    return;
  }

  loadedPresetFileName = fileName;
  pPresetName = fileName;
  discoverPresetFiles();
  pPresetIndex.setMax(static_cast<int>(presetPaths.size()));
  const int savedIndex = findPresetIndexByFileName(fileName);
  if (savedIndex >= 0) {
    pPresetIndex.setWithoutEventNotifications(savedIndex);
  }
  captureLoadedPresetSnapshot();
  pPresetStatus = overwritingLoadedPreset ? "Preset overwritten" : "Preset created";
  rebuildGuiPanel();
  ofLogNotice("ofApp")
      << (overwritingLoadedPreset ? "プリセットを上書きしました: "
                                  : "プリセットを新規作成しました: ")
      << presetPath;
}

//--------------------------------------------------------------
void ofApp::onPresetIndexChanged(int &index) {
  if (index < 0 || static_cast<size_t>(index) >= presetPaths.size()) {
    pPresetName = "";
    pPresetStatus = "No preset selected";
    loadedPresetFileName.clear();
    clearLoadedPresetSnapshot();
    return;
  }

  const string presetPath = presetPaths[static_cast<size_t>(index)];
  try {
    ofJson preset = ofLoadJson(presetPath);
    if (!preset.is_object()) {
      loadedPresetFileName.clear();
      clearLoadedPresetSnapshot();
      pPresetStatus = "Preset load failed";
      ofLogWarning("ofApp") << "プリセットはJSONオブジェクトである必要があります: " << presetPath;
      return;
    }

    migrateLegacyScaleKey(preset);
    migrateLegacyPersonConfidenceKey(preset);
    isLoadingGuiSettings = true;
    ofDeserialize(preset, presetParams);
    isLoadingGuiSettings = false;
    applyVideoColorMode();
    loadedPresetFileName = ofFilePath::getFileName(presetPath);
    pPresetName = loadedPresetFileName;
    captureLoadedPresetSnapshot();
    pPresetStatus = "Preset loaded";
    rebuildGuiPanel();
    saveGuiSettings();
    ofLogNotice("ofApp") << "プリセットを読み込みました: " << pPresetName.get();
  } catch (const std::exception &error) {
    isLoadingGuiSettings = false;
    loadedPresetFileName.clear();
    clearLoadedPresetSnapshot();
    pPresetStatus = "Preset load failed";
    ofLogError("ofApp") << "プリセットを読み込めませんでした: " << presetPath
                         << " (" << error.what() << ")";
  }
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
  gui.add(pRealtime);
  if (!realtimeMode) gui.add(&openVideoButton);
  gui.add(pPresetIndex);
  gui.add(pPresetName);
  gui.add(&savePresetButton);
  gui.add(&revertPresetButton);
  gui.add(pPresetStatus);

  // ★追加: Realtime(カメラ)モードの時だけカメラ選択UI・左右反転トグルを表示
  if (realtimeMode) {
      gui.add(pCameraIndex);
      gui.add(pCameraName);
      gui.add(pFlipHorizontal);
  }


  gui.add(pClassic);
  if (pClassic.get()) {
    gui.add<float>(pClassicMaskThreshold);
  } else {
    gui.add<float>(pYoloConfidence);
  }
  gui.add(pVertexCount);
  gui.add(pGraphicsOffsetScale);
  gui.add(pAspectRatio);
  gui.add(pLooseContour);
  gui.add(pLooseContourStrength);
  gui.add<float>(pColorUpdateIntervalSec);
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
  gui.add<float>(pGraphicsOffsetSize);
  gui.add(pGraphicsOffsetRound);
  gui.add<float>(pGraphicsStrokeWeight);
  gui.add(pGraphicsStrokeRound);

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
    pMainWindowFps = ofToString(ofGetFrameRate(), 1) + " / " +
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
  ofSetFrameRate(value);
  videoProcessor.processFps = static_cast<float>(value);
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onExportAlphaChanged(bool &value) {
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onYoloConfidenceChanged(float &value) {
  personSegmenter.yoloConfidenceThreshold = value;
  if (!personSegmenter.isClassic()) {
    videoProcessor.requestReprocess();
    lastRealtimeProcessMs = 0;
  }
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onClassicMaskThresholdChanged(float &value) {
  personSegmenter.classicMaskThreshold = value;
  if (personSegmenter.isClassic()) {
    videoProcessor.requestReprocess();
    lastRealtimeProcessMs = 0;
  }
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onClassicChanged(bool &value) {
  personSegmenter.setClassic(value);
  humanData = HumanContourData();
  videoProcessor.humanData = HumanContourData();
  videoProcessor.requestReprocess();
  lastRealtimeProcessMs = 0;
  if (!personSegmenter.isLoaded()) {
    ofLogError("ofApp") << (value ? "Classic" : "YOLO")
                        << " model is unavailable";
  }
  saveGuiSettings();
  if (!isLoadingGuiSettings) rebuildGuiPanel();
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
void ofApp::onAspectRatioChanged(float &value) {
  personSegmenter.aspectRatioPercent = value;
  // Videoが一時停止中でも、次のupdateで保持中の元フレームから検出し直す。
  videoProcessor.requestReprocess();
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onColorUpdateIntervalChanged(float &value) {
  colorUpdateIntervalMs = static_cast<uint64_t>(value * 1000.0f);
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoPeopleCountChanged(int &) {
  if (isLoadingGuiSettings) return;
  selectVideoPaletteColors();
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
void ofApp::onVideoColorLockChanged(bool &) {
  if (isLoadingGuiSettings) return;
  applyVideoColorMode();
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::selectVideoPaletteColors() {
  videoPaletteIndices.clear();
  if (!videoProcessor.isLoaded()) return;

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
void ofApp::onGraphicsOffsetSizeChanged(float &value) {
    if (humanGraphicsScene) humanGraphicsScene->offsetSize = value;
    saveGuiSettings();
}
void ofApp::onGraphicsStrokeWeightChanged(float &value) {
    if (humanGraphicsScene) humanGraphicsScene->strokeWeight = value;
    saveGuiSettings();
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
        value == gux::StandardRenderRecipe::RecipeId ||
        value == gux::MixRenderRecipe::RecipeId ||
        value == gux::FloatingBridgeRenderRecipe::RecipeId;
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
        }
    }
    saveGuiSettings();
}


//--------------------------------------------------------------
//--------------------------------------------------------------
void ofApp::onRealtimeChanged(bool &value) {
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
    cam.close();
    ofLogNotice("ofApp") << "Videoモードへ移行したためカメラを閉じました。";
  }

  rebuildGuiPanel();

  // ★追加: Realtimeモード（カメラ入力）に戻った時、
  // ウィンドウサイズをカメラの解像度 (W, H) に戻す
  if (realtimeMode && mainWindow && mainWindow->getGLFWWindow()) {
    GLFWwindow *handle = mainWindow->getGLFWWindow();
    // Videoモードの古い制約を解除してからサイズを戻す。
    glfwSetWindowAspectRatio(handle, GLFW_DONT_CARE, GLFW_DONT_CARE);
    // setWindowShape() を通し、Retinaの論理サイズと物理ピクセルの
    // 変換をopenFrameworks側に任せる。
    mainWindow->setWindowShape(W, H);
    if (lockMainWindowAspectRatio) {
      glfwSetWindowAspectRatio(handle, W, H);
    }
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
  ofFileDialogResult selection = ofSystemLoadDialog("Open Video");
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
    pVideoStatusText = "Export requires a loaded video in Video mode";
    return;
  }
  if (!mainWindow || !mainWindow->getGLFWWindow()) {
    pVideoStatusText = "Export requires an open Main Window";
    return;
  }

  // Mainの描画座標をここで固定する。Controls側から押されても
  // ofGetWidth()/ofGetHeight() はControlsのサイズを返す可能性がある。
  exportCanvasWidth = mainWindow->getWidth();
  exportCanvasHeight = mainWindow->getHeight();
  if (exportCanvasWidth <= 0 || exportCanvasHeight <= 0) {
    pVideoStatusText = "Export failed: invalid Main Window size";
    return;
  }

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
  exportFrameRate = static_cast<double>(pMainWindowTargetFps.get());
  if (!std::isfinite(exportSourceFrameRate) || exportSourceFrameRate <= 0.0) {
    exportSourceFrameRate = 30.0;
  }
  if (!std::isfinite(exportFrameRate) || exportFrameRate <= 0.0) {
    exportFrameRate = 30.0;
  }
  exportTotalFrames = sourceDurationSeconds > 0.0
      ? std::max(1, static_cast<int>(std::llround(
            sourceDurationSeconds * exportFrameRate)))
      : exportSourceTotalFrames;
  if (exportSourceWidth <= 0 || exportSourceHeight <= 0 ||
      exportSourceTotalFrames <= 0 || exportTotalFrames <= 0) {
    pVideoStatusText = "Export failed: source video has no frames";
    exportVideoPlayer.close();
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
    exportVideoPlayer.close();
    return;
  }
  exportWidth = static_cast<int>(evenWidth);
  exportOffsetX = 0.5f *
      (exportWidth - exportCanvasWidth * exportScale);

  exportFbo.allocate(exportWidth, exportHeight, GL_RGBA);
  if (!exportFbo.isAllocated()) {
    pVideoStatusText = "Export failed: could not allocate 3000px canvas";
    exportVideoPlayer.close();
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
      !ofDirectory::createDirectory(exportMovieDirectory, false, true)) {
    pVideoStatusText = "Export failed: could not create output folder";
    exportVideoPlayer.close();
    return;
  }

  exportAlpha = pExportAlpha.get();

  // Mainの描画用Sceneとは別インスタンスを使うため、書き出し中も
  // Main Windowのループ再生と色・輪郭の状態を維持できる。
  exportScene = std::make_shared<HumanGraphicsScene>(*humanGraphicsScene);
  exportScene->setCanvasSize(exportCanvasWidth, exportCanvasHeight);
  // 浮遊Behaviorは時系列状態を持つため、Main側とは独立した状態で書き出す。
  exportScene->setSceneBehavior(
      std::string(humanGraphicsScene->sceneBehaviorId()));

  exportVideoPlayer.setLoopState(OF_LOOP_NONE);
  // AVFoundationでは停止中のfirstFrame()が新規フレームを通知しない場合が
  // あるため、最初の1フレームだけ再生状態で確実にデコードする。
  exportVideoPlayer.play();
  exportVideoPlayer.firstFrame();
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
  if (exportDecodedSourceFrame != desiredSourceFrame &&
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

    const ofPixels& framePixels = exportVideoPlayer.getPixels();
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
    exportDecodedData = personSegmenter.detect(
        rgbMat, exportCanvasWidth, exportCanvasHeight);
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
  exportScene->draw(false);
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
    cancelImageSequenceExport();
    startMovieExport();
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
// メインウィンドウの大きさ・縦横比を、読み込んだ動画に合わせる
//--------------------------------------------------------------
void ofApp::resizeMainWindowToVideo() {
  if (!mainWindow) return;

  if (!mainWindow->getGLFWWindow()) return;

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
  // 動画モード中はユーザーがサイズを変えても、
  // 読み込み済み動画の縦横比を維持する。
  if (lockMainWindowAspectRatio) {
    glfwSetWindowAspectRatio(handle, w, h);
  }
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
  // Main Windowが閉じられていてもControls Windowは残るため、
  // Controls側でAキーを受けてMain Windowを復帰できるようにする。
  if (args.key == 'a' || args.key == 'A') {
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
    selectVideoPaletteColors();
    applyVideoColorMode();
    // 動画の解像度・縦横比にメインウィンドウを合わせる
    resizeMainWindowToVideo();

    pVideoStatusText = "再生中: " + path +
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

  GLFWwindow *handle = mainWindow->getGLFWWindow();
  if (handle) {
    if (!realtimeMode && videoProcessor.isLoaded() &&
        lockMainWindowAspectRatio) {
      glfwSetWindowAspectRatio(handle, videoProcessor.getVideoWidth(),
                               videoProcessor.getVideoHeight());
    } else if (realtimeMode && lockMainWindowAspectRatio) {
      glfwSetWindowAspectRatio(handle, W, H);
    }
  }

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
    if (index >= 0 && index < cameraDevices.size()) {
        // カメラの名前表示を更新
        pCameraName = cameraDevices[index].deviceName;

        // Videoモードではカメラデバイスを開かない。
        if (!realtimeMode) {
            saveGuiSettings();
            return;
        }

        // 既存のカメラを閉じて、新しいIDで開き直す
        cam.close();
        cam.setDeviceID(index);
        cam.setup(W, H); // W, H は ofApp.h で定義されている解像度
        
        ofLogNotice() << "Switched to Camera " << index << ": " << cameraDevices[index].deviceName;
        saveGuiSettings();
    }
}

//--------------------------------------------------------------
void ofApp::onFlipHorizontalChanged(bool &value) {
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::update() {
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
    // Realtime=true: 従来通りWebカメラ映像を処理する
    // ============================================
    cam.update();

    const float fps = static_cast<float>(pMainWindowTargetFps.get());
    const uint64_t intervalMs = static_cast<uint64_t>(1000.0f / fps);
    const uint64_t now = ofGetElapsedTimeMillis();

    if (cam.isFrameNew()) {
      const ofPixels &cameraPixels = cam.getPixels();
      const int frameWidth = cameraPixels.getWidth();
      const int frameHeight = cameraPixels.getHeight();
      if (frameWidth <= 0 || frameHeight <= 0) {
        return;
      }

      // カメラプレビューはAIモデルの読込成否に関係なく常に更新する。
      colorImg.setFromPixels(cameraPixels);

      // 左右反転はプレビューとAI推論の両方へ反映する。
      if (pFlipHorizontal) {
        colorImg.mirror(false, true);
      }

      // AIモデルが利用可能な場合だけ、設定した間隔で人物検出を行う。
      if (personSegmenter.isLoaded() &&
          (now - lastRealtimeProcessMs) >= intervalMs) {
        // カメラは要求解像度とは異なるサイズを返すことがあるため、
        // 実際に届いたフレームサイズでMatを作成する。
        cv::Mat rgbMat(frameHeight, frameWidth, CV_8UC3,
                       colorImg.getPixels().getData());

        humanData =
            personSegmenter.detect(rgbMat, ofGetWidth(), ofGetHeight());
        lastRealtimeProcessMs = now;
      }
    }
  } else {
    // ============================================
    // Realtime=false: ドロップされた動画ファイルを処理する
    // (処理の実体はVideoProcessingクラスに分離している)
    // ============================================
    videoProcessor.update();
  }

  // 現在のアクティブなシーンへ座標データを渡す
  // (Realtimeの状態に応じて、渡すデータの出所だけを切り替える)
  if (currentScene) {
    const HumanContourData &activeData = realtimeMode ? humanData : videoProcessor.humanData;
    currentScene->update(activeData);
  }
}

//--------------------------------------------------------------
void ofApp::draw() {
  // Exportボタンを押した後はMain Windowの通常描画を停止する。
  if (isExportingImageSequence) return;

  // 現在のシーンのグラフィックを描画
  // (Realtimeでもオフラインでも、Scene側は同じ描画ロジックのまま)
  if (currentScene) {
    currentScene->draw();
  }

  // Dキーが押されている時のみ、デバッグ用HUDを表示
  if (showDebug) {
    drawDebug();
  }
}

//--------------------------------------------------------------
void ofApp::drawDebug() {
  ofPushStyle();

  // 背景に半透明の黒幕を敷く（グラフィックの上に見やすく重ねるため）
  ofSetColor(0, 0, 0, 210);
  ofFill();
  ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

  ofSetColor(255);

  if (realtimeMode) {
    // カメラ実写を表示
    colorImg.draw(0, 0, W, H);

    // ★変更: contourFinderのブロブではなく、
    //   PersonSegmenterが出したhumanData(人物ごとの輪郭)をオーバーレイする
    ofNoFill();
    ofSetLineWidth(4);
    for (size_t i = 0; i < humanData.contours.size(); i++) {
      ofSetColor(0, 255, 0);
      humanData.contours[i].draw();

      if (i < humanData.boundingBoxes.size()) {
        ofSetColor(255, 255, 0);
        ofDrawRectangle(humanData.boundingBoxes[i]);
      }
    }
  } else {
    // 動画モード時は、読み込んでいる動画のプレビューを表示する
    if (videoProcessor.isLoaded()) {
      videoProcessor.videoPlayer.draw(0, 0, videoProcessor.getVideoWidth(), videoProcessor.getVideoHeight());

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

  // ステータスと操作ガイド
  string info = "=== [DEBUG MODE] Person Detection ===\n";
  info += "Model: " + string(pClassic.get() ? "Classic Selfie" : "YOLO11-seg") + "\n";
  const float activeThreshold = pClassic.get()
      ? personSegmenter.classicMaskThreshold
      : personSegmenter.yoloConfidenceThreshold;
  info += "Mode: " + string(realtimeMode ? "Realtime (Camera)" : "Video File") + "\n";
  if (realtimeMode) {
    info += "Camera initialized: " + string(cam.isInitialized() ? "Yes" : "No") + "\n";
    info += "Camera frame: " + ofToString(colorImg.getWidth()) + " x " +
            ofToString(colorImg.getHeight()) + "\n";
    info += "AI model loaded: " + string(personSegmenter.isLoaded() ? "Yes" : "No") + "\n";
    info += "Detected People: " + ofToString(humanData.numHumans) + "\n";
    info += "Threshold: " + ofToString(activeThreshold, 2) + " [UP/DOWN: Adjust]\n";
  } else {
    info += "Detected People: " + ofToString(videoProcessor.humanData.numHumans) + "\n";
    info += "Threshold: " + ofToString(activeThreshold, 2) + " [UP/DOWN: Adjust]\n";
    info += "Target FPS: " + ofToString(pMainWindowTargetFps.get()) + "\n";
    info += "Playing: " + string(videoProcessor.isVideoPlaying() ? "Yes" : "No (Paused)") + "\n";
  }
  info += "Scene Selection: '1' -> Scene 1\n";
  info += "Press 'D' to CLOSE this debug overlay.";

  ofDrawBitmapStringHighlight(info, 20, H + 25);

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

  else if (key == '4') {
    currentScene = humanGraphicsScene;
  }
  // ============================================
  // 全体設定のキー操作
  // （GUIのスライダーと同じofParameterを操作するので、
  //   キーで動かすとバー側の表示にも即座に反映される）
  // ============================================
  // 現在の検出モードの閾値 (UP/DOWN, 0.0〜1.0)
  else if (key == OF_KEY_UP) {
    if (pClassic.get()) {
      pClassicMaskThreshold = std::min(1.0f, pClassicMaskThreshold.get() + 0.05f);
    } else {
      pYoloConfidence = std::min(1.0f, pYoloConfidence.get() + 0.05f);
    }
  } else if (key == OF_KEY_DOWN) {
    if (pClassic.get()) {
      pClassicMaskThreshold = std::max(0.0f, pClassicMaskThreshold.get() - 0.05f);
    } else {
      pYoloConfidence = std::max(0.0f, pYoloConfidence.get() - 0.05f);
    }
  }
  // 頂点数 ('[' / ']', 4〜100)
  else if (key == '[') {
    pVertexCount = std::max(4, pVertexCount.get() - 1);
  } else if (key == ']') {
    pVertexCount = std::min(100, pVertexCount.get() + 1);
  }
  // 色の更新頻度 ('-' / '=', 0〜20秒)
  else if (key == '-' || key == '_') {
    pColorUpdateIntervalSec = std::max(0.0f, pColorUpdateIntervalSec.get() - 0.5f);
  } else if (key == '=' || key == '+') {
    pColorUpdateIntervalSec = std::min(20.0f, pColorUpdateIntervalSec.get() + 0.5f);
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
