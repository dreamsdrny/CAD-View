"""二维几何计算；模型坐标为 Y 向上，角度使用度。"""
from __future__ import annotations

import math
from typing import Iterable

Point = tuple[float, float]
EPS = 1e-9


def distance(a: Point, b: Point) -> float:
    return math.hypot(b[0] - a[0], b[1] - a[1])


def angle(a: Point, b: Point) -> float:
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])) % 360


def rotate(p: Point, center: Point, degrees: float) -> Point:
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    x, y = p[0] - center[0], p[1] - center[1]
    return center[0] + x * c - y * s, center[1] + x * s + y * c


def project(p: Point, a: Point, b: Point, clamp: bool = True) -> Point:
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    if length2 < EPS:
        return a
    t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2
    if clamp:
        t = max(0.0, min(1.0, t))
    return a[0] + t * dx, a[1] + t * dy


def intersection(a: Point, b: Point, c: Point, d: Point, segments: bool = True) -> Point | None:
    ux, uy = b[0] - a[0], b[1] - a[1]
    vx, vy = d[0] - c[0], d[1] - c[1]
    cross = ux * vy - uy * vx
    if abs(cross) < EPS:
        return None
    t = ((c[0] - a[0]) * vy - (c[1] - a[1]) * vx) / cross
    s = ((c[0] - a[0]) * uy - (c[1] - a[1]) * ux) / cross
    if segments and not (-EPS <= t <= 1 + EPS and -EPS <= s <= 1 + EPS):
        return None
    return a[0] + t * ux, a[1] + t * uy


def segment_gap(a: Point, b: Point, c: Point, d: Point) -> float:
    if intersection(a, b, c, d) is not None:
        return 0.0
    return min(distance(a, project(a, c, d)), distance(b, project(b, c, d)),
               distance(c, project(c, a, b)), distance(d, project(d, a, b)))


def polygon_area(points: list[Point]) -> float:
    return abs(signed_area(points))


def signed_area(points: list[Point]) -> float:
    if len(points) < 3:
        return 0.0
    return sum(a[0] * b[1] - b[0] * a[1]
               for a, b in zip(points, points[1:] + points[:1])) / 2


def poly_length(points: list[Point], closed: bool = False) -> float:
    return sum(distance(a, b) for a, b in zip(points, points[1:])) + (
        distance(points[-1], points[0]) if closed and len(points) > 1 else 0)


def bounds(points: Iterable[Point]) -> tuple[float, float, float, float]:
    pts = list(points)
    if not pts:
        return (-1, -1, 1, 1)
    return min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)


def bulge_arc(a: Point, b: Point, bulge: float) -> tuple[Point, float, float, float] | None:
    chord = distance(a, b)
    if abs(bulge) < EPS or chord < EPS:
        return None
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    offset = chord * (1 - bulge * bulge) / (4 * bulge)
    center = (mid[0] - (b[1] - a[1]) / chord * offset,
              mid[1] + (b[0] - a[0]) / chord * offset)
    return center, distance(center, a), angle(center, a), math.degrees(4 * math.atan(bulge))


def arc_points(center: Point, radius: float, start: float, sweep: float,
               tolerance: float = 0.005) -> list[Point]:
    if radius < EPS:
        return [center]
    step = math.degrees(2 * math.acos(max(-1.0, min(1.0, 1 - tolerance / radius))))
    count = max(2, min(8192, math.ceil(abs(sweep) / max(step, 0.01))))
    return [(center[0] + radius * math.cos(math.radians(start + sweep * i / count)),
             center[1] + radius * math.sin(math.radians(start + sweep * i / count)))
            for i in range(count + 1)]


def poly_segments(points: list[Point], closed: bool, bulges: list[float]):
    for i in range(len(points) if closed else max(0, len(points) - 1)):
        a, b = points[i], points[(i + 1) % len(points)]
        arc = bulge_arc(a, b, bulges[i] if i < len(bulges) else 0)
        yield a, b, arc


def poly_metrics(points: list[Point], closed: bool, bulges: list[float]) -> tuple[float, float]:
    length, area = 0.0, 0.0
    for a, b, arc in poly_segments(points, closed, bulges):
        area += (a[0] * b[1] - b[0] * a[1]) / 2
        if arc:
            _, radius, _, sweep = arc
            theta = math.radians(sweep)
            length += radius * abs(theta)
            area += radius * radius * (theta - math.sin(theta)) / 2
        else:
            length += distance(a, b)
    return length, abs(area) if closed else 0.0
