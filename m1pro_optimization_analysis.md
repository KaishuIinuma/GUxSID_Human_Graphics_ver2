# M1 Pro パフォーマンス最適化分析

## 状況
- 開発機: M4 Mac
- 実行機: M1 Pro → **5FPS**（目標60FPS）
- ボトルネックは主に **YOLOの推論 (CPU only)** と **描画のループコスト**

## 実装済み最適化

| # | 内容 | ファイル | 期待効果 |
|---|------|---------|--------|
| ✅ 1 | YOLO推論を `std::async` で非同期化 | `ofApp.cpp / ofApp.h` | FPSが推論レイテンシから独立 |
| ✅ 2 | `ShapePainter::drawStroke` をバッチ `ofMesh` 化 | `ShapePainter.h` | 描画ドローコール削減 |

---
## ボトルネック一覧と優先度

### 🔴 最重要: YOLO推論が CPU のみ（実装で修正可能）

**場所:** [`PersonSegmenter.cpp` L12-13](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/PersonSegmenter.cpp#L12-L13)

```cpp
net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
```

M1 ProにはApple Neural Engine (ANE) と Metal GPU が搭載されているが、
OpenCV DNNはデフォルトのCPUで動いている。

**修正方針:**
- Apple Silicon ではCoreMLバックエンド (`DNN_BACKEND_COREML`) に切り替えると
  ANEまたはGPUを使える。ただしOpenCVのビルドにCoreMLサポートが必要。
- 代替: ONNXRuntimeをCoreML EPで動かす方が現実的で確実。
- 短期対策: `DNN_BACKEND_OPENCV` + `DNN_TARGET_CPU` のまま、
  推論スレッドを分離して描画をブロックしない（非同期化）。

---

### 🔴 重要: 推論がメインスレッドでブロッキング実行（実装で修正可能）

**場所:** [`ofApp.cpp` L1719-1728](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/ofApp.cpp#L1719-L1728)

```cpp
// メインスレッドでブロッキング実行 → 描画が止まる
humanData = personSegmenter.detect(rgbMat, ofGetWidth(), ofGetHeight());
```

`detect()` の処理時間（YOLO推論 + 後処理）が1フレーム分丸ごと使われる。
→ 推論を別スレッドで実行し、完了したら humanData を swap するだけにする。

**現在の構造:**
```
update() → detect() (重い、同期) → humanData更新 → draw()
```
**理想の構造:**
```
update() → 前フレームの humanData でそのまま描画
         → 別スレッドで detect() 実行中
         → 完了したら atomicにswap
```

---

### 🟡 中程度: ShapePainter のストローク描画が頂点数×ドローコール

**場所:** [`ShapePainter.h` L29-86](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/render/ShapePainter.h#L29-L86)

頂点数N のポリゴンに対して:
- セグメント矩形: N 回 `ofDrawRectangle` → 各々 `ofMesh` を生成・描画
- Round Join: N 回 `ofDrawCircle`

→ 頂点が 32個でも 32回の個別ドローコール。GPUに転送するオーバーヘッドが大きい。

**修正方針 (実装可能):**
全セグメントを1つの `ofMesh` にまとめてから1回の `mesh.draw()` で描画。

---

### 🟡 中程度: MaterialPainter/ShapePainter でのofPath使用

**場所:** [`ShapePainter.h` L15-22](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/render/ShapePainter.h#L15-L22), [`MaterialPainter.h` L68-75](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/material/MaterialPainter.h#L68-L75)

```cpp
void drawFill(...) {
    ofPath path;   // ← 毎フレーム、毎オブジェクトで生成・テッセレーション
    path.moveTo(...);
    ...
    path.draw();   // 内部でtessellation実行
}
```

`ofPath::draw()` は内部で tessellation (GLU) を毎回走らせる。
`SceneObject::geometry` が変化しないフレーム(pipelineSignatureで検知済み)では
**メッシュをキャッシュ**して使い回せる。

---

### 🟡 中程度: LooseContourProcessor が毎フレーム distanceTransform

**場所:** [`LooseContourProcessor.h` L57-82](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/geometry/LooseContourProcessor.h#L57-L82)

Loose Contourが有効な場合、`distanceTransform` × 2 + `GaussianBlur` + 全画素ループ を
毎フレーム実行している。ただし `pipelineSignature` でキャッシュされているため、
**Detection が更新された時だけ**実行される設計になっている → 現在は問題小。

---

### 🟠 提案のみ: YOLO入力解像度を下げる

**場所:** [`ofApp.cpp` L141](file:///Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/src/ofApp.cpp#L141)

```cpp
personSegmenter.loadModel(modelPath, 640);  // 640×640入力
```

`inputSize=320` に下げると推論時間が約4分の1になる。
精度は若干落ちるが、大人数の全身検出には十分な場合が多い。
→ GUI の `Vertex Count` と同様、GUIで変更できると便利。

---

### 🟠 提案のみ: yolo11n より小さいモデルへの切り替え

現在: `yolo11n-seg` (nano)
- さらに軽いモデルを使う選択肢はほぼないが、
  **personクラスのみに再学習したシングルクラスモデル**は、
  80クラスのCOCO版より後処理が速い (nc=1 → クラスループが1回)。

---

### 🟠 提案のみ: 別スレッドの推論 (std::async / ofThread)

上記「🔴 推論が同期ブロック」の修正案の詳細:

```cpp
// ofApp.h に追加
std::future<HumanContourData> detectionFuture;
HumanContourData latestHumanData;  // 完成したデータをここに保持

// ofApp::update() で
if (detectionFuture.valid() &&
    detectionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    latestHumanData = detectionFuture.get();
}
if (cam.isFrameNew() && !detectionFuture.valid()) {
    cv::Mat frame = ...;
    detectionFuture = std::async(std::launch::async,
        [this, frame]() { return personSegmenter.detect(frame, ...); });
}
```

注意: `cv::dnn::Net` はスレッドセーフではないため、
`PersonSegmenter` インスタンスを detect 専用スレッドに完全に渡す設計が必要。

---

## 実装で今すぐ対応できる項目

| 項目 | 期待効果 | 難易度 |
|------|---------|--------|
| ① ShapePainter::drawStroke をバッチ ofMesh 化 | 描画コスト -40~60% | ★★☆ |
| ② SolidFill を ofPath→ofMesh 直描きに変更 | テッセレーション排除 | ★★☆ |
| ③ detect() の非同期化 (std::async) | FPS が推論レイテンシから独立 | ★★★ |

---

## 提案のみ (コード変更なし)

| 項目 | 期待効果 |
|------|---------|
| A: YOLO入力解像度を640→320 (GUIで調整) | 推論 ~4倍速 |
| B: CoreML / ONNX Runtime CoreML EP 対応 | ANE使用で推論 ~10倍速 |
| C: yolo11n-seg をperson専用1クラスモデルに差し替え | 後処理高速化 |
| D: Realtimeの検出FPSをMain FPSと分離してGUIで制限 | 推論頻度を明示的に制御 |
