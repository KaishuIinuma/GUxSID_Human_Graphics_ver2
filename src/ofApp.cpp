#include "ofApp.h"
#include <GLFW/glfw3.h> // guiWindow/mainWindowの表示切り替え・リサイズ用
#include <algorithm>

namespace {
const std::string kSettingsDirectoryName =
    "Library/Application Support/GUxSID_Human_Graphics_ver2";
const std::string kControlsFileName = "controls.json";
const std::string kRunningMarkerFileName = "running.marker";

std::string settingsDirectoryPath() {
  return ofFilePath::join(ofFilePath::getUserHomeDir(), kSettingsDirectoryName);
}
} // namespace

//--------------------------------------------------------------
void ofApp::setup() {
  ofSetFrameRate(60);

  // 起動直後にマーカーを作成する。初期化途中のクラッシュも次回起動で検知する。
  beginRunSession();

  // ============================================
  // ★追加/修正: カメラデバイスの取得と初期化
  // ============================================
  cameraDevices = cam.listDevices();
  for(size_t i = 0; i < cameraDevices.size(); i++) {
      ofLogNotice() << "Camera " << i << ": " << cameraDevices[i].deviceName;
  }
  
  // 通常起動時も、復旧後も内蔵カメラを優先する。
  if (!cameraDevices.empty()) {
      cam.setDeviceID(getDefaultCameraIndex());
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
  ofxBaseGui::loadFont("mono.ttf", 18, true, true, 72);
  ofxBaseGui::setDefaultWidth(controlWindowWidth - 40);
  ofxBaseGui::setDefaultHeight(30);
  ofxBaseGui::setDefaultTextPadding(8);

  pRealtime.set("Realtime", true);
  // ★追加: カメラ選択用パラメーターの初期化
  int maxCameraIndex = std::max(0, (int)cameraDevices.size() - 1);
  int defaultCameraIndex = getDefaultCameraIndex();
  pCameraIndex.set("Camera ID", defaultCameraIndex, 0, maxCameraIndex);
  
  string defaultCamName = cameraDevices.empty() ? "No Camera Found" : cameraDevices[defaultCameraIndex].deviceName;
  pCameraName.set("Camera Name", defaultCamName);

  discoverPresetFiles();
  pPresetIndex.set("Preset ID", static_cast<int>(presetPaths.size()), 0,
                   static_cast<int>(presetPaths.size()));
  pPresetName.set("Preset Name", "No preset selected");

  // ★追加: 左右反転トグル（デフォルトOFF）
  pFlipHorizontal.set("Flip Horizontal", false);

  pContourThreshold.set("Person Confidence", personSegmenter.confThreshold, 0.0f, 1.0f);

  pVertexCount.set("Vertex Count", vertexCount, 4, 100);
  pColorUpdateIntervalSec.set("Color Update Interval (s)", static_cast<float>(colorUpdateIntervalMs) / 1000.0f, 0.0f, 20.0f);
 

  pGraphicsEnableBase.set("Graphics Base Enable", true);
  pGraphicsEnableOffset.set("Graphics Offset Enable", true);
  pGraphicsEnableStroke.set("Graphics Stroke Enable", true);
  pGraphicsOffsetSize.set("Graphics Offset Size", 200.0f, 0.0f, 600.0f);
  pGraphicsOffsetScale.set("Graphics Offset Scale", 1.0f, 0.1f, 2.0f); // ★追加: 0.1(10%)〜2.0(200%)で調整
  pGraphicsOffsetRound.set("Graphics Offset Round Mode", true);
  pGraphicsStrokeWeight.set("Graphics Stroke Weight", 10.0f, 0.1f, 200.0f);
  pGraphicsStrokeRound.set("Graphics Stroke Round Mode", true);
  pGraphicsBaseMaterial.set("Graphics Base 0:ベタ 1:グラデ", 0, 0, 1);
  pGraphicsStrokeMaterial.set("Graphics Stroke 0:ベタ 1:グラデ", 0, 0, 1);
  pRenderRecipeId.set("Recipe ID", "standard_render");
  pMergeEventId.set("Event ID", "standard_event");
  pSceneLayoutId.set("Layout ID", "standard_layout");
  pSceneBehaviorId.set("Behavior ID", "standard_behavior");

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


  pRealtimeFps.set("Realtime FPS", 30.0f, 0.5f, 60.0f);
  pVideoFps.set("Video FPS", 30, 1, 60);
  pVideoStatusText.set("Video Status", "Realtimeをオフにすると、このウィンドウに動画ファイルをドロップできます");
  pCrashStatusText.set(
      "Recovery Status",
      "WARNING: Previous run crashed. Parameters were reset to defaults.");

  // 表示状態にかかわらず、すべての操作可能なパラメータを保存対象にする。
  setupGuiPersistence();
  ofSerialize(defaultGuiSettings, guiParams);
  if (previousRunCrashed) {
    logSavedGuiSettingsBeforeCrashReset();
    ofLogWarning("ofApp") << "前回の終了を確認できないため、GUIパラメーターを初期値に戻しました。";
  } else {
    loadGuiSettings();
  }

  // 保存済みモードを先に確定し、Video起動時にカメラを開かないようにする。
  // ofParameterのリスナーはこの後で登録するため、ここでは明示的に反映する。
  realtimeMode = pRealtime.get();

  pCameraIndex.addListener(this, &ofApp::onCameraIndexChanged);
  pRealtime.addListener(this, &ofApp::onRealtimeChanged);
  pFlipHorizontal.addListener(this, &ofApp::onFlipHorizontalChanged);
  pContourThreshold.addListener(this, &ofApp::onContourThresholdChanged);
  pVertexCount.addListener(this, &ofApp::onVertexCountChanged);
  pColorUpdateIntervalSec.addListener(this, &ofApp::onColorUpdateIntervalChanged);

  pRealtimeFps.addListener(this, &ofApp::onRealtimeFpsChanged);
  pVideoFps.addListener(this, &ofApp::onVideoFpsChanged);
  pPresetIndex.addListener(this, &ofApp::onPresetIndexChanged);

  playButton.setup("Play");
  pauseButton.setup("Pause");
  restartButton.setup("Restart from beginning");
  exportImageSequenceButton.setup("Export Image Sequence");
  resetParametersButton.setup("Reset Parameters");
  playButton.addListener(this, &ofApp::onPlayPressed);
  pauseButton.addListener(this, &ofApp::onPausePressed);
  restartButton.addListener(this, &ofApp::onRestartPressed);
  exportImageSequenceButton.addListener(this, &ofApp::onExportImageSequencePressed);
  resetParametersButton.addListener(this, &ofApp::onResetParametersPressed);

  // 保存済みの値を、GUI以外の実行状態にも反映する。
  int savedCameraIndex = pCameraIndex.get();
  onCameraIndexChanged(savedCameraIndex);
  personSegmenter.confThreshold = pContourThreshold.get();
  vertexCount = pVertexCount.get();
  colorUpdateIntervalMs = static_cast<uint64_t>(pColorUpdateIntervalSec.get() * 1000.0f);
  if (humanGraphicsScene) {
    humanGraphicsScene->enableBase = pGraphicsEnableBase.get();
    humanGraphicsScene->enableOffset = pGraphicsEnableOffset.get();
    humanGraphicsScene->enableStroke = pGraphicsEnableStroke.get();
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
  videoProcessor.processFps = static_cast<float>(pVideoFps.get());

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
  guiParams.add(pContourThreshold);
  guiParams.add(pVertexCount);
  guiParams.add(pColorUpdateIntervalSec);
  guiParams.add(pGraphicsEnableBase);
  guiParams.add(pGraphicsEnableOffset);
  guiParams.add(pGraphicsEnableStroke);
  guiParams.add(pGraphicsOffsetSize);
  guiParams.add(pGraphicsOffsetScale);
  guiParams.add(pGraphicsOffsetRound);
  guiParams.add(pGraphicsStrokeWeight);
  guiParams.add(pGraphicsStrokeRound);
  guiParams.add(pGraphicsBaseMaterial);
  guiParams.add(pGraphicsStrokeMaterial);
  guiParams.add(pRenderRecipeId);
  guiParams.add(pMergeEventId);
  guiParams.add(pSceneLayoutId);
  guiParams.add(pSceneBehaviorId);
  guiParams.add(pRealtimeFps);
  guiParams.add(pVideoFps);

  // プリセットは見た目・検出設定だけを変更し、現在の入力モード
  // （RealtimeかVideoか）は変更しない。root名はcontrols.jsonと同じにして、
  // 同じJSON形式のうち該当する項目だけを読み込めるようにする。
  presetParams.clear();
  presetParams.setName("ControlsSettings");
  presetParams.add(pCameraIndex);
  presetParams.add(pFlipHorizontal);
  presetParams.add(pContourThreshold);
  presetParams.add(pVertexCount);
  presetParams.add(pColorUpdateIntervalSec);
  presetParams.add(pGraphicsEnableBase);
  presetParams.add(pGraphicsEnableOffset);
  presetParams.add(pGraphicsEnableStroke);
  presetParams.add(pGraphicsOffsetSize);
  presetParams.add(pGraphicsOffsetScale);
  presetParams.add(pGraphicsOffsetRound);
  presetParams.add(pGraphicsStrokeWeight);
  presetParams.add(pGraphicsStrokeRound);
  presetParams.add(pGraphicsBaseMaterial);
  presetParams.add(pGraphicsStrokeMaterial);
  presetParams.add(pRenderRecipeId);
  presetParams.add(pMergeEventId);
  presetParams.add(pSceneLayoutId);
  presetParams.add(pSceneBehaviorId);
  presetParams.add(pRealtimeFps);
  presetParams.add(pVideoFps);
}

//--------------------------------------------------------------
void ofApp::loadGuiSettings() {
  const auto settingsPath = ofFilePath::join(settingsDirectoryPath(), kControlsFileName);
  if (!ofFile(settingsPath, ofFile::Reference).exists()) return;

  const ofJson settings = ofLoadJson(settingsPath);
  if (settings.is_object()) {
    isLoadingGuiSettings = true;
    ofDeserialize(settings, guiParams);
    isLoadingGuiSettings = false;
  }
}

//--------------------------------------------------------------
void ofApp::logSavedGuiSettingsBeforeCrashReset() const {
  const auto settingsPath =
      ofFilePath::join(settingsDirectoryPath(), kControlsFileName);
  if (!ofFile(settingsPath, ofFile::Reference).exists()) {
    ofLogWarning("ofApp")
        << "クラッシュ復旧: 前回のGUI設定ファイルは見つかりませんでした: "
        << settingsPath;
    return;
  }

  try {
    const ofJson savedSettings = ofLoadJson(settingsPath);
    ofLogWarning("ofApp")
        << "クラッシュ復旧: 初期値へ戻す前のGUI設定 (" << settingsPath
        << "):\n"
        << savedSettings.dump(2);
  } catch (const std::exception& error) {
    ofLogError("ofApp")
        << "クラッシュ復旧: 前回のGUI設定を読み出せませんでした: "
        << settingsPath << " (" << error.what() << ")";
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
  endRunSession();
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
void ofApp::beginRunSession() {
  const string settingsDirectory = settingsDirectoryPath();
  ofDirectory::createDirectory(settingsDirectory, false, true);
  const string markerPath =
      ofFilePath::join(settingsDirectory, kRunningMarkerFileName);
  previousRunCrashed = ofFile(markerPath, ofFile::Reference).exists();

  ofJson marker;
  marker["startedAt"] = ofGetTimestampString("%Y-%m-%dT%H:%M:%S");
  if (!ofSavePrettyJson(markerPath, marker)) {
    ofLogError("ofApp") << "実行状態マーカーを保存できませんでした: " << markerPath;
  }
}

//--------------------------------------------------------------
void ofApp::endRunSession() {
  const string markerPath =
      ofFilePath::join(settingsDirectoryPath(), kRunningMarkerFileName);
  if (ofFile(markerPath, ofFile::Reference).exists() &&
      !ofFile::removeFile(markerPath, false)) {
    ofLogWarning("ofApp") << "実行状態マーカーを削除できませんでした: " << markerPath;
  }
}

//--------------------------------------------------------------
void ofApp::discoverPresetFiles() {
  presetPaths.clear();

  // data/presets/ を標準の配置先にし、data/直下のJSONも読み込めるようにする。
  const vector<string> directories = {
      ofToDataPath("presets", true),
      ofToDataPath("", true),
  };
  for (const auto &directoryPath : directories) {
    ofDirectory directory(directoryPath);
    if (!directory.exists()) continue;
    directory.allowExt("json");
    directory.listDir();
    for (const auto &file : directory.getFiles()) {
      presetPaths.push_back(file.getAbsolutePath());
    }
  }
  std::sort(presetPaths.begin(), presetPaths.end());
}

//--------------------------------------------------------------
void ofApp::resetGuiParametersToDefaults() {
  isLoadingGuiSettings = true;
  ofDeserialize(defaultGuiSettings, guiParams);
  isLoadingGuiSettings = false;

  // カメラは保存済みの外部カメラではなく、必ず内蔵カメラへ戻す。
  int defaultCameraIndex = getDefaultCameraIndex();
  pCameraIndex = defaultCameraIndex;
  onCameraIndexChanged(defaultCameraIndex);
  pPresetIndex = static_cast<int>(presetPaths.size());
  pPresetName = "No preset selected";

  rebuildGuiPanel();
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onResetParametersPressed() {
  resetGuiParametersToDefaults();
}

//--------------------------------------------------------------
void ofApp::onPresetIndexChanged(int &index) {
  if (index < 0 || static_cast<size_t>(index) >= presetPaths.size()) {
    pPresetName = "No preset selected";
    return;
  }

  const string presetPath = presetPaths[static_cast<size_t>(index)];
  try {
    const ofJson preset = ofLoadJson(presetPath);
    if (!preset.is_object()) {
      ofLogWarning("ofApp") << "プリセットはJSONオブジェクトである必要があります: " << presetPath;
      return;
    }

    isLoadingGuiSettings = true;
    ofDeserialize(preset, presetParams);
    isLoadingGuiSettings = false;
    pPresetName = ofFilePath::getFileName(presetPath);
    rebuildGuiPanel();
    saveGuiSettings();
    ofLogNotice("ofApp") << "プリセットを読み込みました: " << pPresetName.get();
  } catch (const std::exception &error) {
    isLoadingGuiSettings = false;
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
  gui.add(pRealtime);
  if (previousRunCrashed) {
    gui.add(pCrashStatusText);
  }
  gui.add(pPresetIndex);
  gui.add(pPresetName);
  gui.add(&resetParametersButton);

  // ★追加: Realtime(カメラ)モードの時だけカメラ選択UI・左右反転トグルを表示
  if (realtimeMode) {
      gui.add(pCameraIndex);
      gui.add(pCameraName);
      gui.add(pFlipHorizontal);
      gui.add<float>(pRealtimeFps);
  }


  gui.add<float>(pContourThreshold);
  gui.add(pVertexCount);
  gui.add<float>(pColorUpdateIntervalSec);


  gui.add(pGraphicsEnableBase);
  gui.add(pGraphicsEnableOffset);
  gui.add(pGraphicsEnableStroke);
  gui.add<float>(pGraphicsOffsetSize);
  gui.add<float>(pGraphicsOffsetScale);
  gui.add(pGraphicsOffsetRound);
  gui.add<float>(pGraphicsStrokeWeight);
  gui.add(pGraphicsStrokeRound);
  gui.add(pGraphicsBaseMaterial);
  gui.add(pGraphicsStrokeMaterial);
  gui.add(pRenderRecipeId);
  gui.add(pMergeEventId);
  gui.add(pSceneLayoutId);
  gui.add(pSceneBehaviorId);

  // 動画モード時のみUIを追加
  if (!realtimeMode) {
    gui.add(pVideoFps);
    gui.add(&playButton);
    gui.add(&pauseButton);
    gui.add(&restartButton);
    gui.add(&exportImageSequenceButton);
    gui.add(pVideoStatusText);
  }
}

//--------------------------------------------------------------
void ofApp::onContourThresholdChanged(float &value) {
  // ★変更: 2値化しきい値ではなく、YOLOの人物信頼度しきい値として使う
  personSegmenter.confThreshold = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVertexCountChanged(int &value) {
  vertexCount = value;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onColorUpdateIntervalChanged(float &value) {
  colorUpdateIntervalMs = static_cast<uint64_t>(value * 1000.0f);
  saveGuiSettings();
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
    if (humanGraphicsScene) {
        humanGraphicsScene->setRenderRecipe(value);
        const std::string activeId(humanGraphicsScene->renderRecipeId());
        if (activeId != value) {
            ofLogWarning("ofApp") << "Unknown Recipe ID: " << value
                                  << ". Active Recipe: " << activeId;
        }
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
    if (humanGraphicsScene) {
        humanGraphicsScene->setSceneLayout(value);
        const std::string activeId(humanGraphicsScene->sceneLayoutId());
        if (activeId != value) {
            ofLogWarning("ofApp") << "Unknown Layout ID: " << value
                                  << ". Active Layout: " << activeId;
        }
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
void ofApp::onRealtimeFpsChanged(float &value) {
  // 設定変更後に前回の処理間隔を持ち越さないようにする。
  lastRealtimeProcessMs = 0;
  saveGuiSettings();
}

//--------------------------------------------------------------
void ofApp::onVideoFpsChanged(int &value) {
  videoProcessor.processFps = static_cast<float>(value);
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
void ofApp::onExportImageSequencePressed() {
  startImageSequenceExport();
}

//--------------------------------------------------------------
void ofApp::startImageSequenceExport() {
  if (isExportingImageSequence) {
    pVideoStatusText = "Export is already running";
    return;
  }
  if (realtimeMode || !videoProcessor.isLoaded() || !humanGraphicsScene) {
    pVideoStatusText = "Export requires a loaded video in Video mode";
    return;
  }

  if (!exportVideoPlayer.load(videoProcessor.getLoadedFileName())) {
    pVideoStatusText = "Export failed: could not load source video";
    return;
  }

  exportWidth = static_cast<int>(exportVideoPlayer.getWidth());
  exportHeight = static_cast<int>(exportVideoPlayer.getHeight());
  exportTotalFrames = exportVideoPlayer.getTotalNumFrames();
  if (exportWidth <= 0 || exportHeight <= 0 || exportTotalFrames <= 0) {
    pVideoStatusText = "Export failed: source video has no frames";
    exportVideoPlayer.close();
    return;
  }

  const auto exportRoot = ofFilePath::join(
      ofFilePath::getUserHomeDir(), "Movies/GUxSID_Human_Graphics");
  const auto exportFolder = ofFilePath::getBaseName(
      videoProcessor.getLoadedFileName()) + "_" +
      ofGetTimestampString("%Y%m%d_%H%M%S");
  exportOutputDirectory = ofFilePath::join(exportRoot, exportFolder);
  if (!ofDirectory::createDirectory(exportOutputDirectory, false, true)) {
    pVideoStatusText = "Export failed: could not create output folder";
    exportVideoPlayer.close();
    return;
  }

  exportColorImg.allocate(exportWidth, exportHeight);
  exportFbo.allocate(exportWidth, exportHeight, GL_RGBA);
  exportPixels.allocate(exportWidth, exportHeight, OF_PIXELS_RGBA);

  // Mainの描画用Sceneとは別インスタンスを使うため、書き出し中も
  // Main Windowのループ再生と色・輪郭の状態を維持できる。
  exportScene = std::make_shared<HumanGraphicsScene>(*humanGraphicsScene);
  // 浮遊Behaviorは時系列状態を持つため、Main側とは独立した状態で書き出す。
  exportScene->setSceneBehavior(
      std::string(humanGraphicsScene->sceneBehaviorId()));

  exportVideoPlayer.setLoopState(OF_LOOP_NONE);
  // AVFoundationでは停止中のfirstFrame()が新規フレームを通知しない場合が
  // あるため、最初の1フレームだけ再生状態で確実にデコードする。
  exportVideoPlayer.play();
  exportVideoPlayer.firstFrame();
  exportFrameIndex = 0;
  exportAwaitingFirstFrame = true;
  isExportingImageSequence = true;
  pVideoStatusText = "Exporting: 0 / " + ofToString(exportTotalFrames);
}

//--------------------------------------------------------------
void ofApp::updateImageSequenceExport() {
  if (!isExportingImageSequence) return;

  // 一度に1フレームだけ処理する。Main Windowの通常ループは停止中で、
  // この専用プレイヤーだけをフレーム送りする。
  exportVideoPlayer.update();
  if (!exportVideoPlayer.isFrameNew()) return;

  if (exportAwaitingFirstFrame) {
    exportVideoPlayer.setPaused(true);
    exportAwaitingFirstFrame = false;
  }

  const ofPixels& framePixels = exportVideoPlayer.getPixels();
  if (!framePixels.isAllocated()) return;

  cv::Mat rgbMat;
  const int channels = framePixels.getNumChannels();
  if (channels == 4) {
    cv::Mat rgbaMat(exportHeight, exportWidth, CV_8UC4,
                    const_cast<unsigned char*>(framePixels.getData()));
    cv::cvtColor(rgbaMat, rgbMat, cv::COLOR_RGBA2RGB);
  } else if (channels == 3) {
    rgbMat = cv::Mat(exportHeight, exportWidth, CV_8UC3,
                     const_cast<unsigned char*>(framePixels.getData()));
  } else if (channels == 1) {
    cv::Mat grayMat(exportHeight, exportWidth, CV_8UC1,
                    const_cast<unsigned char*>(framePixels.getData()));
    cv::cvtColor(grayMat, rgbMat, cv::COLOR_GRAY2RGB);
  } else {
    pVideoStatusText = "Export failed: unsupported pixel format";
    cancelImageSequenceExport();
    return;
  }
  const HumanContourData exportData = personSegmenter.detect(
      rgbMat, exportWidth, exportHeight);

  exportScene->update(exportData);
  exportFbo.begin();
  exportScene->draw();
  exportFbo.end();

  exportFbo.readToPixels(exportPixels);
  // FBOから読むピクセルはOpenGL座標系なので、PNG用に上下を反転する。
  exportPixels.mirror(true, false);

  const std::string fileName = "frame_" +
      ofToString(exportFrameIndex + 1, 6, '0') + ".png";
  const auto outputPath = ofFilePath::join(exportOutputDirectory, fileName);
  if (!ofSaveImage(exportPixels, outputPath)) {
    pVideoStatusText = "Export failed while saving: " + outputPath;
    cancelImageSequenceExport();
    return;
  }

  ++exportFrameIndex;
  if (exportFrameIndex >= exportTotalFrames) {
    pVideoStatusText = "Finished: " + ofToString(exportFrameIndex) +
        " frames\n" + exportOutputDirectory;
    cancelImageSequenceExport();
    return;
  }

  pVideoStatusText = "Exporting: " + ofToString(exportFrameIndex) +
      " / " + ofToString(exportTotalFrames);
  exportVideoPlayer.nextFrame();
}

//--------------------------------------------------------------
void ofApp::cancelImageSequenceExport() {
  isExportingImageSequence = false;
  exportAwaitingFirstFrame = false;
  exportVideoPlayer.close();
  exportScene.reset();
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
  const std::string &path = dragInfo.files[0];

  pVideoStatusText = "読み込み中: " + path;

  bool ok = videoProcessor.loadVideo(path);
  if (ok) {
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

    const float fps = pRealtimeFps.get();
    const uint64_t intervalMs = static_cast<uint64_t>(1000.0f / fps);
    const uint64_t now = ofGetElapsedTimeMillis();

    if (cam.isFrameNew() && personSegmenter.isLoaded() &&
        (now - lastRealtimeProcessMs) >= intervalMs) {
      const ofPixels &cameraPixels = cam.getPixels();
      const int frameWidth = cameraPixels.getWidth();
      const int frameHeight = cameraPixels.getHeight();
      if (frameWidth <= 0 || frameHeight <= 0) {
        return;
      }

      colorImg.setFromPixels(cameraPixels);

      // ★追加: 左右反転トグルがONのときは、以降のAI推論・
      //   デバッグプレビューすべてに反映されるよう、ここでcolorImg自体を反転する。
      //   (mirror(bFlipVertical, bFlipHorizontal))
      if (pFlipHorizontal) {
        colorImg.mirror(false, true);
      }

      // カメラは要求解像度とは異なるサイズを返すことがあるため、
      // 固定の W/H ではなく、実際に届いたフレームサイズで作成する。
      cv::Mat rgbMat(frameHeight, frameWidth, CV_8UC3,
                     colorImg.getPixels().getData());

      // ============================================
      // ★変更: YOLO11-seg(person専用)で人物ごとの輪郭を検出する。
      //   PersonSegmenterが内部でレターボックス・推論・NMS・
      //   マスク→輪郭変換まで行い、ofGetWidth()/Height()座標系の
      //   HumanContourDataをそのまま返してくれる。
      // ============================================
      humanData = personSegmenter.detect(rgbMat, ofGetWidth(), ofGetHeight());
      lastRealtimeProcessMs = now;
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
  string info = "=== [DEBUG MODE] YOLO11-seg Person Detection ===\n";
  info += "Model: YOLO11-seg (person only)\n";
  info += "Mode: " + string(realtimeMode ? "Realtime (Camera)" : "Video File") + "\n";
  if (realtimeMode) {
    info += "Detected People: " + ofToString(humanData.numHumans) + "\n";
    info += "Confidence: " + ofToString(personSegmenter.confThreshold, 2) + " [UP/DOWN: Adjust]\n";
  } else {
    info += "Detected People: " + ofToString(videoProcessor.humanData.numHumans) + "\n";
    info += "Confidence: " + ofToString(personSegmenter.confThreshold, 2) + " [UP/DOWN: Adjust]\n";
    info += "Video FPS setting: " + ofToString(videoProcessor.processFps, 1) + "\n";
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
  // 輪郭の閾値 (UP/DOWN, 0.0〜1.0)
  else if (key == OF_KEY_UP) {
    pContourThreshold = std::min(1.0f, pContourThreshold.get() + 0.05f);
  } else if (key == OF_KEY_DOWN) {
    pContourThreshold = std::max(0.0f, pContourThreshold.get() - 0.05f);
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
    ofBackground(40); // GUIウィンドウの背景色（暗いグレー）
    gui.draw();
  }
}
