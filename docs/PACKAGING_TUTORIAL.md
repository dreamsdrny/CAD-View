# 从源码到可安装软件：Windows 打包教程

这篇教程面向第一次做 Windows 分发的开发者，讲清楚三件事：

1. 为什么一个 `CAD.exe` 拷给别人通常打不开。
2. 怎样把 Qt 程序需要的 DLL 和插件收集齐全。
3. 怎样用 NSIS 把它做成 `CAD-<版本>-Setup.exe`，并验证安装、启动、卸载都正常。

本文对应本仓库现有的实现，所有命令都可以直接在 `D:\Evan\CAD-Software` 下执行。README 偏“怎么用”，本文偏“原理和怎么做”。

---

## 1. 先理解：什么叫“可安装”

开发机上双击 `build\native\bin\CAD.exe` 能运行，是因为：

- 编译器 MinGW 的 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` 在 PATH 里。
- Qt 的 `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll` 等在 Qt SDK 的 `bin` 目录里。
- Qt 平台插件 `platforms\qwindows.dll` 能被 Qt 找到。

换到一台没装 Qt/MinGW 的电脑，这些文件都不存在，程序会直接报“找不到 xxx.dll”或“could not find or load the Qt platform plugin windows”。

所以“可安装”= 交付一个目录，里面包含：

```text
CAD\
    CAD.exe                 你的程序
    Qt6Core.dll 等           Qt 运行时
    libgcc_s_seh-1.dll 等    MinGW 运行时
    platforms\qwindows.dll   Qt 平台插件（必须有）
    imageformats\...         其他按需插件
    docs\...                 使用说明
```

安装器（Setup.exe）只是把这个目录复制到用户机器，并创建快捷方式、注册卸载信息。

---

## 2. 工具链准备

本仓库把工具链放在 `.toolchain` 下，避免污染系统：

| 工具 | 默认路径 | 作用 |
| --- | --- | --- |
| Qt 6 MinGW 套件 | `.toolchain\6.8.3\mingw_64` | Qt 库、`windeployqt.exe` |
| MinGW 13.1.0 | `.toolchain\Tools\mingw1310_64` | `g++.exe`、运行时 DLL |
| CMake / Ninja | `.venv\Lib\site-packages\cmake` | 配置和编译 |
| NSIS 3.11 | `.toolchain\nsis\nsis-3.11` | 生成安装程序 |

检查是否齐全：

```powershell
Test-Path .toolchain\6.8.3\mingw_64\lib\cmake\Qt6\Qt6Config.cmake
Test-Path .toolchain\Tools\mingw1310_64\bin\g++.exe
Test-Path .toolchain\nsis\nsis-3.11\makensis.exe
```

任何一项是 `False`，就先用 `aqtinstall` 装 Qt/MinGW，或安装 NSIS 3 后通过 `-NsisRoot` 参数指定位置。

> Qt 版本、编译器版本、编译器 ABI 必须匹配。这里 Qt 是 MinGW 64 位构建，就一定要用同系列的 MinGW，不能用 MSVC 编译出的 Qt DLL 混搭。

---

## 3. 第一步：编译出 Release 版 CAD.exe

执行：

```powershell
.\build.bat
```

它实际调用 `scripts\build-native.ps1`，核心命令是：

```powershell
cmake -S . -B build/native -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=<QtRoot> `
      -DCMAKE_CXX_COMPILER=<CompilerRoot>/bin/g++.exe
cmake --build build/native --parallel 4
ctest --test-dir build/native --output-on-failure
```

要点：

- `CMAKE_BUILD_TYPE=Release`：分发一定要 Release，Debug 版依赖调试运行库，体积大且不适合发布。
- `CMAKE_PREFIX_PATH` 指向 Qt 套件，CMake 的 `find_package(Qt6 ...)` 才能找到 Qt。
- 顶层 `CMakeLists.txt` 里 `project(CAD VERSION 0.2.1 ...)` 决定了安装包版本号和文件名，改版本只改这里。
- 编译完成后产物在 `build\native\bin\CAD.exe`。

`build-native.ps1` 末尾还会执行一次 `windeployqt` 到 `build\native\bin`，那是给本机开发调试用的。正式打包不直接拿这个目录，后面会重新生成干净的 staging 目录。

---

## 4. 第二步：用 windeployqt 收集 Qt 依赖

手工找 DLL 很容易漏。Qt 自带 `windeployqt.exe`，它会分析 exe 的导入表，把需要的 Qt DLL 和插件复制过去：

```powershell
<QtRoot>\bin\windeployqt.exe --release --compiler-runtime `
    --no-translations --no-opengl-sw <目标目录>\CAD.exe
```

参数含义：

- `--release`：按 Release 模式部署。
- `--compiler-runtime`：同时复制 MinGW 运行库（`libgcc_s_seh-1.dll` 等）。
- `--no-translations`：不复制 Qt 自带翻译，减少体积；程序界面文字由代码提供。
- `--no-opengl-sw`：不复制软件 OpenGL 兼容层，本程序用 Qt Widgets 绘制，不需要。

执行后目标目录大致是：

```text
CAD.exe
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
Qt6Network.dll
D3Dcompiler_47.dll
libgcc_s_seh-1.dll
libstdc++-6.dll
libwinpthread-1.dll
platforms\qwindows.dll
styles\qmodernwindowsstyle.dll
imageformats\qgif.dll  qico.dll  qjpeg.dll
generic\qtuiotouchplugin.dll
networkinformation\qnetworklistmanager.dll
tls\qcertonlybackend.dll  qschannelbackend.dll
```

最常见的“装完打不开”就是漏了 `platforms\qwindows.dll`，或者把 exe 单独拷走而没带上 DLL。记住：**exe 和这些文件必须保持相对位置不变**。

---

## 5. 第三步：用 CMake install 建立干净的分发目录

不要直接把 `build\native\bin` 压缩发出去，因为里面可能有测试程序、旧的 DLL、调试文件。正确做法是让 CMake 只“安装”需要发布的东西：

顶层 `CMakeLists.txt`：

```cmake
install(TARGETS CAD RUNTIME DESTINATION .)
install(FILES README.md docs/USER_GUIDE.md DESTINATION docs)
```

然后执行：

```powershell
cmake --install build/native --prefix <staging>\CAD --config Release
```

这样 `<staging>\CAD` 里只有 `CAD.exe` 和 `docs\`，不会混入测试可执行文件。

`scripts\package-native.ps1` 用一个随机 GUID 建临时 staging 目录，避免上次残留：

```powershell
$stage = Join-Path $build ('package-' + [guid]::NewGuid().ToString('N'))
$stageRoot = Join-Path $stage 'CAD'
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
cmake --install $build --prefix $stageRoot --config Release
windeployqt ... "$stageRoot\CAD.exe"
```

> 为什么要 staging？因为 `build\native\bin` 同时是开发运行目录，内容会变。发布目录必须每次从零生成，结果才可复现。

---

## 6. 第四步：生成便携 ZIP

依赖收集完，直接压缩整个 `CAD` 目录：

```powershell
Compress-Archive -LiteralPath <staging>\CAD `
                 -DestinationPath dist\CAD-0.2.1-Windows-x64.zip -Force
```

用户解压后运行 `CAD\CAD.exe` 即可，不需要管理员权限。便携包也是排查安装器问题的好工具：如果 ZIP 能跑而安装器不能，问题通常出在安装器或安装路径。

### 6.1 生成单文件便携 EXE

ZIP 需要用户先解压。如果只希望发一个文件，可以用 NSIS 把 staging 目录做成自解压启动器：

```powershell
makensis /V3 /WX /INPUTCHARSET UTF8 `
    "/DINPUT_DIR=<staging>\CAD" `
    "/DOUTPUT_FILE=dist\CAD-0.2.1.exe" `
    "/DVERSION=0.2.1" `
    "/DICON_FILE=<build>\generated\branding\qiao.ico" `
    resources\windows\portable.nsi
```

`portable.nsi` 不创建快捷方式，也不写卸载注册表。首次运行时把文件释放到 `%LOCALAPPDATA%\Qiao\CAD-Portable\<版本>`，随后执行 `CAD.exe`。它会把命令行参数转给应用，所以仍然支持把 DXF 拖到 EXE 上打开。

单文件 EXE 的本质仍是解压后运行：Windows 必须能从磁盘加载旁边的 DLL 和 `platforms\qwindows.dll`。它只是把解压步骤自动化了。

双击 `package-exe.bat` 可以一次生成 ZIP 和单文件 EXE。

---

## 7. 第五步：用 NSIS 写安装器

NSIS 脚本在 `resources\windows\installer.nsi`。安装器要做四件事：

1. 把 staging 目录复制到安装位置。
2. 写卸载程序 `Uninstall.exe`。
3. 创建开始菜单/桌面快捷方式。
4. 在注册表登记卸载信息，让“设置 → 应用”能看到它。

### 7.1 基本头和编译参数

```nsis
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

Name "CAD"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Qiao\CAD"
RequestExecutionLevel user
SetCompressor /SOLID lzma
```

- `Unicode true`：支持中文路径和中文界面。
- `RequestExecutionLevel user`：按当前用户安装，不弹 UAC，普通用户可装。
- `InstallDir` 用 `%LOCALAPPDATA%`，符合每用户安装习惯。
- `Name` 是显示名，`OutFile`、`VERSION`、`ICON_FILE` 由打包脚本通过 `/D` 传入。

打包脚本调用：

```powershell
makensis.exe /V3 /WX /INPUTCHARSET UTF8 `
    "/DINPUT_DIR=$stageRoot" `
    "/DOUTPUT_FILE=$setup" `
    "/DVERSION=$version" `
    "/DICON_FILE=$build\generated\branding\qiao.ico" `
    resources\windows\installer.nsi
```

`/WX` 表示警告也当错误，防止生成半成品。

### 7.2 安装页面

```nsis
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"
```

`MUI_PAGE_DIRECTORY` 让用户选安装目录，`MUI_PAGE_FINISH` 可带“启动 CAD”勾选项。

### 7.3 安装主体

```nsis
Section "CAD" SEC_MAIN
    SetShellVarContext current
    SetOutPath "$INSTDIR"
    File /r "${INPUT_DIR}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\Qiao CAD"
    CreateShortcut "$SMPROGRAMS\Qiao CAD\CAD.lnk" "$INSTDIR\CAD.exe" "" "$INSTDIR\CAD.exe" 0
    CreateShortcut "$DESKTOP\CAD.lnk" "$INSTDIR\CAD.exe" "" "$INSTDIR\CAD.exe" 0

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "DisplayName" "CAD"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
SectionEnd
```

- `File /r` 会递归复制整个 staging 目录，所以 `platforms\` 等子目录会一起被带上。
- `SetShellVarContext current` 保证快捷方式写到当前用户，不要求管理员。
- 卸载注册表放在 `HKCU`，与每用户安装对应。

### 7.4 卸载主体

```nsis
Section "Uninstall"
    Delete "$DESKTOP\CAD.lnk"
    Delete "$SMPROGRAMS\Qiao CAD\CAD.lnk"
    RMDir "$SMPROGRAMS\Qiao CAD"

    Delete "$INSTDIR\CAD.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\qt.conf"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\tls"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD"
SectionEnd
```

注意：如果以后 `windeployqt` 新增了别的插件目录（例如 `iconengines`、`iconengines`、`translations`），**卸载段也要同步加 `RMDir /r`**，否则卸载后会留空目录。只删已知程序文件，不删用户自己放在安装目录里的图纸。

### 7.5 测试模式

为了自动化验证时不污染本机，脚本支持 `/TESTMODE`：

```nsis
Function .onInit
    StrCpy $TestMode 0
    ${GetParameters} $Parameters
    ClearErrors
    ${GetOptions} $Parameters "/TESTMODE" $0
    ${IfNot} ${Errors}
        StrCpy $TestMode 1
    ${EndIf}
FunctionEnd
```

`/TESTMODE` 时跳过快捷方式、卸载注册表和注册表清理。这样 CI 或验证脚本可以反复安装、启动、卸载。

---

## 8. 第六步：一键打包

`package.bat` 调用 `scripts\package-native.ps1`，完整顺序是：

1. 调 `build-native.ps1` 编译并跑测试（加 `-SkipBuild` 可跳过）。
2. 从 CMakeCache 读取版本号。
3. 建随机 staging 目录，`cmake --install`。
4. `windeployqt` 部署 Qt/MinGW 运行时。
5. 生成 `dist\CAD-<版本>-Windows-x64.zip`。
6. `makensis` 用 `portable.nsi` 生成 `dist\CAD-<版本>.exe` 单文件便携版。
7. `makensis` 用 `installer.nsi` 生成 `dist\CAD-<版本>-Setup.exe`。
8. 直接运行 staging 里的 `CAD.exe --smoke-test`，确认部署后的程序能启动。

执行：

```powershell
.\package.bat
```

只重新打包、不重新编译：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package-native.ps1 -SkipBuild
```

输出：

```text
dist\CAD-0.2.1.exe
dist\CAD-0.2.1-Setup.exe
dist\CAD-0.2.1-Windows-x64.zip
```

只想要单文件便携 EXE 时，使用 `package-exe.bat`；只想要 ZIP 时，使用 `package-portable.bat`。

---

## 9. 第七步：验证安装包

`scripts\verify-package.ps1` 做真实安装测试：

```powershell
.\scripts\verify-package.ps1 `
    -Setup dist\CAD-0.2.1-Setup.exe `
    -Archive dist\CAD-0.2.1-Windows-x64.zip `
    -SingleExe dist\CAD-0.2.1.exe `
    -TemporaryRoot "$env:TEMP"
```

它依次检查：

1. 静默安装到临时新目录：`Setup.exe /S /TESTMODE /D=<目录>`。
2. `CAD.exe` 存在，文件属性里 `ProductName=CAD`、`OriginalFilename=CAD.exe`。
3. EXE 带 Windows 图标。
4. `platforms\qwindows.dll` 存在。
5. 运行 `CAD.exe --smoke-test`，退出码为 0。
6. 静默卸载，确认 `CAD.exe` 被删除。
7. 检查 ZIP 里有 `CAD/CAD.exe` 和 `CAD/platforms/qwindows.dll`，且不含测试程序、源码、Python 环境。
8. 启动 `dist\CAD-0.2.1.exe`，等它释放缓存，再运行释放出的 `CAD.exe --smoke-test`。

`--smoke-test` 在 `src\ui\main.cpp` 里实现：显示主窗口，800ms 后正常退出，因此能覆盖“加载 Qt 插件并创建窗口”这条最容易失败的路径。

> 验证一定要用安装后的程序，而不是 `build\native\bin\CAD.exe`。后者能跑不代表分发目录完整。

---

## 10. 常见问题排查

### 10.1 双击 Setup.exe 没反应或被拦截

- Windows SmartScreen 对未签名 exe 会提示“Windows 已保护你的电脑”。这是未做代码签名的正常现象，可点“更多信息 → 仍要运行”。
- 企业环境可能禁止运行未签名安装包，需要代码签名证书或让 IT 加白名单。
- 杀软误报可以用 VirusTotal 复查；发布前最好做签名。

### 10.2 安装后启动报缺少 DLL

- 先确认安装目录里 `Qt6Core.dll`、`libgcc_s_seh-1.dll`、`platforms\qwindows.dll` 都在。
- 设置 `QT_DEBUG_PLUGINS=1` 再运行，会打印插件加载失败原因：

```powershell
$env:QT_DEBUG_PLUGINS=1
& "$env:LOCALAPPDATA\Programs\Qiao\CAD\CAD.exe"
```

- 如果提示找不到 `qwindows.dll`，检查 `platforms` 文件夹是否被误删、是否被杀软隔离。

### 10.3 中文用户名/中文路径

- NSIS 已是 `Unicode true`，Qt 6 也按 UTF-16 处理路径，一般没问题。
- 若遇到问题，优先测试 `%LOCALAPPDATA%\Programs\Qiao\CAD` 之外的纯英文路径，以定位是否为路径编码问题。

### 10.4 版本号没变

- 版本来自顶层 `CMakeLists.txt` 的 `project(CAD VERSION 0.2.1 ...)`。
- EXE 文件属性来自 `resources\windows\CAD.rc.in`。
- 安装器版本来自 `package-native.ps1` 读 `CMakeCache.txt` 后传入 `/DVERSION`。
- 改版本后要重新 configure，只改代码不重新跑 CMake 可能仍是旧值。

### 10.5 安装包变大或变慢

- `windeployqt` 会带上实际用到的 Qt 模块。不要手动删除看起来“没用”的 DLL，删错会导致运行失败。
- 需要更小体积时，先确认哪些 Qt 模块没被链接，再在 CMake 里裁掉对应组件，而不是事后删文件。

### 10.6 想支持双击 .dxf/.dwg 打开

当前安装器没有注册文件关联。要支持的话，在安装段增加：

```nsis
WriteRegStr HKCU "Software\Classes\.dxf" "" "QiaoCAD.DXF"
WriteRegStr HKCU "Software\Classes\QiaoCAD.DXF\shell\open\command" "" '"$INSTDIR\CAD.exe" "%1"'
```

并在卸载段删除对应键。`.dwg` 同理。注意：这会抢占已有 CAD 软件的关联，建议做成可选组件并提示用户。

---

## 11. 最小可复现练习

如果你想从零理解，可以按下面顺序做一个最小版本：

1. 建一个 Qt Widgets 程序，编译出 `demo.exe`。
2. 新建空目录 `demo-package`，把 `demo.exe` 放进去。
3. 执行：

```powershell
windeployqt --release --compiler-runtime demo-package\demo.exe
```

4. 把 `demo-package` 拷到另一台没装 Qt 的电脑，确认能运行。
5. 写一个最小 NSIS 脚本：

```nsis
Unicode true
Name "Demo"
OutFile "Demo-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\Demo"
RequestExecutionLevel user
Section
    SetOutPath "$INSTDIR"
    File /r "demo-package\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd
Section "Uninstall"
    Delete "$INSTDIR\demo.exe"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\platforms"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
```

6. 用 `makensis Demo.nsi` 生成安装包，安装后运行、再卸载。

走完这一遍，再看本仓库的 `package-native.ps1` 和 `installer.nsi` 就会很清楚每一行在做什么。

---

## 12. 发布前检查清单

- [ ] `.\build.bat` 编译和 CTest 全部通过。
- [ ] `.\package.bat` 成功生成 Setup 和 ZIP。
- [ ] `verify-package.ps1` 安装/启动/卸载全部通过。
- [ ] 在一台没装 Qt、没装 MinGW 的干净 Windows 上试装并启动。
- [ ] 安装目录含 `platforms\qwindows.dll`。
- [ ] 安装/卸载后开始菜单、桌面、设置里的应用列表都正确。
- [ ] 版本号、文件名、EXE 文件属性一致。
- [ ] 发布物不包含测试 exe、源码、`.venv`、Python 文件。

---

## 13. 相关文件速查

| 文件 | 作用 |
| --- | --- |
| `CMakeLists.txt` | 版本号、install 规则 |
| `src/branding/CMakeLists.txt` | 图标生成、`.rc`/`.qrc` 配置 |
| `resources/windows/installer.nsi` | NSIS 安装/卸载逻辑 |
| `resources/windows/portable.nsi` | 单文件便携自解压启动器 |
| `scripts/build-native.ps1` | 配置、编译、测试、本机部署 |
| `scripts/package-native.ps1` | staging、windeployqt、ZIP、单文件 EXE、Setup |
| `scripts/verify-package.ps1` | 安装/启动/卸载/解压/单文件自动验证 |
| `build.bat` / `package.bat` / `package-exe.bat` / `package-portable.bat` | 给非 PowerShell 用户的入口 |
