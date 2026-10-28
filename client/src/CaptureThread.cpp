#include "CaptureThread.h"

#include <opencv2/core/version.hpp>
#include <opencv2/videoio.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

CaptureThread::CaptureThread(QString url, QObject* parent)
    : QThread(parent), url_(std::move(url)) {
    qRegisterMetaType<cv::Mat>("cv::Mat");
}

void CaptureThread::requestStop() {
    stop_.store(true);
}

void CaptureThread::run() {
    const char* requested = std::getenv("LPR_RTSP_TRANSPORT");
    const char* transport = "tcp";
    if (requested != nullptr && std::strcmp(requested, "udp") == 0) {
        transport = "udp";
    }
    const std::string options = std::string("rtsp_transport;") + transport +
                                "|fflags;nobuffer|flags;low_delay|max_delay;500000";
    // 必须在 open 之前设置。OpenCV 用分号分隔的 key;value 传给 FFmpeg。
#if defined(_WIN32)
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", options.c_str());
#else
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", options.c_str(), 1);
#endif

    cv::VideoCapture cap;
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 3)
    cap.set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 8000);
    cap.set(cv::CAP_PROP_READ_TIMEOUT_MSEC, 3000);
#endif
    if (!cap.open(url_.toStdString(), cv::CAP_FFMPEG)) {
        emit errorOccurred(QStringLiteral("打不开 RTSP：") + url_);
        return;
    }
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    int emptyStreak = 0;
    while (!stop_.load()) {
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            if (stop_.load()) {
                break;
            }
            if (++emptyStreak >= 3) {
                emit errorOccurred(QStringLiteral("拉流中断，没有读到画面"));
                break;
            }
            continue;
        }
        emptyStreak = 0;
        // read() 可能复用内部缓冲。clone() 之后，下一帧覆盖原缓冲也不会碰到已经发出去的图。
        emit frameReady(frame.clone());
    }

    cap.release();
}
