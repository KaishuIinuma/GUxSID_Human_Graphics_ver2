#pragma once

#include "ofAppGLFWWindow.h"
#include "ofMain.h"
#include "ofxGui.h"
#include "ofxOpenCv.h"
#include <memory>
#include <opencv2/dnn.hpp>

#include "BaseScene.h"

#include "PersonSegmenter.h"
#include "scene4.h"
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

  static inline int background_color = 255;

  // Scene4の人物グラフィック結合方法。
  // RenderedGraphic: 実際に描画するポリゴン／ストロークが接触した時だけ結合する。
  // CentroidDistance: 重心間距離が下記の閾値以内の人物を結合する軽量モード。
  enum class Scene4MergeMode { RenderedGraphic, CentroidDistance };
  static inline Scene4MergeMode scene4MergeMode =
      Scene4MergeMode::RenderedGraphic;

  // CentroidDistanceモードで結合する重心間距離（描画座標のピクセル単位）。
  // 必要に応じてここを変更する。
  static inline float scene4CentroidMergeDistance = 50.0f;

  // RenderedGraphicモードの結合判定に使うマスク解像度。
  // 小さくするほど軽くなるが、接触判定の細かさは下がる。
  static inline float scene4MergeMaskScale = 0.1f;

  // Strokeだけを結合する際の負荷と見た目の調整値。
  // 低解像度マスクでも外周近似を維持するため、均等リサンプリングは使わない。
  static inline float scene4StrokeMergeMaskMinScale = 0.25f;
  static inline float scene4StrokeMergeApproximationPx = 6.0f;
  static inline size_t scene4StrokeMergeMaxVertices = 64;

  // ============================================
  // ★追加: Control Window(GUIウィンドウ)のサイズ。
  //   main.cpp側のウィンドウ生成時にこの値を参照する。
  //   Realtime=falseにすると動画関連のウィジェットが増えるため、
  //   縦のサイズは余裕を持たせておくと良い。
  // ============================================
  static inline int controlWindowWidth = 600;
  static inline int controlWindowHeight = 900;

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

  void onCameraIndexChanged(int &index); // カメラIDが変更された時のリスナー

  // ============================================
  // ★追加: 読み取った映像の左右反転（Realtimeモードのみ有効）
  // ============================================
  ofParameter<bool> pFlipHorizontal; // true: 左右反転（鏡像）表示にする

  // ============================================
  // 人物輪郭の座標データ（ここで計算し全シーンへ渡す）
  // Realtime=trueのときはこちらをScene側へ渡す
  // ============================================
  HumanContourData humanData;

  // シーン管理
  std::shared_ptr<BaseScene> currentScene;

  std::shared_ptr<Scene4> scene4;

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

  // Main Windowの縦横比ロック。RealtimeとVideoの両方を
  // 一括で制御する。Realtimeはカメラ、Videoは読み込み動画の比率で固定。
  static inline bool lockMainWindowAspectRatio = true;

  ofxPanel gui;
  ofParameterGroup guiParams;
  // プリセット用の設定群。入力モード（Realtime / Video）は含めない。
  ofParameterGroup presetParams;
  bool isLoadingGuiSettings = false;
  bool previousRunCrashed = false;
  ofJson defaultGuiSettings;

  // 全体設定
  // ★変更: MediaPipeの二値化しきい値から、YOLOの人物信頼度しきい値に転用。
  //   実体は personSegmenter.confThreshold (0.0〜1.0)。
  ofParameter<float> pContourThreshold; // YOLO 人物信頼度のしきい値 0.0〜1.0
  ofParameter<int> pVertexCount;        // 頂点数 4〜100
  ofParameter<float> pColorUpdateIntervalSec; // 色の更新頻度(秒) 0〜20

  ofParameter<bool> pScene4EnableBase;   // ベース描画のON/OFF
  ofParameter<bool> pScene4EnableOffset; // オフセット描画のON/OFF
  ofParameter<bool> pScene4EnableStroke; // ストローク描画のON/OFF
  ofParameter<float> pScene4OffsetSize;  // オフセットサイズ
  ofParameter<float> pScene4OffsetScale;
  ofParameter<bool>
      pScene4OffsetRound; // オフセット凸角(true: Round, false: Straight)
  ofParameter<float> pScene4StrokeWeight; // ストローク太さ
  ofParameter<bool>
      pScene4StrokeRound; // 角モード(true: Round, false: Straight)
  // 0: Solid / 1: Linear Gradient。BaseとOutlineは独立して選択できる。
  ofParameter<int> pScene4BaseMaterial;
  ofParameter<int> pScene4StrokeMaterial;

  // GUIリスナー関数
  void onScene4EnableBaseChanged(bool &value);
  void onScene4EnableOffsetChanged(bool &value);
  void onScene4EnableStrokeChanged(bool &value);
  void onScene4OffsetSizeChanged(float &value);
  void onScene4OffsetScaleChanged(float &value);
  void onScene4OffsetRoundChanged(bool &value);
  void onScene4StrokeWeightChanged(float &value);
  void onScene4StrokeRoundChanged(bool &value);
  void onScene4BaseMaterialChanged(int &value);
  void onScene4StrokeMaterialChanged(int &value);

  // ============================================
  // ★追加: Realtime / 動画モード関連のGUIパラメータ
  // ============================================

  // true: Webカメラのリアルタイム映像を使用（従来通り）
  // false: ドロップされた動画ファイルを使用
  ofParameter<bool> pRealtime;

  // Realtimeモード時、セグメンテーション処理を行うfps (0.5〜60)
  ofParameter<float> pRealtimeFps;

  // 動画モード時、セグメンテーション処理を行うfps (1〜60)
  ofParameter<int> pVideoFps;

  // 動画モード時にControl Windowへ表示するステータス文言
  // (ドロップ待ち／読み込み中／再生中のファイル名などを表示)
  ofParameter<string> pVideoStatusText;

  // 前回が異常終了だった場合だけControlsに表示する復旧ステータス。
  ofParameter<string> pCrashStatusText;

  // 再生・一時停止・最初から再生ボタン（動画モード時のみパネルに表示）
  ofxButton playButton;
  ofxButton pauseButton;
  ofxButton restartButton;
  ofxButton exportImageSequenceButton;
  ofxButton resetParametersButton;

  void setupGuiParameters();
  void onContourThresholdChanged(float &value);
  void onVertexCountChanged(int &value);
  void onColorUpdateIntervalChanged(float &value);
  void onScene2OffsetChanged(float &value);
  void onScene3StrokeWeightChanged(float &value);
  void onRealtimeChanged(bool &value);
  void onRealtimeFpsChanged(float &value);
  void onVideoFpsChanged(int &value);
  void onPlayPressed();
  void onPausePressed();
  void onRestartPressed();
  void onExportImageSequencePressed();
  void onResetParametersPressed();

  // Realtime/動画モードの切り替えに応じて、Control Windowの
  // パネル構成（表示するウィジェット）を組み直す
  void rebuildGuiPanel();

  // 読み込んだ動画の解像度にメインウィンドウの大きさを合わせる
  void resizeMainWindowToVideo();

  // dragEvent / onGuiWindowFileDragged の両方から呼ばれる共通処理
  void handleDroppedFile(const ofDragInfo &dragInfo);

  // 動画のループ再生とは独立して、Scene4をPNG連番として書き出す。
  void startImageSequenceExport();
  void updateImageSequenceExport();
  void cancelImageSequenceExport();

  // GUI設定の永続化と、Controls Windowの表示／再生成（Aキー）
  void setupGuiPersistence();
  void loadGuiSettings();
  void saveGuiSettings() const;
  void resetGuiParametersToDefaults();
  void discoverPresetFiles();
  void onPresetIndexChanged(int &index);
  int getDefaultCameraIndex() const;
  void beginRunSession();
  void endRunSession();
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
  int exportFrameIndex = 0;
  int exportTotalFrames = 0;
  int exportWidth = 0;
  int exportHeight = 0;
  std::string exportOutputDirectory;
  ofVideoPlayer exportVideoPlayer;
  ofxCvColorImage exportColorImg;
  ofFbo exportFbo;
  ofPixels exportPixels;
  std::shared_ptr<Scene4> exportScene;

  // カメラ入力
  ofVideoGrabber cam;

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

  // サイズ定義（Webカメラ入力の解像度。動画モードの解像度とは別物）
  const int W = 1920;
  const int H = 1080;
};
