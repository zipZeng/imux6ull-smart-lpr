#pragma once

#include "CaptureThread.h"
#include "DetectThread.h"
#include "PlateDetector.h"

#include <QImage>
#include <QMainWindow>

#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onStartDetect();
    void onStopDetect();
    void onSelectImage();
    void onViewDebug();
    void onFrame(cv::Mat frame);
    void onCaptureError(const QString& message);
    void onCaptureFinished();
    void onDetection(LocateResult result);
    void onDetectError(const QString& message);
    void onDetectFinished();

private:
    void refreshView();
    void setStreaming(bool streaming);
    void applyDetection(const QImage& source, const LocateResult& result);
    void paintPlates(QImage* canvas) const;
    static void paintLabel(QLabel* label, const QImage& image, const QString& placeholder);
    static QImage matToQImage(const cv::Mat& mat);
    static cv::Mat qImageToBgr(const QImage& image);
    static QString sampleDirectory();

    QLineEdit* urlEdit_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* detectStartButton_ = nullptr;
    QPushButton* detectStopButton_ = nullptr;
    QPushButton* selectButton_ = nullptr;
    QPushButton* debugButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* plateStatusLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QLabel* videoLabel_ = nullptr;
    QLabel* roiLabel_ = nullptr;

    CaptureThread* capture_ = nullptr;
    DetectThread* detect_ = nullptr;
    PlateDetector detector_;

    QImage baseImage_;
    QImage roiImage_;
    QImage edgeImage_;
    QImage binaryImage_;
    QImage maskImage_;
    std::vector<PlateCandidate> plates_;
    bool drawPlates_ = false;
    bool sawFrame_ = false;
    bool stopRequested_ = false;
    bool hasDetection_ = false;
    QString pendingError_;
    QString pendingDetectError_;
};
