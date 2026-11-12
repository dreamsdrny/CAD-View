from __future__ import annotations

import math
from pathlib import Path
import sys

from PySide6.QtCore import QSettings, Qt, QTimer
from PySide6.QtGui import QAction, QActionGroup, QColor, QFont, QKeySequence, QPainter
from PySide6.QtPrintSupport import QPrinter
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QColorDialog, QComboBox, QDialog, QDialogButtonBox,
    QDockWidget, QDoubleSpinBox, QFileDialog, QFormLayout, QHBoxLayout, QHeaderView,
    QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMainWindow,
    QMessageBox, QPushButton, QSpinBox, QTabWidget, QTableWidget, QTableWidgetItem,
    QTextEdit, QToolBar, QVBoxLayout, QWidget,
)

from . import analysis, editing, fileio, geometry as g
from .canvas import CadCanvas, TOOLS
from .model import Document, Entity, Layer, demo_document

STYLE = """
QMainWindow, QWidget { background: #18232e; color: #d9e5ee; font-family: 'Microsoft YaHei', 'Segoe UI'; font-size: 12px; }
QMenuBar, QToolBar { background: #1d2b38; spacing: 5px; border: none; }
QMenuBar::item { padding: 7px 12px; }
QMenuBar::item:selected, QMenu::item:selected { background: #2e4659; }
QMenu { background: #223240; border: 1px solid #3c5365; padding: 5px; }
QMenu::item { padding: 7px 24px; }
QToolButton { padding: 7px 9px; border: 1px solid transparent; border-radius: 4px; }
QToolButton:hover, QPushButton:hover { background: #2d4355; }
QToolButton:checked { background: #22483f; color: #69e6c9; border: 1px solid #3f8a79; }
QPushButton { background: #263c4e; border: 1px solid #385568; padding: 7px 12px; border-radius: 4px; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: #101c26; border: 1px solid #344d60; padding: 6px; border-radius: 3px; }
QTextEdit, QListWidget, QTableWidget { background: #121d27; border: 1px solid #2d4253; selection-background-color: #31536a; }
QHeaderView::section { background: #223342; border: none; padding: 7px; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: #1b2a37; padding: 10px 18px; border-right: 1px solid #263a4a; }
QTabBar::tab:selected { background: #223b47; color: #69e6c9; border-bottom: 2px solid #56d9bd; }
QDockWidget::title { background: #213240; padding: 8px; font-weight: bold; }
QStatusBar { background: #1d2b38; }
QCheckBox { spacing: 6px; padding: 4px; }
QLabel#brand { color: #56d9bd; font-size: 16px; font-weight: bold; padding: 5px 12px; }
QLabel#hint { color: #91a9bb; padding: 5px 0; }
QToolTip { background: #294456; color: #fff; border: 1px solid #52748a; padding: 5px; }
"""


def number_widget(value=0, minimum=-1e9, maximum=1e9):
    widget = QDoubleSpinBox()
    widget.setRange(minimum, maximum)
    widget.setDecimals(6)
    widget.setValue(value)
    return widget


class MainWindow(QMainWindow):
    def __init__(self, load_demo=True):
        super().__init__()
        self.settings = QSettings("EngineeringTools", "CADWorkbench")
        self.setWindowTitle("CAD Workbench · 轻量化工程 CAD")
        self.resize(1440, 920)
        self.tabs = QTabWidget()
        self.tabs.setTabsClosable(True)
        self.tabs.setMovable(True)
        self.tabs.tabCloseRequested.connect(self.close_tab)
        self.tabs.currentChanged.connect(self.active_changed)
        self.setCentralWidget(self.tabs)
        self._refreshing = False
        self.tool_actions = {}
        self._build_menus()
        self._build_tools()
        self._build_properties()
        self._build_layers()
        self._build_analysis()
        self._build_console()
        self.coord_label = QLabel("X 0.0000    Y 0.0000")
        self.count_label = QLabel()
        self.statusBar().addPermanentWidget(self.count_label)
        self.statusBar().addPermanentWidget(self.coord_label)
        self.add_document(demo_document() if load_demo else Document())
        self.log("欢迎使用 CAD Workbench 0.1 · 模型空间 / 二维工程工作台")
        self.log("滚轮缩放 · 中键平移 · 双击图形属性查看数值 · F8 正交 · F3 捕捉")
        self.log("命令示例：LINE → 0,0 → @20,0；或输入 20<45 指定相对距离和角度。")

    @property
    def canvas(self) -> CadCanvas | None:
        widget = self.tabs.currentWidget()
        return widget if isinstance(widget, CadCanvas) else None

    @property
    def document(self):
        return self.canvas.document if self.canvas else None

    def action(self, text, callback, shortcut=None, checkable=False):
        action = QAction(text, self)
        if shortcut:
            action.setShortcut(QKeySequence(shortcut))
        action.setCheckable(checkable)
        action.triggered.connect(callback)
        return action

    def _build_menus(self):
        file_menu = self.menuBar().addMenu("文件")
        self.new_action = self.action("新建", lambda: self.add_document(Document()), "Ctrl+N")
        self.open_action = self.action("打开 DXF / DWG…", self.open_file, "Ctrl+O")
        self.save_action = self.action("保存", self.save_file, "Ctrl+S")
        for a in (self.new_action, self.open_action, self.save_action):
            file_menu.addAction(a)
        file_menu.addAction(self.action("另存为…", lambda: self.save_file(save_as=True), "Ctrl+Shift+S"))
        file_menu.addSeparator()
        file_menu.addAction(self.action("导出 PDF（适合页面）…", self.export_pdf))
        file_menu.addAction(self.action("设置图形单位…", self.set_units))
        file_menu.addAction(self.action("配置 DWG 转换器…", self.configure_converter))
        file_menu.addAction(self.action("打开演示板", lambda: self.add_document(demo_document())))
        file_menu.addSeparator()
        file_menu.addAction(self.action("退出", self.close, "Alt+F4"))
        edit_menu = self.menuBar().addMenu("编辑")
        self.undo_action = self.action("撤销", lambda: self.history(False), "Ctrl+Z")
        self.redo_action = self.action("恢复", lambda: self.history(True), "Ctrl+Y")
        edit_menu.addActions([self.undo_action, self.redo_action])
        edit_menu.addSeparator()
        edit_menu.addAction(self.action("全选可见对象", lambda: self.canvas.select_ids(set(self.canvas.items_by_id)), "Ctrl+A"))
        edit_menu.addAction(self.action("删除", lambda: self.canvas.delete_selected(), "Delete"))
        edit_menu.addAction(self.action("移动", lambda: self.canvas.set_tool("move")))
        edit_menu.addAction(self.action("复制", lambda: self.canvas.set_tool("copy")))
        edit_menu.addAction(self.action("旋转…", self.rotate_selected))
        edit_menu.addAction(self.action("镜像", lambda: self.canvas.set_tool("mirror")))
        edit_menu.addAction(self.action("矩形阵列…", self.array_selected))
        edit_menu.addAction(self.action("偏移…", self.offset_selected))
        edit_menu.addAction(self.action("修剪直线…", lambda: self.trim_extend(False)))
        edit_menu.addAction(self.action("延伸直线…", lambda: self.trim_extend(True)))
        edit_menu.addAction(self.action("分解多段线", self.explode_selected))
        edit_menu.addAction(self.action("合并直线", self.join_selected))
        edit_menu.addAction(self.action("编辑 / 批量属性…", self.edit_properties))
        self.draw_menu = self.menuBar().addMenu("绘图")
        self.measure_menu = self.menuBar().addMenu("测量")
        view_menu = self.menuBar().addMenu("视图")
        self.fit_action = self.action("显示全部", lambda: self.canvas.fit_drawing(), "Home")
        view_menu.addAction(self.fit_action)
        view_menu.addAction(self.action("框选缩放", lambda: self.canvas.set_tool("zoom")))
        self.snap_action = self.action("对象捕捉", lambda checked: self.toggle_mode("snap_enabled", checked), "F3", True)
        self.ortho_action = self.action("正交模式", lambda checked: self.toggle_mode("ortho", checked), "F8", True)
        self.polar_action = self.action("45° 极轴追踪", lambda checked: self.toggle_mode("polar", checked), "F10", True)
        self.grid_action = self.action("网格", lambda checked: self.toggle_mode("grid", checked), "F7", True)
        view_menu.addActions([self.snap_action, self.ortho_action, self.polar_action, self.grid_action])
        self.view_menu = view_menu
        engineering = self.menuBar().addMenu("工程分析")
        for text, kind in [("板框尺寸", "board"), ("孔径统计", "holes"), ("最小间距检查", "clearance"),
                           ("焊盘尺寸检查", "pads"), ("工艺边检查", "edge"), ("查找重复对象", "duplicates"),
                           ("图形统计", "statistics")]:
            engineering.addAction(self.action(text, lambda checked=False, k=kind: self.run_analysis(k)))
        engineering.addAction(self.action("删除重复对象（保留每组第一个）", self.remove_duplicates))
        help_menu = self.menuBar().addMenu("帮助")
        help_menu.addAction(self.action("首版支持范围", self.about))

    def _build_tools(self):
        files = QToolBar("文件与视图", self)
        files.setMovable(False)
        self.addToolBar(files)
        brand = QLabel("◈  CAD WORKBENCH")
        brand.setObjectName("brand")
        files.addWidget(brand)
        files.addSeparator()
        files.addActions([self.new_action, self.open_action, self.save_action])
        files.addSeparator()
        files.addActions([self.undo_action, self.redo_action, self.fit_action])
        files.addSeparator()
        files.addActions([self.snap_action, self.ortho_action, self.polar_action, self.grid_action])
        self.addToolBarBreak()
        tools = QToolBar("绘制与测量", self)
        tools.setMovable(False)
        self.addToolBar(tools)
        self.tool_group = QActionGroup(self)
        for tool, text in [("select", "选择"), ("line", "直线"), ("polyline", "多段线"),
                           ("rectangle", "矩形"), ("circle", "圆"), ("arc", "圆弧"),
                           ("ellipse", "椭圆"), ("text", "文字"), ("move", "移动"), ("copy", "复制"),
                           ("distance", "距离"), ("angle", "角度"), ("dimension", "标注")]:
            a = self.action(text, lambda checked=False, t=tool: self.choose_tool(t), checkable=True)
            a.setToolTip(TOOLS[tool])
            self.tool_group.addAction(a)
            self.tool_actions[tool] = a
            tools.addAction(a)
            if tool in ("text", "copy"):
                tools.addSeparator()
            if tool in ("line", "polyline", "rectangle", "circle", "arc", "ellipse", "text"):
                self.draw_menu.addAction(a)
            elif tool in ("distance", "angle", "dimension"):
                self.measure_menu.addAction(a)
        for tool, text in [("horizontal", "水平距离"), ("vertical", "垂直距离"), ("continuous", "连续测量"), ("coordinate", "坐标查询")]:
            self.measure_menu.addAction(self.action(text, lambda checked=False, t=tool: self.choose_tool(t)))
        self.measure_menu.addAction(self.action("对象半径 / 直径 / 面积 / 周长", self.measure_selected))

    def dock(self, title, widget, area):
        dock = QDockWidget(title, self)
        dock.setObjectName(title)
        dock.setWidget(widget)
        self.addDockWidget(area, dock)
        self.view_menu.addAction(dock.toggleViewAction())
        return dock

    def _build_properties(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.selection_label = QLabel("未选择对象")
        self.selection_label.setObjectName("hint")
        layout.addWidget(self.selection_label)
        self.property_table = QTableWidget(0, 2)
        self.property_table.setHorizontalHeaderLabels(["属性", "数值"])
        self.property_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.property_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.property_table.verticalHeader().hide()
        self.property_table.cellDoubleClicked.connect(lambda *_: self.edit_properties())
        layout.addWidget(self.property_table)
        button = QPushButton("编辑 / 批量修改属性")
        button.clicked.connect(self.edit_properties)
        layout.addWidget(button)
        self.property_dock = self.dock("对象属性", widget, Qt.DockWidgetArea.RightDockWidgetArea)
        self.property_dock.setMinimumWidth(285)

    def _build_layers(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        self.current_layer_combo = QComboBox()
        self.current_layer_combo.currentTextChanged.connect(self.set_current_layer)
        layout.addWidget(QLabel("当前绘图图层"))
        layout.addWidget(self.current_layer_combo)
        self.layer_table = QTableWidget(0, 5)
        self.layer_table.setHorizontalHeaderLabels(["图层", "可见", "锁定", "颜色", "线宽"])
        self.layer_table.verticalHeader().hide()
        self.layer_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        for i in range(1, 5):
            self.layer_table.setColumnWidth(i, 47)
        self.layer_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.layer_table.itemChanged.connect(self.layer_item_changed)
        self.layer_table.cellDoubleClicked.connect(self.layer_double_click)
        layout.addWidget(self.layer_table)
        row = QHBoxLayout()
        for label, callback in [("新增", self.add_layer), ("删除", self.delete_layer)]:
            button = QPushButton(label)
            button.clicked.connect(callback)
            row.addWidget(button)
        layout.addLayout(row)
        self.layer_dock = self.dock("图层管理", widget, Qt.DockWidgetArea.RightDockWidgetArea)

    def _build_analysis(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        hint = QLabel("已选对象优先；未选择时使用以下图层。\n钻孔和焊盘用途需由工程师指定。")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        layout.addWidget(hint)
        self.scope_combo = QComboBox()
        layout.addWidget(self.scope_combo)
        for label, kind in [("板框尺寸 / 面积", "board"), ("孔径统计", "holes"),
                            ("最小间距检查", "clearance"), ("焊盘尺寸检查", "pads"),
                            ("工艺边 / 板边检查", "edge"), ("重复图形检查", "duplicates")]:
            button = QPushButton(label)
            button.clicked.connect(lambda checked=False, k=kind: self.run_analysis(k))
            layout.addWidget(button)
        self.results = QListWidget()
        self.results.itemClicked.connect(self.select_finding)
        layout.addWidget(self.results)
        self.analysis_dock = self.dock("PCB / FPC 工程分析", widget, Qt.DockWidgetArea.LeftDockWidgetArea)
        self.analysis_dock.setMinimumWidth(260)

    def _build_console(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(8, 3, 8, 6)
        self.console = QTextEdit()
        self.console.setReadOnly(True)
        self.console.setMaximumHeight(105)
        layout.addWidget(self.console)
        row = QHBoxLayout()
        row.addWidget(QLabel("命令 >"))
        self.command_input = QLineEdit()
        self.command_input.setPlaceholderText("输入 CAD 命令、X,Y、@ΔX,ΔY 或 距离<角度；Enter 执行")
        self.command_input.returnPressed.connect(self.execute_command)
        row.addWidget(self.command_input)
        layout.addLayout(row)
        self.console_dock = self.dock("命令与测量记录", widget, Qt.DockWidgetArea.BottomDockWidgetArea)

    def add_document(self, document):
        canvas = CadCanvas(document)
        canvas.changed.connect(lambda: self.document_changed(canvas))
        canvas.selection_changed.connect(self.refresh_properties)
        canvas.message.connect(self.log)
        canvas.coordinates.connect(lambda x, y: self.coord_label.setText(f"X {x:.4f}    Y {y:.4f}  {canvas.document.unit}"))
        canvas.tool_changed.connect(self.reflect_tool)
        index = self.tabs.addTab(canvas, document.title)
        self.tabs.setCurrentIndex(index)
        self.active_changed()
        QTimer.singleShot(0, canvas.fit_drawing)

    def active_changed(self, *_):
        if not hasattr(self, "layer_table") or not self.canvas:
            return
        self.refresh_layers()
        self.refresh_properties()
        self.reflect_tool(self.canvas.tool)
        self.results.clear()
        for action, mode in [(self.snap_action, "snap_enabled"), (self.ortho_action, "ortho"), (self.polar_action, "polar"), (self.grid_action, "grid")]:
            action.setChecked(getattr(self.canvas, mode))
        self.update_title()

    def document_changed(self, canvas):
        self.results.clear()
        index = self.tabs.indexOf(canvas)
        self.tabs.setTabText(index, canvas.document.title + (" *" if canvas.document.modified else ""))
        if canvas == self.canvas:
            self.refresh_layers()
            self.refresh_properties()
            self.update_title()

    def update_title(self):
        if not self.document:
            return
        self.setWindowTitle(f"{self.document.title}{' *' if self.document.modified else ''} — CAD Workbench")
        self.count_label.setText(f"模型空间  |  {len(self.document.entities)} 个对象  |  {self.document.unit}    ")
        self.undo_action.setEnabled(bool(self.document._undo))
        self.redo_action.setEnabled(bool(self.document._redo))

    def log(self, text):
        if hasattr(self, "console"):
            self.console.append(text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))
        self.statusBar().showMessage(text, 10000)

    def error(self, exc):
        self.log(str(exc))
        QMessageBox.warning(self, "操作未完成", str(exc))

    def choose_tool(self, tool):
        self.canvas.set_tool(tool)
        self.reflect_tool(self.canvas.tool)
        self.canvas.setFocus()

    def reflect_tool(self, tool):
        for key, action in self.tool_actions.items():
            action.setChecked(key == tool)

    def toggle_mode(self, mode, value):
        setattr(self.canvas, mode, value)
        if value and mode in ("ortho", "polar"):
            other = "polar" if mode == "ortho" else "ortho"
            setattr(self.canvas, other, False)
            (self.polar_action if other == "polar" else self.ortho_action).setChecked(False)
        self.canvas.viewport().update()

    def history(self, redo):
        self.canvas.set_tool("select")
        label = self.document.redo() if redo else self.document.undo()
        if label:
            self.canvas.rebuild()
            self.document_changed(self.canvas)
            self.log(("恢复：" if redo else "撤销：") + label)

    def open_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "打开 CAD 文件", "", "CAD 文件 (*.dxf *.dwg);;DXF (*.dxf);;DWG (*.dwg)")
        if not path:
            return
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            document = fileio.open_document(path, self.settings.value("dwg_converter", ""))
            self.add_document(document)
            self.log(f"已打开：{path} · {len(document.entities)} 个可显示对象")
            for warning in document.warnings:
                self.log(warning)
        except Exception as exc:
            self.error(exc)
        finally:
            QApplication.restoreOverrideCursor()

    def save_file(self, checked=False, save_as=False) -> bool:
        document = self.document
        path = document.path
        if save_as or not path:
            path, selected_filter = QFileDialog.getSaveFileName(self, "保存 CAD 文件", path or f"{document.title}.dxf",
                                                               "DXF 文件 (*.dxf);;DWG 文件 (*.dwg)")
            if not path:
                return False
            if not Path(path).suffix:
                path += ".dwg" if "DWG" in selected_filter else ".dxf"
        if Path(path).suffix.lower() not in (".dxf", ".dwg"):
            self.error("请使用 .dxf 或 .dwg 扩展名。")
            return False
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            fileio.save_document(document, path, self.settings.value("dwg_converter", ""))
            self.document_changed(self.canvas)
            self.log(f"已保存：{path}")
            return True
        except Exception as exc:
            self.error(exc)
            return False
        finally:
            QApplication.restoreOverrideCursor()

    def confirm_close(self, index):
        canvas = self.tabs.widget(index)
        if not canvas.document.modified:
            return True
        self.tabs.setCurrentIndex(index)
        choice = QMessageBox.question(self, "保存修改", f"是否保存“{canvas.document.title}”的修改？",
                                      QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel)
        return choice == QMessageBox.StandardButton.Discard or (choice == QMessageBox.StandardButton.Save and self.save_file())

    def close_tab(self, index):
        if not self.confirm_close(index):
            return
        widget = self.tabs.widget(index)
        self.tabs.removeTab(index)
        widget.deleteLater()
        if not self.tabs.count():
            self.add_document(Document())

    def closeEvent(self, event):
        for index in range(self.tabs.count()):
            if not self.confirm_close(index):
                event.ignore()
                return
        event.accept()

    def configure_converter(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择 ODAFileConverter.exe", self.settings.value("dwg_converter", ""),
                                             "可执行程序 (*.exe);;所有文件 (*)")
        if path:
            self.settings.setValue("dwg_converter", path)
            self.log("DWG 转换器已配置：" + path)

    def set_units(self):
        labels = list(fileio.UNIT_NAMES.values())
        current = labels.index(self.document.unit) if self.document.unit in labels else 0
        unit, ok = QInputDialog.getItem(self, "图形单位", "只设置单位声明，不缩放现有坐标：", labels, current, False)
        if ok and unit != self.document.unit:
            self.document.checkpoint("设置单位")
            self.document.unit = unit
            self.document.insunits = next(code for code, name in fileio.UNIT_NAMES.items() if name == unit)
            self.document_changed(self.canvas)

    def export_pdf(self):
        if not self.document.entities:
            self.error("没有可导出的图形。")
            return
        path, _ = QFileDialog.getSaveFileName(self, "导出 PDF", self.document.title + ".pdf", "PDF (*.pdf)")
        if not path:
            return
        if not path.lower().endswith(".pdf"):
            path += ".pdf"
        printer = QPrinter(QPrinter.PrinterMode.HighResolution)
        printer.setOutputFormat(QPrinter.OutputFormat.PdfFormat)
        printer.setOutputFileName(path)
        painter = QPainter()
        if not painter.begin(printer):
            self.error("无法创建 PDF，请检查文件是否被占用。")
            return
        try:
            from PySide6.QtCore import QRectF
            x0, y0, x1, y1 = self.document.bounds()
            source = QRectF(x0 - 2, -y1 - 2, max(1, x1 - x0) + 4, max(1, y1 - y0) + 4)
            page = printer.pageRect(QPrinter.Unit.DevicePixel)
            # Scene render excludes the view grid, cursor and interaction previews.
            ids = self.canvas.selected_ids
            self.canvas.select_ids(set())
            self.canvas.scene().render(painter, QRectF(page), source, Qt.AspectRatioMode.KeepAspectRatio)
            self.canvas.select_ids(ids)
        finally:
            painter.end()
        self.log("已导出 PDF（适合页面，非指定工程比例）：" + path)

    @staticmethod
    def format_value(value):
        if isinstance(value, float):
            return f"{value:.6f}".rstrip("0").rstrip(".")
        if isinstance(value, tuple):
            return ", ".join(f"{v:.4f}" for v in value)
        return str(value)

    def refresh_properties(self):
        if not hasattr(self, "property_table") or not self.canvas:
            return
        selected = [e for e in self.document.entities if e.id in self.canvas.selected_ids]
        self.selection_label.setText(f"已选择 {len(selected)} 个对象" if selected else "未选择对象 · 单击图形查看几何属性")
        props = selected[0].properties() if len(selected) == 1 else ({"数量": len(selected), "图层数": len({e.layer for e in selected})} if selected else {})
        self.property_table.setRowCount(len(props))
        for row, (key, value) in enumerate(props.items()):
            self.property_table.setItem(row, 0, QTableWidgetItem(key))
            self.property_table.setItem(row, 1, QTableWidgetItem(self.format_value(value)))

    def refresh_layers(self):
        if not self.document:
            return
        self._refreshing = True
        scope = self.scope_combo.currentText()
        self.current_layer_combo.clear()
        self.current_layer_combo.addItems(list(self.document.layers))
        self.current_layer_combo.setCurrentText(self.document.current_layer)
        self.scope_combo.clear()
        self.scope_combo.addItem("全部可见图层")
        self.scope_combo.addItems(list(self.document.layers))
        self.scope_combo.setCurrentText(scope if scope in self.document.layers else "全部可见图层")
        self.layer_table.setRowCount(len(self.document.layers))
        for row, layer in enumerate(self.document.layers.values()):
            self.layer_table.setItem(row, 0, QTableWidgetItem(layer.name))
            for column, value in ((1, layer.visible), (2, layer.locked)):
                item = QTableWidgetItem()
                item.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable | Qt.ItemFlag.ItemIsSelectable)
                item.setCheckState(Qt.CheckState.Checked if value else Qt.CheckState.Unchecked)
                self.layer_table.setItem(row, column, item)
            color = QTableWidgetItem("●")
            color.setForeground(QColor(layer.color))
            color.setToolTip("双击修改颜色")
            self.layer_table.setItem(row, 3, color)
            self.layer_table.setItem(row, 4, QTableWidgetItem(str(layer.lineweight)))
        self._refreshing = False

    def set_current_layer(self, name):
        if not self._refreshing and name in self.document.layers:
            self.document.current_layer = name
            self.log("当前图层：" + name)

    def layer_item_changed(self, item):
        if self._refreshing or item.column() not in (1, 2):
            return
        name = self.layer_table.item(item.row(), 0).text()
        self.document.checkpoint("修改图层")
        layer = self.document.layers[name]
        setattr(layer, "visible" if item.column() == 1 else "locked", item.checkState() == Qt.CheckState.Checked)
        self.canvas.rebuild()
        self.document_changed(self.canvas)

    def layer_double_click(self, row, column):
        name = self.layer_table.item(row, 0).text()
        layer = self.document.layers[name]
        if column == 3:
            color = QColorDialog.getColor(QColor(layer.color), self, "图层颜色")
            if not color.isValid():
                return
            self.document.checkpoint("图层颜色")
            layer.color = color.name()
        elif column == 4:
            width, ok = QInputDialog.getDouble(self, "图层线宽", "线宽 (mm)：", layer.lineweight, 0.05, 2.11, 2)
            if not ok:
                return
            self.document.checkpoint("图层线宽")
            layer.lineweight = width
        else:
            return
        self.canvas.rebuild()
        self.document_changed(self.canvas)

    def add_layer(self):
        name, ok = QInputDialog.getText(self, "新增图层", "图层名称：")
        if not ok:
            return
        name = name.strip()
        if not name or name in self.document.layers or any(c in name for c in '<>/\\":;?*|=,'):
            self.error("图层名称为空、重复或包含 DXF 不允许的字符。")
            return
        self.document.checkpoint("新增图层")
        self.document.layers[name] = Layer(name)
        self.document.current_layer = name
        self.document_changed(self.canvas)

    def delete_layer(self):
        row = self.layer_table.currentRow()
        if row < 0:
            return
        name = self.layer_table.item(row, 0).text()
        if name == "0" or any(e.layer == name for e in self.document.entities):
            self.error("默认图层 0 或含对象的图层不能删除。请先移动或删除该图层的对象。")
            return
        if fileio.source_layer_in_use(self.document, name):
            self.error("该图层仍被源文件中的布局、块或未支持对象引用，不能删除。")
            return
        self.document.checkpoint("删除图层")
        del self.document.layers[name]
        if self.document.current_layer == name:
            self.document.current_layer = "0"
        self.document_changed(self.canvas)

    def edit_properties(self):
        entities = self.document.editable(self.canvas.selected_ids)
        if not entities:
            self.error("请选择可编辑对象。锁定图层和源文件只读对象不能编辑。")
            return
        dialog = QDialog(self)
        dialog.setWindowTitle("编辑属性" if len(entities) == 1 else f"批量修改 {len(entities)} 个对象")
        form = QFormLayout(dialog)
        layer = QComboBox()
        layer.addItem("保持原图层")
        layer.addItems([l.name for l in self.document.layers.values() if l.visible and not l.locked])
        form.addRow("图层", layer)
        color = QComboBox()
        color.addItems(["保持原颜色", "随图层", "自定义颜色"])
        form.addRow("颜色", color)
        fields = {}
        e = entities[0]
        if len(entities) == 1:
            if e.kind != "ellipse":
                for i, p in enumerate(e.points):
                    for j, axis in enumerate(("X", "Y")):
                        field = number_widget(p[j])
                        fields[(i, j)] = field
                        form.addRow(f"点 {i+1} · {axis}", field)
            if e.kind in ("circle", "arc"):
                fields["radius"] = number_widget(e.radius, 0.000001)
                form.addRow("半径", fields["radius"])
            if e.kind in ("text", "dimension"):
                fields["text"] = QLineEdit(e.data.get("text", "<>" if e.kind == "dimension" else ""))
                form.addRow("文字（标注 <> = 实测值）", fields["text"])
                if e.kind == "text":
                    fields["height"] = number_widget(e.data.get("height", 2.5), 0.000001)
                    form.addRow("文字高度", fields["height"])
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        chosen_color = None
        if color.currentIndex() == 2:
            chosen_color = QColorDialog.getColor(QColor(e.color or self.document.layers[e.layer].color), self)
            if not chosen_color.isValid():
                return
        candidate = editing.clone(e)
        if len(entities) == 1:
            for key, field in fields.items():
                if isinstance(key, tuple):
                    i, j = key
                    p = list(candidate.points[i])
                    p[j] = field.value()
                    candidate.points[i] = tuple(p)
                elif key == "radius":
                    candidate.radius = field.value()
                else:
                    candidate.data[key] = field.text() if isinstance(field, QLineEdit) else field.value()
            if candidate.kind == "line" and g.distance(*candidate.points) <= g.EPS:
                self.error("直线长度必须大于 0。")
                return
            if candidate.kind == "rectangle" and candidate.points != e.points:
                # Arbitrary vertex edits make this a polyline, avoiding false rectangular metrics.
                candidate.kind = "polyline"
        self.document.checkpoint("修改属性")
        if len(entities) == 1:
            e.points, e.radius, e.data, e.kind = candidate.points, candidate.radius, candidate.data, candidate.kind
        for entity in entities:
            if layer.currentIndex() > 0:
                entity.layer = layer.currentText()
            if color.currentIndex() == 1:
                entity.color = None
            elif chosen_color:
                entity.color = chosen_color.name()
        self.canvas.rebuild()
        self.document_changed(self.canvas)

    def editable_selection(self):
        entities = self.document.editable(self.canvas.selected_ids)
        if not entities:
            raise ValueError("请先选择可编辑对象。")
        return entities

    def complete_edit(self, label):
        self.canvas.rebuild()
        self.document_changed(self.canvas)
        self.log(label + "完成")

    def rotate_selected(self):
        try:
            entities = self.editable_selection()
            dialog = QDialog(self)
            dialog.setWindowTitle("旋转对象")
            form = QFormLayout(dialog)
            boxes = [e.bbox() for e in entities]
            x = number_widget((min(b[0] for b in boxes) + max(b[2] for b in boxes)) / 2)
            y = number_widget((min(b[1] for b in boxes) + max(b[3] for b in boxes)) / 2)
            degrees = number_widget(90, -36000, 36000)
            for label, widget in [("中心 X", x), ("中心 Y", y), ("角度 (°，逆时针)", degrees)]:
                form.addRow(label, widget)
            buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
            buttons.accepted.connect(dialog.accept)
            buttons.rejected.connect(dialog.reject)
            form.addRow(buttons)
            if dialog.exec() == QDialog.DialogCode.Accepted:
                self.document.checkpoint("旋转")
                for e in entities:
                    e.transform(center=(x.value(), y.value()), degrees=degrees.value())
                self.complete_edit("旋转")
        except ValueError as exc:
            self.error(exc)

    def array_selected(self):
        try:
            entities = self.editable_selection()
            dialog = QDialog(self)
            dialog.setWindowTitle("矩形阵列")
            form = QFormLayout(dialog)
            rows, columns = QSpinBox(), QSpinBox()
            for widget in (rows, columns):
                widget.setRange(1, 100)
                widget.setValue(2)
            dx, dy = number_widget(10), number_widget(10)
            for label, widget in [("行数（含原对象）", rows), ("列数（含原对象）", columns), ("列距 ΔX", dx), ("行距 ΔY", dy)]:
                form.addRow(label, widget)
            buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
            buttons.accepted.connect(dialog.accept)
            buttons.rejected.connect(dialog.reject)
            form.addRow(buttons)
            if dialog.exec() != QDialog.DialogCode.Accepted:
                return
            if rows.value() * columns.value() * len(entities) > 10000:
                raise ValueError("单次阵列最多生成 10000 个对象。")
            self.document.checkpoint("阵列")
            for i in range(rows.value()):
                for j in range(columns.value()):
                    if i == j == 0:
                        continue
                    for e in entities:
                        clone = editing.clone(e)
                        clone.transform(offset=(j * dx.value(), i * dy.value()))
                        self.document.entities.append(clone)
            self.complete_edit("阵列")
        except ValueError as exc:
            self.error(exc)

    def offset_selected(self):
        try:
            entities = self.editable_selection()
            value, ok = QInputDialog.getDouble(self, "偏移", "距离：圆/闭合凸轮廓正值外扩；直线正值向左", 1, -1e6, 1e6, 4)
            if not ok:
                return
            additions = [editing.offset(e, value) for e in entities]
            self.document.checkpoint("偏移")
            self.document.entities.extend(additions)
            self.complete_edit("偏移")
            self.canvas.select_ids({e.id for e in additions})
        except ValueError as exc:
            self.error(exc)

    def explode_selected(self):
        try:
            entities = self.editable_selection()
            additions = [piece for e in entities for piece in editing.explode(e)]
            self.document.checkpoint("分解")
            ids = {e.id for e in entities}
            self.document.entities = [e for e in self.document.entities if e.id not in ids] + additions
            self.complete_edit("分解")
            self.canvas.select_ids({e.id for e in additions})
        except ValueError as exc:
            self.error(exc)

    def join_selected(self):
        try:
            entities = self.editable_selection()
            joined = editing.join(entities)
            self.document.checkpoint("合并")
            ids = {e.id for e in entities}
            self.document.entities = [e for e in self.document.entities if e.id not in ids] + [joined]
            self.complete_edit("合并")
            self.canvas.select_ids({joined.id})
        except ValueError as exc:
            self.error(exc)

    def trim_extend(self, extend):
        try:
            entities = self.editable_selection()
            if len(entities) != 2 or any(e.kind != "line" for e in entities):
                raise ValueError("请选择两条直线。")
            labels = [f"{i+1}: {e.points[0]} → {e.points[1]}" for i, e in enumerate(entities)]
            target_label, ok = QInputDialog.getItem(self, "延伸" if extend else "修剪", "要修改的直线（另一条为边界）：", labels, 0, False)
            if not ok:
                return
            index = labels.index(target_label)
            endpoint, ok = QInputDialog.getItem(self, "选择端点", "将哪一端改到边界交点：", ["起点", "终点"], 1, False)
            if not ok:
                return
            points = editing.trim_extend(entities[index], entities[1-index], 0 if endpoint == "起点" else 1, extend)
            self.document.checkpoint("延伸" if extend else "修剪")
            entities[index].points = points
            self.complete_edit("延伸" if extend else "修剪")
        except ValueError as exc:
            self.error(exc)

    def measure_selected(self):
        entities = [e for e in self.document.entities if e.id in self.canvas.selected_ids]
        if not entities:
            self.log("请先选择对象；属性面板会自动显示长度、面积、半径等几何数据。")
        for e in entities:
            self.log(" · ".join(f"{k}: {self.format_value(v)}" for k, v in e.properties().items()) + f" · 单位 {self.document.unit}")

    def analysis_scope(self):
        selected = self.canvas.selected_ids
        layer = self.scope_combo.currentText()
        return [e for e in self.document.visible_entities() if
                (e.id in selected if selected else layer == "全部可见图层" or e.layer == layer)]

    def get_board(self, scope):
        closed = [e for e in scope if e.closed or e.kind == "circle"]
        if len(closed) != 1:
            raise ValueError("请只选择一个闭合对象作为板框，或在范围中选择只含一个闭合板框的图层。")
        return closed[0]

    def run_analysis(self, kind):
        try:
            scope = self.analysis_scope()
            if not scope:
                raise ValueError("分析范围内没有可见对象。")
            results = []
            if kind == "board":
                board = self.get_board(scope)
                results = [analysis.Finding(f"{key} = {value:.4f}" + (f" {self.document.unit}²" if key == "面积" else f" {self.document.unit}"),
                                             {board.id}, value) for key, value in analysis.board_metrics(board).items()]
            elif kind == "holes":
                results = analysis.hole_statistics(scope)
                self.log("孔径统计仅统计范围内的圆；请使用钻孔图层/选区，圆不自动判定为真实钻孔。")
            elif kind == "duplicates":
                results = analysis.duplicate_checks(scope)
            elif kind == "statistics":
                results = analysis.drawing_statistics(self.document)
            elif kind in ("clearance", "edge"):
                value, ok = QInputDialog.getDouble(self, "工程检查", f"最小要求 ({self.document.unit})：", 0.2 if kind == "clearance" else 3, 0.000001, 1e6, 4)
                if not ok:
                    return
                if kind == "edge":
                    board = self.get_board(scope)
                    objects = [e for e in self.document.visible_entities() if e.layer != board.layer]
                    results = analysis.edge_checks(board, objects, value)
                    self.log("工艺边检查：选定板框与其他可见图层的对象比较。")
                else:
                    results = analysis.clearance_checks(scope, value)
                self.log("间距按轮廓计算；圆/直线采用解析算法，曲线离散公差 0.005 图形单位，不包含线宽与网络信息。")
            elif kind == "pads":
                width, ok = QInputDialog.getDouble(self, "焊盘检查", "最小宽度：", 1, 0.000001, 1e6, 4)
                if not ok:
                    return
                height, ok = QInputDialog.getDouble(self, "焊盘检查", "最小高度：", 1, 0.000001, 1e6, 4)
                if not ok:
                    return
                results = analysis.pad_checks(scope, width, height)
                self.log("焊盘检查：矩形按边长；圆按直径；其他闭合轮廓按轴对齐包围盒，需先指定焊盘图层。")
            self.results.clear()
            for result in results:
                item = QListWidgetItem(result.description)
                item.setData(Qt.ItemDataRole.UserRole, result.entity_ids)
                self.results.addItem(item)
            if not results:
                self.results.addItem("没有结果 / 未发现违反当前规则的对象")
            self.analysis_dock.show()
            self.log(f"分析完成：{len(results)} 条结果；单击结果定位对象。")
        except (ValueError, ArithmeticError) as exc:
            self.error(exc)

    def select_finding(self, item):
        ids = item.data(Qt.ItemDataRole.UserRole)
        if ids:
            self.canvas.select_ids(ids)
            items = [self.canvas.items_by_id[key] for key in ids if key in self.canvas.items_by_id]
            if items:
                rect = items[0].boundingRect()
                for graphic in items[1:]:
                    rect = rect.united(graphic.boundingRect())
                self.canvas.centerOn(rect.center())

    def remove_duplicates(self):
        scope = self.analysis_scope()
        findings = analysis.duplicate_checks(scope)
        deletions = set()
        for finding in findings:
            entities = [e for e in self.document.entities if e.id in finding.entity_ids]
            for e in entities[1:]:
                if e in self.document.editable({e.id}):
                    deletions.add(e.id)
        if not deletions:
            self.log("未发现可删除的重复对象。")
            return
        self.document.checkpoint("清理重复对象")
        self.document.entities = [e for e in self.document.entities if e.id not in deletions]
        self.complete_edit(f"清理 {len(deletions)} 个重复对象")

    def execute_command(self):
        text = self.command_input.text().strip()
        self.command_input.clear()
        if not text:
            self.canvas.finish()
            return
        self.log("> " + text)
        aliases = {"L": "line", "LINE": "line", "PL": "polyline", "PLINE": "polyline", "RECTANG": "rectangle", "REC": "rectangle",
                   "CIRCLE": "circle", "C": "circle", "ARC": "arc", "ELLIPSE": "ellipse", "TEXT": "text",
                   "MOVE": "move", "M": "move", "COPY": "copy", "CO": "copy", "MIRROR": "mirror", "MI": "mirror",
                   "DIST": "distance", "DI": "distance", "AREA": "measure", "ID": "coordinate", "DIM": "dimension",
                   "DIMALIGNED": "dimension", "ANGLE": "angle", "SELECT": "select"}
        command = text.upper()
        try:
            if command == "C" and self.canvas.tool == "polyline":
                self.canvas.finish(closed=True)
            elif command in aliases:
                tool = aliases[command]
                self.measure_selected() if tool == "measure" else self.choose_tool(tool)
            elif command in ("U", "UNDO", "REDO"):
                self.history(command == "REDO")
            elif command in ("ERASE", "E"):
                self.canvas.delete_selected()
            elif command in ("ZOOM", "Z", "EXTENTS"):
                self.canvas.fit_drawing()
            elif command in ("OFFSET", "O"):
                self.offset_selected()
            elif command in ("ROTATE", "RO"):
                self.rotate_selected()
            elif command == "ARRAY":
                self.array_selected()
            elif command in ("EXPLODE", "X"):
                self.explode_selected()
            elif command in ("JOIN", "J"):
                self.join_selected()
            elif command in ("TRIM", "TR", "EXTEND", "EX"):
                self.trim_extend(command in ("EXTEND", "EX"))
            else:
                if self.canvas.tool in ("select", "zoom"):
                    raise ValueError("请先输入绘图或测量命令，例如 LINE、CIRCLE、DIST。")
                base = self.canvas.points[-1] if self.canvas.points else (0, 0)
                if text.startswith("@"):
                    dx, dy = map(float, text[1:].split(","))
                    point = base[0] + dx, base[1] + dy
                elif "<" in text:
                    length, degrees = map(float, text.split("<"))
                    point = base[0] + length * math.cos(math.radians(degrees)), base[1] + length * math.sin(math.radians(degrees))
                elif "," in text:
                    x, y = map(float, text.split(","))
                    point = x, y
                else:
                    length = float(text)
                    cursor = self.canvas._cursor or (base[0] + 1, base[1])
                    theta = math.radians(g.angle(base, cursor))
                    point = base[0] + length * math.cos(theta), base[1] + length * math.sin(theta)
                self.canvas.accept_point(point)
        except (ValueError, ArithmeticError):
            self.log("命令或坐标无效。支持 X,Y、@ΔX,ΔY、距离<角度（角度单位：度）。")

    def about(self):
        QMessageBox.information(self, "CAD Workbench 0.1", "轻量化二维 CAD · Python / PySide6 / ezdxf\n\n"
                                "已实现：DXF 读写、多文档、二维绘制、常用编辑、测量、对齐标注、图层、撤销/恢复、PCB/FPC 基础分析。\n\n"
                                "DWG 通过用户安装的 ODA File Converter 转换。\n"
                                "首版编辑模型空间中的二维对象；源文件布局、块和未支持对象保留。\n"
                                "后续：圆角/倒角、拉伸、填充、块编辑、图纸空间、比例打印、Gerber 对照。\n\n详见 README.md。")


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CAD Workbench")
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE)
    app.setFont(QFont("Microsoft YaHei", 9))
    window = MainWindow()
    window.show()
    if len(sys.argv) > 1 and Path(sys.argv[1]).is_file():
        try:
            document = fileio.open_document(sys.argv[1], window.settings.value("dwg_converter", ""))
            window.add_document(document)
            for warning in document.warnings:
                window.log(warning)
        except Exception as exc:
            window.error(exc)
    sys.exit(app.exec())
