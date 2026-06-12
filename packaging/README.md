# Windows 发布包说明

本目录存放发布包相关脚本，不存放实际运行包。

## 生成步骤

1. 先构建 Release 版本：

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  AutoFocusSystem.sln `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /m
```

2. 生成 Windows x64 压缩包：

```powershell
.\packaging\package-windows.ps1 -Version 0.1.0
```

3. 将 `release/AutoFocusSystem-v0.1.0-windows-x64.zip` 上传到 GitHub Releases。

## 依赖

脚本默认使用以下本机路径：

- Qt: `D:\Qt\6.5.3\msvc2019_64\bin`
- OpenCV: `D:\openCV\opencv\build\x64\vc16\bin`
- Spinnaker: `D:\Program Files\Teledyne\Spinnaker\bin64\vs2015`

如果依赖安装路径不同，可通过脚本参数覆盖。

