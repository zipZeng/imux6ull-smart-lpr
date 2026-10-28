# 基于 i.MX6ULL 的远程车牌定位

开发板只负责拿摄像头画面。广州的一台云服务器只转发这一路画面。看画面和找车牌在 Linux 或 Windows 的 Qt 上位机上做，两边用的是同一份 `client/` 源码。开发板和上位机都只连接这台服务器，互相不直连。

这个仓库里的车牌程序只画框、裁出车牌小图，不识别车牌上的字。定位算法在 `client/src/PlateDetector.cpp`，阈值在该文件顶部的 `cfg`。

## 各台机器干什么

```text
i.MX6ULL 开发板                 云服务器 134.175.47.224
USB 摄像头 /dev/video1          MediaMTX 监听 :8554
        │                                │
        │  RTSP/TCP 推流                 │  RTSP/TCP 拉流
        └──── rtsp://134.175.47.224:8554/live ────┘
                                         │
                          Linux 的 lpr_client，或 Windows 的 lpr_client.exe
                          Qt 窗口显示画面，需要时再做车牌定位
```

| 机器 | 要跑的程序 | 不要做的事 |
| --- | --- | --- |
| 开发板 | 拷过去的 `board/stream.sh`，调用板上已有的 `ffmpeg` | 不编译 Qt，不跑 OpenCV，不把整个仓库交叉编译上去 |
| 云服务器（轻量应用服务器，广州，Ubuntu） | `mediamtx` 和 `server/mediamtx.yml` | 不解码，不找车牌，不保存录像 |
| Linux 电脑 | `client/build/lpr_client` | 开发板的脚本不用在这里执行 |
| Windows 电脑 | `client\build\Release\lpr_client.exe` | 不把虚拟机里的 `client/build` 拷过来编译 |

开发板通过手机热点上网，再把流推到公网地址。热点的连接步骤见 [docs/开发板连接手机热点.md](docs/开发板连接手机热点.md)。`board/stream.sh` 读取 YUYV，再编成 MJPEG 推出去。这块摄像头的 MJPEG 模式没有帧，不能原样转发。

更早一次是把 MediaMTX 放在 VMware 里的 Ubuntu 上，靠 Windows 端口转发。那次的地址和报错留在 [docs/开发流程与排错.md](docs/开发流程与排错.md)，当前不要再推到 `192.168.172.240`。

## 目录

| 路径 | 内容 |
| --- | --- |
| `board/stream.sh` | 板端推流脚本。默认 `/dev/video1`，读取 YUYV 640×480、10 fps，编成 MJPEG 后走 RTSP/TCP |
| `server/mediamtx.yml` | MediaMTX 的项目配置。路径名是 `live`，端口 8554，只允许 TCP |
| `client/` | Linux 和 Windows 共用的 Qt 上位机。Linux 用 Qt 5，Windows 用 Qt 6 |
| `docs/架构设计.md` | 画面从摄像头到窗口的每一步：采什么、在哪编码、用什么协议、MediaMTX 拿到后做什么 |
| `docs/联调说明.md` | 公网服务器地址、带宽和流量、延迟参数、UDP 对照 |
| `docs/开发板连接手机热点.md` | 每次上电后加载 8188EU、连上手机热点并拿到地址 |
| `docs/定位算法.md` | 车牌框怎么找出来：四条路径、每个函数、阈值和样图结果 |
| `samples/` | 选图和改阈值后用来回归的图片。不部署到开发板或云服务器 |
| 仓库根目录的 `mediamtx.yml` | 解压 MediaMTX 时带上的官方默认配置，大约 760 行。不要用它启动 |

项目用的是 `server/mediamtx.yml`。可执行文件 `mediamtx` 从发布页下载，不放进 Git。云服务器上把程序和这份短配置放在同一目录，启动命令是 `./mediamtx mediamtx.yml`。

## 云服务器

MediaMTX 跑在公网 `134.175.47.224` 上，系统是 Ubuntu。程序和配置放在服务器的 `~/imux6ull-smart-lpr/`：可执行文件 `mediamtx`，配置是仓库里的 `server/mediamtx.yml`（拷过去后文件名是 `mediamtx.yml`）。压缩包自带的那份长配置不要拿来启动。

轻量应用服务器的防火墙要放行入站 TCP `8554`。配置里只允许 TCP，不用开 UDP。带宽是 4 Mbps，每月流量 300 GB，够联调，不适合全天挂着推。

SSH 里前台启动的进程会在断开连接时退出。要在关掉终端后继续听端口：

```bash
cd ~/imux6ull-smart-lpr
nohup ./mediamtx mediamtx.yml > mediamtx.log 2>&1 &
```

看到日志里的 `[RTSP] listener opened on :8554` 就可以。服务器重启后这条命令要再执行一次。

## 上位机

同一份 `client/` 在两种系统上各编译一次。Linux 用 Qt 5，Windows 用 Qt 6。地址栏默认都是 `rtsp://134.175.47.224:8554/live`。

在看画面的 Linux 上安装依赖并编译：

```bash
sudo apt install build-essential cmake qtbase5-dev libopencv-dev vlc
cmake -S client -B client/build
cmake --build client/build
./client/build/lpr_client
```

`lpr_client` 依赖这台机器上的 Qt 5 和 OpenCV，不能拷到开发板上跑。

Windows 编译只需要 `client/CMakeLists.txt` 和 `client/src/`。不要拷虚拟机里的 `client/build`，那是 Linux 的编译缓存；如果已经拷过去，先删掉再配置。Qt 和 OpenCV 用 `client/CMakeLists.txt` 里的 `QT_ROOT`、`OpenCV_DIR`，当前是 `F:\software\Qt\6\6.9.3\msvc2022_64` 和 `D:\Project\QT\Opencv_operatorVideo\third_party\opencv\build`。换机器时改这两个路径。样图在仓库的 `samples/`，编译用不到；从 `client\build\Release` 启动时，选图对话框会向上找到这一层。

```bat
cmake -S client -B client\build -G "Visual Studio 17 2022" -A x64
cmake --build client\build --config Release
```

`client\build\Release\lpr_client.exe` 旁边会带上 Qt 运行库和 OpenCV 的 FFmpeg DLL。把这个目录拷到别的 Windows 电脑可以直接打开。

## 按这个顺序开

1. 确认云服务器上的 MediaMTX 仍在听 8554。日志里要有 `listener opened on :8554`。
2. 开发板按 [docs/开发板连接手机热点.md](docs/开发板连接手机热点.md) 连上热点并拿到地址，确认摄像头在 `/dev/video1`，然后推流：

   ```bash
   ./stream.sh rtsp://134.175.47.224:8554/live
   ```

   服务器日志出现 `is publishing to path 'live'` 再开上位机。没有这句就去拉流，会得到 404，日志是 `no stream is available on path 'live'`。

3. Linux 上执行 `./client/build/lpr_client`，或在 Windows 上双击 `lpr_client.exe`。地址栏保持默认的 `rtsp://134.175.47.224:8554/live`。点「开始」看画面，点「开始定位」才画车牌框。

没有开发板时，也可以先打开上位机，点「选择图片」，选 `samples/demo_synthetic.bmp`。左侧应出现一个红框，右侧出现车牌小图和「检测到车牌」。这一步不经过云服务器，用来确认当前这套阈值还在。

没有装上位机的电脑可以用 VLC，并且要走 TCP：`vlc --rtsp-tcp rtsp://134.175.47.224:8554/live`。这个地址没有账号密码，知道 URL 的人都能看。

## 上位机窗口

- 「开始 / 停止」：连接或断开 RTSP。
- 「开始定位 / 停止定位」：在拉流之后，大约每 200 毫秒拿最新一帧找车牌。画面仍按视频帧率刷新。
- 「选择图片」：不拉流时，用一张本地图片做定位。样图在 `samples/`。
- 「查看边缘与掩码」：看这次定位的中间图，用来对照算法，不是给最终用户看的。
- 右侧：有没有找到车牌、裁出来的车牌、宽高比、得分、倾斜角。红框画在左侧画面上。

## 进一步看

系统设计、线程和算法在整条链路里的位置见 [docs/架构设计.md](docs/架构设计.md)。四条找牌路径、阈值和样图结果见 [docs/定位算法.md](docs/定位算法.md)。公网地址、带宽、延迟和 UDP 见 [docs/联调说明.md](docs/联调说明.md)。每次上电后把开发板连上手机热点见 [docs/开发板连接手机热点.md](docs/开发板连接手机热点.md)。虚拟机那次联调的步骤和报错见 [docs/开发流程与排错.md](docs/开发流程与排错.md)。
