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
