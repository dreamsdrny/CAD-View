"""DXF 适配器。保留源文档中的布局、块与未支持对象。"""
from __future__ import annotations

import io
import copy
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from collections import Counter

import ezdxf
from ezdxf import colors

from .model import Document, Entity, Layer

UNIT_NAMES = {0: "未指定", 1: "inch", 2: "ft", 4: "mm", 5: "cm", 6: "m", 9: "mil", 13: "µm"}


def _color(aci: int, true_color=None) -> str | None:
    if true_color is not None:
        return "#%06x" % true_color
    if aci in (0, 256):
        return None
    if abs(aci) == 7:
        return "#dce7ef"
    return "#%02x%02x%02x" % colors.aci2rgb(abs(aci))


def _convert_entity(e, layer: str | None = None, editable=True) -> list[Entity]:
    kind = e.dxftype()
    layer = layer or e.dxf.layer
    attrs = {"layer": layer, "color": _color(e.dxf.get("color", 256), e.dxf.get("true_color")),
             "editable": editable, "source_handle": e.dxf.get("handle") if editable else None}
    xy = lambda v: (float(v[0]), float(v[1]))
    # OCS/3D entities stay in the original file rather than silently flattening coordinates.
    if e.dxf.hasattr("extrusion") and tuple(e.dxf.extrusion) != (0, 0, 1):
        return []
    if kind == "LINE":
        if abs(e.dxf.start.z) > 1e-8 or abs(e.dxf.end.z) > 1e-8:
            return []
        return [Entity("line", [xy(e.dxf.start), xy(e.dxf.end)], **attrs)]
    if kind in ("CIRCLE", "ARC"):
        if abs(e.dxf.center.z) > 1e-8:
            return []
        data = {} if kind == "CIRCLE" else {"start": e.dxf.start_angle,
                                             "sweep": (e.dxf.end_angle - e.dxf.start_angle) % 360}
        return [Entity(kind.lower(), [xy(e.dxf.center)], radius=e.dxf.radius, data=data, **attrs)]
    if kind == "LWPOLYLINE":
        if abs(e.dxf.get("elevation", 0)) > 1e-8:
            return []
        pts = list(e.get_points("xyseb"))
        return [Entity("polyline", [(p[0], p[1]) for p in pts], closed=e.closed,
                       data={"bulges": [p[4] for p in pts], "widths": [(p[2], p[3]) for p in pts],
                             "const_width": e.dxf.get("const_width", 0)}, **attrs)]
    if kind == "POLYLINE" and e.is_2d_polyline:
        if abs(e.dxf.elevation.z) > 1e-8:
            return []
        return [Entity("polyline", [xy(v.dxf.location) for v in e.vertices], closed=e.is_closed,
                       data={"bulges": [v.dxf.get("bulge", 0) for v in e.vertices],
                             "widths": [(v.dxf.get("start_width", 0), v.dxf.get("end_width", 0))
                                        for v in e.vertices]}, **attrs)]
    if kind == "ELLIPSE":
        if abs(e.dxf.center.z) > 1e-8 or abs(e.dxf.major_axis.z) > 1e-8:
            return []
        start, end = e.dxf.start_param, e.dxf.end_param
        if end <= start:
            end += math.tau
        return [Entity("ellipse", [xy(e.dxf.center), xy(e.dxf.major_axis)],
                       closed=abs(end - start - math.tau) < 1e-6,
                       data={"ratio": e.dxf.ratio, "start": start, "end": end}, **attrs)]
    if kind == "TEXT":
        # Non-default alignment/style remains source-backed to preserve typography.
        if e.dxf.get("halign", 0) or e.dxf.get("valign", 0):
            attrs["editable"] = False
        return [Entity("text", [xy(e.dxf.insert)],
                       data={"text": e.dxf.text, "height": e.dxf.height,
                             "rotation": e.dxf.get("rotation", 0)}, **attrs)]
    if kind == "MTEXT":
        attrs["editable"] = False
        return [Entity("text", [xy(e.dxf.insert)],
                       data={"text": e.plain_text(), "height": e.dxf.char_height,
                             "rotation": e.dxf.get("rotation", 0)}, **attrs)]
    if kind in ("INSERT", "DIMENSION"):
        result = []
        for child in e.virtual_entities():
            child_layer = layer if child.dxf.get("layer", "0") == "0" else child.dxf.layer
            children = _convert_entity(child, child_layer, editable=False)
            for item in children:
                item.color = item.color or attrs["color"]
            result.extend(children)
        return result
    return []


def read_dxf(path: str | Path) -> Document:
    source = ezdxf.readfile(path)
    stream = io.StringIO()
    source.write(stream)
    d = Document(Path(path).stem)
    d.path = str(path)
    d.source_text = stream.getvalue()
    d.insunits = source.header.get("$INSUNITS", 0)
    d.unit = UNIT_NAMES.get(d.insunits, f"单位代码 {d.insunits}")
    d.layers = {}
    for layer in source.layers:
        d.layers[layer.dxf.name] = Layer(layer.dxf.name,
                                       _color(layer.dxf.color, layer.dxf.get("true_color")) or "#8fc8e8",
                                       not layer.is_off() and not layer.is_frozen(), layer.is_locked(),
                                       max(0.05, layer.dxf.get("lineweight", 25) / 100))
    skipped = Counter()
    for source_entity in source.modelspace():
        try:
            converted = _convert_entity(source_entity)
        except (ValueError, TypeError, ArithmeticError, ezdxf.DXFError):
            converted = []
        if not converted:
            skipped[source_entity.dxftype()] += 1
        d.entities.extend(converted)
        if converted and all(e.editable for e in converted):
            d.imported_handles.add(source_entity.dxf.handle)
            d.imported_baseline[source_entity.dxf.handle] = copy.deepcopy(converted[0])
    for entity in d.entities:
        d.layers.setdefault(entity.layer, Layer(entity.layer))
    if skipped:
        d.warnings.append("以下对象保留在源文件中，首版暂不显示/编辑：" +
                          "、".join(f"{k} × {v}" for k, v in skipped.items()))
    if any(not e.editable for e in d.entities):
        d.warnings.append("块、导入标注及复杂文字以只读图形显示；保存时保留源对象。")
    if d.insunits == 0:
        d.warnings.append("文件未声明单位，测量值按图形坐标单位显示；请在文件菜单确认单位。")
    d.current_layer = next((l.name for l in d.layers.values() if l.visible and not l.locked), "0")
    d.mark_saved()
    return d


def _write_entity(msp, e: Entity):
    attrs = {"layer": e.layer}
    if e.color:
        attrs["true_color"] = int(e.color.lstrip("#"), 16)
    if e.kind == "line":
        msp.add_line(*e.points, dxfattribs=attrs)
    elif e.kind == "circle":
        msp.add_circle(e.points[0], e.radius, dxfattribs=attrs)
    elif e.kind == "arc":
        start = e.data.get("start", 0)
        msp.add_arc(e.points[0], e.radius, start, start + e.data.get("sweep", 90), dxfattribs=attrs)
    elif e.kind in ("polyline", "rectangle"):
        bulges = e.data.get("bulges", [])
        widths = e.data.get("widths", [])
        points = []
        for i, (x, y) in enumerate(e.points):
            sw, ew = widths[i] if i < len(widths) else (0, 0)
            points.append((x, y, sw, ew, bulges[i] if i < len(bulges) else 0))
        msp.add_lwpolyline(points, close=e.closed, format="xyseb",
                           dxfattribs={**attrs, "const_width": e.data.get("const_width", 0)})
    elif e.kind == "ellipse":
        msp.add_ellipse(e.points[0], e.points[1], e.data.get("ratio", 0.5),
                        e.data.get("start", 0), e.data.get("end", math.tau), dxfattribs=attrs)
    elif e.kind == "text":
        msp.add_text(e.data.get("text", ""), dxfattribs={**attrs, "insert": e.points[0],
                     "height": e.data.get("height", 2.5), "rotation": e.data.get("rotation", 0)})
    elif e.kind == "dimension":
        # Aligned dimension with actual DIMENSION object + rendered anonymous block.
        from .geometry import distance, project
        a, b, c = e.points
        foot = project(c, a, b, False)
        offset = distance(c, foot)
        if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) < 0:
            offset = -offset
        dim = msp.add_aligned_dim(a, b, offset, text=e.data.get("text", "<>"),
                                  dxfattribs=attrs,
                                  override={"dimtxt": 2, "dimasz": 1, "dimdec": 3})
        dim.render()


def write_dxf(document: Document, path: str | Path):
    source = ezdxf.read(io.StringIO(document.source_text)) if document.source_text else ezdxf.new("R2010")
    # Loaded older files need a modern version for LWPOLYLINE/true color.
    if source.dxfversion < "AC1015":
        source.dxfversion = "AC1015"
    source.header["$INSUNITS"] = document.insunits
    msp = source.modelspace()
    current_by_handle = {e.source_handle: e for e in document.entities if e.editable and e.source_handle}
    kept = set()
    for handle in document.imported_handles:
        entity = source.entitydb.get(handle)
        if entity is not None and entity.is_alive:
            current = current_by_handle.get(handle)
            if current is not None and current == document.imported_baseline.get(handle):
                kept.add(current.id)
            elif current is not None and _update_source(entity, current):
                kept.add(current.id)
            else:
                msp.delete_entity(entity)
    for layer in document.layers.values():
        target = source.layers.get(layer.name) if layer.name in source.layers else source.layers.new(layer.name)
        target.dxf.true_color = int(layer.color.lstrip("#"), 16)
        target.dxf.lineweight = min((0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211),
                                    key=lambda x: abs(x - round(layer.lineweight * 100)))
        target.thaw()
        target.on() if layer.visible else target.off()
        target.lock() if layer.locked else target.unlock()
    for name in list(source.layers.entries):
        layer = source.layers.entries[name]
        if layer.dxf.name not in document.layers and layer.dxf.name not in ("0", "Defpoints"):
            source.layers.remove(layer.dxf.name)
    for entity in document.entities:
        if entity.editable and entity.id not in kept:
            _write_entity(msp, entity)
    # Write beside destination then replace atomically, so a failure cannot truncate the original.
    destination = Path(path)
    fd, temporary = tempfile.mkstemp(suffix=".dxf", dir=destination.parent)
    os.close(fd)
    try:
        source.saveas(temporary)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _update_source(source, e: Entity) -> bool:
    """Update supported geometry in-place, retaining linetypes, XDATA and text styles."""
    kind = source.dxftype()
    if kind == "LINE" and e.kind == "line":
        source.dxf.start, source.dxf.end = e.points
    elif kind in ("CIRCLE", "ARC") and e.kind == kind.lower():
        source.dxf.center, source.dxf.radius = e.points[0], e.radius
        if kind == "ARC":
            source.dxf.start_angle = e.data.get("start", 0)
            source.dxf.end_angle = e.data.get("start", 0) + e.data.get("sweep", 90)
    elif kind == "LWPOLYLINE" and e.kind in ("polyline", "rectangle"):
        widths, bulges = e.data.get("widths", []), e.data.get("bulges", [])
        source.set_points([(p[0], p[1], *(widths[i] if i < len(widths) else (0, 0)),
                            bulges[i] if i < len(bulges) else 0) for i, p in enumerate(e.points)], format="xyseb")
        source.closed = e.closed
    elif kind == "POLYLINE" and e.kind == "polyline" and len(source.vertices) == len(e.points):
        for i, (v, p) in enumerate(zip(source.vertices, e.points)):
            v.dxf.location = p
            v.dxf.bulge = e.data.get("bulges", [])[i] if i < len(e.data.get("bulges", [])) else 0
    elif kind == "ELLIPSE" and e.kind == "ellipse":
        source.dxf.center, source.dxf.major_axis = e.points
        source.dxf.ratio = e.data.get("ratio", 0.5)
        source.dxf.start_param, source.dxf.end_param = e.data.get("start", 0), e.data.get("end", math.tau)
    elif kind == "TEXT" and e.kind == "text":
        source.dxf.insert, source.dxf.text = e.points[0], e.data.get("text", "")
        source.dxf.height, source.dxf.rotation = e.data.get("height", 2.5), e.data.get("rotation", 0)
    else:
        return False
    source.dxf.layer = e.layer
    if e.color:
        source.dxf.true_color = int(e.color.lstrip("#"), 16)
    else:
        source.dxf.discard("true_color")
        source.dxf.color = 256
    return True


def source_layer_in_use(document: Document, name: str) -> bool:
    if not document.source_text:
        return False
    source = ezdxf.read(io.StringIO(document.source_text))
    # Imported editable objects are represented by the current model, but everything
    # else (including paper space and nested blocks) still lives in the source.
    return any(e.dxf.get("layer") == name and e.dxf.get("handle") not in document.imported_handles
               for block in source.blocks for e in block)


def convert_file(path: str | Path, destination: str | Path, converter: str):
    """ODA File Converter 命令行适配。用户需独立安装并提供可执行文件。"""
    if not converter or not Path(converter).is_file():
        raise ValueError("DWG 需要安装 ODA File Converter，并在“文件 → 配置 DWG 转换器”中选择程序。")
    path, destination = Path(path), Path(destination)
    with tempfile.TemporaryDirectory(prefix="cad-convert-") as temp:
        input_dir, output_dir = Path(temp) / "input", Path(temp) / "output"
        input_dir.mkdir()
        output_dir.mkdir()
        shutil.copy2(path, input_dir / path.name)
        command = [converter, str(input_dir), str(output_dir), "ACAD2018", destination.suffix[1:].upper(),
                   "0", "1", path.name]
        result = subprocess.run(command, capture_output=True, timeout=120,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        converted = output_dir / (path.stem + destination.suffix)
        if result.returncode or not converted.is_file():
            raise ValueError("ODA 转换失败，请确认转换器版本、文件格式与许可。" +
                             result.stderr.decode(errors="replace")[:500])
        # Keep the user's existing destination intact until conversion has succeeded.
        fd, staged = tempfile.mkstemp(suffix=destination.suffix, dir=destination.parent)
        os.close(fd)
        try:
            shutil.copy2(converted, staged)
            os.replace(staged, destination)
        finally:
            if os.path.exists(staged):
                os.unlink(staged)


def open_document(path: str, converter: str = "") -> Document:
    if Path(path).suffix.lower() == ".dwg":
        with tempfile.TemporaryDirectory(prefix="cad-dwg-") as temp:
            converted = Path(temp) / (Path(path).stem + ".dxf")
            convert_file(path, converted, converter)
            document = read_dxf(converted)
            document.path = path
            document.title = Path(path).stem
            return document
    return read_dxf(path)


def save_document(document: Document, path: str, converter: str = ""):
    if Path(path).suffix.lower() == ".dwg":
        with tempfile.TemporaryDirectory(prefix="cad-dwg-") as temp:
            intermediate = Path(temp) / (Path(path).stem + ".dxf")
            write_dxf(document, intermediate)
            convert_file(intermediate, path, converter)
    else:
        write_dxf(document, path)
    document.path = path
    document.title = Path(path).stem
    document.mark_saved()
