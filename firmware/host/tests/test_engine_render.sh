#!/bin/bash
# tests/test_engine_render.sh  (run from firmware/host/)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
make render
mkdir -p "$ROOT/artifacts/cpp"
./render "$ROOT/dbscreamz_lab/static/audio/guitar_long.wav" "$ROOT/artifacts/cpp/Wukong.wav" \
  --character Wukong --trace "$ROOT/artifacts/cpp/Wukong_trace.csv"
python3 "$ROOT/tools/compare_metrics.py" \
  "$ROOT/artifacts/ref/Wukong.wav" "$ROOT/artifacts/cpp/Wukong.wav" \
  --trace-a "$ROOT/artifacts/ref/Wukong_trace.csv" \
  --trace-b "$ROOT/artifacts/cpp/Wukong_trace.csv"
echo "test_engine_render OK"
