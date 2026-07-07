#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 input.yuv width height output_pattern"
    echo "Example: $0 output/test10.yuv 640 480 output/frame_%03d.jpg"
    exit 1
fi

ffmpeg -f rawvideo -pixel_format yuyv422 -video_size "$2x$3" -i "$1" "$4"
