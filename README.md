# AutoFocusSystem

基于 Qt、OpenCV、FLIR Spinnaker 和串口电机控制的自动对焦实验软件。项目包含实时图像显示、清晰度评价、自动对焦搜索、电机控制、图像保存和日志导出功能。

## 功能

- 实时采集 FLIR/Spinnaker 相机图像，并显示当前清晰度。
- 通过串口控制步进电机，支持手动前进、后退和急停。
- 自动对焦流程包含初始方向判断、爬山搜索、小步长扫描、高斯拟合和最终回焦。
- 支持保存当前原始图像、导出运行日志，并可在自动对焦完成后自动保存实验结果。
- 右侧页签式控制面板用于运行、连接、参数和数据管理。

## 项目结构

```text
AutoFocusSystem/
├─ AutoFocusSystem.sln              # Visual Studio 解决方案
├─ AutoFocusSystem/                 # 主程序源码和工程文件
│  ├─ MainWindow.cpp/.h/.ui          # 主窗口、界面初始化、数据保存和日志导出
│  ├─ MainWindowAutoFocus.cpp        # 自动对焦状态机和搜索策略
│  ├─ MainWindowImage.cpp            # 相机帧刷新、图像显示和清晰度计算调度
│  ├─ MainWindowMotor.cpp            # 串口连接、电机控制和急停
│  ├─ MotorSerialPort.cpp/.h         # 正点原子电机串口协议封装
│  ├─ SpinnakerCamera.cpp            # FLIR/Spinnaker 相机取帧
│  ├─ Sharpness.cpp/.h               # Tenengrad 清晰度评价
│  ├─ GaussianAutoFocus.cpp/.h       # Gauss-Newton 高斯拟合
│  ├─ AppTheme.qss                   # Qt 样式
│  └─ AutoFocusSystem.qrc            # Qt 资源文件
├─ docs/                             # 算法和使用说明
├─ tools/                            # 独立实验工具
└─ packaging/                        # Windows 打包脚本和发布说明
```

## 自动对焦逻辑

自动对焦主流程位于 `AutoFocusSystem/MainWindowAutoFocus.cpp`，由 `MainWindow::processAutoFocus()` 周期推进。

1. 自动模式下每次相机刷新后记录当前位置和清晰度评价值。
2. 初始阶段固定均匀采样 5 点，并对位置和清晰度做线性拟合。
3. 根据拟合斜率符号确定后续扫描方向。
4. 进入爬山搜索后，如果清晰度连续三次下降，则停止搜索并回到历史峰值。
5. 接近焦面时切换小步长扫描；切换条件要求最近 3 点斜率同时超过固定阈值和动态阈值。
6. 峰值确认后优先使用高斯拟合中心作为目标位置，异常时回退到采样峰值。
7. 最终回焦后复核目标位置和当前清晰度，满足条件后结束自动对焦。

更详细的算法说明见 [docs/autofocus-algorithm.md](docs/autofocus-algorithm.md)。

## 构建环境

当前工程按以下本机环境配置：

- Visual Studio 2022 / MSBuild v143
- Qt 6.5.3 msvc2019_64
- OpenCV 4.12.0
- Teledyne FLIR Spinnaker SDK
- Windows x64

Debug 构建示例：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  AutoFocusSystem.sln `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

Release 构建示例：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  AutoFocusSystem.sln `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m
```

## 发布包

Windows 运行包由 `packaging/package-windows.ps1` 生成。脚本会复制 Release 版 `AutoFocusSystem.exe`，调用 `windeployqt` 收集 Qt 运行库，并补充 OpenCV 和 Spinnaker 运行库。

```powershell
.\packaging\package-windows.ps1 -Version 0.1.0
```

生成结果位于：

```text
release/AutoFocusSystem-v0.1.0-windows-x64.zip
```

`release/` 和 `dist/` 不进入源码仓库。

## 运行说明

1. 连接 FLIR 相机和电机驱动器。
2. 启动程序后在“连接”页签选择串口和波特率。
3. 点击“连接”，确认电机状态正常。
4. 手动模式下可用“前进/后退”调整初始位置。
5. 切换到自动模式后，程序开始自动采样、搜索焦面并回焦。
6. 在“数据”页签可保存当前图像、导出日志或打开保存目录。

## 注意

- 工程文件包含本机依赖路径，迁移到其它机器时需要同步调整 Qt、OpenCV 和 Spinnaker 路径。
- Release 包不包含相机驱动安装程序，目标机器仍需安装对应 Spinnaker 驱动或运行库。
- 自动对焦参数应结合实际电机步距、镜头行程和样品情况调整。

