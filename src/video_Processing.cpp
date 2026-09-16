#include "video_Processing.h"
#include <cmath>

//--------------------------------------------------------------
void VideoProcessing::setup(PersonSegmenter *segmenter) {
  segmenterPtr = segmenter;
}

//--------------------------------------------------------------
bool VideoProcessing::loadVideo(const std::string &path) {
  bool ok = videoPlayer.load(path);
  if (!ok) {
    ofLogError("VideoProcessing") << "動画の読み込みに失敗しました: " << path;
    loaded = false;
    return false;
  }

  videoWidth = videoPlayer.getWidth();
  videoHeight = videoPlayer.getHeight();
  loadedFileName = path;

  allocateBuffers(videoWidth, videoHeight);

  // 前の動画の輪郭データが一瞬でも表示されないようクリアしておく
  humanData = HumanContourData();

  videoPlayer.setLoopState(OF_LOOP_NORMAL);
  videoPlayer.play();
  playing = true;
  loaded = true;
  lastProcessedSampleIndex = -1;

  ofLogNotice("VideoProcessing") << "動画を読み込みました: " << path
                                  << " (" << videoWidth << "x" << videoHeight << ")";
  return true;
}

//--------------------------------------------------------------
void VideoProcessing::allocateBuffers(int w, int h) {
  colorImg.allocate(w, h);
}

//--------------------------------------------------------------
void VideoProcessing::play() {
  if (!loaded) return;
  videoPlayer.setPaused(false);
  playing = true;
  lastProcessedSampleIndex = -1;
}

//--------------------------------------------------------------
void VideoProcessing::pause() {
  if (!loaded) return;
  videoPlayer.setPaused(true);
  playing = false;
}

//--------------------------------------------------------------
void VideoProcessing::restart() {
  if (!loaded) return;
  videoPlayer.setPosition(0.0f);
  videoPlayer.play();
  videoPlayer.setPaused(false);
  playing = true;
  lastProcessedSampleIndex = -1;
}

//--------------------------------------------------------------
void VideoProcessing::update() {
  if (!loaded) return;

  videoPlayer.update();

  if (!playing) return;

  if (!videoPlayer.isFrameNew()) return;

  // 実時間を待つのではなく、動画のフレーム番号で検出対象を決める。
  // 30fps動画を5fpsに設定した場合、約6フレームごとにだけ処理するため、
  // 動画の再生時間・再生速度は変わらない。
  const float targetFps = processFps > 0.0f ? processFps : 30.0f;
  const float durationSeconds = videoPlayer.getDuration();
  const int totalFrames = videoPlayer.getTotalNumFrames();
  const float sourceFps = (durationSeconds > 0.0f && totalFrames > 0)
                              ? static_cast<float>(totalFrames) / durationSeconds
                              : 30.0f;
  const int currentFrame = std::max(0, videoPlayer.getCurrentFrame());
  const int64_t sampleIndex = static_cast<int64_t>(std::floor(
      static_cast<double>(currentFrame) * targetFps / sourceFps));

  if (sampleIndex != lastProcessedSampleIndex) {
    processCurrentFrame();
    lastProcessedSampleIndex = sampleIndex;
  }
}

//--------------------------------------------------------------
void VideoProcessing::processCurrentFrame() {
  if (!segmenterPtr || !segmenterPtr->isLoaded()) return;
  if (videoWidth <= 0 || videoHeight <= 0) return;

  // ============================================
  // ★変更: MediaPipeの二値化+輪郭抽出処理から、
  //   PersonSegmenter(YOLO11-seg)へのdetect()呼び出し一発に簡素化。
  //   人物ごとの輪郭・重心・外接矩形をまとめて返してくれる。
  // ============================================
  colorImg.setFromPixels(videoPlayer.getPixels());

  cv::Mat rgbMat(videoHeight, videoWidth, CV_8UC3, colorImg.getPixels().getData());

  humanData = segmenterPtr->detect(rgbMat, ofGetWidth(), ofGetHeight());
}
