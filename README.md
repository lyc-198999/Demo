# 自动对焦系统项目说明

## 项目结构

本项目是基于 Qt、OpenCV、Spinnaker 和串口电机协议的自动对焦实验程序。后期维护时优先按职责查找文件：

| 文件 | 职责 |
| --- | --- |
| `Demo/main.cpp` | Qt 程序入口，创建并显示主窗口。 |
| `Demo/Demo.h` | `MainWindow` 类声明，集中保存界面、相机、电机和自动对焦状态。 |
| `Demo/Demo.cpp` | 主窗口初始化、参数控件绑定、模式切换、状态栏显示和日志输出。 |
| `Demo/DemoImage.cpp` | 相机帧刷新、图像显示、清晰度刷新和自动对焦周期推进。 |
| `Demo/DemoMotor.cpp` | 串口刷新、驱动器连接、手动移动、急停和电机状态同步。 |
| `Demo/DemoAutoFocus.cpp` | 自动对焦状态机、判峰、过峰确认、小步长复核和最终回焦。 |
| `Demo/Sharpness.cpp/.h` | 0-100 归一化 Tenengrad 清晰度评价函数。 |
| `Demo/GaussianAutoFocus.cpp/.h` | Gauss-Newton 高斯拟合，用于估计焦面位置。 |
| `Demo/SerialPort.cpp/.h` | 正点原子自定义串口协议封装和驱动器状态读取。 |
| `Demo/ImOpenCV.cpp` | FLIR/Spinnaker 相机取帧，并转换为 OpenCV `cv::Mat`。 |

## 自动对焦算法

自动对焦流程位于 `Demo/DemoAutoFocus.cpp`：

1. 自动模式下每次相机刷新后采样当前位置和清晰度。
2. 使用归一化 Tenengrad 清晰度，接口为 `Sharpness::Calculate`。
3. 扫描时持续记录 `autoFocusPositions` 和 `autoFocusSharpnessValues`。
4. 判峰逻辑要求最高点前出现有效上升，并且最高点后出现 3 个有效低于峰值的过峰采样。
5. 接近焦面后自动切换为小步长移动，避免大步长越过焦面。
6. 峰值确认后优先使用高斯拟合中心作为目标；拟合中心异常时退回采样峰值。
7. 最终回焦后会复核当前清晰度，未达到采样峰值比例时继续小步长复核。

关键参数可以直接在界面“电机参数”区域修改：

| 参数 | 默认值 | 用途 |
| --- | --- | --- |
| 手动步数 | 3200 脉冲 | 手动前进/后退单次位移。 |
| 手动速度 | 800 RPM | 手动移动速度。 |
| 手动加减速 | 20 | 手动移动加减速度。 |
| 自动步数 | 3200 脉冲 | 自动对焦常规扫描步长。 |
| 小步长 | 800 脉冲 | 焦面附近扫描、复核和受限修正步长。 |
| 自动速度 | 500 RPM | 自动对焦移动速度。 |
| 自动加减速 | 15 | 自动对焦移动加减速度。 |

## 算法接口

清晰度评价：

```cpp
double sharpness = Sharpness::Calculate(frame);
```

高斯拟合：

```cpp
GaussianAutoFocus::FitResult fit =
    GaussianAutoFocus::FitGaussNewton(positions, sharpnessValues);
```

自动对焦状态机由 `MainWindow::processAutoFocus()` 推进，通常不应在其它文件中直接操作采样数组。需要调整判峰、过峰次数、小步长策略时，优先修改 `Demo/DemoAutoFocus.cpp` 中的具名 `DemoAutoFocusDetail` 辅助函数。

## 编码和维护约定

- 源码、工程文件和文档使用 UTF-8 带签名编码保存。
- 所有异常、日志和界面提示使用中文。
- 不使用未命名命名空间；仅使用具名 `Detail` 或职责命名空间。
- 新增功能应优先放入对应职责文件，避免继续扩大 `Demo.cpp`。
- 旧的勒让德拟合和未使用的 `AutoFocus.*` 已删除，当前算法以 Tenengrad 清晰度和高斯拟合为主。
