/**
 * 传统 OpenCV 车牌定位。本文件禁止包含 Qt 头文件。
 * 处理步骤与原先控制台版本一致：灰度、高斯、垂直 Sobel、二值化、
 * 先膨胀后腐蚀、外轮廓，再按面积、宽高比 2.0~5.5、矩形度筛选。
 * 调参只改下面的 cfg。
 */

#include "PlateDetector.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// =============================================================================
// 可调参数（调试时优先只改这一段，改完重新编译）
// 国内小型汽车蓝牌外廓通常为 440 mm x 140 mm，宽高比约 3.14 : 1。
// =============================================================================
namespace cfg {

/// 0 = 静态图片（默认）；1 = 打开摄像头，按帧调用同一套定位接口。
constexpr int kInputMode = 0;
/// 摄像头编号。笔记本内置摄像头一般是 0。
constexpr int kCameraIndex = 0;

/// 高斯核边长，必须是正奇数。越大越模糊，字符边缘也会变弱。
constexpr int kGaussianKsize = 5;
/// 为 0 时由 OpenCV 按核大小自动计算 sigma。
constexpr double kGaussianSigma = 0.0;

/// Sobel 核大小，只能是 1、3、5、7。3 对车牌字符最常用。
constexpr int kSobelKsize = 3;

/// true：大津法自动阈值；false：使用 kFixedThreshold。
constexpr bool kUseOtsu = true;
/// 大津阈值若低于该值，说明边缘图几乎全黑、直方图太偏，改用固定阈值。
constexpr double kMinOtsuThreshold = 25.0;
/// 固定阈值，仅在关闭大津法或大津结果过低时使用。范围 0~255。
constexpr int kFixedThreshold = 80;

/// 闭运算前是否先做一次小核开运算，去掉孤立噪点，避免噪点被粘成大块。
constexpr bool kEnablePreOpen = true;
constexpr int kOpenKernelSize = 3;

/// 闭运算矩形核（宽、高）。宽核用于把左右相邻的字符粘成一块。
/// 该尺寸是针对大约 kRefImageWidth 像素宽的画面调整的。
constexpr int kCloseKernelW = 17;
constexpr int kCloseKernelH = 5;
constexpr int kCloseIterations = 1;
/// 按图像宽度缩放闭运算核。车在画面中的占比变化很大时，仍需手工改 kCloseKernelW。
constexpr bool kScaleKernelWithImage = true;
constexpr int kRefImageWidth = 1000;

/// 面积阈值同时受“绝对像素面积”和“占整图比例”约束，取二者交集。
constexpr double kMinAreaAbs = 800.0;
constexpr double kMaxAreaAbs = 300000.0;
constexpr double kMinAreaRatio = 0.0015;
constexpr double kMaxAreaRatio = 0.20;

/// 外接矩形宽高比 width / height。蓝牌标准约 3.14，上下限留给倾斜和透视。
constexpr double kStandardAspect = 440.0 / 140.0;
constexpr double kMinAspect = 2.0;
constexpr double kMaxAspect = 5.5;

/// 矩形度 = 轮廓面积 / 外接矩形面积。过低说明形状零碎，不像实心车牌块。
constexpr double kMinExtent = 0.40;
/// 边缘轮廓相对水平方向的最大倾斜角（度）。侧拍车牌另走颜色旋转框，上限更高。
constexpr double kMaxTiltDeg = 20.0;
constexpr double kMaxRotatedTiltDeg = 50.0;
constexpr double kMinRotatedAspect = 1.55;
constexpr double kMaxRotatedAspect = 4.8;
constexpr double kMinRotatedExtent = 0.55;
constexpr double kMinRotatedPaint = 0.55;
constexpr double kMinRotatedEdgeMean = 8.0;
constexpr int kMinRotatedLongSide = 100;
constexpr double kMaxRotatedLongRatio = 0.48;

/// 强透视或夜间反光时，底色掩码连不成块，改在较高阈值的字符边缘上找旋转矩形。
constexpr int kEdgeBinaryFloor = 60;
constexpr double kEdgeRotatedMinAspect = 2.0;
constexpr double kEdgeRotatedMaxAspect = 5.2;
constexpr double kEdgeRotatedMinExtent = 0.32;
constexpr double kEdgeRotatedMaxTilt = 42.0;
constexpr double kEdgeRotatedMinPaint = 0.32;
constexpr double kEdgeRotatedMinEdge = 18.0;
constexpr int kEdgeRotatedMinLong = 100;

/// 候选框的像素尺寸下限，以及相对整图的尺寸上限。
constexpr int kMinPlateWidth = 60;
constexpr int kMinPlateHeight = 18;
constexpr double kMaxWidthRatio = 0.80;
constexpr double kMaxHeightRatio = 0.55;

/// 候选框内部垂直边缘的平均强度下限，用来去掉没有字符笔画的横条。
constexpr bool kEnableEdgeDensityCheck = true;
constexpr double kMinEdgeMean = 12.0;

/// 用蓝牌、黄牌底色去掉车标和镀铬字。边缘候选只要沾到一定底色即可；
/// 颜色掩码另找一块实心车牌，用来补上字符没连成整块的情况。
constexpr bool kEnablePlateColorCheck = true;
constexpr double kMinEdgeColorRatio = 0.18;
constexpr double kMinColorBlobRatio = 0.60;
constexpr int kMinColorBlobWidth = 110;
constexpr int kMinColorBlobHeight = 36;
constexpr double kMaxColorBlobWidthRatio = 0.65;

/// 重叠候选的 IoU 超过该值时只保留得分更高的一个。
constexpr double kNmsIou = 0.30;
/// 最多保留多少个车牌框。
constexpr int kMaxPlates = 3;

/// 裁剪 ROI 时在外接矩形四周外扩的像素，避免切掉字符边缘。
constexpr int kRoiPadding = 8;
constexpr int kDrawThickness = 2;

/// 显示窗口的最大宽度。界面缩放由 Qt 负责，此值保留给算法侧标注图。
constexpr int kDisplayMaxWidth = 640;

}  // namespace cfg

namespace {

int ensureOdd(int value, int minValue) {
    int result = std::max(value, minValue);
    if (result % 2 == 0) {
        ++result;
    }
    return result;
}

bool validateConfig(std::string& error) {
    if (cfg::kGaussianKsize < 1 || cfg::kGaussianKsize % 2 == 0) {
        error = "kGaussianKsize 必须为正奇数。";
        return false;
    }
    if (cfg::kSobelKsize != 1 && cfg::kSobelKsize != 3 &&
        cfg::kSobelKsize != 5 && cfg::kSobelKsize != 7) {
        error = "kSobelKsize 只能是 1、3、5、7。";
        return false;
    }
    if (cfg::kFixedThreshold < 0 || cfg::kFixedThreshold > 255) {
        error = "kFixedThreshold 必须在 0 到 255 之间。";
        return false;
    }
    if (cfg::kMinAspect <= 0.0 || cfg::kMinAspect >= cfg::kMaxAspect) {
        error = "宽高比上下限无效，需要 0 < kMinAspect < kMaxAspect。";
        return false;
    }
    if (cfg::kMinAreaRatio < 0.0 || cfg::kMaxAreaRatio <= cfg::kMinAreaRatio) {
        error = "面积比例范围无效。";
        return false;
    }
    if (cfg::kCloseKernelW < 1 || cfg::kCloseKernelH < 1 || cfg::kCloseIterations < 1) {
        error = "闭运算核尺寸和迭代次数必须为正。";
        return false;
    }
    if (cfg::kMinExtent <= 0.0 || cfg::kMinExtent > 1.0) {
        error = "kMinExtent 必须在 (0, 1] 内。";
        return false;
    }
    return true;
}

double rectIou(const cv::Rect& a, const cv::Rect& b) {
    const cv::Rect intersection = a & b;
    const double interArea = static_cast<double>(intersection.area());
    const double unionArea = static_cast<double>(a.area()) + static_cast<double>(b.area()) - interArea;
    if (unionArea <= 0.0) {
        return 0.0;
    }
    return interArea / unionArea;
}

/// 把最小外接矩形的长边方向折算成相对水平线的倾斜角，范围约 [-90, 90]。
double longEdgeTiltDeg(const cv::RotatedRect& rotated) {
    double angle = rotated.angle;
    if (rotated.size.width < rotated.size.height) {
        angle += 90.0;
    }
    while (angle > 90.0) {
        angle -= 180.0;
    }
    while (angle <= -90.0) {
        angle += 180.0;
    }
    return angle;
}

double platePaintRatio(const cv::Mat& src, const cv::Rect& box) {
    if (src.empty() || src.channels() != 3) {
        return 1.0;
    }
    const cv::Rect safe = box & cv::Rect(0, 0, src.cols, src.rows);
    if (safe.area() <= 0) {
        return 0.0;
    }
    cv::Mat hsv;
    cv::cvtColor(src(safe), hsv, cv::COLOR_BGR2HSV);
    cv::Mat blue;
    cv::Mat yellow;
    cv::Mat paint;
    cv::inRange(hsv, cv::Scalar(95, 50, 40), cv::Scalar(140, 255, 255), blue);
    cv::inRange(hsv, cv::Scalar(10, 70, 70), cv::Scalar(40, 255, 255), yellow);
    cv::bitwise_or(blue, yellow, paint);
    return static_cast<double>(cv::countNonZero(paint)) / static_cast<double>(safe.area());
}

cv::Mat plateColorMask(const cv::Mat& src) {
    cv::Mat hsv;
    cv::Mat blue;
    cv::Mat yellow;
    cv::Mat paint;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(100, 80, 50), cv::Scalar(140, 255, 255), blue);
    cv::inRange(hsv, cv::Scalar(15, 80, 80), cv::Scalar(40, 255, 255), yellow);
    cv::bitwise_or(blue, yellow, paint);
    const cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(17, 5));
    cv::morphologyEx(paint, paint, cv::MORPH_CLOSE, closeKernel);
    const cv::Mat openKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(paint, paint, cv::MORPH_OPEN, openKernel);
    return paint;
}

std::vector<PlateCandidate> keepPaintedPlates(
    const cv::Mat& src,
    std::vector<PlateCandidate> plates,
    double minRatio) {
    if (!cfg::kEnablePlateColorCheck || src.channels() != 3) {
        return plates;
    }
    std::vector<PlateCandidate> kept;
    for (auto& plate : plates) {
        const double ratio = platePaintRatio(src, plate.box);
        if (ratio < minRatio) {
            continue;
        }
        plate.score += 0.25 * ratio;
        kept.push_back(plate);
    }
    return kept;
}

cv::Mat warpPlate(const cv::Mat& src, const cv::Point2f corners[4]) {
    std::vector<cv::Point2f> points(corners, corners + 4);
    std::sort(points.begin(), points.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });
    const cv::Point2f topLeft = points[0].x < points[1].x ? points[0] : points[1];
    const cv::Point2f topRight = points[0].x < points[1].x ? points[1] : points[0];
    const cv::Point2f bottomLeft = points[2].x < points[3].x ? points[2] : points[3];
    const cv::Point2f bottomRight = points[2].x < points[3].x ? points[3] : points[2];
    const float width = std::max(cv::norm(topRight - topLeft), cv::norm(bottomRight - bottomLeft));
    const float height = std::max(cv::norm(bottomLeft - topLeft), cv::norm(bottomRight - topRight));
    if (width < 8.f || height < 8.f) {
        return {};
    }
    const cv::Point2f source[4] = {topLeft, topRight, bottomRight, bottomLeft};
    const cv::Point2f target[4] = {
        {0.f, 0.f}, {width - 1.f, 0.f}, {width - 1.f, height - 1.f}, {0.f, height - 1.f}};
    const cv::Mat transform = cv::getPerspectiveTransform(source, target);
    cv::Mat roi;
    cv::warpPerspective(src, roi, transform, cv::Size(std::lround(width), std::lround(height)));
    if (roi.rows > roi.cols) {
        cv::rotate(roi, roi, cv::ROTATE_90_CLOCKWISE);
    }
    return roi;
}

std::vector<PlateCandidate> findTiltedColorPlates(const cv::Mat& src, const cv::Mat& edges) {
    std::vector<PlateCandidate> found;
    if (src.empty() || src.channels() != 3 || edges.empty()) {
        return found;
    }

    cv::Mat hsv;
    cv::Mat blue;
    cv::Mat yellow;
    cv::Mat paint;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(100, 80, 50), cv::Scalar(140, 255, 255), blue);
    cv::inRange(hsv, cv::Scalar(15, 80, 80), cv::Scalar(40, 255, 255), yellow);
    cv::bitwise_or(blue, yellow, paint);
    cv::Mat closed;
    const cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(11, 11));
    cv::morphologyEx(paint, closed, cv::MORPH_CLOSE, closeKernel);
    const cv::Mat openKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(closed, closed, cv::MORPH_OPEN, openKernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(closed.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const double imageArea = static_cast<double>(src.cols) * static_cast<double>(src.rows);
    const double minArea = std::max(cfg::kMinAreaAbs, cfg::kMinAreaRatio * imageArea);
    const double maxArea = std::min(80000.0, 0.08 * imageArea);
    const double maxLongSide = src.cols * cfg::kMaxRotatedLongRatio;
    const cv::Rect imageRect(0, 0, src.cols, src.rows);

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea || area > maxArea) {
            continue;
        }
        const cv::RotatedRect rotated = cv::minAreaRect(contour);
        const double tilt = longEdgeTiltDeg(rotated);
        if (std::abs(tilt) > cfg::kMaxRotatedTiltDeg) {
            continue;
        }
        float longSide = rotated.size.width;
        float shortSide = rotated.size.height;
        if (longSide < shortSide) {
            std::swap(longSide, shortSide);
        }
        if (shortSide < 28.f || longSide < cfg::kMinRotatedLongSide || longSide > maxLongSide) {
            continue;
        }
        const double aspect = static_cast<double>(longSide) / static_cast<double>(shortSide);
        if (aspect < cfg::kMinRotatedAspect || aspect > cfg::kMaxRotatedAspect) {
            continue;
        }
        const double extent = area / (static_cast<double>(longSide) * static_cast<double>(shortSide));
        if (extent < cfg::kMinRotatedExtent) {
            continue;
        }

        cv::Point2f corners[4];
        rotated.points(corners);
        std::vector<cv::Point> polygon(4);
        for (int i = 0; i < 4; ++i) {
            polygon[i] = corners[i];
        }
        cv::Mat shape(src.size(), CV_8UC1, cv::Scalar(0));
        cv::fillConvexPoly(shape, polygon, cv::Scalar(255));
        cv::Mat painted;
        cv::bitwise_and(paint, shape, painted);
        const int inside = cv::countNonZero(shape);
        if (inside <= 0) {
            continue;
        }
        const double paintRatio = static_cast<double>(cv::countNonZero(painted)) / static_cast<double>(inside);
        if (paintRatio < cfg::kMinRotatedPaint) {
            continue;
        }

        const cv::Rect box = cv::boundingRect(contour) & imageRect;
        if (box.area() <= 0 || box.y <= 12) {
            continue;
        }
        const double edgeMean = cv::mean(edges(box))[0];
        if (edgeMean < cfg::kMinRotatedEdgeMean) {
            continue;
        }

        const double aspectScore = 1.0 - std::min(
            std::abs(aspect - cfg::kStandardAspect) / cfg::kStandardAspect, 1.0);
        PlateCandidate candidate;
        candidate.box = box;
        candidate.hasCorners = true;
        for (int i = 0; i < 4; ++i) {
            candidate.corners[i] = corners[i];
        }
        candidate.area = area;
        candidate.aspect = aspect;
        candidate.extent = extent;
        candidate.edgeMean = edgeMean;
        candidate.tiltDeg = tilt;
        candidate.score = 0.45 * aspectScore + 0.20 * std::min(extent, 1.0) +
                          0.10 * std::min(edgeMean / 40.0, 1.0) + 0.25 * paintRatio;
        found.push_back(candidate);
    }
    return found;
}

std::vector<PlateCandidate> findTiltedEdgePlates(const cv::Mat& src, const cv::Mat& edges) {
    std::vector<PlateCandidate> found;
    if (src.empty() || src.channels() != 3 || edges.empty()) {
        return found;
    }

    cv::Mat otsuBinary;
    const double otsu = cv::threshold(edges, otsuBinary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Mat binary;
    cv::threshold(edges, binary, std::max(static_cast<double>(cfg::kEdgeBinaryFloor), otsu), 255, cv::THRESH_BINARY);
    cv::Mat opened;
    cv::morphologyEx(binary, opened, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    double widthScale = 1.0;
    if (cfg::kScaleKernelWithImage && cfg::kRefImageWidth > 0) {
        widthScale = std::clamp(static_cast<double>(src.cols) / static_cast<double>(cfg::kRefImageWidth), 0.7, 1.5);
    }
    const int kernelW = ensureOdd(static_cast<int>(std::lround(cfg::kCloseKernelW * widthScale)), 3);
    const int kernelH = ensureOdd(static_cast<int>(std::lround(cfg::kCloseKernelH * std::clamp(widthScale, 0.8, 1.6))), 3);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernelW, kernelH));
    cv::Mat dilated;
    cv::Mat mask;
    cv::dilate(opened, dilated, kernel);
    cv::erode(dilated, mask, kernel);

    cv::Mat hsv;
    cv::Mat blue;
    cv::Mat yellow;
    cv::Mat paint;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(95, 35, 35), cv::Scalar(140, 255, 255), blue);
    cv::inRange(hsv, cv::Scalar(12, 60, 60), cv::Scalar(40, 255, 255), yellow);
    cv::bitwise_or(blue, yellow, paint);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const double imageArea = static_cast<double>(src.cols) * static_cast<double>(src.rows);
    const double maxArea = std::min(80000.0, 0.08 * imageArea);
    const double maxLongSide = src.cols * 0.45;
    const cv::Rect imageRect(0, 0, src.cols, src.rows);

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < 700.0 || area > maxArea) {
            continue;
        }
        const cv::RotatedRect rotated = cv::minAreaRect(contour);
        const double tilt = longEdgeTiltDeg(rotated);
        if (std::abs(tilt) > cfg::kEdgeRotatedMaxTilt) {
            continue;
        }
        float longSide = rotated.size.width;
        float shortSide = rotated.size.height;
        if (longSide < shortSide) {
            std::swap(longSide, shortSide);
        }
        if (shortSide < 24.f || longSide < cfg::kEdgeRotatedMinLong || longSide > maxLongSide) {
            continue;
        }
        const double aspect = static_cast<double>(longSide) / static_cast<double>(shortSide);
        if (aspect < cfg::kEdgeRotatedMinAspect || aspect > cfg::kEdgeRotatedMaxAspect) {
            continue;
        }
        const double extent = area / (static_cast<double>(longSide) * static_cast<double>(shortSide));
        if (extent < cfg::kEdgeRotatedMinExtent) {
            continue;
        }

        cv::Point2f corners[4];
        rotated.points(corners);
        const double paintRatio = [&]() {
            std::vector<cv::Point> polygon(4);
            for (int i = 0; i < 4; ++i) {
                polygon[i] = corners[i];
            }
            cv::Mat shape(src.size(), CV_8UC1, cv::Scalar(0));
            cv::fillConvexPoly(shape, polygon, cv::Scalar(255));
            cv::Mat painted;
            cv::bitwise_and(paint, shape, painted);
            const int inside = cv::countNonZero(shape);
            if (inside <= 0) {
                return 0.0;
            }
            return static_cast<double>(cv::countNonZero(painted)) / static_cast<double>(inside);
        }();
        if (paintRatio < cfg::kEdgeRotatedMinPaint) {
            continue;
        }

        const cv::Rect box = cv::boundingRect(contour) & imageRect;
        if (box.area() <= 0 || box.y <= 12 || box.x <= 2 || box.x + box.width >= src.cols - 2) {
            continue;
        }
        const double edgeMean = cv::mean(edges(box))[0];
        if (edgeMean < cfg::kEdgeRotatedMinEdge) {
            continue;
        }

        const double aspectScore = 1.0 - std::min(
            std::abs(aspect - cfg::kStandardAspect) / cfg::kStandardAspect, 1.0);
        PlateCandidate candidate;
        candidate.box = box;
        candidate.hasCorners = true;
        for (int i = 0; i < 4; ++i) {
            candidate.corners[i] = corners[i];
        }
        candidate.area = area;
        candidate.aspect = aspect;
        candidate.extent = extent;
        candidate.edgeMean = edgeMean;
        candidate.tiltDeg = tilt;
        candidate.score = 0.35 * aspectScore + 0.15 * std::min(extent / 0.60, 1.0) +
                          0.15 * std::min(edgeMean / 40.0, 1.0) + 0.25 * paintRatio;
        found.push_back(candidate);
    }
    return found;
}

std::vector<PlateCandidate> suppressOverlap(std::vector<PlateCandidate> plates) {
    std::sort(plates.begin(), plates.end(),
              [](const PlateCandidate& a, const PlateCandidate& b) {
                  return a.score > b.score;
              });
    std::vector<PlateCandidate> kept;
    for (const auto& candidate : plates) {
        bool overlapped = false;
        const cv::Point center(
            candidate.box.x + candidate.box.width / 2,
            candidate.box.y + candidate.box.height / 2);
        for (const auto& selected : kept) {
            cv::Rect grown = selected.box;
            const int growX = grown.width / 5;
            const int growY = grown.height / 5;
            grown.x -= growX;
            grown.y -= growY;
            grown.width += growX * 2;
            grown.height += growY * 2;
            if (rectIou(candidate.box, selected.box) > cfg::kNmsIou || grown.contains(center)) {
                overlapped = true;
                break;
            }
        }
        if (overlapped) {
            continue;
        }
        kept.push_back(candidate);
        if (static_cast<int>(kept.size()) >= cfg::kMaxPlates) {
            break;
        }
    }
    return kept;
}

}  // namespace

// =============================================================================
// 1~5. 预处理：灰度、高斯、垂直 Sobel、二值化、闭运算
// =============================================================================

PreprocessResult PlateDetector::preProcess(const cv::Mat& src) const {
    PreprocessResult result;
    if (src.empty()) {
        result.error = "预处理失败：输入图像为空。";
        return result;
    }
    if (src.cols < 32 || src.rows < 32) {
        result.error = "预处理失败：图像尺寸过小。";
        return result;
    }

    if (src.channels() == 1) {
        result.gray = src.clone();
    } else if (src.channels() == 3) {
        cv::cvtColor(src, result.gray, cv::COLOR_BGR2GRAY);
    } else if (src.channels() == 4) {
        cv::cvtColor(src, result.gray, cv::COLOR_BGRA2GRAY);
    } else {
        result.error = "预处理失败：仅支持 1、3、4 通道图像。";
        return result;
    }

    cv::Mat blurred;
    cv::GaussianBlur(
        result.gray, blurred,
        cv::Size(cfg::kGaussianKsize, cfg::kGaussianKsize),
        cfg::kGaussianSigma, cfg::kGaussianSigma);

    // dx=1, dy=0：只响应垂直方向的亮度跳变，对应字符的竖笔画。
    cv::Mat sobel16;
    cv::Sobel(blurred, sobel16, CV_16S, 1, 0, cfg::kSobelKsize);
    cv::convertScaleAbs(sobel16, result.edges);

    if (cfg::kUseOtsu) {
        result.thresholdUsed = cv::threshold(
            result.edges, result.binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        if (result.thresholdUsed < cfg::kMinOtsuThreshold) {
            result.thresholdUsed = cv::threshold(
                result.edges, result.binary, cfg::kFixedThreshold, 255, cv::THRESH_BINARY);
        }
    } else {
        result.thresholdUsed = cv::threshold(
            result.edges, result.binary, cfg::kFixedThreshold, 255, cv::THRESH_BINARY);
    }

    cv::Mat binary = result.binary;
    if (cfg::kEnablePreOpen) {
        const int openSize = ensureOdd(cfg::kOpenKernelSize, 1);
        const cv::Mat openKernel = cv::getStructuringElement(
            cv::MORPH_RECT, cv::Size(openSize, openSize));
        cv::morphologyEx(result.binary, binary, cv::MORPH_OPEN, openKernel);
    }

    // 核宽随图像宽度轻微缩放，并保证为奇数，且不超过图像本身。
    double widthScale = 1.0;
    if (cfg::kScaleKernelWithImage && cfg::kRefImageWidth > 0) {
        widthScale = static_cast<double>(src.cols) / static_cast<double>(cfg::kRefImageWidth);
        widthScale = std::clamp(widthScale, 0.7, 1.5);
    }
    int kernelW = ensureOdd(static_cast<int>(std::lround(cfg::kCloseKernelW * widthScale)), 3);
    int kernelH = ensureOdd(static_cast<int>(std::lround(cfg::kCloseKernelH * std::clamp(widthScale, 0.8, 1.6))), 3);
    if (kernelW >= src.cols) {
        kernelW = ensureOdd(std::max(1, src.cols - 1), 1);
    }
    if (kernelH >= src.rows) {
        kernelH = ensureOdd(std::max(1, src.rows - 1), 1);
    }
    result.closeKernelW = kernelW;
    result.closeKernelH = kernelH;

    const cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernelW, kernelH));
    // 闭运算 = 先膨胀后腐蚀。膨胀把字符间的空隙补上，腐蚀再把外轮廓收回去。
    cv::Mat dilated;
    cv::dilate(binary, dilated, closeKernel, cv::Point(-1, -1), cfg::kCloseIterations);
    cv::erode(dilated, result.mask, closeKernel, cv::Point(-1, -1), cfg::kCloseIterations);

    if (result.mask.empty()) {
        result.error = "预处理失败：形态学结果为空。";
        return result;
    }
    result.ok = true;
    return result;
}

// =============================================================================
// 6~7. 外轮廓提取与蓝牌几何筛选
// =============================================================================

std::vector<PlateCandidate> PlateDetector::getPlateContours(
    const cv::Mat& mask,
    const cv::Mat& edges,
    const cv::Size& imageSize,
    FilterStats& stats) const {
    stats = FilterStats{};
    std::vector<PlateCandidate> candidates;
    if (mask.empty() || imageSize.width <= 0 || imageSize.height <= 0) {
        return candidates;
    }

    // findContours 会改输入图，必须先拷贝。RETR_EXTERNAL 只要最外层轮廓。
    cv::Mat contourInput = mask.clone();
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(contourInput, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    stats.totalContours = static_cast<int>(contours.size());

    const double imageArea = static_cast<double>(imageSize.width) * static_cast<double>(imageSize.height);
    const double minArea = std::max(cfg::kMinAreaAbs, cfg::kMinAreaRatio * imageArea);
    const double maxArea = std::min(cfg::kMaxAreaAbs, cfg::kMaxAreaRatio * imageArea);
    const int maxWidth = static_cast<int>(imageSize.width * cfg::kMaxWidthRatio);
    const int maxHeight = static_cast<int>(imageSize.height * cfg::kMaxHeightRatio);
    const cv::Rect imageRect(0, 0, imageSize.width, imageSize.height);

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea || area > maxArea) {
            ++stats.rejectArea;
            continue;
        }

        const cv::Rect box = cv::boundingRect(contour);
        if (box.width < cfg::kMinPlateWidth || box.height < cfg::kMinPlateHeight ||
            box.width > maxWidth || box.height > maxHeight || box.height <= 0) {
            ++stats.rejectSize;
            continue;
        }

        const double aspect = static_cast<double>(box.width) / static_cast<double>(box.height);
        if (aspect < cfg::kMinAspect || aspect > cfg::kMaxAspect) {
            ++stats.rejectAspect;
            continue;
        }

        // 闭运算后的轮廓边缘常有凹凸，顶点往往不是严格的 4 个。
        // 用“轮廓面积 / 外接矩形面积”衡量是不是一块实心矩形。
        const double extent = area / static_cast<double>(box.area());
        if (extent < cfg::kMinExtent) {
            ++stats.rejectExtent;
            continue;
        }

        const double tilt = longEdgeTiltDeg(cv::minAreaRect(contour));
        if (std::abs(tilt) > cfg::kMaxTiltDeg) {
            ++stats.rejectTilt;
            continue;
        }

        double edgeMean = 0.0;
        if (cfg::kEnableEdgeDensityCheck && !edges.empty()) {
            const cv::Rect safe = box & imageRect;
            if (safe.width <= 0 || safe.height <= 0) {
                ++stats.rejectEdge;
                continue;
            }
            edgeMean = cv::mean(edges(safe))[0];
            if (edgeMean < cfg::kMinEdgeMean) {
                ++stats.rejectEdge;
                continue;
            }
        }

        const double aspectScore = 1.0 - std::min(
            std::abs(aspect - cfg::kStandardAspect) / cfg::kStandardAspect, 1.0);
        const double edgeScore = std::min(edgeMean / 40.0, 1.0);
        PlateCandidate candidate;
        candidate.box = box;
        candidate.area = area;
        candidate.aspect = aspect;
        candidate.extent = extent;
        candidate.edgeMean = edgeMean;
        candidate.tiltDeg = tilt;
        candidate.score = 0.55 * aspectScore + 0.30 * std::min(extent, 1.0) + 0.15 * edgeScore;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PlateCandidate& a, const PlateCandidate& b) {
                  return a.score > b.score;
              });

    std::vector<PlateCandidate> kept;
    for (const auto& candidate : candidates) {
        bool overlapped = false;
        for (const auto& selected : kept) {
            if (rectIou(candidate.box, selected.box) > cfg::kNmsIou) {
                overlapped = true;
                break;
            }
        }
        if (overlapped) {
            continue;
        }
        kept.push_back(candidate);
        if (static_cast<int>(kept.size()) >= cfg::kMaxPlates) {
            break;
        }
    }

    stats.kept = static_cast<int>(kept.size());
    return kept;
}

// =============================================================================
// 8. 绘制车牌框并裁剪 ROI
// =============================================================================

void PlateDetector::drawAndCropPlate(
    const cv::Mat& src,
    const std::vector<PlateCandidate>& plates,
    cv::Mat& annotated,
    std::vector<cv::Mat>& rois) const {
    annotated.release();
    rois.clear();
    if (src.empty()) {
        return;
    }

    annotated = src.clone();
    const cv::Rect imageRect(0, 0, src.cols, src.rows);
    int index = 1;
    for (const auto& plate : plates) {
        cv::Rect expanded = plate.box;
        expanded.x -= cfg::kRoiPadding;
        expanded.y -= cfg::kRoiPadding;
        expanded.width += cfg::kRoiPadding * 2;
        expanded.height += cfg::kRoiPadding * 2;
        expanded &= imageRect;
        if (expanded.width <= 0 || expanded.height <= 0) {
            continue;
        }

        if (plate.hasCorners) {
            cv::Point2f center(0.f, 0.f);
            for (const cv::Point2f& corner : plate.corners) {
                center += corner;
            }
            center *= 0.25f;
            cv::Point2f expandedCorners[4];
            std::vector<cv::Point> polygon(4);
            for (int i = 0; i < 4; ++i) {
                expandedCorners[i] = center + (plate.corners[i] - center) * 1.08f;
                polygon[i] = expandedCorners[i];
            }
            cv::polylines(annotated, polygon, true, cv::Scalar(0, 255, 0), cfg::kDrawThickness, cv::LINE_AA);
            const cv::Mat warped = warpPlate(src, expandedCorners);
            if (!warped.empty()) {
                rois.push_back(warped);
            }
        } else {
            cv::rectangle(annotated, expanded, cv::Scalar(0, 255, 0), cfg::kDrawThickness, cv::LINE_AA);
            rois.push_back(src(expanded).clone());
        }
        const std::string label = "Plate " + std::to_string(index);
        const int textY = std::max(expanded.y - 8, 18);
        cv::putText(annotated, label, cv::Point(expanded.x, textY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        ++index;
    }
}

// =============================================================================
// 单帧入口
// =============================================================================

LocateResult PlateDetector::locatePlate(const cv::Mat& frame) const {
    LocateResult result;
    std::string configError;
    if (!validateConfig(configError)) {
        result.error = configError;
        return result;
    }
    if (frame.empty()) {
        result.error = "输入图像为空。";
        return result;
    }

    const PreprocessResult prepared = preProcess(frame);
    if (!prepared.ok) {
        result.error = prepared.error;
        return result;
    }

    result.edges = prepared.edges;
    result.binary = prepared.binary;
    result.mask = prepared.mask;
    result.closeKernelW = prepared.closeKernelW;
    result.closeKernelH = prepared.closeKernelH;
    result.thresholdUsed = prepared.thresholdUsed;
    std::vector<PlateCandidate> plates = getPlateContours(
        prepared.mask, prepared.edges, frame.size(), result.stats);
    plates = keepPaintedPlates(frame, std::move(plates), cfg::kMinEdgeColorRatio);
    if (cfg::kEnablePlateColorCheck && frame.channels() == 3) {
        FilterStats colorStats;
        std::vector<PlateCandidate> colorPlates = getPlateContours(
            plateColorMask(frame), prepared.edges, frame.size(), colorStats);
        const int maxColorWidth = static_cast<int>(frame.cols * cfg::kMaxColorBlobWidthRatio);
        colorPlates.erase(
            std::remove_if(colorPlates.begin(), colorPlates.end(),
                           [&](const PlateCandidate& plate) {
                               return plate.box.width < cfg::kMinColorBlobWidth ||
                                      plate.box.height < cfg::kMinColorBlobHeight ||
                                      plate.box.width > maxColorWidth ||
                                      plate.box.y <= 1;
                           }),
            colorPlates.end());
        colorPlates = keepPaintedPlates(frame, std::move(colorPlates), cfg::kMinColorBlobRatio);
        plates.insert(plates.end(), colorPlates.begin(), colorPlates.end());
        const std::vector<PlateCandidate> tilted = findTiltedColorPlates(frame, prepared.edges);
        plates.insert(plates.end(), tilted.begin(), tilted.end());
        const std::vector<PlateCandidate> tiltedEdges = findTiltedEdgePlates(frame, prepared.edges);
        plates.insert(plates.end(), tiltedEdges.begin(), tiltedEdges.end());
    }
    result.plates = suppressOverlap(std::move(plates));
    result.stats.kept = static_cast<int>(result.plates.size());
    drawAndCropPlate(frame, result.plates, result.annotated, result.rois);
    result.found = !result.rois.empty();
    result.ok = !result.annotated.empty();
    if (!result.ok) {
        result.error = "绘制结果为空。";
    }
    return result;
}
