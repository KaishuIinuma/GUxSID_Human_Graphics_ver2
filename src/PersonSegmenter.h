#pragma once

#include "ofMain.h"
#include <opencv2/dnn.hpp>
#include "core/HumanContourData.h"

// ============================================
// YOLO11-seg (Ultralytics) のONNXモデルを使って、
// 「人物ごとに個別の輪郭」を検出するクラス。
//
// MediaPipe Selfie Segmentationとの違い:
//   - 旧実装は「人物領域」を1枚の二値マスクとして塗るだけだったため、
//     複数人が重なると輪郭がくっついて1つの塊になっていた。
//   - YOLO11-segはインスタンスセグメンテーションなので、
//     検出した人物1人ずつに専用のマスク・輪郭が得られる。
//     -> 重なっていても個別のHumanContourDataとして扱える。
//   - COCOなど実世界の多様な写真で学習されているため、
//     暗所や高感度ノイズのある映像への汎化も期待できる。
//
// ============================================
// ★前提（このクラスが仮定しているモデル仕様）
// ============================================
//   Ultralyticsの `yolo export model=yolo11n-seg.pt format=onnx imgsz=640`
//   で書き出した標準的なYOLOv8/YOLO11-segのONNXモデルを想定。
//
//   - 入力: [1, 3, 640, 640] (RGB, 0.0〜1.0に正規化, レターボックス済み)
//   - 出力0 (検出): [1, 4+nc+32, 8400]
//       行方向: [cx, cy, w, h, class1〜classNのスコア, mask係数32個]
//       (cx,cy,w,hは640入力空間でのピクセル座標。クラススコアは0〜1)
//   - 出力1 (プロトタイプマスク): [1, 32, 160, 160]
//
//   nc（クラス数）はモデルによって異なる(COCO=80、personのみ再学習なら1)ため、
//   出力0の形状から自動計算する。personClassIdは学習データのクラス順に
//   合わせて指定すること（COCO学習済みモデルなら0=person）。
//
//   ★注意: ここで実装しているデコード処理は一般的なYOLOv8/11-seg export
//   フォーマットに基づいた実装だが、Ultralyticsのバージョンやexportオプション
//   (--nms有無、opset等)によって出力テンソルの詳細が変わることがある。
//   実際に読み込んだモデルで動作確認・チューニングが必要。
// ============================================
class PersonSegmenter {
public:
  // modelPath: ONNXファイルのパス
  // inputSizeArg: モデルの入力解像度（Ultralyticsのデフォルトは640）
  bool loadModel(const std::string &modelPath, int inputSizeArg =64);
  bool loadClassicModel(const std::string &modelPath);
  void setClassic(bool enabled) { classic = enabled; }
  bool isClassic() const { return classic; }
  bool isLoaded() const { return classic ? classicLoaded : loaded; }

  // rgbFrame: 8UC3のRGB画像（元解像度のまま渡してよい。内部でレターボックスする）
  // outputWidth / outputHeight: 結果の輪郭座標をこの座標系にスケールして返す
  //   (ofGetWidth()/ofGetHeight()を渡せば、そのままScene側にわたせる)
  HumanContourData detect(
      const cv::Mat &rgbFrame,
      int outputWidth,
      int outputHeight
  );

  // ============================================
  // チューニング用パラメータ（ofApp側のGUIから変更する想定）
  // ============================================
  float confThreshold = 0.6;   // クラス信頼度のしきい値 (0.0〜1.0)
  float aspectRatioPercent = 0.0f; // 検出前の元フレームの縦横比変化率 (%)
  float nmsThreshold = 0.45f;   // NMS(重複検出除去)のIoUしきい値
  float maskThreshold = 0.5f;   // インスタンスマスクの二値化しきい値
  int personClassId = 0;        // "person"クラスのID（COCO学習済みなら0）
  int maxDetections = 20;       // 1フレームあたりの最大検出人数（暴走防止）
  double minContourArea = 500.0; // これより小さい輪郭はノイズとして除外(出力解像度基準)

private:
  cv::dnn::Net net;
  cv::dnn::Net classicNet;
  bool loaded = false;
  bool classicLoaded = false;
  bool classic = false;
  int inputSize = 640;
  HumanContourData detectClassic(const cv::Mat &rgbFrame,
                                 int outputWidth, int outputHeight);

  struct Detection {
    cv::Rect2f box;                // 640入力空間でのバウンディングボックス(x,y,w,h)
    float confidence = 0.0f;
    std::vector<float> maskCoeffs; // マスク係数（プロトタイプ数と同じ次元、通常32）
  };

  // 元画像をinputSize x inputSizeへレターボックス(アスペクト比維持+パディング)する。
  // scale: 元画像→レターボックス画像への縮小率
  // padX/padY: パディング量(片側)
  cv::Mat letterbox(const cv::Mat &src, float &scale, int &padX, int &padY) const;
};
