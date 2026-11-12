import copy
import math

import ezdxf
import pytest

from cad_app import analysis, editing, fileio, geometry as g
from cad_app.model import Document, Entity, Layer, demo_document


def test_exact_bulge_metrics_and_mirror():
    # Two positive semicircles form a radius-5 circle; both metrics must be exact.
    e = Entity("polyline", [(-5, 0), (5, 0)], closed=True, data={"bulges": [1, 1]})
    assert e.properties()["面积"] == pytest.approx(math.pi * 25)
    assert e.properties()["周长"] == pytest.approx(math.tau * 5)
    e.transform(mirror_axis=((0, 0), (0, 1)))
    assert e.data["bulges"] == [-1, -1]
    assert e.properties()["面积"] == pytest.approx(math.pi * 25)
    pieces = editing.explode(e)
    assert len(pieces) == 2
    assert all(p.radius == pytest.approx(5) for p in pieces)


def test_rotated_rectangle_geometry_and_history():
    d = Document()
    e = Entity("rectangle", [(0, 0), (20, 0), (20, 10), (0, 10)], closed=True)
    d.add(e)
    d.mark_saved()
    d.checkpoint("旋转")
    e.transform(center=(0, 0), degrees=45)
    assert e.properties()["长"] == pytest.approx(20)
    assert e.properties()["宽"] == pytest.approx(10)
    assert e.properties()["面积"] == pytest.approx(200)
    assert d.modified
    assert d.undo() == "旋转"
    assert not d.modified
    assert d.redo() == "旋转"
    assert d.modified


def test_intersections_projection_and_gap():
    assert g.intersection((0, 0), (2, 0), (1, -1), (1, 1)) == (1, 0)
    assert g.intersection((0, 0), (2, 0), (3, -1), (3, 1)) is None
    assert g.project((3, 2), (0, 0), (1, 0)) == (1, 0)
    assert g.project((3, 2), (0, 0), (1, 0), False) == (3, 0)
    assert g.segment_gap((0, 0), (2, 0), (1, 1), (3, 1)) == pytest.approx(1)


def test_exact_arc_and_ellipse_bounds():
    arc = Entity("arc", [(0, 0)], radius=10, data={"start": 13, "sweep": 140})
    assert arc.bbox()[3] == pytest.approx(10)
    ellipse = Entity("ellipse", [(0, 0), (3, 4)], closed=True, data={"ratio": 0.5})
    assert ellipse.bbox() == pytest.approx((-math.sqrt(13), -math.sqrt(18.25), math.sqrt(13), math.sqrt(18.25)))


def test_offset_join_and_trim_extend():
    rect = Entity("rectangle", [(0, 0), (10, 0), (10, 6), (0, 6)], closed=True)
    outer = editing.offset(rect, 1)
    assert outer.properties()["面积"] == pytest.approx(96)
    with pytest.raises(ValueError, match="坍缩"):
        editing.offset(rect, -4)
    joined = editing.join([Entity("line", [(10, 0), (10, 6)]), Entity("line", [(0, 0), (10, 0)]),
                           Entity("line", [(0, 6), (0, 0)]), Entity("line", [(10, 6), (0, 6)])])
    assert joined.closed
    assert joined.properties()["面积"] == pytest.approx(60)
    a = Entity("line", [(0, 0), (10, 0)])
    b = Entity("line", [(5, -5), (5, 5)])
    assert editing.trim_extend(a, b, 1, False) == [(0, 0), (5, 0)]
    c = Entity("line", [(15, -5), (15, 5)])
    assert editing.trim_extend(a, c, 1, True) == [(0, 0), (15, 0)]
    with pytest.raises(ValueError, match="靠近"):
        editing.trim_extend(a, c, 0, True)


def test_clearance_tangent_nested_and_board_edge():
    a = Entity("circle", [(0, 0)], radius=1)
    b = Entity("circle", [(3, 0)], radius=1)
    assert analysis.clearance(a, b) == pytest.approx(1)
    b.points = [(2, 0)]
    assert analysis.clearance(a, b) == pytest.approx(0)
    b.radius, b.points = 4, [(0, 0)]
    assert analysis.clearance(a, b) == 0
    assert analysis.boundary_gap(a, b) == pytest.approx(3)
    line = Entity("line", [(-10, 1), (10, 1)])
    assert analysis.clearance(a, line) == pytest.approx(0)
    board = Entity("rectangle", [(0, 0), (80, 0), (80, 50), (0, 50)], closed=True)
    hole = Entity("circle", [(5, 5)], radius=1.6)
    assert analysis.boundary_gap(board, hole) == pytest.approx(3.4)
    assert not analysis.edge_checks(board, [hole], 3)
    assert analysis.edge_checks(board, [hole], 4)
    enclosing = Entity("circle", [(40, 25)], radius=100)
    assert analysis.edge_checks(board, [enclosing], 3)[0].description == "板框外对象"


def test_analysis_scopes_and_duplicates():
    d = demo_document()
    drill = [e for e in d.entities if e.layer == "DRILL"]
    stats = analysis.hole_statistics(drill)
    assert stats[0].value == pytest.approx(3.2)
    assert len(stats[0].entity_ids) == 4
    pads = [e for e in d.entities if e.layer == "PAD"]
    assert not analysis.clearance_checks(pads, 1)
    assert analysis.pad_checks(pads, 2.1, 3)
    board = d.entities[0]
    reverse = Entity("polyline", board.points[::-1], closed=True)
    assert len(analysis.duplicate_checks([board, reverse])) == 1
    reverse.data["bulges"] = [1, 0, 0, 0]
    assert not analysis.duplicate_checks([board, reverse])


def test_layers_lock_and_dirty_state():
    d = Document()
    d.layers["LOCKED"] = Layer("LOCKED", locked=True)
    d.mark_saved()
    d.current_layer = "LOCKED"
    assert not d.modified
    with pytest.raises(ValueError, match="锁定"):
        d.add(Entity("line", [(0, 0), (1, 0)], "LOCKED"))
    e = Entity("line", [(0, 0), (1, 0)], "LOCKED")
    d.entities.append(e)
    assert d.editable({e.id}) == []


def test_dxf_roundtrip_with_dimension_and_audit(tmp_path):
    d = demo_document()
    d.entities.extend([
        Entity("arc", [(10, 10)], radius=3, data={"start": 30, "sweep": 240}),
        Entity("ellipse", [(15, 15), (5, 2)], closed=True, data={"ratio": 0.4}),
        Entity("polyline", [(0, 0), (10, 0)], closed=True, data={"bulges": [1, 1]}),
    ])
    path = tmp_path / "roundtrip.dxf"
    fileio.save_document(d, str(path))
    assert not d.modified
    raw = ezdxf.readfile(path)
    assert not raw.audit().has_errors
    assert len(raw.modelspace().query("DIMENSION")) == 1
    loaded = fileio.read_dxf(path)
    assert loaded.unit == "mm"
    circles = [e for e in loaded.entities if e.kind == "circle"]
    assert len(circles) == 4
    assert all(e.radius == pytest.approx(1.6) for e in circles)
    curved = [e for e in loaded.entities if e.kind == "polyline" and e.data.get("bulges") == [1, 1]][0]
    assert curved.properties()["面积"] == pytest.approx(25 * math.pi)
    second = tmp_path / "second.dxf"
    fileio.write_dxf(loaded, second)
    raw2 = ezdxf.readfile(second)
    assert len(raw2.modelspace()) == len(raw.modelspace())
    assert not raw2.audit().has_errors


def test_preserve_unsupported_layouts_blocks_styles_and_xdata(tmp_path):
    source = ezdxf.new("R2010")
    source.layers.new("KEEP")
    source.layers.new("EMPTY")
    source.appids.new("ENGINEERING")
    line = source.modelspace().add_line((0, 0), (10, 0), dxfattribs={"layer": "KEEP", "linetype": "DASHED", "lineweight": 50})
    line.set_xdata("ENGINEERING", [(1000, "original-data")])
    line_handle = line.dxf.handle
    hatch = source.modelspace().add_hatch(color=3, dxfattribs={"layer": "KEEP"})
    hatch.paths.add_polyline_path([(0, 0), (10, 0), (10, 5), (0, 5)], is_closed=True)
    block = source.blocks.new("COMPONENT")
    block.add_circle((0, 0), 2)
    source.modelspace().add_blockref("COMPONENT", (20, 10))
    source.layouts.get("Layout1").add_text("PAPER SPACE", dxfattribs={"height": 2})
    path = tmp_path / "source.dxf"
    source.saveas(path)
    d = fileio.read_dxf(path)
    assert d.warnings
    assert any(not e.editable for e in d.entities)
    assert fileio.source_layer_in_use(d, "KEEP")
    assert not fileio.source_layer_in_use(d, "EMPTY")
    del d.layers["EMPTY"]
    loaded_line = next(e for e in d.entities if e.kind == "line")
    loaded_line.transform(offset=(2, 3))
    target = tmp_path / "preserved.dxf"
    fileio.write_dxf(d, target)
    result = ezdxf.readfile(target)
    assert len(result.modelspace().query("HATCH")) == 1
    assert len(result.modelspace().query("INSERT")) == 1
    assert len(result.layouts.get("Layout1").query("TEXT")) == 1
    assert "EMPTY" not in result.layers
    moved = result.entitydb[line_handle]
    assert tuple(moved.dxf.start) == (2, 3, 0)
    assert moved.dxf.linetype == "DASHED"
    assert moved.dxf.lineweight == 50
    assert moved.get_xdata("ENGINEERING")[0].value == "original-data"
    assert not result.audit().has_errors


def test_save_failure_leaves_original_file_intact(tmp_path, monkeypatch):
    path = tmp_path / "existing.dxf"
    fileio.write_dxf(demo_document(), path)
    original = path.read_bytes()
    def fail(*args, **kwargs):
        raise OSError("simulated write failure")
    monkeypatch.setattr(ezdxf.document.Drawing, "saveas", fail)
    with pytest.raises(OSError):
        fileio.write_dxf(demo_document(), path)
    assert path.read_bytes() == original
    assert list(tmp_path.iterdir()) == [path]


def test_dwg_requires_configured_converter(tmp_path):
    with pytest.raises(ValueError, match="ODA"):
        fileio.convert_file(tmp_path / "input.dwg", tmp_path / "out.dxf", "")
