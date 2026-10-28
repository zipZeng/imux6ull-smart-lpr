#include "DetectThread.h"

#include <QMutexLocker>

#include <chrono>
#include <utility>

DetectThread::DetectThread(QObject* parent) : QThread(parent) {
    qRegisterMetaType<LocateResult>("LocateResult");
}

void DetectThread::submit(cv::Mat frame) {
    QMutexLocker lock(&mutex_);
    if (stop_) {
        return;
    }
    pending_ = std::move(frame);
    hasFrame_ = true;
    cond_.wakeOne();
}

void DetectThread::requestStop() {
    QMutexLocker lock(&mutex_);
    stop_ = true;
    cond_.wakeOne();
}

void DetectThread::run() {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();

    while (true) {
        cv::Mat frame;
        {
            QMutexLocker lock(&mutex_);
            while (!stop_ && !hasFrame_) {
                cond_.wait(&mutex_);
            }
            if (stop_) {
                break;
            }
            while (!stop_ && clock::now() < next) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(next - clock::now()).count();
                if (left <= 0) {
                    break;
                }
                cond_.wait(&mutex_, static_cast<unsigned long>(left));
            }
            if (stop_) {
                break;
            }
            if (!hasFrame_) {
                continue;
            }
            frame = std::move(pending_);
            hasFrame_ = false;
            next = clock::now() + std::chrono::milliseconds(200);
        }

        if (frame.empty()) {
            continue;
        }

        try {
            emit detectionReady(detector_.locatePlate(frame));
        } catch (const cv::Exception& exception) {
            emit errorOccurred(QString::fromUtf8(exception.what()));
        }
    }
}
