#pragma once

#include <QString>
#include <QThread>

#include <atomic>

#include <opencv2/core.hpp>

Q_DECLARE_METATYPE(cv::Mat)

// 在子线程里用 cv::VideoCapture 拉 RTSP。发给界面的每一帧都是 clone() 出来的独立缓冲。
class CaptureThread : public QThread {
    Q_OBJECT

public:
    explicit CaptureThread(QString url, QObject* parent = nullptr);

    void requestStop();

signals:
    void frameReady(cv::Mat frame);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    QString url_;
    std::atomic<bool> stop_{false};
};
