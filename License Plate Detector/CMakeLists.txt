cmake_minimum_required(VERSION 3.21)

project(LicensePlateDetector VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.9.3，Visual Studio 2022 x64 套件。换机器时改这个缓存变量。
set(QT_ROOT "F:/software/Qt/6/6.9.3/msvc2022_64" CACHE PATH "Qt 6 MSVC 2022 64-bit 安装目录")
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")

# OpenCV 4.10 预编译包，使用 opencv_world4100。
set(OpenCV_DIR "D:/Project/QT/Opencv_operatorVideo/third_party/opencv/build" CACHE PATH "OpenCVConfig.cmake 所在目录")
set(OpenCV_BIN_DIR "${OpenCV_DIR}/x64/vc16/bin" CACHE PATH "OpenCV DLL 目录")

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

find_package(OpenCV REQUIRED)

qt_add_executable(LicensePlateDetector
    WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mainwindow.ui
    src/PlateDetector.h
    src/PlateDetector.cpp
)

target_link_libraries(LicensePlateDetector PRIVATE Qt6::Widgets ${OpenCV_LIBS})
target_include_directories(LicensePlateDetector PRIVATE ${OpenCV_INCLUDE_DIRS})

if(MSVC)
    target_compile_options(LicensePlateDetector PRIVATE /utf-8)
endif()

# 生成后把 OpenCV DLL 和 Qt 运行库放到 exe 旁边，双击即可启动。
add_custom_command(TARGET LicensePlateDetector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OpenCV_BIN_DIR}/$<IF:$<CONFIG:Debug>,opencv_world4100d.dll,opencv_world4100.dll>"
        "$<TARGET_FILE_DIR:LicensePlateDetector>"
    COMMAND "${QT_ROOT}/bin/windeployqt.exe"
        "$<IF:$<CONFIG:Debug>,--debug,--release>"
        --no-translations
        "$<TARGET_FILE:LicensePlateDetector>"
    COMMENT "Copy OpenCV DLL and deploy Qt runtime"
)
