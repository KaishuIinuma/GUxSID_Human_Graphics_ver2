#!/bin/zsh
# YOLO11n-seg を Core ML (FP16, NMSなし) に書き出し、bin/data/ にコンパイル済み
# .mlmodelc を置く。PersonSegmenter は yolo11n-seg.onnx と同じ名前の
# .mlmodelc があれば、それを Neural Engine で直接動かす。
#
# 必要なもの: ultralytics, coremltools (pip install ultralytics coremltools)
# 使い方: tools/export_yolo_coreml.sh [weights.pt] [imgsz]
#   例) tools/export_yolo_coreml.sh ~/yolo11n-seg.pt 640
#   320 にする場合の出力名は yolo11n-seg-320.mlmodelc (GUI の YOLO Input 320 用)
set -euo pipefail
PROJECT_DIR=${0:A:h:h}
WEIGHTS=${1:-$HOME/yolo11n-seg.pt}
IMGSZ=${2:-640}
NAME=yolo11n-seg
[[ $IMGSZ == 640 ]] || NAME=yolo11n-seg-$IMGSZ

WORK=$(mktemp -d)
cp "$WEIGHTS" "$WORK/model.pt"
(cd "$WORK" && python3 -c "
from ultralytics import YOLO
YOLO('model.pt').export(format='coreml', imgsz=$IMGSZ, half=True, nms=False)
")
rm -rf "$WORK/$NAME.mlmodelc" "$PROJECT_DIR/bin/data/$NAME.mlmodelc"
xcrun coremlcompiler compile "$WORK/model.mlpackage" "$WORK"
mv "$WORK/model.mlmodelc" "$PROJECT_DIR/bin/data/$NAME.mlmodelc"
rm -rf "$WORK"
echo "Wrote $PROJECT_DIR/bin/data/$NAME.mlmodelc"
