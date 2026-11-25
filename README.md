# CAD · Qiao · C++ / Qt 6 工程 CAD

> **CAD-View** 仓库主线：C++20 + Qt 6 Widgets 原生实现，运行程序不依赖 Python。
> 早期基于 Python / PySide6 / ezdxf 的 0.1 原型已归档在 [`legacy/python`](legacy/python) 供对照。

正式程序已重构为 **C++20 + Qt 6 Widgets + CMake**。文件读取、几何计算、画布和工程分析均在 C++ 中实现，运行程序不需要 Python。

## 启动

在 `D:\Evan\CAD-Software` 双击 **`start.bat`**，或直接运行：

```powershell
.\build\native\bin\CAD.exe
```

启动后默认新建**空白图纸**，不加载 PCB 演示板；需要示例时使用“文件 → 打开演示板”。主窗口默认采用紧凑尺寸，并按当前屏幕可用区域自动限制，不会超出桌面；手动调整后下次启动会恢复窗口大小和位置。

画布获得焦点后可用**方向键移动图纸**（`Shift` 加速、`Ctrl` 微调），`+` / `-` 缩放，`Home` 显示全部；命令栏输入时方向键仍正常移动文字光标。也可从“视图 → 平移与缩放”菜单操作。

打开指定图纸：

```powershell
.\build\native\bin\CAD.exe "D:\图纸\board.dxf"
```

本机已准备 Qt 6.8.3 / MinGW 13.1 工具链，构建后的 EXE 和 Qt DLL 位于 `build/native/bin`。移动到其他 Windows x64 电脑时复制**整个 bin 文件夹**，包含 `platforms/qwindows.dll`，不能只复制 EXE。

程序名称为 **CAD**，采用紫色 **Qiao** 图标和紫色界面强调色。正式分发请使用 `dist` 中的安装包或便携 ZIP；两者只包含应用及所需运行库。

## Windows 打包

日常开发双击 `build.bat` 编译，再双击 `start.bat` 启动，无需安装。只分发免安装版时，双击 **`package-portable.bat`** 生成 ZIP；需要单文件免安装 EXE 时双击 **`package-exe.bat`**。说明见 [如何实现软件压缩包安装](docs/如何实现%20%20软件%20成压缩包实现安装的.md)。

双击 **`package.bat`**，自动执行编译、测试、Qt 运行库部署、便携包、单文件 EXE 和安装 EXE 生成：

```text
dist/CAD-0.3.0.exe                单文件便携 EXE，双击运行，无需安装
dist/CAD-0.3.0-Setup.exe          单文件安装包，安装 CAD 并创建桌面/开始菜单快捷方式
dist/CAD-0.3.0-Windows-x64.zip    便携包，完整解压后运行 CAD/CAD.exe
```

单文件 EXE 是自解压便携启动器，不写卸载记录、不建快捷方式；首次运行会把运行库解压到 `%LOCALAPPDATA%\Qiao\CAD-Portable\0.3.0`，之后直接复用。版本升级使用独立目录，不复用旧版本程序。

安装包包含全部必要 DLL，不要求目标机器安装 Python 或 Qt SDK。默认安装到当前用户 `%LOCALAPPDATA%\Programs\Qiao\CAD`，可在安装界面更改路径。从源码制作安装程序的完整步骤见 [如何将代码打包成安装程序](docs/如何将代码打包成安装程序.md) 和 [详细打包教程](docs/PACKAGING_TUTORIAL.md)。

本机已准备 NSIS 3.11，其他开发机安装 NSIS 3 后可通过 `scripts/package-native.ps1 -NsisRoot "C:\Program Files (x86)\NSIS"` 指定位置。详细维护说明见 [docs/BRANDING_AND_PACKAGING.md](docs/BRANDING_AND_PACKAGING.md)。

开发测试程序 `cad_core_tests.exe` / `cad_ui_tests.exe` 使用 Qt Test 和测试插件，运行测试请使用 `build.bat` 或对应 Qt 开发终端；它们不是交付应用的一部分。

`.venv/Scripts` 是 Python 工具环境，C++ 源码不放在那里。当前使用 `.venv` 中安装的开发工具提供 CMake/Ninja，应用运行不加载 Python；也可使用独立 CMake/Ninja/Qt SDK 构建。

## 目录与模块

```text
CAD-Software/
├── CMakeLists.txt              顶层构建、Qt 模块、原生测试
├── CMakePresets.json           Release 构建预设
├── .clang-format              C++ 排版约定
├── src/
│   ├── branding/              名称/版本/紫色主题/Qiao 图标资源管理
│   ├── core/                  cad_core：二维 CAD 核心
│   │   ├── Geometry.*         点线、求交、投影、面积、bulge 圆弧
│   │   ├── Entity.*           图元、变换、几何属性
│   │   ├── Document.*         文档、图层、增量撤销/恢复
│   │   ├── SpatialIndex.*     BVH 空间索引：视口/选择/捕捉
│   │   └── Editing.*          偏移、分解、合并、修剪/延伸
│   ├── io/                    cad_io：文件适配
│   │   ├── DxfAdapter.*       原生 ASCII DXF 读写、源记录保留
│   │   └── DwgAdapter.*       内置 LibreDWG 读取 / 可选 ODA 转换
│   ├── engineering/           cad_engineering：专业分析
│   │   ├── Analysis.*         板框、孔径、间距、焊盘、工艺边、重复
│   │   ├── Panelization.*     PCS→SET 拼版、工艺边、连接筋、融合
│   │   └── FpcMaterialPlanner.* SET→Panel 排料、利用率、搜索、报告
│   └── ui/                    cad_ui：Qt 桌面界面
│       ├── CadCanvas.*        批量绘制、交互、捕捉、测量
│       ├── PanelizationDialog.* 唯一的“拼版”窗口：SET 拼版 + 材料方案页
│       ├── MainWindow.*       菜单、属性/图层/分析面板、后台任务
│       └── main.cpp           原生入口
├── scripts/build-native.ps1   配置 → 编译 → 测试 → 部署 Qt 运行库
├── scripts/package-native.ps1 安装 EXE 与便携 ZIP 打包
├── tools/branding/            C++ 多尺寸图标生成器
├── resources/                 Qt 图标资源、Windows 图标/版本与安装模板
├── tests/native/              C++ 核心/Qt 交互测试、独立 DXF 校验脚本
├── docs/                      架构说明与迭代路线
├── legacy/python/             重构前 0.1 Python 原型归档
├── build/native/bin/          可直接启动的原生 EXE 与 DLL（不提交）
├── .toolchain/                本机 Qt/MinGW SDK（不提交）
├── .venv/                     开发辅助工具环境（不提交）
├── build.bat                  一键编译和部署
├── package.bat                一键构建并打包
└── start.bat                  启动 C++ 正式程序
```

详细设计见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
从打开原图到 SET 拼版、材料方案、导出和常见问题的完整步骤见 [使用教程](docs/使用教程.md)。
拼版与材料方案的统一规则、公式和边界见 [拼版与材料方案说明](docs/PANELIZATION.md)。
代码职责、已完成的首阶段重构及后续计划见 [代码梳理与重构计划](docs/代码梳理与重构计划.md)。

## 已实现功能

| 模块 | 当前 C++ 版能力 |
| --- | --- |
| 文件 | 原生 ASCII DXF 打开、保存、另存为；后台读写；多文档；保存修改提示；DWG 外部转换适配 |
| 视图 | 滚轮围绕鼠标缩放、中键平移、显示全部、框选缩放、网格 |
| 绘制 | 直线、多段线、矩形、圆、圆弧、椭圆、文字 |
| 选择 | 单选、Shift 多选、左→右窗口选择、右→左交叉选择、全选可见对象 |
| 编辑 | 移动、复制、旋转、镜像（保留原对象）、删除、矩形阵列、限定范围偏移/修剪/延伸、分解、合并 |
| 测量 | 两点、水平、垂直、连续距离，三点夹角，坐标；对象长度/半径/直径/面积/周长属性 |
| 标注 | 对齐线性标注、标注文字编辑，`<>` 自动实测值；DXF DIMENSION + 图形块输出 |
| 图层/属性 | 新建、删除空层、当前层、显示/隐藏、锁定、颜色、线宽；编辑坐标、半径、文字、高度；批量层/颜色 |
| 辅助 | 端点、中点、圆心、圆象限点、直线段交点/垂足捕捉；正交；45° 极轴；命令精确输入 |
| 历史 | 每文档独立增量撤销/恢复，最近 100 个操作 |
| 安全 | 每 60 秒后台生成独立恢复快照；异常退出后恢复未保存副本，不覆盖原图 |
| PCB/FPC | 后台板框尺寸、孔径分组、间距、焊盘尺寸、板边/外部对象、同属性重复图形检查与清理 |
| 拼版 | 唯一 `PANEL` 入口，窗口内分“拼版 SET”和“材料方案 / 利用率”两页：确认单 PCS 外轮廓，旋转/交错/固定 SET，工艺框与自动/手动连接筋，PCS/ARRAY/SET 尺寸；当前 SET 自动参与材料比较，可搜索不同 PCS→SET 拼法，计算 SET→Panel 排料、利用率、备料与预计出货；导出 DXF/PDF/可选 ODA DWG、CSV/PDF 报告；[规则与限制](docs/PANELIZATION.md) |
| 输出 | PDF 适合页面导出，不包含网格或光标；未实现指定工程比例 |

首版偏移支持直线、圆和无圆弧段的直边凸闭合多段线。修剪/延伸支持两条直线，操作时指定目标线与端点。合并支持同图层、端点连续的直线。分解支持矩形/多段线，bulge 圆弧拆为真实圆弧。

## 常用操作

- `Ctrl+N/O/S`：新建/打开/保存；`Ctrl+Shift+S`：另存为。
- `Ctrl+Z/Y`：撤销/恢复；`Ctrl+A`：全选；`Delete`：删除。
- `F3/F7/F8/F10`：捕捉/网格/正交/极轴；`Home`：显示全部。
- `Esc`：结束工具；`Enter` 或右键：完成多段线/连续测量；画多段线时 `C` 闭合。
- 单击图形自动显示几何属性；双击属性表可编辑。
- 双击文字 / 标注文字，或选中后按 `Ctrl+E`（“编辑 → 编辑文字 / 标注”），可修改内容和公差；`%%P` 显示为 `±`，`%%D` 为 `°`，`%%C` 为 `Ø`。
- 工具栏和“对象属性”面板提供“编辑文字 / 标注”按钮；选择可编辑文字时自动切换到对象属性。双击文字空隙或受支持标注的尺寸线也可打开文字编辑，不用输入命令。
- 外部 DXF 标注中可安全识别的单行文字支持编辑：`<>%%P0.1` 保留原实测值并修改公差；`120.00±0.1` 使用固定显示文字。仅改变显示内容，不改变板框、尺寸线、箭头或实测尺寸，支持撤销 / 恢复和保存重开。
- 锁定图层可以选择与测量，不能编辑；隐藏图层不参与显示与按图层检查。

底部命令栏支持：

```text
LINE
0,0
@20,0
20<90
```

`X,Y` 为绝对坐标，`@ΔX,ΔY` 为相对上一点坐标，`距离<角度` 为相对上一点极坐标，角度单位为度。单独数值沿光标方向输入长度。空命令按 Enter 完成操作。

常用命令：`LINE/L`、`PLINE/PL`、`RECTANG/REC`、`CIRCLE/C`、`ARC`、`ELLIPSE`、`TEXT`、`DDEDIT/TEXTEDIT/DIMEDIT`、`MOVE/M`、`COPY/CO`、`MIRROR/MI`、`DIST/DI`、`ANGLE`、`ID`、`DIM/DIMALIGNED`、`AREA`、`ERASE/E`、`UNDO/U`、`REDO`、`ZOOM/Z/EXTENTS`、`OFFSET/O`、`ROTATE/RO`、`ARRAY`、`TRIM/TR`、`EXTEND/EX`、`EXPLODE/X`、`JOIN/J`、`PANEL/PANELARRAY`。旧命令 `FPCPLAN/MATERIAL` 现在也打开同一个“拼版”窗口，不再单独弹出材料窗口。

### PCB 演示检查

从“文件 → 打开演示板”打开演示板；板框为 80 × 50 mm，四个 Ø3.2 mm 孔，2 × 3 mm 焊盘。启动软件默认不会打开演示板。

1. 板框：清除选择，左侧范围选择 `BOARD`，执行“板框分析”。
2. 孔径：清除选择，范围选 `DRILL`，执行“孔径分析”，得到 Ø3.2 × 4。
3. 焊盘/间距：清除选择，范围选 `PAD`，执行对应检查。
4. 工艺边：只选一个板框，检查其与其他所有可见对象（包括板框同层）；可隐藏不参与的丝印层。
5. 点击结果高亮并居中相关对象。再次按图层检查前清除该选择。

分析按文档单位，不自动将圆解释为真实钻孔。闭合对象在间距检查中作为实心区域，包含/重叠为 0；工艺边检查计算轮廓距离。曲线离散目标误差为 0.005 图形单位；分析会报告误差上界，超过分段预算时停止并报告。不包括网络、线宽、铜厚、阻焊和叠层规则。普通闭合焊盘按轴对齐包围盒，原生矩形按边长，圆按直径。

## 文件兼容边界

- 当前原生适配器显示/编辑二维 `LINE`、`LWPOLYLINE`、传统 `POLYLINE/VERTEX/SEQEND`、`CIRCLE`、`ARC`、`ELLIPSE`、基本左对齐 `TEXT` 和本软件创建的对齐 `DIMENSION`。
- 多段线 bulge、起终宽度和常宽读写保留，画布/PDF 按宽度绘制；长度、面积和间距仍按中心轮廓计算。
- 支持由上述二维对象组成的 `INSERT` 块展开，包括基点、平移、旋转、等比缩放/镜像、嵌套及行列阵列。无曲线且无宽度的折线/直线可非等比缩放。未编辑实例保留 INSERT，编辑后的实例输出为独立图元。
- 二维实心 `HATCH` 闭合多段线边界（包括 bulge 圆弧和多个环）、二维 `SOLID`/`TRACE` 可只读显示；支持有控制点、节点向量及可选正权重的 1–8 次二维 `SPLINE` 近似只读预览。预览保留原始源记录，不参与工程间距和工艺边计算；样条预览也不参与捕捉和长度测量。含不支持对象或只读预览的块尽可能部分显示，整块预览只读，保存时保留原始块引用。
- `MTEXT` 支持基本内容、`\P`/`^J` 显式段落、按栏宽自动换行、行距、锚点和旋转的只读预览；文字绘制、空间索引与显示全部共用文字排版边界。普通 `TEXT` 支持左/中/右、上/中/下对齐及宽度因子的只读预览。复杂字体/格式和 Aligned/Fit 拉伸文字尚不等价于专业 CAD。外部 `DIMENSION` 如有可用的匿名图形块，则按原始块坐标显示（包括块内嵌套标注），不重新计算标注值；顶层标注有唯一直接单行文字且能安全识别数值时允许文字 / 公差编辑，其他几何仍只读。保存时仅为修改的标注复制图形块并更新文字覆盖，不影响共享同块的其他标注，保留原样式、定义点、箭头及原始块；复杂格式覆盖或嵌套标注仍只读。
- 图案填充、边线式 HATCH 边界、部分 SPLINE（如周期样条、仅有拟合点）、非等比缩放曲线、曲线拟合/网格 POLYLINE、无可用图形块的外部标注、布局和 3D/非默认 OCS 对象当前**不完全显示/编辑**，保留源记录并在命令区提示。布局对象数量会单独提示；无可显示模型对象或所有图层隐藏时，界面明确说明空白原因。样条按每个节点区间固定采样，不能保证与原曲线的误差上界或中望 CAD 显示完全一致。
- 新文档输出 R2010 ASCII DXF；二进制 DXF 暂不支持。Genesis/R12 传统多段线可直接读写，旧版新增图元采用传统记录，颜色使用 ACI；旧版文件新增椭圆/标注需转为新版文档。
- 编辑源对象尽量保留句柄、线型、扩展数据；复制/分解产生的新对象主要继承几何、图层与颜色，非完整源属性复制。
- 新文字限定为单行 TEXT；含换行的文字不能保存，避免破坏 DXF 记录。中文文字/图层使用 DXF Unicode 转义；显示使用系统字体，未实现 SHX 等价渲染。
- DXF 保存使用 `QSaveFile` 原子提交；后台保存过程中继续编辑时，不会将未写入的更改标为已保存。
- **0.2.3 直接读取原始 DWG**：没有 ODA 时，自动调用随包提供的 GNU LibreDWG 0.14 `dwg2dxf.exe`，不依赖 Python，不自动改读同名 DXF。识别 R11/R12、R13、R14、2000、2004、2007、2010、2013、2018 系列文件头；跨软件兼容取决于标准 DWG 格式和对象类型，并非按软件名称过滤。转换后的 ASCII DXF 由现有二维适配器显示，专有代理对象、3D、布局及不支持图元不能保证完整。引擎诊断在命令区报告，转换/解析失败明确报错。
- 已配置或自动找到 [ODA File Converter](https://www.opendesign.com/guestfiles/oda_file_converter) 时优先使用 ODA；查找已配置路径、`CAD_ODA_CONVERTER`、程序附近目录、PATH 和 Program Files 中的常见安装目录。ODA 不随包分发，保存 DWG 仍需 ODA，输出 ACAD2018。没有 ODA 时“保存”自动进入 DXF 另存为，保留原 DWG。转换中间数据不保证无损保留原 DWG，因此建议始终保留原图备份。
- LibreDWG 在独立进程中运行，临时文件参数为固定英文相对名，支持中文用户目录及方括号文件名。可用 `CAD_LIBREDWG_READER` 指定其他 `dwg2dxf.exe`；该变量存在但为空/无效时不会自动使用内置引擎。转换可取消、超时 120 秒，输入和输出受大小限制，损坏文件/未知文件头明确报错。
- 构建/打包脚本自动下载并校验固定版本 LibreDWG 官方发行物，缓存于 `build/dependencies`。完整对应源码、GPLv3 许可证与声明随程序放在 `tools/libredwg`；首次构建需联网，后续可使用已校验缓存。手工 CMake 部署时也需运行 `scripts/prepare-dwg-reader.ps1 -Destination <程序目录>/tools/libredwg`。
- DXF 默认限制 256 MiB、200 万条记录、400 万组码、单个多段线 100 万个顶点、块展开 200 万对象及 32 层嵌套；后台打开时可取消，开发接口可调整限制。

## 构建与测试

本机双击 **`build.bat`** 即完成 Release 编译、CTest 和 Qt DLL 部署。

自动恢复的使用方式和限制见 [使用说明](docs/USER_GUIDE.md#自动恢复未保存的图纸)。原文件不被恢复快照覆盖，恢复后需另存；检查周期 60 秒不等于绝对零丢失。

大图离屏读取、索引、绘制、平移/缩放和峰值内存的可重复测试及本机参考结果见 [大图性能基准](docs/大图性能基准.md)。

也可执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native.ps1
```

使用自己的 Qt MinGW 安装：

```powershell
.\scripts\build-native.ps1 -QtRoot "C:\Qt\6.8.3\mingw_64" -CompilerRoot "C:\Qt\Tools\mingw1310_64"
```

Qt Creator / CLion / Visual Studio 可直接打开顶层 `CMakeLists.txt`。MSVC 构建需要匹配的 Qt MSVC kit；MinGW 与 MSVC kit 不可混用。使用普通 CMake/Ninja 构建：

```powershell
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/mingw_64"
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

上述命令需在对应编译器/Qt 的开发终端运行。部署独立目录：

```powershell
windeployqt --release --compiler-runtime build/native/bin/CAD.exe
```

测试包括：解析圆弧面积、编辑算法、增量历史、空间查询、PCB 规则、DXF 往返/源数据保留、50,000 对象压力用例、鼠标绘图、命令、后台读取。

新增 `recovery` 测试组覆盖异常进程退出/残留锁、未命名/多文档、保存/关闭与后台快照的竞态、完整性校验、损坏文件和写入失败；界面测试覆盖恢复入口、取消退出、按标签清理，以及后台保存期间继续编辑。`panel` 验证轮廓、拼版、连接筋与尺寸往返；`fpc_material` 验证两级排布、面积、良率/出货取整和工艺约束。当前 CTest 包含 `core`、`panel`、`fpc_material`、`ui`、`recovery`、`benchmark_smoke` 六组。

独立验证共享标注块的文字编辑与自动恢复（仅开发环境使用 ezdxf，不是运行依赖）：

```powershell
.\.venv\Scripts\python.exe tests/native/DxfSafetyInterop.py build/native/bin/cad_dxf_safety_probe.exe
```

该脚本生成有效 DXF，审计原始/编辑/恢复文件的错误与修复，检查标注样式、箭头、定义点、共享原始块和其他对象不被修改，并验证原图哈希与编辑/恢复输出。

本机验证：4 个 CTest 测试组通过，独立 ezdxf 审计无错误/无修复；包含程序名称/多尺寸图标、恢复与文件安全验证。50,000 条直线压力用例不包含窗口首次绘制，耗时随机器及负载变化，不代表真实复杂图纸性能。

可选用独立读取器验证 DXF（仅开发验证使用 Python）：

```powershell
.\.venv\Scripts\python.exe tests/native/DxfInterop.py build/native/bin/CAD.exe
```

使用本地 ODA 和不同版本的真实 DWG 样本验证转换、保存及重新打开（模型对象数量检查）：

```powershell
.\.venv\Scripts\python.exe tests/native/DwgInterop.py build/native/bin/CAD.exe "C:/path/ODAFileConverter.exe" "C:/samples/r2010.dwg" "C:/samples/r2018.dwg"
```

该脚本不验证所有块、布局、文字和扩展属性的完整性；这些仍需配合专业 CAD 阅读器和样本矩阵检查。

运行真实多版本 DWG 的打开矩阵（上游源码附带样本，包含 AC1009–AC1032 九代格式；可在末尾追加生产图纸）：

```powershell
.\.venv\Scripts\python.exe tests/native/DwgReadMatrix.py build/native/bin/CAD.exe build/dependencies/libredwg-0.14/source/libredwg-0.14/test/test-data build/native/dwg-matrix "D:\图纸\board.dwg"
```

矩阵检查实际载入路径仍为 DWG、使用真实读取引擎、源文件哈希不变，并记录可显示对象数。AC1009 上游样本带非零 Z，现有二维渲染器不投影这类图形，单独标记为 `not-rendered`，不算显示成功。其余二维样本必须有可显示对象；不等价于所有厂商/专有对象的完整性验收。

检查实际界面后台打开流程并输出 JSON 报告和截图（失败返回非零退出码）：

```powershell
.\build\native\bin\CAD.exe --check-open "D:\图纸\board.dwg" "D:\检查\open.json" "D:\检查\open.png"
```

报告包含实际载入路径、对象/图层数、可见对象数、显示状态（`displayStatus`）、边界与兼容性警告。`success` 仅表示解析成功，不代表图纸完整显示；还需检查 `hasDisplayableEntities` 和警告。测试转换器桩仅验证进程参数与安全保存，不等价于真实 ODA/DWG 兼容验证。

## 性能与下一步

当前实现了原生批量绘制、视口裁剪、BVH 空间查询、局部路径缓存、增量撤销和后台任务。它们消除了原型中的主要结构性瓶颈，**不承诺任意图纸无卡顿或无 Bug**。路径缓存首次建立、编辑后的索引重建仍在界面线程；空间索引当前整树重建；尚未实现 GPU/LOD、加载百分比进度、保存及分析任务取消和真实百万对象性能验收。

后续路线见 [docs/ROADMAP.md](docs/ROADMAP.md)，包括完整 DXF/DWG SDK、块和布局、圆角/倒角、拉伸、填充、完整标注、Gerber/Excellon 对照、工程报告和大图性能优化。
