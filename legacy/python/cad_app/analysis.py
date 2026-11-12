"""PCB/FPC 工程检查。单位沿用文档，不推断圆形是否为钻孔。"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
import math

from . import geometry as g
from .model import Document, Entity


@dataclass
class Finding:
    description: str
    entity_ids: set[str] = field(default_factory=set)
    value: float | None = None


def hole_statistics(entities: list[Entity], precision: int = 3) -> list[Finding]:
    groups = {}
    for e in entities:
        if e.kind == "circle":
            groups.setdefault(round(e.radius * 2, precision), []).append(e)
    return [Finding(f"孔径 {diameter:.{precision}f} · 数量 {len(items)}", {e.id for e in items}, diameter)
            for diameter, items in sorted(groups.items())]


def board_metrics(entity: Entity) -> dict:
    if not (entity.closed or entity.kind == "circle"):
        raise ValueError("请选择一个闭合多段线、矩形或圆作为板框。")
    x0, y0, x1, y1 = entity.bbox()
    props = entity.properties()
    if "面积" not in props:
        area = g.polygon_area(entity.outline())
        perimeter = g.poly_length(entity.outline())
    else:
        area, perimeter = props["面积"], props["周长"]
    return {"包围盒宽": x1 - x0, "包围盒高": y1 - y0, "面积": area, "周长": perimeter}


def point_inside(point: g.Point, polygon: list[g.Point]) -> bool:
    inside = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > point[1]) != (b[1] > point[1]):
            x = (b[0] - a[0]) * (point[1] - a[1]) / (b[1] - a[1]) + a[0]
            if point[0] < x:
                inside = not inside
    return inside


def _closed(e: Entity):
    return e.closed or e.kind == "circle"


def _contains(e: Entity, point: g.Point):
    if e.kind == "circle":
        return g.distance(point, e.points[0]) < e.radius
    return _closed(e) and point_inside(point, e.outline())


def boundary_gap(a: Entity, b: Entity, tolerance=0.005) -> float:
    if a.kind == b.kind == "circle":
        d = g.distance(a.points[0], b.points[0])
        return max(0.0, d - a.radius - b.radius, abs(a.radius - b.radius) - d)
    if a.kind == "circle" or b.kind == "circle":
        circle, other = (a, b) if a.kind == "circle" else (b, a)
        points = other.outline(tolerance)
        center = circle.points[0]
        segments = list(zip(points, points[1:]))
        if not segments:
            return max(0, g.distance(center, points[0]) - circle.radius) if points else math.inf
        minimum = min(g.distance(center, g.project(center, p, q)) for p, q in segments)
        maximum = max(g.distance(center, p) for p in points)
        if minimum <= circle.radius <= maximum:
            return 0.0
        return minimum - circle.radius if minimum > circle.radius else circle.radius - maximum
    pa, pb = a.outline(tolerance), b.outline(tolerance)
    sa, sb = list(zip(pa, pa[1:])), list(zip(pb, pb[1:]))
    if not sa or not sb:
        return math.inf
    minimum = math.inf
    for p, q in sa:
        for r, s in sb:
            minimum = min(minimum, g.segment_gap(p, q, r, s))
            if minimum < g.EPS:
                return 0.0
    return minimum


def clearance(a: Entity, b: Entity) -> float:
    # Closed outlines are treated as filled copper/pads for clearance checking.
    if _contains(a, b.points[0]) or _contains(b, a.points[0]):
        return 0.0
    return boundary_gap(a, b)


def clearance_checks(entities: list[Entity], minimum: float) -> list[Finding]:
    shapes = [e for e in entities if e.kind in ("line", "circle", "arc", "polyline", "rectangle", "ellipse")]
    if len(shapes) > 1500:
        raise ValueError("首版间距检查请先选择不超过 1500 个对象，或按图层缩小范围。")
    results = []
    boxes = [e.bbox() for e in shapes]
    for i, a in enumerate(shapes):
        ax0, ay0, ax1, ay1 = boxes[i]
        for j in range(i + 1, len(shapes)):
            bx0, by0, bx1, by1 = boxes[j]
            if math.hypot(max(0, ax0 - bx1, bx0 - ax1), max(0, ay0 - by1, by0 - ay1)) >= minimum:
                continue
            gap = clearance(a, shapes[j])
            if gap < minimum:
                results.append(Finding(f"间距 {gap:.4f} < {minimum:.4f} · {a.layer} / {shapes[j].layer}",
                                       {a.id, shapes[j].id}, gap))
    return sorted(results, key=lambda f: f.value)


def edge_checks(board: Entity, entities: list[Entity], minimum: float) -> list[Finding]:
    if not _closed(board):
        raise ValueError("板框必须闭合。")
    results = []
    for e in entities:
        if e.id == board.id or e.kind in ("text", "dimension"):
            continue
        gap = boundary_gap(board, e)
        contained = all(_contains(board, p) for p in e.outline())
        if not contained or gap < minimum:
            results.append(Finding(("板框外对象" if not contained else f"距板边 {gap:.4f} < {minimum:.4f}"),
                                   {board.id, e.id}, gap))
    return results


def pad_checks(entities: list[Entity], min_width: float, min_height: float) -> list[Finding]:
    findings = []
    for e in entities:
        if not _closed(e):
            continue
        if e.kind == "circle":
            w = h = 2 * e.radius
        elif e.kind == "rectangle":
            w, h = g.distance(e.points[0], e.points[1]), g.distance(e.points[1], e.points[2])
        else:
            x0, y0, x1, y1 = e.bbox()
            w, h = x1 - x0, y1 - y0
        if w < min_width or h < min_height:
            findings.append(Finding(f"尺寸 {w:.4f} × {h:.4f} 小于要求 {min_width} × {min_height}", {e.id}))
    return findings


def duplicate_checks(entities: list[Entity]) -> list[Finding]:
    groups = {}
    for e in entities:
        if e.kind == "line":
            points = tuple(sorted(tuple(round(v, 6) for v in p) for p in e.points))
            key = (e.kind, points)
        elif e.kind == "circle":
            key = (e.kind, tuple(round(v, 6) for v in e.points[0]), round(e.radius, 6))
        elif e.kind in ("polyline", "rectangle"):
            # Include bulges: different arc geometry must never be reported as identical.
            pts = [tuple(round(v, 6) for v in p) for p in e.points]
            bulges = e.data.get("bulges", [])
            forward = [(p, round(bulges[i] if i < len(bulges) else 0, 6)) for i, p in enumerate(pts)]
            if e.closed and forward:
                rotations = [tuple(forward[i:] + forward[:i]) for i in range(len(forward))]
                reverse = [(pts[i], -round(bulges[(i - 1) % len(pts)] if (i - 1) % len(pts) < len(bulges) else 0, 6))
                           for i in range(len(pts) - 1, -1, -1)]
                rotations += [tuple(reverse[i:] + reverse[:i]) for i in range(len(reverse))]
                key = ("polyline", True, min(rotations))
            else:
                reverse = [(pts[i], -round(bulges[i - 1] if 0 <= i - 1 < len(bulges) else 0, 6))
                           for i in range(len(pts) - 1, -1, -1)]
                key = ("polyline", False, min(tuple(forward), tuple(reverse)))
        else:
            continue
        groups.setdefault(key, []).append(e)
    return [Finding(f"重复图形 × {len(items)} · " + ", ".join(sorted({e.layer for e in items})),
                    {e.id for e in items}) for items in groups.values() if len(items) > 1]


def drawing_statistics(document: Document) -> list[Finding]:
    kinds = Counter(e.kind for e in document.entities)
    return [Finding(f"{kind}: {count}", {e.id for e in document.entities if e.kind == kind})
            for kind, count in sorted(kinds.items())]
