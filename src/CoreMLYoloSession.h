#pragma once

#include <opencv2/core.hpp>
#include <memory>
#include <string>
#include <vector>

// ============================================
// YOLO11-seg を Core ML で直接動かすセッション。
//
// ONNX Runtime の CoreML EP 経由だと Neural Engine (ANE) は約20ms かかったが、
// Core ML 用に書き出したモデル (.mlmodelc, FP16) を直接読むと
// M5 の ANE で約2ms になる。
//
// - 入力: inputSize x inputSize の RGB 8UC3 (レターボックス済み)
// - 出力: ONNX版と同じ形の float32 テンソル2つ
//     [1, 4+nc+32, N] (検出) と [1, 32, H, W] (プロトタイプマスク)
//   なので PersonSegmenter のデコード処理はそのまま使える。
//
// computeUnits: "CPUAndNeuralEngine" / "CPUAndGPU" / "ALL" / "CPUOnly"
// ============================================
class CoreMLYoloSession {
public:
  CoreMLYoloSession(const std::string &modelPath, int inputSize,
                    const std::string &computeUnits);
  ~CoreMLYoloSession();

  std::vector<cv::Mat> run(const cv::Mat &rgbLetterboxed);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
