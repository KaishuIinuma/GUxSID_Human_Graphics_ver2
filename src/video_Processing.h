#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"
#include "core/HumanContourData.h"
#include "PersonSegmenter.h"

// ============================================
// 動画ファイル(またはjpeg/pngの静止画)を使ったオフライン処理
// (Control WindowのRealtimeチェックボックスがfalseのときに使用)
// 静止画は1フレームだけの動画として扱い、再生操作は無視する。
//
// ofAppが読み込み済みのPersonSegmenter(YOLO11-seg)を
// ポインタで参照するだけで、モデルの二重読み込みは行わない。
// カメラ入力(ofApp::update内の処理)とまったく同じ
// 「PersonSegmenter::detect()を呼ぶだけ」の流れを動画フレームに対して行い、
// 結果を ofApp::humanData と同じ形式(HumanContourData)で保持する。
// ============================================
class VideoProcessing {
public:
  // segmenterPtr は ofApp 側の実体(PersonSegmenter)を指すポインタ。
  void setup(PersonSegmenter *segmenterPtr);

  // 毎フレーム ofApp::update() から呼び出す。
  // processFps に応じて、必要なタイミングだけ
  // 新しいフレームのセグメンテーション処理を行う。
  void update();
  void setOutputView(int width, int height, float cropX, float cropY);

  // 動画ファイルまたは静止画(jpeg/png)をロードする。成功したら true を返す。
  // 動画は成功時に自動的に再生開始する。
  bool loadVideo(const std::string &path);
  static bool isStillImagePath(const std::string &path);

  // Control Windowの再生・一時停止・最初から再生ボタンから呼ばれる
  void play();
  void pause();
  void restart();
  void requestReprocess();
  void setPlaybackSpeed(float speed);

  bool isLoaded() const { return loaded; }
  bool isVideoPlaying() const { return playing; }
  bool isStillImage() const { return stillMode; }

  // 現在表示しているフレーム(動画の現フレームまたは静止画)。
  const ofTexture& getTexture() const;
  const ofPixels& getPixels() const;

  int getVideoWidth() const { return videoWidth; }
  int getVideoHeight() const { return videoHeight; }
  std::string getLoadedFileName() const { return loadedFileName; }

  // 処理するfps（Control Windowのfpsバーで変更可能。1〜60）
  // ※動画そのものの再生フレームレートではなく、
  //   「セグメンテーション処理を何fpsで行うか」を決める値。
  float processFps = 30.0f;

  // 動画の再生速度。Controlsから設定し、再生操作後も維持する。
  float playbackSpeed = 1.0f;

  // 出力: 人物輪郭データ（ofApp::humanDataと全く同じ形式）
  // Scene側はこれを渡された humanData と区別せずに使える
  HumanContourData humanData;

  // デバッグ描画や情報表示に使えるよう公開しておく
  ofVideoPlayer videoPlayer;

private:
  PersonSegmenter *segmenterPtr = nullptr;

  bool loaded = false;
  bool playing = true;
  bool stillMode = false;
  ofImage stillImage;

  int videoWidth = 0;
  int videoHeight = 0;
  std::string loadedFileName;

  ofxCvColorImage colorImg;

  // 最後にセグメンテーションした動画時間上のサンプル番号。
  // 実時間ではなく動画フレームを基準に間引くために使う。
  int64_t lastProcessedSampleIndex = -1;
  bool reprocessRequested = false;
  int outputWidth = 0;
  int outputHeight = 0;
  float cropPositionX = 0.5f;
  float cropPositionY = 0.5f;

  void allocateBuffers(int w, int h);
  void processCurrentFrame();
};
