#pragma once

#include "ofAppGLFWWindow.h"
#include "ofMain.h"
#include "ofxGui.h"
#include "ofxOpenCv.h"
#include <future>
#include <memory>
#include <opencv2/dnn.hpp>

#include "BaseScene.h"

#include "PersonSegmenter.h"
#include "HumanGraphicsScene.h"
#include "video_Processing.h"

// Color Pallate
class ofApp : public ofBaseApp {

public:
  void setup() override;
  void exit() override;
  void update() override;
  void draw() override;
  void keyPressed(int key) override;
  void drawGui(ofEventArgs &args);

  // メインウィンドウにドロップされたときに呼ばれる
  // (ofBaseAppの仮想関数。ofRunApp(mainWindow, mainApp)によって
  //  メインウィンドウのイベントへ自動的に紐付けられる。値渡しのシグネチャ)
  void dragEvent(ofDragInfo dragInfo) override;

  // ★追加: Control Window(guiWindow)にドロップされたときに呼ばれる。
  //   ofAddListenerでイベントに紐付ける関数は「参照渡し」のシグネチャで
  //   ないと一致しない(ofEventUtils.hのテンプレート要件)ため、
  //   値渡しのdragEventとは別にこちらを用意している。
  //   (main.cppでguiWindowのfileDragEventに紐付ける)
  void onGuiWindowFileDragged(ofDragInfo &dragInfo);
  void onGuiWindowKeyPressed(ofKeyEventArgs &args);

  // デバッグ描画用関数
  void drawDebug();

  // color pallate
  //  ★修正: static を付けることで、インスタンスなしに
  //    ofApp::colorPallate として参照できるようにする。
  //    (C++17のinline staticなので、この場で初期化するだけで良く、
  //     .cppファイル側に別途定義を書く必要はない)
  static constexpr size_t colorPaletteSize = 6;
  // scene1〜4の色を更新する間隔（ミリ秒）
  // ★修正: GUI/キー操作で実行中に変更できるよう constexpr を外した
  //   (C++17のinline staticなので.cpp側の定義は不要)
  static inline uint64_t colorUpdateIntervalMs = 10000;

  // 輪郭のリサンプリング頂点数（scene1〜4共通、GUI/キー操作で変更可能）
  static inline int vertexCount = 16;

  static inline int background_color = 252;

  // HumanGraphicsSceneの人物グラフィック結合方法。
  // RenderedGraphic: 実際に描画するポリゴン／ストロークが接触した時だけ結合する。
  // CentroidDistance: 重心間距離が下記の閾値以内の人物を結合する軽量モード。
  enum class CompositionMergeMode { RenderedGraphic, CentroidDistance };
  static inline CompositionMergeMode compositionMergeMode =
      CompositionMergeMode::RenderedGraphic;

  // CentroidDistanceモードで結合する重心間距離（描画座標のピクセル単位）。
  // 必要に応じてここを変更する。
  static inline float compositionCentroidMergeDistance = 50.0f;

  // RenderedGraphicモードの結合判定に使うマスク解像度。
  // 小さくするほど軽くなるが、接触判定の細かさは下がる。
  static inline float compositionMergeMaskScale = 0.1f;

  // Strokeだけを結合する際の負荷と見た目の調整値。
  // 低解像度マスクでも外周近似を維持するため、均等リサンプリングは使わない。
  static inline float compositionStrokeMergeMaskMinScale = 0.25f;
  static inline float compositionStrokeMergeApproximationPx = 6.0f;
  static inline size_t compositionStrokeMergeMaxVertices = 64;

  // ============================================
  // ★追加: Control Window(GUIウィンドウ)のサイズ。
  //   main.cpp側のウィンドウ生成時にこの値を参照する。
  //   Realtime=falseにすると動画関連のウィジェットが増えるため、
  //   縦のサイズは余裕を持たせておくと良い。
  // ============================================
  static inline int controlWindowWidth = 600;
  static inline int controlWindowHeight = 990;

  static inline ofColor colorPallate[colorPaletteSize] = {
      // ofColor(RGB)
      ofColor(244, 180, 208), // COZY _ CMYK:0,40,0,0
      ofColor(0, 0, 0),       // MINIMAL _ CMYK:0,0,0,0
      ofColor(98, 117, 52),   // Utility _ CMYK:70,50,100,0
      ofColor(234, 85, 50),   // Playful _ CMYK:0,80,80,0
      ofColor(0, 129, 198),   // SPORT _ CMYK:100,30,5,0
      ofColor(242, 188, 0),   // Classic _ CMYK:5,30,100,0
  };

  // ============================================
  // ★追加: カメラデバイス選択用
  // ============================================
  vector<ofVideoDevice> cameraDevices; // 認識されたカメラのリスト
  ofParameter<int> pCameraIndex;       // GUIで選択するカメラID
  ofParameter<string> pCameraName;     // 選択中カメラの名前表示用

  // data/presets/ 内のJSONプリセットを選ぶためのUI。
  // 0以降が presetPaths の各ファイルに対応し、末尾は「選択なし」。
  // ofxGuiの整数スライダーで最後のファイルも選べるよう、選択なしを末尾に置く。
  vector<string> presetPaths;
  ofParameter<int> pPresetIndex;
  ofParameter<string> pPresetName;
  ofParameter<string> pPresetStatus;
  string loadedPresetFileName;
  ofJson loadedPresetSnapshot;
  bool hasLoadedPresetSnapshot = false;
  bool presetIsDirty = false;
  ofEventListener presetParameterChangedListener;

  void onCameraIndexChanged(int &index); // カメラIDが変更された時のリスナー

  // ============================================
  // 2入力: 系統ごとのカメラ選択
  // pCameraIndex = カメラ1(上段), pCamera2Index = カメラ2(下段)。
  // Controlsには認識したカメラを「番号: 名前」のトグルで並べ、1つだけ選べる。
  // 2系統が同じカメラを選んだ場合は、カメラ2を開かずカメラ1の映像を共有する。
  // ============================================
  ofParameter<int> pCamera2Index;
  void onCamera2IndexChanged(int &index);
  ofParameterGroup cameraChoiceGroups[2];
  std::vector<ofParameter<bool>> cameraChoices[2];
  ofEventListeners cameraChoiceListeners;
  // 実際に開いているデバイス番号 (-1 = 閉じている / カメラ2は共有中)
  int openedCameraDevice[2] = {-1, -1};
  void setupCameraChoices();
  void syncCameraChoices();
  void onCameraChoiceChanged(int input, int device, bool value);
  void applyCameraSelection();
  static std::string cameraDisplayName(const ofVideoDevice &device);

  // ============================================
  // ★追加: 読み取った映像の左右反転（Realtimeモードのみ有効）
  // ============================================
  ofParameter<bool> pFlipHorizontal; // true: 左右反転（鏡像）表示にする

  // ============================================
  // プロジェクター投影用の余白
  // 4K(3840x2160)のうち中央3840x990だけが投影されるため、
  // ONの間はその範囲だけに描画し、外側を白で塗る。
  // ============================================
  ofParameter<bool> pProjectionSafeArea;
  void onProjectionSafeAreaChanged(bool &value);
  // Main Window内で実際に描画する範囲（余白OFFならウィンドウ全体）
  ofRectangle mainCanvasRect() const;
  int mainCanvasWidth() const;
  int mainCanvasHeight() const;
  void drawProjectionMargins();
  // Fキー: Main Windowの全画面表示を切り替える
  void toggleMainWindowFullscreen();
  static constexpr int kProjectionFrameWidth = 3840;
  static constexpr int kProjectionFrameHeight = 2160;
  static constexpr int kProjectionSafeWidth = 3840;
  static constexpr int kProjectionSafeHeight = 1020;
  int lastSceneCanvasWidth = 0;
  int lastSceneCanvasHeight = 0;

  // ============================================
  // 人物輪郭の座標データ（ここで計算し全シーンへ渡す）
  // Realtime=trueのときはこちらをScene側へ渡す
  // ============================================
  HumanContourData humanData;

  // シーン管理
  std::shared_ptr<BaseScene> currentScene;

  std::shared_ptr<HumanGraphicsScene> humanGraphicsScene;

  // デバッグ表示フラグ（基本は非表示、Dキーで切り替え）
  bool showDebug = false;

  // ============================================
  // 調整用GUI（ofxGUI・別ウィンドウ）
  // ============================================

  // main.cppで生成されたGUI用の別ウィンドウへの参照。
  // 起動時に表示し、閉じてしまった場合はAキーで再作成する。
  std::shared_ptr<ofAppGLFWWindow> guiWindow;
  bool guiVisible = true;

  // ★追加: メインウィンドウ（映像を表示している方）への参照。
  //   動画を読み込んだ際に、そのウィンドウの解像度・縦横比を
  //   動画に合わせてリサイズするために使用する。main.cppで設定される。
  std::shared_ptr<ofAppGLFWWindow> mainWindow;

  // 切り抜ける余白の中での位置。中央を初期画角とする。
  float cropPositionX = 0.5f;
  float cropPositionY = 0.5f;

  ofxPanel gui;
  ofParameterGroup guiParams;
  // プリセット用の設定群。入力モード（Realtime / Video）は含めない。
  ofParameterGroup presetParams;
  bool isLoadingGuiSettings = false;

  // Main Windowの動作状況と描画目標FPSをControlsへ表示する。
  // ステータス3種は保存せず、目標FPSだけcontrols.jsonへ保存する。
  ofParameter<string> pMainWindowResolution;
  ofParameter<string> pMainWindowFps;
  ofParameter<string> pRunTime;
  ofParameter<int> pMainWindowTargetFps;
  uint64_t lastControlsMetricsUpdateMs = 0;

  // 全体設定
  ofParameter<float> pYoloConfidence;
  ofParameter<bool> pYoloInput320;
  ofParameter<int> pVertexCount;        // 頂点数 4〜100
  ofParameter<bool> pLooseContour;       // 安定した緩い輪郭
  ofParameter<float> pLooseContourStrength; // 輪郭変形幅。人物幅に対する割合(%)
  ofParameter<float> pAspectRatio; // 検出前の元フレームの縦横比変化率(%)
  ofParameter<float> pColorUpdateIntervalSec; // 色の更新頻度(秒) 0〜20
  // Video専用。1人は指定色、2人以上は動画読込時に選んだ色を固定できる。
  ofParameter<int> pVideoPeopleCount;
  ofParameter<int> pVideoSoloColor;
  ofParameter<bool> pVideoColorLock;
  ofParameter<float> pVideoPlaybackSpeed;
  std::vector<size_t> videoPaletteIndices;

  ofParameter<bool> pGraphicsEnableBase;   // ベース描画のON/OFF
  ofParameter<bool> pGraphicsEnableOffset; // オフセット描画のON/OFF
  ofParameter<bool> pGraphicsEnableStroke; // ストローク描画のON/OFF
  ofParameter<bool> pGraphicsEnableBridgeStroke; // 重心を結ぶストロークのON/OFF
  ofParameter<bool> pGraphicsMergeEnable;  // 人物グラフィック結合のON/OFF
  ofParameter<float> pGraphicsOffsetSize;  // オフセットサイズ
  ofParameter<float> pGraphicsOffsetScale;
  ofParameter<bool>
      pGraphicsOffsetRound; // オフセット凸角(true: Round, false: Straight)
  ofParameter<float> pGraphicsStrokeWeight; // ストローク太さ
  ofParameter<bool>
      pGraphicsStrokeRound; // 角モード(true: Round, false: Straight)
  // 0: Solid / 1: Linear Gradient。BaseとOutlineは独立して選択できる。
  ofParameter<int> pGraphicsBaseMaterial;
  ofParameter<int> pGraphicsStrokeMaterial;
  ofParameter<string> pRenderRecipeId;
  ofParameter<string> pMergeEventId;
  ofParameter<string> pSceneLayoutId;
  ofParameter<string> pSceneBehaviorId;
  // 色紙の影。紙が背景からわずかに浮いて見えるようにする。
  ofParameter<bool> pPaperShadowEnable;
  ofParameter<float> pPaperShadowLift;    // 浮き量（影のずれ・ぼかし）
  ofParameter<float> pPaperShadowAngle;   // 影の向き（0 = 右, 90 = 真下）
  ofParameter<float> pPaperShadowOpacity; // 影の濃さの倍率
  // 背景のグラデーション。白の上にオフホワイトのにじみを漂わせる。
  ofParameter<bool> pBackgroundGradientEnable;
  ofParameter<float> pBackgroundGradientDepth; // にじみの濃さ
  ofParameter<float> pBackgroundGradientSpeed; // 漂う速さ

  // 音のソフト（TouchDesignerなど）へ人物の情報をOSCで送る。送信先は同じMacの127.0.0.1。
  ofParameter<bool> pSoundOscEnable;
  // 送信先ポートは誤操作で変わらないよう固定する。変える場合はこの値を書き換える。
  static constexpr int kSoundOscPort = 7400;
  ofParameter<string> pSoundOscTarget;  // 表示専用（Controlsで変更できない）
  gux::SoundOscSender soundOscSender;
  void onSoundOscEnableChanged(bool &value);

  // GUIリスナー関数
  void onGraphicsEnableBaseChanged(bool &value);
  void onGraphicsEnableOffsetChanged(bool &value);
  void onGraphicsEnableStrokeChanged(bool &value);
  void onGraphicsEnableBridgeStrokeChanged(bool &value);
  void onGraphicsMergeEnableChanged(bool &value);
  void onGraphicsOffsetSizeChanged(float &value);
  void onGraphicsOffsetScaleChanged(float &value);
  void onGraphicsOffsetRoundChanged(bool &value);
  void onGraphicsStrokeWeightChanged(float &value);
  void onLookEnableChanged(bool &value);
  void onLookValueChanged(float &value);
  void applyLookSettings();
  void onGraphicsStrokeRoundChanged(bool &value);
  void onGraphicsBaseMaterialChanged(int &value);
  void onGraphicsStrokeMaterialChanged(int &value);
  void onRenderRecipeIdChanged(string &value);
  void onMergeEventIdChanged(string &value);
  void onSceneLayoutIdChanged(string &value);
  void onSceneBehaviorIdChanged(string &value);

  // ============================================
  // ★追加: Realtime / 動画モード関連のGUIパラメータ
  // ============================================

  // true: Webカメラのリアルタイム映像を使用（従来通り）
  // false: ドロップされた動画ファイルを使用
  ofParameter<bool> pRealtime;

  // PNG連番書き出し時に背景を透明にする。
  ofParameter<bool> pExportAlpha;

  // 動画モード時にControl Windowへ表示するステータス文言
  // (ドロップ待ち／読み込み中／再生中のファイル名などを表示)
  ofParameter<string> pVideoStatusText;

  // 再生・一時停止・最初から再生ボタン（動画モード時のみパネルに表示）
  ofxButton playButton;
  ofxButton pauseButton;
  ofxButton restartButton;
  ofxButton openVideoButton;
  ofxButton exportImageSequenceButton;
  ofxButton savePresetButton;
  ofxButton revertPresetButton;

  void setupGuiParameters();
  void updateControlsMetrics();
  void onYoloConfidenceChanged(float &value);
  void onYoloInput320Changed(bool &value);
  void onVertexCountChanged(int &value);
  void onLooseContourChanged(bool &value);
  void onLooseContourStrengthChanged(float &value);
  void onAspectRatioChanged(float &value);
  void onColorUpdateIntervalChanged(float &value);
  void onVideoPeopleCountChanged(int &value);
  void onVideoSoloColorChanged(int &value);
  void onVideoColorLockChanged(bool &value);
  void onVideoPlaybackSpeedChanged(float &value);
  void selectVideoPaletteColors();
  void applyVideoColorMode();
  void onScene2OffsetChanged(float &value);
  void onScene3StrokeWeightChanged(float &value);
  void onRealtimeChanged(bool &value);
  void onMainWindowTargetFpsChanged(int &value);
  void applyTargetFrameRate(int value);
  void finishRealtimeDetection(bool applyResult);
  void onExportAlphaChanged(bool &value);
  void onPlayPressed();
  void onPausePressed();
  void onRestartPressed();
  void onOpenVideoPressed();
  void onExportImageSequencePressed();
  void onSavePresetPressed();
  void onRevertPresetPressed();

  // Realtime/動画モードの切り替えに応じて、Control Windowの
  // パネル構成（表示するウィジェット）を組み直す
  void rebuildGuiPanel();

  // 読み込んだ動画の解像度にメインウィンドウの大きさを合わせる
  void resizeMainWindowToVideo();
  void applyMainWindowAspectLock();

  // dragEvent / onGuiWindowFileDragged の両方から呼ばれる共通処理
  void handleDroppedFile(const ofDragInfo &dragInfo);
  void loadVideoFile(const std::string &path);

  // 動画のループ再生とは独立して、HumanGraphicsSceneをPNG連番として書き出し、
  // 完了後にAVFoundationでアルファ付きProRes 4444 MOVへ変換する。
  void startImageSequenceExport();
  void updateImageSequenceExport();
  void cancelImageSequenceExport();
  void startMovieExport();
  void updateMovieExport();

  // GUI設定の永続化と、Controls Windowの表示／再生成（Aキー）
  void setupGuiPersistence();
  void loadGuiSettings();
  void saveGuiSettings() const;
  void discoverPresetFiles();
  void onPresetIndexChanged(int &index);
  void updatePresetDirtyState();
  void captureLoadedPresetSnapshot();
  void clearLoadedPresetSnapshot();
  int findPresetIndexByFileName(const string &fileName) const;
  int getDefaultCameraIndex() const;
  void showGuiWindow();
  void showMainWindow();
  void onFlipHorizontalChanged(bool &value);

  // 閉じたMain Windowを再生成した場合に接続するイベント用ラッパー。
  void onRecreatedMainWindowUpdate(ofEventArgs &args);
  void onRecreatedMainWindowDraw(ofEventArgs &args);
  void onRecreatedMainWindowKeyPressed(ofKeyEventArgs &args);
  void onRecreatedMainWindowExit(ofEventArgs &args);

  // ============================================
  // ★追加: Realtime/動画モードの状態と、動画処理の実体
  // ============================================

  // true: カメラ入力（従来通り） / false: 動画ファイル入力
  bool realtimeMode = true;

  // Realtimeモードで最後にセグメンテーションを実行した時刻
  uint64_t lastRealtimeProcessMs = 0;

  // 動画のデコード・AIセグメンテーション処理を担当する別クラス
  // (UIはofApp側、処理ロジックはこちらに分離)
  VideoProcessing videoProcessor;

  bool isExportingImageSequence = false;
  bool exportAwaitingFirstFrame = false;
  uint64_t exportFrameWaitStartedAtMillis = 0;
  bool exportAlpha = false;
  bool isExportingMovie = false;
  int exportFrameIndex = 0;
  int exportTotalFrames = 0;
  int exportSourceTotalFrames = 0;
  int exportDecodedSourceFrame = -1;
  int exportSourceWidth = 0;
  int exportSourceHeight = 0;
  int exportCanvasWidth = 0;
  int exportCanvasHeight = 0;
  int exportWidth = 0;
  int exportHeight = 0;
  float exportScale = 1.0f;
  float exportOffsetX = 0.0f;
  float exportCropPositionX = 0.5f;
  float exportCropPositionY = 0.5f;
  double exportFrameRate = 30.0;
  double exportSourceFrameRate = 30.0;
  float exportTimelineStartSeconds = 0.0f;
  HumanContourData exportDecodedData;
  std::string exportOutputDirectory;
  std::string exportPngDirectory;
  std::string exportMovieDirectory;
  std::string exportMoviePath;
  std::string exportMovieCodecName;
  std::future<std::string> exportMovieFuture;
  ofVideoPlayer exportVideoPlayer;
  // 静止画の書き出しでは、動画デコードの代わりにこの画素を1枚だけ使う。
  bool exportFromStillImage = false;
  ofPixels exportStillPixels;
  ofFbo exportFbo;
  ofPixels exportPixels;
  std::shared_ptr<HumanGraphicsScene> exportScene;

  // カメラ入力 (cam = カメラ1 / cam2 = カメラ2。cam2はカメラ1と別の機器の時だけ開く)
  ofVideoGrabber cam;
  ofVideoGrabber cam2;
  ofxCvColorImage colorImg2;

  // 画像処理バッファ（カメラ画像の一時保持・左右反転に使用）
  ofxCvColorImage colorImg;

  // ============================================
  // ★変更: MediaPipe Selfie Segmentation から
  //   YOLO11-seg(person専用)によるインスタンスセグメンテーションに変更。
  //   人物ごとに個別の輪郭が得られるため、複数人が近づいても
  //   ブロブが融合して1人分として検出される問題が起きにくい。
  //   VideoProcessing側もこのインスタンスを共有して使う
  //   (ポインタで渡す。モデルの二重読み込みはしない)。
  // ============================================
  PersonSegmenter personSegmenter;

  // ============================================
  // Realtime推論は一度に一件。GUI設定やVideoへの切替時には完了を待つ。
  // ============================================
  std::future<HumanContourData> detectionFuture;

  // ============================================
  // 2画面モード (2入力2出力)
  // 出力キャンバス3840x2160に、3550x950の映像を上下2段で置く。
  //   上段 (0, 0)    : 系統1 → プロジェクター左2台
  //   下段 (0, 1080) : 系統2 → プロジェクター右2台 (キャンバスセンターラインから)
  // それ以外は黒。Realtimeモードで Dual Output がONの間だけ有効。
  // 系統1は既存の humanGraphicsScene / personSegmenter / humanData を使い、
  // 系統2は下の *2 のメンバーを使う。GUI設定は毎フレーム系統1から写す。
  // カメラ2がカメラ1と同じデバイスなら、カメラ1のフレームを共有する。
  // ============================================
  static constexpr int kDualCanvasWidth = 3840;
  static constexpr int kDualCanvasHeight = 2160;
  static constexpr int kDualBandWidth = 3550;
  static constexpr int kDualBandHeight = 950;
  static constexpr int kDualUpperBandY = 0;
  static constexpr int kDualLowerBandY = 1080;
  // Controls の「Dual Output (2 Screens)」で切り替える。Realtime モードのときだけ有効。
  ofParameter<bool> pDualOutput;
  void onDualOutputChanged(bool &value);
  bool guiRebuildPending = false;
  bool dualOutputActive() const { return realtimeMode && pDualOutput.get(); }
  std::shared_ptr<HumanGraphicsScene> humanGraphicsScene2;
  PersonSegmenter personSegmenter2;
  std::future<HumanContourData> detectionFuture2;
  HumanContourData humanData2;
  uint64_t lastRealtimeProcessMs2 = 0;
  ofFbo dualBandFbo[2];
  void syncSecondPipelineSettings();
  void allocateDualBandFbos();
  void drawDualOutput();

  // サイズ定義（Webカメラ入力の解像度。動画モードの解像度とは別物）
  const int W = 1920;
  const int H = 1080;
};
