# C++ / Qt 模块架构

## 依赖方向

```text
CAD (main) ── cad_branding (名称 / Qiao 图标 / 紫色主题)
       ↓
cad_ui (Qt Widgets / Concurrent / PDF)
       ↓                    ↓
cad_io              cad_engineering
       └──────────┬─────────┘
                  ↓
              cad_core
          Qt Core + Qt Gui 数值类型
```

`core` 不依赖 Widgets；专业分析不调用窗口对象；文件适配不持有画布。可独立测试并接入未来命令行批处理。

## 品牌与分发

`src/branding` 管理程序名称、版本读取、图标加载与样式。品牌常量在其 `CMakeLists.txt` 中集中定义，版本源于顶层 `project(... VERSION ...)`。

`tools/branding/IconGenerator.cpp` 在构建时生成紫色 Qiao 多尺寸 ICO/PNG，Qt `.qrc` 管理窗口图标，Windows `.rc` 将图标和版本信息嵌入 `CAD.exe`。运行时不依赖外部图标路径。

`scripts/package-native.ps1` 通过 CMake install 建立新的干净分发目录，部署 Qt DLL，再输出便携 ZIP 和 NSIS 安装 EXE，防止旧构建文件或测试程序被混入分发包。详见 `BRANDING_AND_PACKAGING.md`。

## 模型

- 模型坐标 `double`、Y 向上；画布用 `QTransform` 映射到屏幕。
- `EntityId` 为文档内单调递增 ID，`QHash` 按 ID 查询；图层以名称索引。
- `sourceKey` 为源 DXF 记录身份，与 ID 独立；复制产生新的 ID 和空 sourceKey。
- `Entity` 存储真实几何与 bulge，显示路径是缓存派生数据。圆/多段线面积和长度采用解析计算。
- `Document` 管理单位、图层、图元与修改状态；当前绘图图层和选择是交互状态，不计为图形编辑。

## 增量历史

每个 `Change` 只存受影响图元的 before/after，图层以 Qt 隐式共享值保存。编辑事务一次提交，验证失败不提交；撤销删除 after、插回 before，恢复方向相反。最多 100 个操作，独立 saved cursor 记录保存点和分叉状态。

## 渲染与空间索引

单个 QWidget 批量使用 QPainter 绘制，不为每个对象创建独立 QWidget/QGraphicsItem。

- `SpatialIndex` 为平衡 BVH，叶节点最多 16 条记录，以包围盒最长轴分割。
- 视口绘制、点击候选、框选候选、对象捕捉和最小间距候选均使用包围盒查询。
- 每个实体按需缓存 QPainterPath：首次可见、选择或导出时创建，图元变化后失效；颜色/图层可见状态独立读取。
- 画布裁剪不影响模型；PDF 调用同一文档绘制入口并排除网格/选择/光标。

当前索引在编辑后整树重建，首次可见路径准备仍在 GUI 线程。这是后续增量索引、后台路径准备与分级显示的优化点；暂无 GPU 性能承诺。

## 后台任务

QtConcurrent 线程池执行 DXF/DWG 读写和工程检查，QFutureWatcher 在主线程接收结果。

- 工作线程使用独立 Document 值快照，不访问 QWidget。
- Qt 容器隐式共享，工作线程只读快照；界面修改时自动分离。
- 保存完成后仅当几何、图层、单位仍等于保存快照时标记已保存。
- 检查完成后比对文档与当前标签，变化后不发布旧检查结果。
- 文档有任务时阻止销毁；退出前等待任务结束。后台打开可取消；保存和分析目前无取消/百分比进度。

## 自动恢复

`src/ui/RecoveryManager.*` 独立管理恢复快照，主窗口只提供恢复列表及文档生命周期通知。

- `Document::revision()` 随编辑、撤销和恢复单调递增，结合标题/路径判断是否需要重新生成快照；`markUnsaved()` 使恢复文档保持未保存状态，不伪造一条撤销记录。
- 每 60 秒检查所有文档，使用主线程创建的 Document 值快照串行提交 QtConcurrent 工作任务，不在工作线程访问窗口。手动入口可立即请求快照。快照成功不改变保存点。
- 每实例使用 UUID 会话目录及 `QLockFile`；活动实例的快照不进入恢复列表，异常退出留下的锁由 Qt 的进程检查识别。文件为有限长 JSON 元数据 + DXF 数据，带版本、源路径、标题、当前层、UTC 时间和 SHA-256 校验；通过禁用直接写入回退的 `QSaveFile` 原子提交。
- 恢复时使用现有 DXF 解析器，清空文档保存路径，并标记未保存。原恢复快照在新副本快照提交前保留；明确保存或关闭/放弃修改才清理对应快照。写入期间关闭文档不会让迟到的结果复活，取消整窗口退出不清除已经确认过的其他文档。
- 不存储撤销历史、选择/测量记录/视口，不是无损 DWG 备份；图纸保留能力以 DXF 适配器为准。默认 DXF 数据限制 256 MiB，损坏快照单独报告，未写入成功的修改无法恢复。

## DXF 适配

原生 ASCII DXF 组码解析器，没有 Python 运行时调用。

1. 解析组码和值，识别 section/table/record。
2. 支持的二维模型对象转为 Entity，存 baseline 和 sourceKey。
3. 未支持对象保留在原始文档数据中，界面报告。
4. 保存保留未修改对象记录；已修改对象更新几何字段与属性；删除对象移除源记录。
5. 新对象输出标准 DXF 图元；对齐标注输出 DIMENSION 及匿名图形块。
6. 补齐新增句柄、表记录计数和注册应用，避免独立读取器自动修复。
7. QSaveFile 原子提交，失败不截断目标。

此适配器是明确限定范围的首版实现。完整商业 CAD 兼容需要替换/扩展 `DxfAdapter` 与 `DwgAdapter`，评估 ODA Drawings SDK、libdxfrw 等库并建立真实文件兼容矩阵。

## 工程分析

### 专用拼版

`engineering/FpcMaterialPlanner` 提供软板二级规划：已验证PCS→SET有限候选搜索、SET→材料工作区网格/双分区混排、可区分的面积口径、PCS/整SET良率取整与最小材料张数。0.3.0 起材料页作为 `ui/PanelizationDialog` 内的 `MaterialPlanSection` 维护，不再有独立 `FpcMaterialDialog`；当前 SET 自动作为基准，QtConcurrent 中搜索、取消/预算约束，修改输入会使结果失效，CSV/PDF报告和材料图使用同一计算结果。自动搜索不承诺全局最优，不加入硬板规则，详见 `PANELIZATION.md`。

`core/ContourOperations` 负责真实几何边界、显式选边闭合/分叉/自交校验及 polygon 边界转换；`engineering/Panelization` 负责仅旋转/平移的 PCS 排布、固定 SET、精确轮廓净距、工艺框、连接筋和融合输出。`ui/PanelizationDialog` 使用原文档副本选边，预览参数变化并生成全新毫米文档，不沿用源 DXF 的未显示记录。融合曲线使用明确弦高公差，PCS 模板保留 bulge。

`core/DimensionLayout` 为拼版毫米标注提供共享的文字/箭头/尺寸线布局，画布与 DXF 图形块使用同一入口；格式、小数位和字高存 CADWB XDATA 以便本软件重开。普通测量标注维持原 mm/mil 行为。生成后的拼版是普通实体，没有持续关联式参数模型；手动改动 PCS 后需重新生成尺寸。操作与边界见 `PANELIZATION.md`。

分析以显式选区/图层定义对象用途。孔径仅统计圆，焊盘仅检查闭合形状；不推断铜层网络或钻孔镀孔类型。曲线离散目标误差 0.005 文档单位，超过分段预算时中止并报告误差上界；输出实体 ID 便于界面定位。后续应新增规则模板、角色映射、制造属性、误差区间和报告结构。
