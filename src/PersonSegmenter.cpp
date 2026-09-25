#include "PersonSegmenter.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

//--------------------------------------------------------------
bool PersonSegmenter::loadModel(const std::string &modelPath, int inputSizeArg) {
  inputSize = inputSizeArg;

  try {
    net = cv::dnn::readNetFromONNX(modelPath);
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    loaded = !net.empty();

    if (loaded) {
      ofLogNotice("PersonSegmenter") << "YOLOモデルの読み込みに成功しました: " << modelPath;
    } else {
      ofLogError("PersonSegmenter") << "YOLOモデルの読み込みに失敗しました: " << modelPath;
    }
  } catch (const cv::Exception &e) {
    ofLogError("PersonSegmenter") << "OpenCV DNN Exception: " << e.what();
    loaded = false;
  }

  return loaded;
}

//--------------------------------------------------------------
bool PersonSegmenter::loadClassicModel(const std::string &modelPath) {
  try {
    classicNet = cv::dnn::readNetFromONNX(modelPath);
    classicNet.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    classicNet.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    classicLoaded = !classicNet.empty();
  } catch (const cv::Exception &e) {
    ofLogError("PersonSegmenter") << "Classic model error: " << e.what();
    classicLoaded = false;
  }
  if (classicLoaded) {
    ofLogNotice("PersonSegmenter") << "Classic model loaded: " << modelPath;
  } else {
    ofLogError("PersonSegmenter") << "Classic model failed to load: " << modelPath;
  }
  return classicLoaded;
}

//--------------------------------------------------------------
cv::Mat PersonSegmenter::letterbox(const cv::Mat &src, float &scale, int &padX, int &padY) const {
  const int srcW = src.cols;
  const int srcH = src.rows;

  scale = std::min(static_cast<float>(inputSize) / srcW, static_cast<float>(inputSize) / srcH);

  const int newW = std::max(1, static_cast<int>(std::round(srcW * scale)));
  const int newH = std::max(1, static_cast<int>(std::round(srcH * scale)));

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(newW, newH));

  padX = (inputSize - newW) / 2;
  padY = (inputSize - newH) / 2;

  // YOLOの慣例に合わせてグレー(114,114,114)でパディングする
  cv::Mat out(inputSize, inputSize, src.type(), cv::Scalar(114, 114, 114));
  resized.copyTo(out(cv::Rect(padX, padY, newW, newH)));
  return out;
}

//--------------------------------------------------------------
HumanContourData PersonSegmenter::detect(const cv::Mat &rgbFrame, int outputWidth, int outputHeight) {
  HumanContourData result;

  if (!isLoaded() || rgbFrame.empty() || outputWidth <= 0 || outputHeight <= 0) {
    return result;
  }

  const int srcW = rgbFrame.cols;
  const int srcH = rgbFrame.rows;

  float scale = 1.0f;
  int padX = 0, padY = 0;
  // 元のRGBフレームを伸縮してからYOLOへ渡す。頂点数調整・Offset・描画の
  // いずれよりも前に適用する。0%では従来の入力をそのまま使う。
  cv::Mat stretchedFrame;
  const cv::Mat* detectionFrame = &rgbFrame;
  if (aspectRatioPercent != 0.0f) {
    const float ratio =
        1.0f + std::clamp(aspectRatioPercent, -50.0f, 50.0f) * 0.01f;
    const float horizontalScale = std::sqrt(ratio);
    const float verticalScale = 1.0f / horizontalScale;
    const float centerX = 0.5f * static_cast<float>(srcW - 1);
    const float centerY = 0.5f * static_cast<float>(srcH - 1);
    const cv::Matx23f transform(
        horizontalScale, 0.0f, (1.0f - horizontalScale) * centerX,
        0.0f, verticalScale, (1.0f - verticalScale) * centerY);
    cv::warpAffine(rgbFrame, stretchedFrame, transform, rgbFrame.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(114, 114, 114));
    detectionFrame = &stretchedFrame;
  }
  if (classic) {
    return detectClassic(*detectionFrame, outputWidth, outputHeight);
  }
  cv::Mat letterboxed = letterbox(*detectionFrame, scale, padX, padY);

  // 0〜1正規化してNCHW形式のblobを作成（入力は既にRGB前提なのでswapRB=false）
  cv::Mat blob = cv::dnn::blobFromImage(
      letterboxed, 1.0 / 255.0, cv::Size(inputSize, inputSize),
      cv::Scalar(0, 0, 0), false, false);
  net.setInput(blob);

  std::vector<std::string> outNames = net.getUnconnectedOutLayersNames();
  std::vector<cv::Mat> outs;
  net.forward(outs, outNames);

  if (outs.size() < 2) {
    ofLogError("PersonSegmenter") << "想定外の出力数です(" << outs.size()
                                   << "個)。YOLO-segモデルではない可能性があります。";
    return result;
  }

  // 出力を次元数で判別する
  // (検出テンソル = 3次元[1,C,N] / プロトタイプマスク = 4次元[1,32,H,W])
  cv::Mat detOut, protoOut;
  for (auto &o : outs) {
    if (o.dims == 3 && detOut.empty()) {
      detOut = o;
    } else if (o.dims == 4 && protoOut.empty()) {
      protoOut = o;
    }
  }
  if (detOut.empty() || protoOut.empty()) {
    ofLogError("PersonSegmenter") << "検出テンソル/プロトタイプマスクの判別に失敗しました。"
                                   << "モデルの出力形式を確認してください。";
    return result;
  }

  const int C = detOut.size[1];
  const int N = detOut.size[2];
  const int protoC = protoOut.size[1]; // 通常32
  const int protoH = protoOut.size[2]; // 通常160
  const int protoW = protoOut.size[3]; // 通常160
  const int nc = C - 4 - protoC;       // クラス数を逆算

  if (nc <= 0) {
    ofLogError("PersonSegmenter") << "出力形状からクラス数を算出できませんでした (C=" << C << ")";
    return result;
  }

  const float *data = reinterpret_cast<const float *>(detOut.data);

  // ============================================
  // 1. 候補ボックスの抽出（クラス=person かつ 信頼度がしきい値以上のみ）
  // ============================================
  std::vector<Detection> candidates;
  std::vector<cv::Rect> boxesForNms;
  std::vector<float> scoresForNms;

  for (int n = 0; n < N; n++) {
    const float cx = data[0 * N + n];
    const float cy = data[1 * N + n];
    const float w  = data[2 * N + n];
    const float h  = data[3 * N + n];

    int bestClass = -1;
    float bestScore = 0.0f;
    for (int c = 0; c < nc; c++) {
      const float s = data[(4 + c) * N + n];
      if (s > bestScore) {
        bestScore = s;
        bestClass = c;
      }
    }

    if (bestClass != personClassId || bestScore < confThreshold) {
      continue;
    }

    Detection det;
    det.box = cv::Rect2f(cx - w / 2.0f, cy - h / 2.0f, w, h);
    det.confidence = bestScore;
    det.maskCoeffs.resize(protoC);
    for (int k = 0; k < protoC; k++) {
      det.maskCoeffs[k] = data[(4 + nc + k) * N + n];
    }

    boxesForNms.emplace_back(
        static_cast<int>(det.box.x), static_cast<int>(det.box.y),
        static_cast<int>(det.box.width), static_cast<int>(det.box.height));
    scoresForNms.push_back(det.confidence);
    candidates.push_back(std::move(det));
  }

  if (candidates.empty()) {
    return result;
  }

  // ============================================
  // 2. NMSで重複検出を除去
  // ============================================
  std::vector<int> keepIndices;
  cv::dnn::NMSBoxes(boxesForNms, scoresForNms, confThreshold, nmsThreshold, keepIndices);

  if (static_cast<int>(keepIndices.size()) > maxDetections) {
    std::sort(keepIndices.begin(), keepIndices.end(), [&](int a, int b) {
      return scoresForNms[a] > scoresForNms[b];
    });
    keepIndices.resize(maxDetections);
  }

  // プロトタイプマスクを [protoC, protoH*protoW] の2D行列として扱う
  cv::Mat protoMat(protoC, protoH * protoW, CV_32F, protoOut.data);

  // 640入力空間 -> 元画像(rgbFrame)空間 -> 出力(output)空間への変換スケール
  const float outScaleX = static_cast<float>(outputWidth) / static_cast<float>(srcW);
  const float outScaleY = static_cast<float>(outputHeight) / static_cast<float>(srcH);
  const float protoScaleX = static_cast<float>(protoW) / static_cast<float>(inputSize);
  const float protoScaleY = static_cast<float>(protoH) / static_cast<float>(inputSize);

  // ============================================
  // 3. 検出ごとにマスクを合成し、輪郭を抽出する
  // ============================================
  for (int idx : keepIndices) {
    const Detection &det = candidates[idx];

    // マスク係数 × プロトタイプ -> 低解像度(160x160)マスク
    cv::Mat coefMat(1, protoC, CV_32F, const_cast<float *>(det.maskCoeffs.data()));
    cv::Mat maskLowRes = coefMat * protoMat;      // [1, protoH*protoW]
    maskLowRes = maskLowRes.reshape(1, protoH);   // [protoH, protoW]

    // シグモイドで0〜1に正規化
    cv::Mat maskProb;
    cv::exp(-maskLowRes, maskProb);
    maskProb = 1.0 / (1.0 + maskProb);

    // boxに対応するプロトタイプ空間の矩形を切り出す
    cv::Rect protoRect(
        static_cast<int>(det.box.x * protoScaleX),
        static_cast<int>(det.box.y * protoScaleY),
        std::max(1, static_cast<int>(det.box.width * protoScaleX)),
        std::max(1, static_cast<int>(det.box.height * protoScaleY)));
    protoRect &= cv::Rect(0, 0, protoW, protoH);
    if (protoRect.width <= 0 || protoRect.height <= 0) continue;

    cv::Mat maskCrop = maskProb(protoRect);

    const int boxPixelW = std::max(1, static_cast<int>(std::round(det.box.width)));
    const int boxPixelH = std::max(1, static_cast<int>(std::round(det.box.height)));

    cv::Mat maskResized;
    cv::resize(maskCrop, maskResized, cv::Size(boxPixelW, boxPixelH));

    cv::Mat maskBinary;
    cv::threshold(maskResized, maskBinary, maskThreshold, 255.0, cv::THRESH_BINARY);
    maskBinary.convertTo(maskBinary, CV_8UC1);

    // box内ローカル座標で輪郭抽出
    std::vector<std::vector<cv::Point>> localContours;
    cv::findContours(maskBinary, localContours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (localContours.empty()) continue;

    // 最も面積の大きい輪郭を採用（マスクのノイズによる小穴を無視するため）
    size_t largestIdx = 0;
    double largestArea = 0.0;
    for (size_t i = 0; i < localContours.size(); i++) {
      const double area = cv::contourArea(localContours[i]);
      if (area > largestArea) {
        largestArea = area;
        largestIdx = i;
      }
    }

    const auto &localContour = localContours[largestIdx];
    if (localContour.size() < 3) continue;

    // 出力空間での面積換算が小さすぎるものはノイズとして除外
    const double outputAreaApprox = largestArea * (outScaleX / scale) * (outScaleY / scale);
    if (outputAreaApprox < minContourArea) continue;

    // ローカル座標(box内) -> 640入力空間 -> 元画像空間 -> 出力空間 へ変換
    ofPolyline poly;
    vector<glm::vec2> pts;
    pts.reserve(localContour.size());

    for (const auto &lp : localContour) {
      const float x640 = det.box.x + lp.x;
      const float y640 = det.box.y + lp.y;

      const float xSrc = (x640 - padX) / scale;
      const float ySrc = (y640 - padY) / scale;

      glm::vec2 p(xSrc * outScaleX, ySrc * outScaleY);
      poly.addVertex(p.x, p.y);
      pts.push_back(p);
    }

    poly.close();
    poly = poly.getSmoothed(2); // マスク由来のガタついた輪郭を滑らかにする

    result.contours.push_back(poly);
    result.contourPoints.push_back(pts);

    // 重心（矩形中心を採用。単純な頂点平均より安定する）
    const float cxSrc = (det.box.x + det.box.width / 2.0f - padX) / scale;
    const float cySrc = (det.box.y + det.box.height / 2.0f - padY) / scale;
    result.centroids.push_back(glm::vec2(cxSrc * outScaleX, cySrc * outScaleY));

    // 外接矩形
    const float bx = (det.box.x - padX) / scale * outScaleX;
    const float by = (det.box.y - padY) / scale * outScaleY;
    const float bw = det.box.width / scale * outScaleX;
    const float bh = det.box.height / scale * outScaleY;
    result.boundingBoxes.push_back(ofRectangle(bx, by, bw, bh));
  }

  result.numHumans = static_cast<int>(result.contours.size());
  return result;
}

//--------------------------------------------------------------
HumanContourData PersonSegmenter::detectClassic(const cv::Mat &rgbFrame,
                                                int outputWidth,
                                                int outputHeight) {
  HumanContourData result;
  // selfie_segmentation.onnx: RGB [1,3,256,256] -> alpha [1,1,256,256].
  // The original MediaPipe graph resizes without letterboxing and uses 0..1 RGB.
  cv::Mat blob = cv::dnn::blobFromImage(
      rgbFrame, 1.0 / 255.0, cv::Size(256, 256),
      cv::Scalar(0, 0, 0), false, false);
  classicNet.setInput(blob);
  cv::Mat alpha = classicNet.forward();
  if (alpha.dims != 4 || alpha.size[0] != 1 || alpha.size[1] != 1 ||
      alpha.size[2] != 256 || alpha.size[3] != 256 || alpha.type() != CV_32F) {
    ofLogError("PersonSegmenter") << "Unexpected Classic model output";
    return result;
  }

  cv::Mat alpha256(256, 256, CV_32F, alpha.ptr<float>());
  cv::Mat alphaSource;
  cv::resize(alpha256, alphaSource, rgbFrame.size(), 0, 0, cv::INTER_LINEAR);
  cv::Mat mask;
  cv::threshold(alphaSource, mask, confThreshold, 255.0, cv::THRESH_BINARY);
  mask.convertTo(mask, CV_8U);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  const float scaleX = static_cast<float>(outputWidth) / rgbFrame.cols;
  const float scaleY = static_cast<float>(outputHeight) / rgbFrame.rows;
  for (const auto &contour : contours) {
    if (contour.size() < 3 ||
        cv::contourArea(contour) * scaleX * scaleY < minContourArea) {
      continue;
    }
    std::vector<glm::vec2> points;
    points.reserve(contour.size());
    ofPolyline poly;
    for (const auto &point : contour) {
      glm::vec2 scaled(point.x * scaleX, point.y * scaleY);
      points.push_back(scaled);
      poly.addVertex(scaled.x, scaled.y);
    }
    poly.close();
    result.contours.push_back(poly.getSmoothed(2));
    result.contourPoints.push_back(std::move(points));

    const cv::Moments moments = cv::moments(contour);
    const cv::Rect bounds = cv::boundingRect(contour);
    const float centerX = moments.m00 > 0.0
        ? static_cast<float>(moments.m10 / moments.m00)
        : bounds.x + bounds.width * 0.5f;
    const float centerY = moments.m00 > 0.0
        ? static_cast<float>(moments.m01 / moments.m00)
        : bounds.y + bounds.height * 0.5f;
    result.centroids.emplace_back(centerX * scaleX, centerY * scaleY);
    result.boundingBoxes.emplace_back(bounds.x * scaleX, bounds.y * scaleY,
                                      bounds.width * scaleX,
                                      bounds.height * scaleY);
  }
  result.numHumans = static_cast<int>(result.contours.size());
  return result;
}
