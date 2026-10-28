#!/usr/bin/env bash
# 在 i.MX6ULL 上把 USB Host 的 UVC 摄像头推到 MediaMTX。
#
# 用法:
#   ./stream.sh rtsp://<服务器IP>:8554/live
#   RTSP_URL=rtsp://<服务器IP>:8554/live ./stream.sh
#
# 可复现参数（默认）: /dev/video1，读取 YUYV 640x480、10 fps，
# 编成 yuvj420p MJPEG（标准 Huffman 表）后走 RTSP/TCP。
# 这块摄像头的 MJPEG 模式没有帧，不能 -c:v copy。
# 可选环境变量: VIDEO_DEVICE、VIDEO_SIZE、VIDEO_FPS、RTSP_TRANSPORT、RTSP_URL。
# 试 UDP 时服务器 mediamtx.yml 也要允许 udp，再执行:
#   RTSP_TRANSPORT=udp ./stream.sh rtsp://<服务器IP>:8554/live

set -euo pipefail

DEVICE="${VIDEO_DEVICE:-/dev/video1}"
SIZE="${VIDEO_SIZE:-640x480}"
FPS="${VIDEO_FPS:-10}"
TRANSPORT="${RTSP_TRANSPORT:-tcp}"

URL="${1:-}"
if [[ -z "${URL}" ]]; then
    URL="${RTSP_URL:-}"
fi

if [[ -z "${URL}" ]]; then
    echo "错误: 没有推流地址。" >&2
    echo "用法: $0 rtsp://<服务器IP>:8554/live" >&2
    echo "或:   RTSP_URL=rtsp://<服务器IP>:8554/live $0" >&2
    exit 1
fi

if [[ ! -e "${DEVICE}" ]]; then
    echo "错误: 找不到摄像头设备节点 ${DEVICE}。" >&2
    echo "请确认 UVC 摄像头已直接插在开发板的 USB Host 上。" >&2
    echo "插好后执行 v4l2-ctl --list-devices，确认节点是 /dev/video1。" >&2
    exit 1
fi

if [[ ! -r "${DEVICE}" ]]; then
    echo "错误: 设备 ${DEVICE} 存在，但当前用户读不了。" >&2
    echo "把用户加入 video 组后重新登录: sudo usermod -aG video \"\$USER\"" >&2
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "错误: 找不到 ffmpeg。请先在开发板上安装 ffmpeg。" >&2
    exit 1
fi

echo "推流 ${DEVICE} (yuyv422 ${SIZE} @ ${FPS}fps -> mjpeg, ${TRANSPORT}) -> ${URL}"

# loglevel=error：量化表提示是 warning，会按帧刷屏。连接失败仍会打出 error。
exec ffmpeg -hide_banner -loglevel error \
    -f v4l2 \
    -input_format yuyv422 \
    -framerate "${FPS}" \
    -video_size "${SIZE}" \
    -i "${DEVICE}" \
    -an \
    -c:v mjpeg \
    -pix_fmt yuvj420p \
    -huffman default \
    -q:v 8 \
    -muxdelay 0 \
    -muxpreload 0 \
    -f rtsp \
    -rtsp_transport "${TRANSPORT}" \
    "${URL}"
