#include "mainwindow.h"

#include <algorithm>
#include <cmath>

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

namespace {

QString ffmpegStatus() {
    const std::string info = cv::getBuildInformation();
    const auto pos = info.find("FFMPEG:");
    if (pos == std::string::npos) {
        return QStringLiteral("当前 OpenCV 没有 FFmpeg 信息，RTSP 可能打不开");
    }
    const auto lineEnd = info.find('\n', pos);
    const auto yes = info.find("YES", pos);
    if (yes == std::string::npos || (lineEnd != std::string::npos && yes > lineEnd)) {
        return QStringLiteral("当前 OpenCV 未启用 FFmpeg，无法拉 RTSP");
    }
    return {};
}

QLabel* makePreview(const QString& placeholder) {
    auto* label = new QLabel(placeholder);
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    label->setStyleSheet(QStringLiteral("background-color: #1e1e1e; color: #c8c8c8;"));
    return label;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("远程车牌定位"));
    int windowWidth = 1120;
    int windowHeight = 720;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        windowWidth = std::min(windowWidth, std::max(720, available.width() * 8 / 10));
        windowHeight = std::min(windowHeight, std::max(480, available.height() * 8 / 10));
    }
    resize(windowWidth, windowHeight);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    urlEdit_ = new QLineEdit(QStringLiteral("rtsp://134.175.47.224:8554/live"));
    urlEdit_->setPlaceholderText(QStringLiteral("rtsp://服务器:8554/live"));
    urlEdit_->setMinimumHeight(32);
    startButton_ = new QPushButton(QStringLiteral("开始"));
    stopButton_ = new QPushButton(QStringLiteral("停止"));
    detectStartButton_ = new QPushButton(QStringLiteral("开始定位"));
    detectStopButton_ = new QPushButton(QStringLiteral("停止定位"));
    selectButton_ = new QPushButton(QStringLiteral("选择图片"));
    debugButton_ = new QPushButton(QStringLiteral("查看边缘与掩码"));
    for (QPushButton* button : {startButton_, stopButton_, detectStartButton_, detectStopButton_, selectButton_, debugButton_}) {
        button->setMinimumHeight(32);
    }
    stopButton_->setEnabled(false);
    detectStartButton_->setEnabled(false);
    detectStopButton_->setEnabled(false);
    debugButton_->setEnabled(false);

    auto* tools = new QHBoxLayout();
    tools->setSpacing(8);
    tools->addWidget(urlEdit_, 1);
    tools->addWidget(startButton_);
    tools->addWidget(stopButton_);
    tools->addWidget(detectStartButton_);
    tools->addWidget(detectStopButton_);
    tools->addWidget(selectButton_);
    tools->addWidget(debugButton_);

    statusLabel_ = new QLabel(QStringLiteral("输入 RTSP 地址后点击开始"));
    QFont statusFont = statusLabel_->font();
    statusFont.setPointSize(12);
    statusLabel_->setFont(statusFont);

    videoLabel_ = makePreview(QStringLiteral("尚未连接"));
    videoLabel_->setMinimumSize(320, 180);

    plateStatusLabel_ = new QLabel(QStringLiteral("定位未开启"));
    QFont plateFont = plateStatusLabel_->font();
    plateFont.setPointSize(14);
    plateFont.setBold(true);
    plateStatusLabel_->setFont(plateFont);
    plateStatusLabel_->setWordWrap(true);

    roiLabel_ = makePreview(QStringLiteral("等待检测"));
    roiLabel_->setMinimumSize(200, 80);
    roiLabel_->setMaximumWidth(320);

    infoLabel_ = new QLabel();
    infoLabel_->setWordWrap(true);
    infoLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLabel_->setMinimumWidth(200);
    infoLabel_->setMaximumWidth(320);

    auto* side = new QVBoxLayout();
    side->setSpacing(8);
    side->addWidget(plateStatusLabel_);
    side->addWidget(roiLabel_);
    side->addWidget(infoLabel_);
    side->addStretch(1);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);
    body->addWidget(videoLabel_, 1);
    body->addLayout(side);

    root->addLayout(tools);
    root->addWidget(statusLabel_);
    root->addLayout(body, 1);
    setCentralWidget(central);

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(detectStartButton_, &QPushButton::clicked, this, &MainWindow::onStartDetect);
    connect(detectStopButton_, &QPushButton::clicked, this, &MainWindow::onStopDetect);
    connect(selectButton_, &QPushButton::clicked, this, &MainWindow::onSelectImage);
    connect(debugButton_, &QPushButton::clicked, this, &MainWindow::onViewDebug);

    const QString ffmpeg = ffmpegStatus();
    if (!ffmpeg.isEmpty()) {
        statusLabel_->setText(ffmpeg);
    }
}

MainWindow::~MainWindow() {
    if (detect_ != nullptr) {
        disconnect(detect_, nullptr, this, nullptr);
        detect_->requestStop();
        detect_->wait();
        delete detect_;
        detect_ = nullptr;
    }
    if (capture_ != nullptr) {
        disconnect(capture_, nullptr, this, nullptr);
        capture_->requestStop();
        capture_->wait();
        delete capture_;
        capture_ = nullptr;
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    refreshView();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    refreshView();
}

void MainWindow::onStart() {
    if (capture_ != nullptr) {
        statusLabel_->setText(QStringLiteral("上一次拉流还没结束"));
        return;
    }

    const QString url = urlEdit_->text().trimmed();
    if (url.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请先填写 RTSP 地址"));
        return;
    }

    const QString ffmpeg = ffmpegStatus();
    if (!ffmpeg.isEmpty()) {
        statusLabel_->setText(ffmpeg);
        return;
    }

    sawFrame_ = false;
    stopRequested_ = false;
    pendingError_.clear();
    drawPlates_ = false;
    plates_.clear();
    baseImage_ = QImage();
    refreshView();

    capture_ = new CaptureThread(url, this);
    connect(capture_, &CaptureThread::frameReady, this, &MainWindow::onFrame, Qt::QueuedConnection);
    connect(capture_, &CaptureThread::errorOccurred, this, &MainWindow::onCaptureError, Qt::QueuedConnection);
    connect(capture_, &QThread::finished, this, &MainWindow::onCaptureFinished);

    setStreaming(true);
    statusLabel_->setText(QStringLiteral("正在连接 ") + url);
    capture_->start();
}

void MainWindow::onStop() {
    if (capture_ == nullptr) {
        return;
    }
    stopRequested_ = true;
    stopButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在停止…"));
    capture_->requestStop();
    if (detect_ != nullptr) {
        detect_->requestStop();
    }
}

void MainWindow::onStartDetect() {
    if (capture_ == nullptr || detect_ != nullptr) {
        return;
    }
    pendingDetectError_.clear();
    detect_ = new DetectThread(this);
    connect(detect_, &DetectThread::detectionReady, this, &MainWindow::onDetection, Qt::QueuedConnection);
    connect(detect_, &DetectThread::errorOccurred, this, &MainWindow::onDetectError, Qt::QueuedConnection);
    connect(detect_, &QThread::finished, this, &MainWindow::onDetectFinished);
    detectStartButton_->setEnabled(false);
    detectStopButton_->setEnabled(true);
    plateStatusLabel_->setText(QStringLiteral("正在定位…"));
    plateStatusLabel_->setStyleSheet(QStringLiteral("color: #1a1a1a;"));
    detect_->start();
}

void MainWindow::onStopDetect() {
    if (detect_ == nullptr) {
        return;
    }
    detectStopButton_->setEnabled(false);
    plateStatusLabel_->setText(QStringLiteral("正在停止定位…"));
    detect_->requestStop();
}

void MainWindow::onSelectImage() {
    if (capture_ != nullptr) {
        statusLabel_->setText(QStringLiteral("拉流时不能选图，请先停止"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择车辆图片"),
        sampleDirectory(),
        QStringLiteral("图片 (*.bmp *.jpg *.jpeg *.png)"));
    if (path.isEmpty()) {
        return;
    }

    QImage image;
    if (!image.load(path)) {
        QMessageBox::warning(this, QStringLiteral("无法打开图片"), QStringLiteral("图片加载失败：\n") + path);
        return;
    }

    try {
        const cv::Mat mat = qImageToBgr(image);
        if (mat.empty()) {
            QMessageBox::warning(this, QStringLiteral("无法打开图片"), QStringLiteral("图片内容为空。"));
            return;
        }
        const LocateResult result = detector_.locatePlate(mat);
        if (!result.ok) {
            QMessageBox::warning(this, QStringLiteral("检测失败"), QString::fromStdString(result.error));
            return;
        }
        statusLabel_->setText(QStringLiteral("已打开图片"));
        applyDetection(image.convertToFormat(QImage::Format_RGB888), result);
    } catch (const cv::Exception& exception) {
        QMessageBox::critical(this, QStringLiteral("OpenCV 异常"), QString::fromUtf8(exception.what()));
    }
}

void MainWindow::onViewDebug() {
    if (edgeImage_.isNull() && binaryImage_.isNull() && maskImage_.isNull()) {
        QMessageBox::information(this, QStringLiteral("中间结果"), QStringLiteral("请先选择一张图片，或在拉流后开始定位。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("边缘图、二值图与掩码"));
    dialog.resize(1440, 560);
    auto* layout = new QHBoxLayout(&dialog);
    const auto addPane = [&](const QString& title, const QImage& image) {
        auto* column = new QVBoxLayout();
        auto* caption = new QLabel(title);
        caption->setAlignment(Qt::AlignCenter);
        auto* view = new QLabel();
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(460, 420);
        view->setStyleSheet(QStringLiteral("background: #111111;"));
        if (!image.isNull()) {
            view->setPixmap(QPixmap::fromImage(image).scaled(460, 420, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        column->addWidget(caption);
        column->addWidget(view, 1);
        layout->addLayout(column);
    };
    addPane(QStringLiteral("垂直边缘"), edgeImage_);
    addPane(QStringLiteral("二值图"), binaryImage_);
    addPane(QStringLiteral("二值掩码"), maskImage_);
    dialog.exec();
}

void MainWindow::onFrame(cv::Mat frame) {
    const QImage image = matToQImage(frame);
    if (image.isNull()) {
        statusLabel_->setText(QStringLiteral("收到的帧格式无法显示"));
        return;
    }
    baseImage_ = image;
    if (!sawFrame_) {
        sawFrame_ = true;
        statusLabel_->setText(QStringLiteral("正在播放 %1×%2").arg(frame.cols).arg(frame.rows));
    }
    if (detect_ != nullptr) {
        detect_->submit(std::move(frame));
    }
    refreshView();
}

void MainWindow::onCaptureError(const QString& message) {
    pendingError_ = message;
    statusLabel_->setText(message);
}

void MainWindow::onCaptureFinished() {
    if (capture_ != nullptr) {
        capture_->deleteLater();
        capture_ = nullptr;
    }
    if (detect_ != nullptr) {
        detect_->requestStop();
    }
    setStreaming(false);
    if (!pendingError_.isEmpty()) {
        statusLabel_->setText(pendingError_);
        pendingError_.clear();
    } else if (stopRequested_) {
        statusLabel_->setText(QStringLiteral("已停止"));
    }
    stopRequested_ = false;
}

void MainWindow::onDetection(LocateResult result) {
    if (!result.ok) {
        plateStatusLabel_->setText(QString::fromStdString(result.error));
        plateStatusLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
        return;
    }
    applyDetection(baseImage_, result);
}

void MainWindow::onDetectError(const QString& message) {
    pendingDetectError_ = message;
    plateStatusLabel_->setText(message);
    plateStatusLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
}

void MainWindow::onDetectFinished() {
    if (detect_ != nullptr) {
        detect_->deleteLater();
        detect_ = nullptr;
    }
    drawPlates_ = false;
    plates_.clear();
    detectStartButton_->setEnabled(capture_ != nullptr);
    detectStopButton_->setEnabled(false);
    if (!pendingDetectError_.isEmpty()) {
        plateStatusLabel_->setText(pendingDetectError_);
        pendingDetectError_.clear();
    } else {
        plateStatusLabel_->setText(QStringLiteral("定位已停止"));
        plateStatusLabel_->setStyleSheet(QStringLiteral("color: #1a1a1a;"));
    }
    refreshView();
}

void MainWindow::setStreaming(bool streaming) {
    startButton_->setEnabled(!streaming);
    stopButton_->setEnabled(streaming);
    urlEdit_->setReadOnly(streaming);
    selectButton_->setEnabled(!streaming);
    detectStartButton_->setEnabled(streaming && detect_ == nullptr);
    if (!streaming) {
        detectStopButton_->setEnabled(detect_ != nullptr);
    }
}

void MainWindow::applyDetection(const QImage& source, const LocateResult& result) {
    if (!source.isNull()) {
        baseImage_ = source;
    }
    plates_ = result.plates;
    drawPlates_ = result.found && !plates_.empty();
    hasDetection_ = true;
    roiImage_ = result.found && !result.rois.empty() ? matToQImage(result.rois.front()) : QImage();
    edgeImage_ = matToQImage(result.edges);
    binaryImage_ = matToQImage(result.binary);
    maskImage_ = matToQImage(result.mask);
    debugButton_->setEnabled(!edgeImage_.isNull() || !binaryImage_.isNull() || !maskImage_.isNull());

    if (result.found && !plates_.empty()) {
        const PlateCandidate& plate = plates_.front();
        plateStatusLabel_->setText(QStringLiteral("检测到车牌"));
        plateStatusLabel_->setStyleSheet(QStringLiteral("color: #1b7a32;"));
        infoLabel_->setText(QStringLiteral("宽高比 %1\n得分 %2\n倾斜 %3°")
                                .arg(plate.aspect, 0, 'f', 2)
                                .arg(plate.score, 0, 'f', 2)
                                .arg(plate.tiltDeg, 0, 'f', 1));
    } else {
        plateStatusLabel_->setText(QStringLiteral("未检测到车牌"));
        plateStatusLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
        infoLabel_->setText(QString());
    }
    refreshView();
}

void MainWindow::paintPlates(QImage* canvas) const {
    if (canvas == nullptr || canvas->isNull() || plates_.empty()) {
        return;
    }
    QPainter painter(canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int thickness = std::max(2, canvas->width() / 300);
    painter.setPen(QPen(QColor(220, 0, 0), thickness));
    for (const PlateCandidate& plate : plates_) {
        if (plate.box.x + plate.box.width > canvas->width() + 8 || plate.box.y + plate.box.height > canvas->height() + 8) {
            continue;
        }
        if (plate.hasCorners) {
            QPolygon polygon;
            for (const cv::Point2f& corner : plate.corners) {
                polygon << QPoint(std::lround(corner.x), std::lround(corner.y));
            }
            painter.drawPolygon(polygon);
        } else {
            painter.drawRect(plate.box.x, plate.box.y, plate.box.width, plate.box.height);
        }
    }
}

void MainWindow::refreshView() {
    QImage canvas = baseImage_;
    if (!canvas.isNull() && canvas.format() != QImage::Format_RGB32 && canvas.format() != QImage::Format_ARGB32) {
        canvas = canvas.convertToFormat(QImage::Format_ARGB32);
    }
    if (drawPlates_) {
        paintPlates(&canvas);
    }
    paintLabel(videoLabel_, canvas, QStringLiteral("尚未连接"));
    const QString roiPlaceholder = hasDetection_ ? QStringLiteral("未检测到车牌") : QStringLiteral("等待检测");
    paintLabel(roiLabel_, roiImage_, roiPlaceholder);
}

void MainWindow::paintLabel(QLabel* label, const QImage& image, const QString& placeholder) {
    if (label == nullptr) {
        return;
    }
    if (image.isNull()) {
        label->setPixmap(QPixmap());
        label->setText(placeholder);
        return;
    }
    if (label->width() < 2 || label->height() < 2) {
        return;
    }
    label->setText(QString());
    label->setPixmap(QPixmap::fromImage(image.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

QImage MainWindow::matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return {};
    }
    if (mat.type() == CV_8UC1) {
        const QImage gray(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return gray.copy();
    }

    cv::Mat rgb;
    if (mat.type() == CV_8UC3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    } else if (mat.type() == CV_8UC4) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);
        const QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGBA8888);
        return image.copy();
    } else {
        return {};
    }

    const QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    return image.copy();
}

cv::Mat MainWindow::qImageToBgr(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    const cv::Mat view(
        rgb.height(),
        rgb.width(),
        CV_8UC3,
        const_cast<uchar*>(rgb.constBits()),
        static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

QString MainWindow::sampleDirectory() {
    const QStringList candidates = {
        QStringLiteral("samples"),
        QStringLiteral("../samples"),
        QStringLiteral("../../samples"),
        QStringLiteral("../../../samples"),
    };
    for (const QString& candidate : candidates) {
        if (QDir(candidate).exists()) {
            return QDir(candidate).absolutePath();
        }
    }
    return {};
}
