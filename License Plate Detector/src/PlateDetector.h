#pragma once

// 车牌定位算法接口。此头文件只依赖 OpenCV，不能包含任何 Qt 头文件。

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

/// 轮廓筛选过程中的淘汰数量，方便对照中间图调参。
struct FilterStats {
    int totalContours = 0;
    int rejectArea = 0;
    int rejectSize = 0;
    int rejectAspect = 0;
    int rejectExtent = 0;
    int rejectTilt = 0;
    int rejectEdge = 0;
    int kept = 0;
};

/// 通过几何规则后的一个车牌候选。
struct PlateCandidate {
    cv::Rect box;
    cv::Point2f corners[4]{};
    bool hasCorners = false;
    double area = 0.0;
    double aspect = 0.0;
    double extent = 0.0;
    double edgeMean = 0.0;
    double tiltDeg = 0.0;
    double score = 0.0;
};

/// 预处理各阶段的图。mask 是闭运算之后、真正拿去找轮廓的二值掩码。
struct PreprocessResult {
    cv::Mat gray;
    cv::Mat edges;
    cv::Mat binary;
    cv::Mat mask;
    int closeKernelW = 0;
    int closeKernelH = 0;
    double thresholdUsed = 0.0;
    bool ok = false;
    std::string error;
};

/// 单帧定位结果。found 表示是否得到车牌 ROI，plates 里是车牌框，rois 是裁剪图。
struct LocateResult {
    cv::Mat annotated;
    cv::Mat edges;
    cv::Mat binary;
    cv::Mat mask;
    std::vector<cv::Mat> rois;
    std::vector<PlateCandidate> plates;
    FilterStats stats;
    int closeKernelW = 0;
    int closeKernelH = 0;
    double thresholdUsed = 0.0;
    bool found = false;
    bool ok = false;
    std::string error;
};

/// 传统车牌定位。只输出车牌框和 ROI，不做文字识别。
class PlateDetector {
public:
    LocateResult locatePlate(const cv::Mat& frame) const;

    PreprocessResult preProcess(const cv::Mat& src) const;

    std::vector<PlateCandidate> getPlateContours(
        const cv::Mat& mask,
        const cv::Mat& edges,
        const cv::Size& imageSize,
        FilterStats& stats) const;

    void drawAndCropPlate(
        const cv::Mat& src,
        const std::vector<PlateCandidate>& plates,
        cv::Mat& annotated,
        std::vector<cv::Mat>& rois) const;
};
