#pragma once

#include "PlateDetector.h"

#include <QImage>
#include <QMainWindow>

class QLabel;
class QResizeEvent;
class QShowEvent;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onSelectImage();
    void onViewDebug();

private:
    void showDetection(const QImage& original, const LocateResult& result);
    void refreshViews();
    void paintLabel(QLabel* label, const QImage& image, const QString& placeholder);

    static cv::Mat qImageToBgr(const QImage& image);
    static QImage matToQImage(const cv::Mat& mat);

    Ui::MainWindow* ui = nullptr;
    PlateDetector detector_;
    QImage originalView_;
    QImage roiView_;
    QImage edgeImage_;
    QImage binaryImage_;
    QImage maskImage_;
    bool hasResult_ = false;
};
