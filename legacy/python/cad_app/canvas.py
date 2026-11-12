from __future__ import annotations

import copy
import math
import uuid

from PySide6.QtCore import QPoint, QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPainterPath, QPen, QTransform
from PySide6.QtWidgets import QGraphicsItem, QGraphicsPathItem, QGraphicsScene, QGraphicsView, QInputDialog

from . import geometry as g
from .model import Document, Entity

TOOLS = {
    "select": "选择：单击对象；左→右框选，右→左交叉选择；Shift 多选",
    "line": "直线：指定起点、终点；可连续绘制，Esc 结束",
    "polyline": "多段线：依次指定顶点；Enter 完成，C 闭合",
    "rectangle": "矩形：指定两个对角点",
    "circle": "圆：指定圆心、圆周上的点",
    "arc": "圆弧：指定圆心、起点、终点（逆时针）",
    "ellipse": "椭圆：指定圆心、长轴端点、短轴长度点",
    "text": "文字：指定插入点",
    "move": "移动：先选择对象，再指定基点、目标点",
    "copy": "复制：先选择对象，再指定基点、目标点",
    "mirror": "镜像：先选择对象，再指定镜像轴上的两点（保留原对象）",
    "distance": "距离：指定两点",
    "horizontal": "水平距离：指定两点",
    "vertical": "垂直距离：指定两点",
    "continuous": "连续测量：依次指定点；Enter 结束",
    "angle": "角度：指定顶点，再指定两条射线上的点",
    "coordinate": "坐标：单击查询 X / Y",
    "dimension": "对齐标注：指定两个测量点，再指定标注位置",
    "zoom": "框选缩放：拖动指定要放大的区域",
}


def scene_point(p):
    return QPointF(p[0], -p[1])


def model_point(p):
    return (p.x(), -p.y())


def entity_path(e: Entity) -> QPainterPath:
    path = QPainterPath()
    if not e.points:
        return path
    if e.kind == "text":
        font = QFont("Microsoft YaHei")
        font.setPixelSize(100)
        raw = QPainterPath()
        for i, line in enumerate(e.data.get("text", "").splitlines()):
            raw.addText(0, i * 130, font, line)
        height = e.data.get("height", 2.5)
        transform = QTransform()
        transform.translate(e.points[0][0], -e.points[0][1])
        transform.rotate(-e.data.get("rotation", 0))
        transform.scale(height / 100, height / 100)
        return transform.map(raw)
    if e.kind == "dimension":
        a, b, c = e.points
        foot = g.project(c, a, b, False)
        delta = (c[0] - foot[0], c[1] - foot[1])
        aa, bb = (a[0] + delta[0], a[1] + delta[1]), (b[0] + delta[0], b[1] + delta[1])
        for p, q in ((a, aa), (b, bb), (aa, bb)):
            path.moveTo(scene_point(p))
            path.lineTo(scene_point(q))
        length = g.distance(a, b)
        if length > g.EPS:
            ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
            size = min(1.0, length / 10)
            for p, direction in ((aa, 1), (bb, -1)):
                for side in (-1, 1):
                    q = (p[0] + direction * ux * size - side * uy * size / 3,
                         p[1] + direction * uy * size + side * ux * size / 3)
                    path.moveTo(scene_point(p))
                    path.lineTo(scene_point(q))
        label = e.data.get("text", "<>").replace("<>", f"{length:.3f}")
        text = Entity("text", [((aa[0] + bb[0]) / 2, (aa[1] + bb[1]) / 2 + 0.8)],
                      data={"text": label, "height": 2})
        path.addPath(entity_path(text))
        return path
    points = e.outline(0.01)
    if points:
        path.moveTo(scene_point(points[0]))
        for point in points[1:]:
            path.lineTo(scene_point(point))
        if e.closed or e.kind == "circle":
            path.closeSubpath()
    return path


class EntityItem(QGraphicsPathItem):
    def __init__(self, entity: Entity, color: str, width: float):
        super().__init__(entity_path(entity))
        self.entity_id = entity.id
        self.setData(0, entity.id)
        self.color = QColor(entity.color or color)
        self.width = max(1.0, min(3.0, width * 4))
        pen = QPen(self.color, self.width)
        pen.setCosmetic(True)
        self.setPen(pen)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)

    def paint(self, painter, option, widget=None):
        pen = QPen(QColor("#f4c66c") if self.isSelected() else self.color, self.width + (1 if self.isSelected() else 0))
        pen.setCosmetic(True)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawPath(self.path())


class CadCanvas(QGraphicsView):
    changed = Signal()
    selection_changed = Signal()
    message = Signal(str)
    coordinates = Signal(float, float)
    tool_changed = Signal(str)

    def __init__(self, document: Document):
        super().__init__()
        self.document = document
        self.setScene(QGraphicsScene(self))
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setViewportUpdateMode(QGraphicsView.ViewportUpdateMode.FullViewportUpdate)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.NoAnchor)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorViewCenter)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setBackgroundBrush(QColor("#101922"))
        self.items_by_id = {}
        self.tool = "select"
        self.points: list[g.Point] = []
        self.snap_enabled = True
        self.ortho = False
        self.polar = False
        self.grid = True
        self._panning = False
        self._drag_start: QPoint | None = None
        self._drag_end: QPoint | None = None
        self._cursor: g.Point | None = None
        self._snap: tuple[g.Point, str] | None = None
        self.scene().selectionChanged.connect(self.selection_changed)
        self.rebuild()

    @property
    def selected_ids(self):
        return {i.entity_id for i in self.scene().selectedItems() if isinstance(i, EntityItem)}

    def rebuild(self):
        ids = self.selected_ids
        self.scene().blockSignals(True)
        self.scene().clear()
        self.items_by_id = {}
        for e in self.document.visible_entities():
            layer = self.document.layers[e.layer]
            item = EntityItem(e, layer.color, layer.lineweight)
            self.scene().addItem(item)
            self.items_by_id[e.id] = item
            if e.id in ids:
                item.setSelected(True)
        x0, y0, x1, y1 = self.document.bounds()
        margin = max(10000, x1 - x0, y1 - y0) * 10
        self.scene().setSceneRect(x0 - margin, -y1 - margin, x1 - x0 + 2 * margin, y1 - y0 + 2 * margin)
        self.scene().blockSignals(False)
        self.selection_changed.emit()
        self.viewport().update()

    def select_ids(self, ids):
        self.scene().blockSignals(True)
        for key, item in self.items_by_id.items():
            item.setSelected(key in ids)
        self.scene().blockSignals(False)
        self.selection_changed.emit()
        self.viewport().update()

    def fit_drawing(self):
        x0, y0, x1, y1 = self.document.bounds()
        w, h = max(10, x1 - x0), max(10, y1 - y0)
        self.fitInView(QRectF(x0 - w * 0.08, -y1 - h * 0.08, w * 1.16, h * 1.16), Qt.AspectRatioMode.KeepAspectRatio)

    def set_tool(self, tool: str):
        if tool in ("move", "copy", "mirror") and not self.document.editable(self.selected_ids):
            self.message.emit("请先选择可编辑对象（图层需可见且未锁定）。")
            return
        self.tool = tool
        self.points.clear()
        self._drag_start = None
        self._snap = None
        self.setCursor(Qt.CursorShape.ArrowCursor if tool == "select" else Qt.CursorShape.CrossCursor)
        self.message.emit(TOOLS[tool])
        self.tool_changed.emit(tool)
        self.viewport().update()

    def _snap_at(self, p: g.Point):
        self._snap = None
        if not self.snap_enabled:
            return p
        tolerance = 10 / max(self.transform().m11(), 1e-8)
        near = []
        segments = []
        for e in self.document.visible_entities():
            x0, y0, x1, y1 = e.bbox()
            if not (x0 - tolerance <= p[0] <= x1 + tolerance and y0 - tolerance <= p[1] <= y1 + tolerance):
                continue
            near.extend(e.snap_points())
            if e.kind in ("line", "polyline", "rectangle"):
                for a, b, arc in g.poly_segments(e.points, e.closed, e.data.get("bulges", [])):
                    if arc is None:
                        segments.append((a, b))
                        if self.points:
                            foot = g.project(self.points[-1], a, b, False)
                            if g.distance(foot, g.project(foot, a, b)) < g.EPS:
                                near.append((foot, "垂足"))
        # Local straight-line intersections; curved intersections are a later module.
        if len(segments) < 100:
            for i, (a, b) in enumerate(segments):
                for c, d in segments[i + 1:]:
                    hit = g.intersection(a, b, c, d)
                    if hit:
                        near.append((hit, "交点"))
        if near:
            closest = min(near, key=lambda candidate: g.distance(candidate[0], p))
            if g.distance(closest[0], p) <= tolerance:
                self._snap = closest
                return closest[0]
        return p

    def _constrain(self, p):
        p = self._snap_at(p)
        if self.points and not self._snap:
            base = self.points[-1]
            if self.tool in ("circle", "arc", "ellipse", "angle"):
                base = self.points[0]
            dx, dy = p[0] - base[0], p[1] - base[1]
            if self.ortho:
                p = (p[0], base[1]) if abs(dx) >= abs(dy) else (base[0], p[1])
            elif self.polar:
                theta = math.radians(round(g.angle(base, p) / 45) * 45)
                r = g.distance(base, p)
                p = (base[0] + r * math.cos(theta), base[1] + r * math.sin(theta))
        return p

    def wheelEvent(self, event):
        before = self.mapToScene(event.position().toPoint())
        factor = 1.2 if event.angleDelta().y() > 0 else 1 / 1.2
        scale = self.transform().m11() * factor
        if 0.00001 < scale < 100000:
            self.scale(factor, factor)
            after = self.mapToScene(event.position().toPoint())
            delta = after - before
            self.centerOn(self.mapToScene(self.viewport().rect().center()) - delta)
        event.accept()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = True
            self._last_pan = event.position().toPoint()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
        elif event.button() == Qt.MouseButton.RightButton:
            self.finish()
        elif event.button() == Qt.MouseButton.LeftButton:
            if self.tool in ("select", "zoom"):
                self._drag_start = event.position().toPoint()
                self._drag_end = self._drag_start
            else:
                self.accept_point(self._constrain(model_point(self.mapToScene(event.position().toPoint()))))
        event.accept()

    def mouseMoveEvent(self, event):
        pos = event.position().toPoint()
        if self._panning:
            delta = self.mapToScene(self._last_pan) - self.mapToScene(pos)
            self.centerOn(self.mapToScene(self.viewport().rect().center()) + delta)
            self._last_pan = pos
        elif self._drag_start:
            self._drag_end = pos
        self._cursor = self._constrain(model_point(self.mapToScene(pos)))
        self.coordinates.emit(*self._cursor)
        self.viewport().update()
        event.accept()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self._panning = False
            self.setCursor(Qt.CursorShape.ArrowCursor if self.tool == "select" else Qt.CursorShape.CrossCursor)
        elif event.button() == Qt.MouseButton.LeftButton and self._drag_start:
            end = event.position().toPoint()
            start = self._drag_start
            self._drag_start = self._drag_end = None
            a, b = self.mapToScene(start), self.mapToScene(end)
            rect = QRectF(a, b).normalized()
            if self.tool == "zoom":
                if (end - start).manhattanLength() > 5:
                    self.fitInView(rect, Qt.AspectRatioMode.KeepAspectRatio)
                self.set_tool("select")
            else:
                shift = bool(event.modifiers() & Qt.KeyboardModifier.ShiftModifier)
                ids = self.selected_ids if shift else set()
                if (end - start).manhattanLength() < 5:
                    tolerance = 7 / self.transform().m11()
                    items = self.scene().items(QRectF(a.x() - tolerance, a.y() - tolerance, tolerance * 2, tolerance * 2))
                    candidates = [i for i in items if isinstance(i, EntityItem)]
                    if candidates:
                        key = candidates[0].entity_id
                        if shift and key in ids:
                            ids.remove(key)
                        else:
                            ids.add(key)
                else:
                    crossing = end.x() < start.x()
                    for key, item in self.items_by_id.items():
                        if (item.path().intersects(rect) if crossing else rect.contains(item.boundingRect())):
                            ids.add(key)
                self.select_ids(ids)
        self.viewport().update()
        event.accept()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape:
            self.set_tool("select")
        elif event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.finish()
        elif event.key() == Qt.Key.Key_C and self.tool == "polyline":
            self.finish(closed=True)
        elif event.key() == Qt.Key.Key_Delete:
            self.delete_selected()
        else:
            super().keyPressEvent(event)

    def add_entity(self, entity):
        try:
            self.document.add(entity)
        except ValueError as exc:
            self.message.emit(str(exc))
            self.points.clear()
            return False
        self.rebuild()
        self.changed.emit()
        return True

    def finish(self, closed=False):
        if self.tool == "polyline" and len(self.points) >= (3 if closed else 2):
            self.add_entity(Entity("polyline", list(self.points), self.document.current_layer, closed=closed))
        elif self.tool == "continuous" and len(self.points) > 1:
            self.message.emit(f"累计长度 = {g.poly_length(self.points):.4f} {self.document.unit}")
        self.set_tool("select")

    def accept_point(self, p: g.Point):
        if not all(math.isfinite(v) for v in p):
            self.message.emit("坐标必须为有限数值。")
            return
        t = self.tool
        self.points.append(p)
        layer = self.document.current_layer
        n = len(self.points)
        e = None
        if t == "coordinate":
            self.message.emit(f"坐标 X = {p[0]:.4f}，Y = {p[1]:.4f} {self.document.unit}")
            self.points.clear()
        elif t == "text":
            text, ok = QInputDialog.getMultiLineText(self, "插入文字", "文字内容：")
            if ok and text:
                height, ok = QInputDialog.getDouble(self, "文字高度", f"高度 ({self.document.unit})", 2.5, 0.001, 100000, 3)
                if ok:
                    e = Entity("text", [p], layer, data={"text": text, "height": height})
            self.points.clear()
        elif t == "line" and n == 2:
            if g.distance(*self.points) > g.EPS:
                e = Entity("line", list(self.points), layer)
            self.points = [p]
        elif t == "rectangle" and n == 2:
            a, b = self.points
            if abs(a[0] - b[0]) > g.EPS and abs(a[1] - b[1]) > g.EPS:
                e = Entity("rectangle", [a, (b[0], a[1]), b, (a[0], b[1])], layer, closed=True)
            self.points.clear()
        elif t == "circle" and n == 2:
            radius = g.distance(*self.points)
            if radius > g.EPS:
                e = Entity("circle", [self.points[0]], layer, radius=radius)
            self.points.clear()
        elif t == "arc" and n == 3:
            center, start, end = self.points
            radius = g.distance(center, start)
            sweep = (g.angle(center, end) - g.angle(center, start)) % 360
            if radius > g.EPS and sweep > g.EPS:
                e = Entity("arc", [center], layer, radius=radius, data={"start": g.angle(center, start), "sweep": sweep})
            self.points.clear()
        elif t == "ellipse" and n == 3:
            center, major, minor = self.points
            r = g.distance(center, major)
            if r > g.EPS:
                foot = g.project(minor, center, major, False)
                ratio = g.distance(minor, foot) / r
                if 0 < ratio <= 1:
                    e = Entity("ellipse", [center, (major[0] - center[0], major[1] - center[1])], layer,
                               closed=True, data={"ratio": ratio})
                else:
                    self.message.emit("短轴必须大于 0 且不超过长轴。")
            self.points.clear()
        elif t in ("distance", "horizontal", "vertical") and n == 2:
            a, b = self.points
            length = g.distance(a, b) if t == "distance" else abs(b[0] - a[0]) if t == "horizontal" else abs(b[1] - a[1])
            self.message.emit(f"测量 = {length:.4f} {self.document.unit} · ΔX {abs(b[0]-a[0]):.4f} · ΔY {abs(b[1]-a[1]):.4f}")
            self.points.clear()
        elif t == "continuous" and n > 1:
            self.message.emit(f"本段 {g.distance(self.points[-2], p):.4f} · 累计 {g.poly_length(self.points):.4f} {self.document.unit}")
        elif t == "angle" and n == 3:
            v, a, b = self.points
            delta = abs(g.angle(v, a) - g.angle(v, b)) % 360
            self.message.emit(f"夹角 = {min(delta, 360 - delta):.4f}°")
            self.points.clear()
        elif t == "dimension" and n == 3:
            if g.distance(self.points[0], self.points[1]) > g.EPS:
                e = Entity("dimension", list(self.points), layer)
            self.points.clear()
        elif t in ("move", "copy", "mirror") and n == 2:
            a, b = self.points
            if g.distance(a, b) <= g.EPS:
                self.points.clear()
                return
            entities = self.document.editable(self.selected_ids)
            if entities:
                self.document.checkpoint({"move": "移动", "copy": "复制", "mirror": "镜像"}[t])
                new_ids = set()
                for entity in entities:
                    target = entity
                    if t != "move":
                        target = copy.deepcopy(entity)
                        target.id = uuid.uuid4().hex
                        target.source_handle = None
                        self.document.entities.append(target)
                    if t == "mirror":
                        target.transform(mirror_axis=(a, b))
                    else:
                        target.transform(offset=(b[0] - a[0], b[1] - a[1]))
                    new_ids.add(target.id)
                self.rebuild()
                self.select_ids(new_ids)
                self.changed.emit()
            self.set_tool("select")
        if e:
            self.add_entity(e)
        self.viewport().update()

    def delete_selected(self):
        entities = self.document.editable(self.selected_ids)
        if not entities:
            self.message.emit("没有可删除的对象。")
            return
        ids = {e.id for e in entities}
        self.document.checkpoint("删除")
        self.document.entities = [e for e in self.document.entities if e.id not in ids]
        self.rebuild()
        self.changed.emit()

    def drawBackground(self, painter, rect):
        super().drawBackground(painter, rect)
        scale = self.transform().m11()
        if self.grid and scale > 0:
            step = 10 ** math.floor(math.log10(35 / scale))
            if step * scale < 18:
                step *= 5
            pen = QPen(QColor("#22303e"), 1)
            pen.setCosmetic(True)
            painter.setPen(pen)
            x = math.floor(rect.left() / step) * step
            while x < rect.right():
                painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()))
                x += step
            y = math.floor(rect.top() / step) * step
            while y < rect.bottom():
                painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))
                y += step
        pen = QPen(QColor("#3a5265"), 1)
        pen.setCosmetic(True)
        painter.setPen(pen)
        painter.drawLine(QPointF(0, rect.top()), QPointF(0, rect.bottom()))
        painter.drawLine(QPointF(rect.left(), 0), QPointF(rect.right(), 0))

    def drawForeground(self, painter, rect):
        pen = QPen(QColor("#56d9bd"), 1.4, Qt.PenStyle.DashLine)
        pen.setCosmetic(True)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        if self.points and self._cursor:
            points = self.points + [self._cursor]
            preview = None
            if self.tool == "rectangle":
                a, b = points[0], points[-1]
                painter.drawRect(QRectF(scene_point(a), scene_point(b)).normalized())
            elif self.tool in ("circle", "arc"):
                radius = g.distance(points[0], points[1])
                preview = Entity("circle", [points[0]], radius=radius)
            elif self.tool == "ellipse" and len(points) >= 3:
                c, a, b = points[:3]
                r = g.distance(c, a)
                if r > g.EPS:
                    ratio = min(1, g.distance(b, g.project(b, c, a, False)) / r)
                    preview = Entity("ellipse", [c, (a[0] - c[0], a[1] - c[1])], data={"ratio": ratio})
            elif self.tool == "dimension" and len(points) >= 3:
                preview = Entity("dimension", points[:3])
            elif self.tool in ("move", "copy", "mirror"):
                a, b = points[0], points[-1]
                if g.distance(a, b) > g.EPS:
                    for e in self.document.editable(self.selected_ids):
                        ghost = copy.deepcopy(e)
                        if self.tool == "mirror":
                            ghost.transform(mirror_axis=(a, b))
                        else:
                            ghost.transform(offset=(b[0] - a[0], b[1] - a[1]))
                        painter.drawPath(entity_path(ghost))
            else:
                path = QPainterPath(scene_point(points[0]))
                for p in points[1:]:
                    path.lineTo(scene_point(p))
                painter.drawPath(path)
            if preview:
                painter.drawPath(entity_path(preview))
        painter.save()
        painter.resetTransform()
        if self._drag_start and self._drag_end:
            crossing = self._drag_end.x() < self._drag_start.x()
            color = QColor("#56d9bd" if crossing else "#7ea7ff")
            painter.setPen(QPen(color, 1, Qt.PenStyle.DashLine if crossing else Qt.PenStyle.SolidLine))
            color.setAlpha(35)
            painter.setBrush(color)
            painter.drawRect(QRectF(QPointF(self._drag_start), QPointF(self._drag_end)).normalized())
        if self._snap:
            point, name = self._snap
            pos = self.mapFromScene(scene_point(point))
            painter.setPen(QPen(QColor("#ffce73"), 1.5))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRect(QRectF(pos.x() - 5, pos.y() - 5, 10, 10))
            painter.drawText(pos + QPoint(12, -10), name)
        if self._cursor and self.tool != "select":
            pos = self.mapFromScene(scene_point(self._cursor))
            painter.setPen(QPen(QColor("#6b879a"), 1))
            painter.drawLine(pos + QPoint(-15, 0), pos + QPoint(15, 0))
            painter.drawLine(pos + QPoint(0, -15), pos + QPoint(0, 15))
            if self.points:
                length = g.distance(self.points[-1], self._cursor)
                painter.setPen(QColor("#9dc4d9"))
                painter.drawText(pos + QPoint(18, 22), f"{length:.3f}  /  {g.angle(self.points[-1], self._cursor):.1f}°")
        painter.restore()
