from __future__ import annotations

import copy
import math
import uuid
from dataclasses import dataclass, field

from . import geometry as g


@dataclass
class Layer:
    name: str
    color: str = "#8fc8e8"
    visible: bool = True
    locked: bool = False
    lineweight: float = 0.25


@dataclass
class Entity:
    kind: str
    points: list[g.Point]
    layer: str = "0"
    radius: float = 0.0
    closed: bool = False
    data: dict = field(default_factory=dict)
    color: str | None = None
    id: str = field(default_factory=lambda: uuid.uuid4().hex)
    editable: bool = True
    source_handle: str | None = None

    def outline(self, tolerance: float = 0.005) -> list[g.Point]:
        if self.kind in ("circle", "arc"):
            start = self.data.get("start", 0)
            sweep = 360 if self.kind == "circle" else self.data.get("sweep", 90)
            return g.arc_points(self.points[0], self.radius, start, sweep, tolerance)
        if self.kind == "ellipse":
            center, major = self.points
            ratio = self.data.get("ratio", 0.5)
            start, end = self.data.get("start", 0), self.data.get("end", math.tau)
            r = g.distance((0, 0), major)
            step = max(0.001, 2 * math.acos(max(-1, min(1, 1 - tolerance / max(r, g.EPS)))))
            n = min(8192, max(16, math.ceil(abs(end - start) / step)))
            return [(center[0] + major[0] * math.cos(t) - major[1] * ratio * math.sin(t),
                     center[1] + major[1] * math.cos(t) + major[0] * ratio * math.sin(t))
                    for t in (start + (end - start) * i / n for i in range(n + 1))]
        if self.kind in ("polyline", "rectangle"):
            result = []
            for a, b, arc in g.poly_segments(self.points, self.closed, self.data.get("bulges", [])):
                result.extend(g.arc_points(*arc, tolerance)[:-1] if arc else [a])
            if self.points:
                result.append(self.points[0] if self.closed else self.points[-1])
            return result
        return self.points

    def bbox(self):
        if self.kind == "circle":
            x, y = self.points[0]
            return x - self.radius, y - self.radius, x + self.radius, y + self.radius
        if self.kind == "arc":
            start, sweep = self.data.get("start", 0), self.data.get("sweep", 90)
            angles = [start, start + sweep]
            angles += [a for a in (0, 90, 180, 270) if (a - start) % 360 <= sweep + g.EPS]
            x, y = self.points[0]
            return g.bounds((x + self.radius * math.cos(math.radians(a)),
                             y + self.radius * math.sin(math.radians(a))) for a in angles)
        if self.kind == "ellipse":
            center, (mx, my) = self.points
            ratio = self.data.get("ratio", 0.5)
            start, end = self.data.get("start", 0), self.data.get("end", math.tau)
            tx, ty = math.atan2(-my * ratio, mx), math.atan2(mx * ratio, my)
            params = [start, end]
            params += [t for t in (tx, tx + math.pi, ty, ty + math.pi)
                       if (t - start) % math.tau <= end - start + g.EPS]
            return g.bounds((center[0] + mx * math.cos(t) - my * ratio * math.sin(t),
                             center[1] + my * math.cos(t) + mx * ratio * math.sin(t)) for t in params)
        if self.kind == "text":
            x, y = self.points[0]
            lines = self.data.get("text", "").splitlines() or [""]
            h = self.data.get("height", 2.5)
            w = max(len(line) for line in lines) * h
            bottom = y - (len(lines) - 1) * h * 1.3
            corners = [(x, bottom), (x + w, bottom), (x + w, y + h), (x, y + h)]
            return g.bounds(g.rotate(p, (x, y), self.data.get("rotation", 0)) for p in corners)
        return g.bounds(self.outline())

    def properties(self) -> dict:
        names = {"line": "直线", "polyline": "多段线", "rectangle": "矩形", "circle": "圆",
                 "arc": "圆弧", "ellipse": "椭圆", "text": "文字", "dimension": "线性标注"}
        p = {"类型": names.get(self.kind, self.kind), "图层": self.layer, "可编辑": "是" if self.editable else "否"}
        if self.kind == "line":
            p.update({"起点": self.points[0], "终点": self.points[1],
                      "长度": g.distance(*self.points), "角度 (°)": g.angle(*self.points)})
        elif self.kind in ("circle", "arc"):
            p.update({"圆心": self.points[0], "半径": self.radius, "直径": self.radius * 2})
            if self.kind == "circle":
                p.update({"面积": math.pi * self.radius ** 2, "周长": math.tau * self.radius})
            else:
                p.update({"起始角 (°)": self.data.get("start", 0), "圆心角 (°)": self.data.get("sweep", 90),
                          "弧长": self.radius * math.radians(abs(self.data.get("sweep", 90)))})
        elif self.kind in ("polyline", "rectangle"):
            length, area = g.poly_metrics(self.points, self.closed, self.data.get("bulges", []))
            p.update({"顶点数量": len(self.points), "闭合": "是" if self.closed else "否", "总长度": length})
            if self.closed:
                p.update({"面积": area, "周长": length})
            if self.kind == "rectangle" and len(self.points) == 4:
                p.update({"长": g.distance(self.points[0], self.points[1]), "宽": g.distance(self.points[1], self.points[2])})
        elif self.kind == "ellipse":
            p.update({"圆心": self.points[0], "长半轴": g.distance((0, 0), self.points[1]),
                      "短长轴比": self.data.get("ratio", 0.5)})
        elif self.kind == "text":
            p.update({"文字": self.data.get("text", ""), "位置": self.points[0], "高度": self.data.get("height", 2.5)})
        elif self.kind == "dimension":
            p.update({"距离": g.distance(self.points[0], self.points[1]), "文字": self.data.get("text", "<>")})
        return p

    def snap_points(self):
        if self.kind in ("circle", "arc", "ellipse"):
            result = [(self.points[0], "圆心")]
            if self.kind == "circle":
                x, y = self.points[0]
                result += [((x + self.radius * dx, y + self.radius * dy), "象限点")
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
            elif self.kind == "arc":
                outline = self.outline()
                result.extend([(outline[0], "端点"), (outline[-1], "端点")])
            return result
        result = [(p, "端点") for p in self.points]
        if self.kind in ("line", "polyline", "rectangle"):
            for a, b, arc in g.poly_segments(self.points, self.closed, self.data.get("bulges", [])):
                if arc:
                    center, r, start, sweep = arc
                    mid = g.arc_points(center, r, start, sweep / 2)[-1]
                else:
                    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
                result.append((mid, "中点"))
        return result

    def transform(self, offset=(0.0, 0.0), center=None, degrees=0.0, mirror_axis=None):
        def apply(p):
            if center is not None:
                p = g.rotate(p, center, degrees)
            if mirror_axis:
                a, b = mirror_axis
                q = g.project(p, a, b, False)
                p = (2 * q[0] - p[0], 2 * q[1] - p[1])
            return p[0] + offset[0], p[1] + offset[1]

        if self.kind == "ellipse":
            c, major = self.points
            end = (c[0] + major[0], c[1] + major[1])
            nc, ne = apply(c), apply(end)
            self.points = [nc, (ne[0] - nc[0], ne[1] - nc[1])]
            if mirror_axis:
                start, end = self.data.get("start", 0), self.data.get("end", math.tau)
                self.data.update(start=-end, end=-start)
        else:
            self.points = [apply(p) for p in self.points]
        if self.kind == "arc":
            start, sweep = self.data.get("start", 0) + degrees, self.data.get("sweep", 90)
            if mirror_axis:
                start = 2 * g.angle(*mirror_axis) - (start + sweep)
            self.data["start"] = start % 360
        if self.kind == "text":
            self.data["rotation"] = (self.data.get("rotation", 0) + degrees) % 360
            if mirror_axis:
                self.data["rotation"] = (2 * g.angle(*mirror_axis) - self.data["rotation"]) % 360
        if mirror_axis and self.data.get("bulges"):
            self.data["bulges"] = [-b for b in self.data["bulges"]]


class Document:
    def __init__(self, title="未命名"):
        self.title = title
        self.path: str | None = None
        self.entities: list[Entity] = []
        self.layers = {"0": Layer("0")}
        self.current_layer = "0"
        self.unit = "mm"
        self.insunits = 4
        self.source_text: str | None = None
        self.imported_handles: set[str] = set()
        self.imported_baseline: dict[str, Entity] = {}
        self.warnings: list[str] = []
        self._undo: list[tuple[str, tuple]] = []
        self._redo: list[tuple[str, tuple]] = []
        self._saved = self.snapshot()

    def snapshot(self):
        return copy.deepcopy((self.entities, self.layers, self.current_layer, self.unit, self.insunits))

    @property
    def modified(self):
        state = self.snapshot()
        return (state[0], state[1], state[3], state[4]) != (self._saved[0], self._saved[1], self._saved[3], self._saved[4])

    def mark_saved(self):
        self._saved = self.snapshot()

    def checkpoint(self, label: str):
        self._undo.append((label, self.snapshot()))
        self._undo = self._undo[-100:]
        self._redo.clear()

    def undo(self) -> str | None:
        if not self._undo:
            return None
        label, state = self._undo.pop()
        self._redo.append((label, self.snapshot()))
        self.entities, self.layers, self.current_layer, self.unit, self.insunits = state
        return label

    def redo(self) -> str | None:
        if not self._redo:
            return None
        label, state = self._redo.pop()
        self._undo.append((label, self.snapshot()))
        self.entities, self.layers, self.current_layer, self.unit, self.insunits = state
        return label

    def editable(self, ids: set[str]) -> list[Entity]:
        return [e for e in self.entities if e.id in ids and e.editable
                and not self.layers[e.layer].locked and self.layers[e.layer].visible]

    def visible_entities(self):
        return [e for e in self.entities if self.layers.get(e.layer, Layer("0")).visible]

    def add(self, entity: Entity):
        layer = self.layers[entity.layer]
        if layer.locked or not layer.visible:
            raise ValueError("当前图层已锁定或隐藏，请切换到可编辑图层。")
        self.checkpoint("绘制")
        self.entities.append(entity)

    def bounds(self):
        boxes = [e.bbox() for e in self.visible_entities()]
        if not boxes:
            return (-50, -35, 50, 35)
        return min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes)


def demo_document() -> Document:
    d = Document("PCB 演示板")
    d.layers = {name: Layer(name, color) for name, color in
                [("0", "#94a3b8"), ("BOARD", "#56d9bd"), ("DRILL", "#ffce73"),
                 ("PAD", "#7ea7ff"), ("SILK", "#c4b5fd")]}
    d.current_layer = "BOARD"
    d.entities.append(Entity("rectangle", [(0, 0), (80, 0), (80, 50), (0, 50)], "BOARD", closed=True))
    for x in (5, 75):
        for y in (5, 45):
            d.entities.append(Entity("circle", [(x, y)], "DRILL", radius=1.6))
    for x in range(24, 57, 4):
        for y in (15, 35):
            d.entities.append(Entity("rectangle", [(x - 1, y - 1.5), (x + 1, y - 1.5),
                                                  (x + 1, y + 1.5), (x - 1, y + 1.5)], "PAD", closed=True))
    d.entities.append(Entity("rectangle", [(22, 18), (58, 18), (58, 32), (22, 32)], "SILK", closed=True))
    d.entities.append(Entity("text", [(26, 23)], "SILK", data={"text": "CAD WORKBENCH", "height": 2.2}))
    d.entities.append(Entity("dimension", [(0, 0), (80, 0), (40, -8)], "0"))
    d.mark_saved()
    return d
