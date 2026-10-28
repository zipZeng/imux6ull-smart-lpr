#pragma once

#include "PlateDetector.h"

#include <QThread>
#include <QMutex>
#include <QWaitCondition>

#include <opencv2/core.hpp>

Q_DECLARE_METATYPE(LocateResult)

// 只保留最新一帧。定位忙时丢掉中间帧，并且大约每 200 ms 才跑一次。
class DetectThread : public QThread {
    Q_OBJECT

public:
    explicit DetectThread(QObject* parent = nullptr);

    void submit(cv::Mat frame);
    void requestStop();

signals:
    void detectionReady(LocateResult result);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    QMutex mutex_;
    QWaitCondition cond_;
    cv::Mat pending_;
    bool hasFrame_ = false;
    bool stop_ = false;
    PlateDetector detector_;
};
