#include "video_Processing.h"
#include "SourceCrop.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <opencv2/imgproc.hpp>

//--------------------------------------------------------------
void VideoProcessing::setup(PersonSegmenter *segmenter) {
  segmenterPtr = segmenter;
}

//--------------------------------------------------------------
bool VideoProcessing::isStillImagePath(const std::string &path) {
  std::string extension = ofFilePath::getFileExt(path);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return extension == "jpg" || extension == "jpeg" || extension == "png";
}

//--------------------------------------------------------------
bool VideoProcessing::loadVideo(const std::string &path) {
  if (isStillImagePath(path)) {
    if (!stillImage.load(path)) {
      ofLogError("VideoProcessing") << "画像の読み込みに失敗しました: " << path;
      loaded = false;
      return false;
    }
    videoPlayer.close();
    stillMode = true;
    videoWidth = stillImage.getWidth();
    videoHeight = stillImage.getHeight();
    loadedFileName = path;
    allocateBuffers(videoWidth, videoHeight);
    humanData = HumanContourData();
    // 静止画は読み込み直後(と表示範囲の変更時)に1回だけ検出する。
    playing = false;
    loaded = true;
    lastProcessedSampleIndex = -1;
    reprocessRequested = true;
    ofLogNotice("VideoProcessing") << "画像を読み込みました: " << path
                                    << " (" << videoWidth << "x" << videoHeight << ")";
    return true;
  }

  bool ok = videoPlayer.load(path);
  if (!ok) {
    ofLogError("VideoProcessing") << "動画の読み込みに失敗しました: " << path;
    loaded = false;
    return false;
  }

  stillMode = false;
  stillImage.clear();
  videoWidth = videoPlayer.getWidth();
  videoHeight = videoPlayer.getHeight();
  loadedFileName = path;

  // Videoモードは映像解析・描画専用。動画に含まれる音声は再生しない。
  videoPlayer.setVolume(0.0f);

  allocateBuffers(videoWidth, videoHeight);

  // 前の動画の輪郭データが一瞬でも表示されないようクリアしておく
  humanData = HumanContourData();

  videoPlayer.setLoopState(OF_LOOP_NORMAL);
  videoPlayer.setSpeed(playbackSpeed);
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
const ofTexture& VideoProcessing::getTexture() const {
  return stillMode ? stillImage.getTexture() : videoPlayer.getTexture();
}

//--------------------------------------------------------------
const ofPixels& VideoProcessing::getPixels() const {
  return stillMode ? stillImage.getPixels() : videoPlayer.getPixels();
}

//--------------------------------------------------------------
void VideoProcessing::play() {
  if (!loaded || stillMode) return;
  videoPlayer.setVolume(0.0f);
  videoPlayer.setSpeed(playbackSpeed);
  videoPlayer.setPaused(false);
  playing = true;
  lastProcessedSampleIndex = -1;
}

//--------------------------------------------------------------
void VideoProcessing::pause() {
  if (!loaded || stillMode) return;
  videoPlayer.setPaused(true);
  playing = false;
}

//--------------------------------------------------------------
void VideoProcessing::restart() {
  if (!loaded || stillMode) return;
  videoPlayer.setVolume(0.0f);
  videoPlayer.setPosition(0.0f);
  videoPlayer.play();
  videoPlayer.setSpeed(playbackSpeed);
  videoPlayer.setPaused(false);
  playing = true;
  lastProcessedSampleIndex = -1;
}

//--------------------------------------------------------------
void VideoProcessing::setPlaybackSpeed(float speed) {
  playbackSpeed = ofClamp(speed, 0.1f, 2.0f);
  if (!loaded || stillMode) return;

  videoPlayer.setSpeed(playbackSpeed);
  // AVFoundationのsetSpeedは一時停止中のプレイヤーを再開するため、
  // Controlsで速度だけ変えた場合は一時停止状態を保つ。
  if (!playing) videoPlayer.setPaused(true);
}

//--------------------------------------------------------------
void VideoProcessing::requestReprocess() {
  reprocessRequested = true;
}

void VideoProcessing::setOutputView(int width, int height, float cropX,
                                    float cropY) {
  if (outputWidth == width && outputHeight == height &&
      cropPositionX == cropX && cropPositionY == cropY) return;
  outputWidth = width;
  outputHeight = height;
  cropPositionX = cropX;
  cropPositionY = cropY;
  requestReprocess();
}

//--------------------------------------------------------------
void VideoProcessing::update() {
  if (!loaded) return;

  if (!stillMode) videoPlayer.update();

  if (reprocessRequested && getPixels().isAllocated()) {
    reprocessRequested = false;
    processCurrentFrame();
    // 再生中は次の新規フレームを通常のサンプル間隔で処理する。
    return;
  }

  if (!playing || stillMode) return;

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
  const ofPixels& framePixels = getPixels();
  if (!framePixels.isAllocated() ||
      framePixels.getWidth() != videoWidth ||
      framePixels.getHeight() != videoHeight) {
    return;
  }

  cv::Mat rgbMat;
  const int channels = framePixels.getNumChannels();
  if (channels == 4) {
    cv::Mat rgbaMat(videoHeight, videoWidth, CV_8UC4,
                    const_cast<unsigned char*>(framePixels.getData()));
    cv::cvtColor(rgbaMat, rgbMat, cv::COLOR_RGBA2RGB);
  } else if (channels == 3) {
    rgbMat = cv::Mat(videoHeight, videoWidth, CV_8UC3,
                     const_cast<unsigned char*>(framePixels.getData()));
  } else if (channels == 1) {
    cv::Mat grayMat(videoHeight, videoWidth, CV_8UC1,
                    const_cast<unsigned char*>(framePixels.getData()));
    cv::cvtColor(grayMat, rgbMat, cv::COLOR_GRAY2RGB);
  } else {
    ofLogError("VideoProcessing")
        << "未対応の動画ピクセル形式です: channels=" << channels;
    return;
  }

  const cv::Rect crop = sourceCrop(videoWidth, videoHeight, outputWidth,
                                    outputHeight, cropPositionX, cropPositionY);
  if (crop.empty()) return;
  humanData = segmenterPtr->detect(rgbMat(crop), outputWidth, outputHeight);
}
