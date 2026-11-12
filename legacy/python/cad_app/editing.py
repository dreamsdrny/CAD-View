"""有明确适用范围的二维编辑运算。"""
import copy
import uuid

from . import geometry as g
from .model import Entity


def clone(e: Entity) -> Entity:
    result = copy.deepcopy(e)
    result.id = uuid.uuid4().hex
    result.source_handle = None
    return result


def offset(e: Entity, amount: float) -> Entity:
    result = clone(e)
    if e.kind == "circle":
        result.radius += amount
        if result.radius <= g.EPS:
            raise ValueError("偏移后半径必须大于 0。")
    elif e.kind == "line":
        a, b = e.points
        length = g.distance(a, b)
        if length < g.EPS:
            raise ValueError("零长度直线不能偏移。")
        result.transform(offset=(-(b[1] - a[1]) / length * amount, (b[0] - a[0]) / length * amount))
    elif e.closed and e.kind in ("polyline", "rectangle") and not any(e.data.get("bulges", [])):
        pts = e.points
        if len(pts) < 3:
            raise ValueError("闭合轮廓至少需要三个顶点。")
        turns = []
        for i, b in enumerate(pts):
            a, c = pts[i - 1], pts[(i + 1) % len(pts)]
            turns.append((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
        if min(turns) < -g.EPS and max(turns) > g.EPS:
            raise ValueError("首版仅支持直边凸多边形偏移；凹轮廓需要后续拓扑算法。")
        direction = -1 if g.signed_area(pts) > 0 else 1
        lines = [offset(Entity("line", [a, b]), amount * direction).points
                 for a, b in zip(pts, pts[1:] + pts[:1])]
        new_points = []
        for i, line in enumerate(lines):
            p = g.intersection(*lines[i - 1], *line, segments=False)
            if p is None:
                raise ValueError("轮廓含平行相邻边，无法偏移，请先清理顶点。")
            new_points.append(p)
        # Reject collapsed/reversed edges on excessive inward offsets.
        for i in range(len(pts)):
            a, b, c, d = pts[i], pts[(i + 1) % len(pts)], new_points[i], new_points[(i + 1) % len(pts)]
            if (b[0] - a[0]) * (d[0] - c[0]) + (b[1] - a[1]) * (d[1] - c[1]) <= g.EPS:
                raise ValueError("偏移距离过大，轮廓已坍缩。")
        result.points = new_points
    else:
        raise ValueError("首版偏移支持直线、圆、直边凸闭合多段线。")
    return result


def explode(e: Entity) -> list[Entity]:
    if e.kind not in ("polyline", "rectangle"):
        raise ValueError("请选择多段线或矩形进行分解。")
    result = []
    for a, b, arc in g.poly_segments(e.points, e.closed, e.data.get("bulges", [])):
        if arc:
            center, radius, start, sweep = arc
            if sweep < 0:
                start, sweep = start + sweep, -sweep
            piece = Entity("arc", [center], e.layer, radius=radius, color=e.color,
                           data={"start": start % 360, "sweep": sweep})
        else:
            piece = Entity("line", [a, b], e.layer, color=e.color)
        result.append(piece)
    return result


def join(lines: list[Entity], tolerance: float = 1e-5) -> Entity:
    if len(lines) < 2 or any(e.kind != "line" for e in lines):
        raise ValueError("首版合并需要至少两条端点相接的直线。")
    if len({e.layer for e in lines}) != 1:
        raise ValueError("请先将待合并直线放到同一图层。")
    remaining = list(lines[1:])
    pts = list(lines[0].points)
    while remaining:
        for i, line in enumerate(remaining):
            a, b = line.points
            if g.distance(pts[-1], a) <= tolerance:
                pts.append(b)
            elif g.distance(pts[-1], b) <= tolerance:
                pts.append(a)
            elif g.distance(pts[0], b) <= tolerance:
                pts.insert(0, a)
            elif g.distance(pts[0], a) <= tolerance:
                pts.insert(0, b)
            else:
                continue
            remaining.pop(i)
            break
        else:
            raise ValueError("直线不连续，无法合并为单一多段线。")
    closed = g.distance(pts[0], pts[-1]) <= tolerance
    if closed:
        pts.pop()
    if closed and len(pts) < 3:
        raise ValueError("无效闭合轮廓。")
    return Entity("polyline", pts, lines[0].layer, closed=closed, color=lines[0].color)


def trim_extend(target: Entity, boundary: Entity, endpoint: int, extend: bool) -> list[g.Point]:
    if target.kind != "line" or boundary.kind != "line":
        raise ValueError("首版修剪/延伸支持两条直线。")
    a, b = target.points
    c, d = boundary.points
    hit = g.intersection(a, b, c, d, segments=False)
    if hit is None or g.distance(hit, g.project(hit, c, d)) > 1e-6:
        raise ValueError("两条直线没有有效边界交点。")
    on_segment = g.distance(hit, g.project(hit, a, b)) < 1e-6
    if extend and on_segment:
        raise ValueError("交点位于对象内部，无需延伸。")
    if not extend and not on_segment:
        raise ValueError("交点不在对象内部，无法修剪。")
    points = list(target.points)
    points[endpoint] = hit
    if g.distance(*points) < g.EPS:
        raise ValueError("操作会生成零长度直线。")
    if extend and g.distance(hit, target.points[endpoint]) > g.distance(hit, target.points[1 - endpoint]):
        raise ValueError("请选择靠近交点的端点进行延伸。")
    return points
