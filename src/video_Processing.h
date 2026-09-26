#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"
#include "core/HumanContourData.h"
#include "PersonSegmenter.h"

// ============================================
// 動画ファイルを使ったオフライン処理
// (Control WindowのRealtimeチェックボックスがfalseのときに使用)
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

  // 動画ファイルをロードする。読み込みに成功したら true を返す。
  // 成功時に videoPlayer は自動的に再生開始する。
  bool loadVideo(const std::string &path);

  // Control Windowの再生・一時停止・最初から再生ボタンから呼ばれる
  void play();
  void pause();
  void restart();
  void requestReprocess();
  void setPlaybackSpeed(float speed);

  bool isLoaded() const { return loaded; }
  bool isVideoPlaying() const { return playing; }

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

  int videoWidth = 0;
  int videoHeight = 0;
  std::string loadedFileName;

  ofxCvColorImage colorImg;

  // 最後にセグメンテーションした動画時間上のサンプル番号。
  // 実時間ではなく動画フレームを基準に間引くために使う。
  int64_t lastProcessedSampleIndex = -1;
  bool reprocessRequested = false;

  void allocateBuffers(int w, int h);
  void processCurrentFrame();
};
