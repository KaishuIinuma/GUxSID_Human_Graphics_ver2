# 引き継ぎメモ: ANE推論テスト & Webカメラ2台化 (2026-10-27)

## 背景
- 本番はビジュアライザーを2台のPCで動かす予定だったが、1台でWebカメラ2台を処理できるなら1台構成にしたい。
- 結論(検討済み): 技術的には可能。ただし現状のコードはカメラ1台前提なので改修が必要。

## 1台・2カメラ化の検討ポイント
- 負荷: YOLO11n-seg(CoreML)は別スレッド非同期推論。2台分でも30fps前後は見込めるが要実測。重ければGUIの「YOLO Input 320」。
- CPU: 1920x1080のフレーム取り込み・mirror・clone が2倍 → 推論よりこちらがボトルネックになる可能性。
- 描画: シーンを2つにするとGPU描画もほぼ2倍。
- 要確認: 本番の出力構成(プロジェクター2台か1画面合成か)と、使用Macが外部ディスプレイ2枚に対応するか(M1/M2/M3無印は1枚までの機種あり)。
- USB: 2台は別ポートに直挿し推奨。同型番カメラはデバイスIDの順番が入れ替わるため uniqueID で固定。
- 改修方針: ofApp の cam / colorImg / detectionFuture / humanGraphicsScene / tracker が単一。
  「カメラ→推論→トラッキング→シーン」をパイプライン構造体にまとめ2つ持たせる。
  推論セッションはカメラごとに持つか交互利用(ANE+GPUに振り分ける案もあり)。

## 今回の変更 (src/PersonSegmenter.cpp)
- MLComputeUnits を "CPUAndGPU" → 既定 "CPUAndNeuralEngine" に変更。
- 環境変数 GUXSID_COREML_UNITS で切替 (CPUAndNeuralEngine / CPUAndGPU / ALL / CPUOnly)。起動時にログ出力。
- 推論時間の平均を約2秒ごとにログ: "YOLO inference avg: X ms (N runs, input S)"。
  ※ static ローカル変数で集計(テスト用の簡易実装)。
- 既存の GUXSID_COREML_PROFILE=1 で演算ごとの割り当て先(ANE/GPU/CPU)を確認可能。
- 変更前のファイル: src/PersonSegmenter.cpp.bak_gpu
- VSCodeでビルド済み(バイナリに変更が入っていることは確認済み)。未実行・未計測。

## 次のステップ
1. ANEとGPUでそれぞれ実行し、ログを保存して比較する (bin/ で実行):
   ./GUxSID_Human_Graphics.app/Contents/MacOS/GUxSID_Human_Graphics 2>&1 | tee log_ane.txt
   GUXSID_COREML_UNITS=CPUAndGPU ./GUxSID_Human_Graphics.app/Contents/MacOS/GUxSID_Human_Graphics 2>&1 | tee log_gpu.txt
   (ANEは初回変換が遅いので2回目で比較。人が映った状態で30〜60秒)
2. 結果を見てデフォルトの計算デバイスを決める。
3. 2カメラ同時取り込み+fps/推論時間表示の検証版を作り、実機で計測する。
4. 問題なければ、本格的にパイプラインを2系統化する。

## 追記 (2026-10-27 午後): Core ML 直接実行で ANE を本採用
- 前回の比較 (ONNX Runtime の CoreML EP 経由) では ANE 20.6ms / GPU 7.9ms で GPU を既定にしていた。
- 遅かったのは ONNX Runtime 経由の変換が原因。Core ML 用に書き出したモデル (FP16) を
  Core ML から直接読むと、M5 で次のようになった (C++、入力コピーと FP16→FP32 変換込み):
  | 経路 | 1回 | 2カメラ並列 (2回分) |
  |---|---|---|
  | Core ML 直接 / ANE | 約2.0ms | 約2.9ms |
  | Core ML 直接 / GPU | 約3.1ms | 約4.3ms |
  | ONNX Runtime / GPU (旧既定) | 約7.9ms | - |
- 精度: bus.jpg で ONNX 版と同じ4人を検出。ボックスの差は数px、マスクの一致率 96〜98%。

### 変更点
- 新規 src/CoreMLYoloSession.h / .mm: Core ML で YOLO を動かすクラス。出力は ONNX 版と同じ形なので、デコード処理は共通。
- src/PersonSegmenter: yolo11n-seg.onnx と同じ名前の yolo11n-seg.mlmodelc が data/ にあれば Core ML (既定 ANE) を使う。無い・読めない時は従来の ONNX Runtime (GPU) に戻る。
  推論時間ログはカメラごと (#1/#2) に分けた。デバッグ表示に「AI backend」行を追加。
- bin/data/yolo11n-seg.mlmodelc: コンパイル済みモデル (5.8MB)。作り直しは tools/export_yolo_coreml.sh。
- リンク設定: Xcode プロジェクトと config.make に `-framework CoreML` を追加。

### 切り替え (環境変数)
- GUXSID_COREML_UNITS=CPUAndGPU など: 計算デバイスを変える (Core ML 直接・ONNX Runtime 共通)。
- GUXSID_YOLO_BACKEND=onnx: 旧経路 (ONNX Runtime) に戻す。

### 実カメラでの計測 (2026-10-27, 2系統とも MacBook Pro 内蔵カメラを共有, 約50秒)
- ログ: bin/log_ane_native.txt
- #1: 平均 5.2ms (最大 6.5ms) / #2: 平均 5.0ms (最大 6.3ms)、どちらも約30回/秒 (カメラのfps上限)。
- 前の GPU 版 (bin/log_ver2_dual.txt) は平均 10.6ms (最大 13.0ms)。約半分になった。
- 単体テストの約2ms より長いのは、2系統が同時に ANE を使うことと描画などの負荷のため。
- 検出人数・輪郭の見た目は確認していない (画面は見ていない)。

### 未確認
- 320 入力用モデルは ONNX 版も無いので未作成 (必要なら export スクリプトに 320 を渡す)。
