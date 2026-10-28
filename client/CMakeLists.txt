cmake_minimum_required(VERSION 3.16)

project(lpr_client VERSION 0.4 LANGUAGES CXX)

# Linux: sudo apt install build-essential cmake qtbase5-dev libopencv-dev
# Windows: Qt 6 MSVC x64 + 带 FFmpeg 的 OpenCV 4。路径见下面的缓存变量。
# libopencv 的 videoio 需要启用 FFmpeg，RTSP 才拉得开。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(WIN32)
    # 这台 Windows 上的 Qt 和 OpenCV。换机器时改这两个缓存变量。
    set(QT_ROOT "F:/software/Qt/6/6.9.3/msvc2022_64" CACHE PATH "Qt 6 MSVC 2022 64-bit 安装目录")
    list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
    set(OpenCV_DIR "D:/Project/QT/Opencv_operatorVideo/third_party/opencv/build" CACHE PATH "OpenCVConfig.cmake 所在目录")
    set(OpenCV_BIN_DIR "${OpenCV_DIR}/x64/vc16/bin" CACHE PATH "OpenCV DLL 目录")
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

if(OpenCV_VERSION VERSION_LESS 4.0)
    message(FATAL_ERROR "需要 OpenCV 4，当前是 ${OpenCV_VERSION}")
endif()

# Visual Studio 是多配置生成器，配置阶段跑探测程序经常找不到 OpenCV DLL。
# Windows 上改由界面里的 ffmpegStatus() 在启动时提示。
if(NOT WIN32)
    set(_ffmpeg_check_src "${CMAKE_BINARY_DIR}/opencv_ffmpeg_check.cpp")
    file(WRITE "${_ffmpeg_check_src}" [[
#include <opencv2/core.hpp>
#include <iostream>
#include <string>

int main() {
    const std::string info = cv::getBuildInformation();
    const auto pos = info.find("FFMPEG:");
    if (pos == std::string::npos) {
        std::cerr << "OpenCV build information has no FFMPEG section\n";
        return 2;
    }
    const auto line_end = info.find('\n', pos);
    const auto yes = info.find("YES", pos);
    if (yes == std::string::npos || (line_end != std::string::npos && yes > line_end)) {
        std::cerr << "OpenCV videoio was built without FFmpeg\n";
        return 3;
    }
    return 0;
}
]])

    try_run(
        OPENCV_FFMPEG_RESULT
        OPENCV_FFMPEG_COMPILED
        "${CMAKE_BINARY_DIR}/opencv_ffmpeg_check"
        "${_ffmpeg_check_src}"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${OpenCV_INCLUDE_DIRS}"
        LINK_LIBRARIES ${OpenCV_LIBS}
        COMPILE_OUTPUT_VARIABLE OPENCV_FFMPEG_COMPILE_LOG
    )

    if(NOT OPENCV_FFMPEG_COMPILED)
        message(FATAL_ERROR "OpenCV FFmpeg 探测程序没编过。\n${OPENCV_FFMPEG_COMPILE_LOG}")
    endif()
    if(NOT OPENCV_FFMPEG_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenCV 未启用 FFmpeg，不能拉 RTSP。请安装带 FFmpeg 的 libopencv-dev，或把 OpenCV_DIR 指到对应构建。")
    endif()
endif()

set(LPR_SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/CaptureThread.h
    src/CaptureThread.cpp
    src/DetectThread.h
    src/DetectThread.cpp
    src/PlateDetector.h
    src/PlateDetector.cpp
)

if(WIN32)
    qt_add_executable(lpr_client WIN32 ${LPR_SOURCES})
else()
    add_executable(lpr_client ${LPR_SOURCES})
endif()

target_include_directories(lpr_client PRIVATE ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    target_link_libraries(lpr_client PRIVATE Qt6::Widgets ${OpenCV_LIBS})
elseif(TARGET opencv_core AND TARGET opencv_imgproc AND TARGET opencv_videoio)
    target_link_libraries(lpr_client PRIVATE Qt5::Widgets opencv_core opencv_imgproc opencv_videoio)
else()
    target_link_libraries(lpr_client PRIVATE Qt5::Widgets ${OpenCV_LIBS})
endif()

if(MSVC)
    target_compile_options(lpr_client PRIVATE /utf-8)
endif()

if(WIN32)
    # 4.10.0 -> opencv_world4100.dll / opencv_videoio_ffmpeg4100_64.dll
    set(_ocv_dll_ver "${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}")
    set(_ffmpeg_dll "${OpenCV_BIN_DIR}/opencv_videoio_ffmpeg${_ocv_dll_ver}_64.dll")
    add_custom_command(TARGET lpr_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OpenCV_BIN_DIR}/$<IF:$<CONFIG:Debug>,opencv_world${_ocv_dll_ver}d.dll,opencv_world${_ocv_dll_ver}.dll>"
            "$<TARGET_FILE_DIR:lpr_client>"
        COMMAND "${QT_ROOT}/bin/windeployqt.exe"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-translations
            "$<TARGET_FILE:lpr_client>"
        COMMENT "Copy OpenCV DLL and deploy Qt runtime"
    )
    if(EXISTS "${_ffmpeg_dll}")
        add_custom_command(TARGET lpr_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ffmpeg_dll}"
                "$<TARGET_FILE_DIR:lpr_client>"
            COMMENT "Copy OpenCV FFmpeg capture DLL"
        )
    else()
        message(WARNING "未找到 ${_ffmpeg_dll}。没有这个 DLL 时窗口能开，但拉不开 RTSP。")
    endif()
endif()

if(WIN32)
    message(STATUS "Qt6 Widgets: ${Qt6Widgets_VERSION}")
else()
    message(STATUS "Qt5 Widgets: ${Qt5Widgets_VERSION}")
endif()
message(STATUS "OpenCV ${OpenCV_VERSION} (videoio + FFmpeg)")
