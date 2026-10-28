#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <algorithm>

#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPolygon>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

QString utf8(const char* text) {
    return QString::fromUtf8(text);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    const QString previewStyle = QStringLiteral(
        "QLabel { background: #202020; color: #d8d8d8; border: 1px solid #666666; }");
    ui->originalLabel->setStyleSheet(previewStyle);
    ui->roiLabel->setStyleSheet(previewStyle);
    ui->originalLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->roiLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->originalLabel->setAlignment(Qt::AlignCenter);
    ui->roiLabel->setAlignment(Qt::AlignCenter);
    ui->statusLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));

    connect(ui->selectButton, &QPushButton::clicked, this, &MainWindow::onSelectImage);
    connect(ui->debugButton, &QPushButton::clicked, this, &MainWindow::onViewDebug);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::onSelectImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        utf8("选择车辆图片"),
        QString(),
        utf8("图片 (*.bmp *.jpg *.jpeg *.png)"));
    if (path.isEmpty()) {
        return;
    }

    QImage image;
    if (!image.load(path)) {
        QMessageBox::warning(
            this,
            utf8("无法打开图片"),
            utf8("图片加载失败，请确认文件存在，且格式为 bmp、jpg 或 png。\n") + path);
        return;
    }

    try {
        const cv::Mat mat = qImageToBgr(image);
        if (mat.empty()) {
            QMessageBox::warning(this, utf8("无法打开图片"), utf8("图片内容为空，无法进行车牌定位。"));
            return;
        }

        const LocateResult result = detector_.locatePlate(mat);
        if (!result.ok) {
            QMessageBox::warning(
                this,
                utf8("检测失败"),
                QString::fromUtf8(result.error.c_str()));
            return;
        }
        showDetection(image, result);
    } catch (const cv::Exception& exception) {
        QMessageBox::critical(this, utf8("OpenCV 异常"), QString::fromUtf8(exception.what()));
    }
}

void MainWindow::showDetection(const QImage& original, const LocateResult& result) {
    QImage canvas = original.convertToFormat(QImage::Format_RGB888);
    if (result.found) {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int thickness = std::max(2, canvas.width() / 300);
        painter.setPen(QPen(QColor(220, 0, 0), thickness));
        for (const PlateCandidate& plate : result.plates) {
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

    originalView_ = canvas;
    roiView_ = result.found && !result.rois.empty() ? matToQImage(result.rois.front()) : QImage();
    edgeImage_ = matToQImage(result.edges);
    binaryImage_ = matToQImage(result.binary);
    maskImage_ = matToQImage(result.mask);
    hasResult_ = true;
    ui->debugButton->setEnabled(!edgeImage_.isNull() || !maskImage_.isNull());

    if (result.found) {
        ui->statusLabel->setText(utf8("✅检测到车牌"));
        ui->statusLabel->setStyleSheet(QStringLiteral("color: #1b7a32; font-size: 16px; font-weight: 600;"));
    } else {
        ui->statusLabel->setText(utf8("❌未检测到车牌"));
        ui->statusLabel->setStyleSheet(QStringLiteral("color: #b42318; font-size: 16px; font-weight: 600;"));
    }
    refreshViews();
}

void MainWindow::onViewDebug() {
    if (edgeImage_.isNull() && binaryImage_.isNull() && maskImage_.isNull()) {
        QMessageBox::information(this, utf8("中间结果"), utf8("请先选择一张图片。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(utf8("边缘图、二值图与掩码"));
    dialog.resize(1440, 560);

    auto* layout = new QHBoxLayout(&dialog);
    const auto addPane = [&](const QString& title, const QImage& image) {
        auto* column = new QVBoxLayout();
        auto* caption = new QLabel(title);
        caption->setAlignment(Qt::AlignCenter);
        auto* view = new QLabel();
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(500, 460);
        view->setStyleSheet(QStringLiteral("background: #111111;"));
        if (!image.isNull()) {
            view->setPixmap(QPixmap::fromImage(image).scaled(
                500, 460, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        column->addWidget(caption);
        column->addWidget(view, 1);
        layout->addLayout(column);
    };
    addPane(utf8("垂直边缘"), edgeImage_);
    addPane(utf8("二值图"), binaryImage_);
    addPane(utf8("二值掩码"), maskImage_);
    dialog.exec();
}

void MainWindow::refreshViews() {
    paintLabel(ui->originalLabel, originalView_, utf8("尚未选择图片"));
    const QString roiPlaceholder = hasResult_ ? utf8("未检测到车牌") : utf8("等待检测");
    paintLabel(ui->roiLabel, roiView_, roiPlaceholder);
}

void MainWindow::paintLabel(QLabel* label, const QImage& image, const QString& placeholder) {
    if (image.isNull()) {
        label->setPixmap(QPixmap());
        label->setText(placeholder);
        return;
    }
    if (label->width() < 2 || label->height() < 2) {
        return;
    }
    label->setText(QString());
    label->setPixmap(QPixmap::fromImage(image).scaled(
        label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    refreshViews();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    refreshViews();
}

cv::Mat MainWindow::qImageToBgr(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    // Format_RGB888 按 R、G、B 存放，行尾可能有对齐填充。
    // 用 bytesPerLine 作为 Mat 的 step，再换成 OpenCV 的 BGR，并让 cvtColor 复制出独立内存。
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

QImage MainWindow::matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return {};
    }
    if (mat.type() == CV_8UC1) {
        const QImage gray(
            mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return gray.copy();
    }

    cv::Mat rgb;
    if (mat.type() == CV_8UC3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    } else if (mat.type() == CV_8UC4) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);
        const QImage image(
            rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGBA8888);
        return image.copy();
    } else {
        return {};
    }

    const QImage image(
        rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    return image.copy();
}
