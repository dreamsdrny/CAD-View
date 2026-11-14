# CAD · Qiao 品牌与 Windows 打包维护

## 配置入口

| 内容 | 维护位置 |
| --- | --- |
| 程序名称 CAD、图标标识 Qiao、紫色主题 | `src/branding/CMakeLists.txt` |
| 版本号 0.2.1 | 顶层 `CMakeLists.txt` 的 `project(CAD VERSION ...)` |
| 图标形状、渐变、Q 标记与 Qiao 文字 | `tools/branding/IconGenerator.cpp` |
| Qt 品牌 API、主题与资源初始化 | `src/branding/AppBrand.h/.cpp` |
| Qt 图标资源 | `resources/branding.qrc.in` |
| EXE 图标与文件属性 | `resources/windows/CAD.rc.in` |
| 安装路径、快捷方式、卸载与安装界面 | `resources/windows/installer.nsi` |
| 构建 / 分发流程 | `scripts/build-native.ps1` / `scripts/package-native.ps1` |

不修改 `build/generated` 中的文件，它们每次由 CMake 自动生成。

## 图标设计与生成

紫色渐变圆角底，白色 Q 与 CAD 构造线，大尺寸附带 Qiao 文字。16/24/32 像素使用 Q 标记，48/64/128/256 像素显示完整 Qiao。图标由 C++/QPainter 生成，无 Python 或外部图片编辑工具依赖。

生成文件：

```text
build/native/generated/branding/qiao.ico
build/native/generated/branding/qiao.png
```

ICO 通过 Windows RC 嵌入 EXE，Qt 通过静态资源加载同一个 ICO。修改图标生成器后运行 `build.bat`，依赖关系会自动重建图标、资源及 EXE。

紫色只用于品牌、界面强调、绘图预览与框选提示；图纸实体的颜色仍由对象/图层控制。

## 编译和打包

```powershell
.\build.bat
.\package.bat
```

已有验证通过的构建时，跳过重复构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-native.ps1 -SkipBuild
```

打包需要 Qt MinGW kit、匹配 MinGW、CMake/Ninja 和 NSIS 3。脚本默认本机 `.toolchain` 配置，也支持 `-QtRoot`、`-CompilerRoot`、`-NsisRoot` 参数。

流程：

1. 编译、执行原生测试。
2. CMake install 只安装 CAD.exe 和使用说明到新的 staging 目录。
3. windeployqt 复制所需 Qt 插件、DLL 和 MinGW 运行库。
4. 生成带顶层 CAD 文件夹的便携 ZIP。
5. NSIS 生成单文件便携 `CAD-<版本>.exe`。
6. NSIS 生成紫色图标的 `CAD-<版本>-Setup.exe`。
7. 使用 staging 中的 CAD.exe 做独立启动检查。

安装默认按当前用户安装，创建桌面与开始菜单 CAD 快捷方式和系统卸载记录。卸载移除安装的程序文件，用户图纸应保存在自己的工程目录。

## 输出与使用

- `build/native/bin/CAD.exe`：开发构建，使用同目录 Qt DLL。
- `dist/CAD-0.2.1.exe`：单文件便携 EXE，双击运行，无需安装。
- `dist/CAD-0.2.1-Setup.exe`：单个安装 EXE，安装时展开完整应用。
- `dist/CAD-0.2.1-Windows-x64.zip`：便携包，完整解压，运行 `CAD/CAD.exe`。

版本号变更后分发文件名、Windows 文件版本和关于窗口同步读取新版本。若后续改变实际 EXE 文件名，需要同时更新 CMake target、RC、启动/构建/打包脚本和 NSIS 模板。

## 验证

- Qt Test 校验应用名称、窗口名称、Qiao 标识以及 16/32/48/256 像素图标可加载。
- Windows 文件属性显示 ProductName=CAD、OriginalFilename=CAD.exe、对应版本号。
- 程序与安装包含 Windows 图标资源，窗口/任务栏使用 Qt 图标资源。
- 分发包不包含测试 EXE、图标生成器、源码、Python 环境或旧版 CADWorkbench.exe。

本机安装/卸载验证可执行（TemporaryRoot 必须为已存在的临时目录）：

```powershell
.\scripts\verify-package.ps1 -Setup dist/CAD-0.2.1-Setup.exe -Archive dist/CAD-0.2.1-Windows-x64.zip -TemporaryRoot "$env:TEMP"
```

验证在随机新目录中静默安装、启动和卸载，用 `/TESTMODE` 跳过快捷方式和卸载注册项，并检查 EXE 名称/版本/图标及 ZIP 内容。
