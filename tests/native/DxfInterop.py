"""Developer-only independent DXF validation; not a runtime dependency."""
import pathlib
import subprocess
import sys
import tempfile
import logging
import ezdxf
from ezdxf import bbox
from collections import Counter

exe = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cad-interop-") as directory:
    path = pathlib.Path(directory) / "native-demo.dxf"
    subprocess.run([str(exe), "--export-demo", str(path)], check=True)
    drawing = ezdxf.readfile(path)
    auditor = drawing.audit()
    assert not auditor.has_errors, auditor.errors
    assert not auditor.has_fixes, auditor.fixes
    assert len(drawing.modelspace()) == 26
    assert len(drawing.modelspace().query("DIMENSION")) == 1
    assert len(drawing.modelspace().query("CIRCLE")) == 4
    print("Native DXF verified independently: 26 entities, DIMENSION, no audit errors/fixes")
    source = ezdxf.new("R2010")
    source.appids.new("CUSTOM")
    frozen = source.layers.new("FROZEN", dxfattribs={"color": -3, "flags": 5, "lineweight": 35})
    original_layer = (frozen.dxf.color, frozen.dxf.flags, frozen.dxf.lineweight,
                      frozen.dxf.get("true_color"), frozen.dxf.linetype)
    arc = source.modelspace().add_arc((10, 20), 5, 15, 170)
    arc.set_xdata("CUSTOM", [(1000, "keep me")])
    text = source.modelspace().add_text("中文测试", dxfattribs={"insert": (1, 2), "height": 2})
    poly = source.modelspace().add_lwpolyline([(0, 0, 1, 2, 1), (10, 0, 3, 4, 0)], format="xyseb")
    hatch = source.modelspace().add_hatch()
    hatch.paths.add_polyline_path([(0, 0), (10, 0), (10, 10), (0, 10)], is_closed=True)
    source_path = pathlib.Path(directory) / "source.dxf"
    output_path = pathlib.Path(directory) / "edited.dxf"
    source.saveas(source_path)
    subprocess.run([str(exe), "--roundtrip-test", str(source_path), str(output_path)], check=True)
    edited = ezdxf.readfile(output_path)
    layer = edited.layers.get("FROZEN")
    assert (layer.dxf.color, layer.dxf.flags, layer.dxf.lineweight,
            layer.dxf.get("true_color"), layer.dxf.linetype) == original_layer
    auditor = edited.audit()
    assert not auditor.has_errors, auditor.errors
    assert not auditor.has_fixes, auditor.fixes
    moved_arc = edited.modelspace().query("ARC").first
    assert tuple(moved_arc.dxf.center) == (12, 23, 0)
    assert moved_arc.dxf.radius == 5
    assert moved_arc.get_xdata("CUSTOM")[0].value == "keep me"
    moved_text = edited.modelspace().query("TEXT").first
    assert ezdxf.decode_dxf_unicode(moved_text.dxf.text) == "中文测试"
    assert tuple(moved_text.dxf.insert) == (3, 5, 0)
    assert tuple(edited.modelspace().query("LWPOLYLINE").first.get_points("xyseb")[0]) == (2, 3, 1, 2, 1)
    assert len(edited.modelspace().query("HATCH")) == 1
    print("Edited ARC/TEXT/LWPOLYLINE checked with independent reader; Unicode, widths, XDATA and HATCH retained")
    # R12 uses real VERTEX sequences; unchanged INSERT instances must survive,
    # while edited instances become independent legacy geometry.
    legacy = ezdxf.new("R12")
    block = legacy.blocks.new("PAD")
    block.add_polyline2d([(0.25, 0, 0.5, 0.5, 1), (-0.25, 0, 0.5, 0.5, 1)],
                         format="xyseb", close=True)
    legacy.modelspace().add_polyline2d([(0, 0), (10, 0)])
    legacy.modelspace().add_blockref("PAD", (5, 5), dxfattribs={"xscale": 2, "yscale": 2})
    legacy.saveas(source_path)
    subprocess.run([str(exe), "--roundtrip-test", str(source_path), str(output_path)], check=True)
    edited = ezdxf.readfile(output_path)
    auditor = edited.audit()
    assert not auditor.has_errors, auditor.errors
    assert not auditor.has_fixes, auditor.fixes
    assert len(edited.modelspace().query("POLYLINE")) == 2
    assert len(edited.modelspace().query("INSERT")) == 0
    print("R12 POLYLINE and edited block instance: no audit errors/fixes")
    if len(sys.argv) > 2:
        production_path = pathlib.Path(sys.argv[2]).resolve()
        original = ezdxf.readfile(production_path)
        subprocess.run([str(exe), "--roundtrip-test", str(production_path), str(output_path)], check=True)
        messages = []
        class Capture(logging.Handler):
            def emit(self, record):
                messages.append(record.getMessage())
        capture = Capture(logging.WARNING)
        logging.getLogger("ezdxf").addHandler(capture)
        try:
            edited = ezdxf.readfile(output_path)
        finally:
            logging.getLogger("ezdxf").removeHandler(capture)
        assert not messages, messages[:3]
        def flattened(drawing):
            for entity in drawing.modelspace():
                if entity.dxftype() == "INSERT":
                    yield from entity.virtual_entities()
                else:
                    yield entity
        original_entities = list(flattened(original))
        edited_entities = list(flattened(edited))
        assert len(original_entities) == len(edited_entities)
        assert all(e.dxftype() == "POLYLINE" for e in original_entities + edited_entities)
        def signature(entity, dx=0, dy=0):
            return (entity.dxf.layer, entity.is_closed,
                    tuple((round(v.dxf.location.x + dx, 6), round(v.dxf.location.y + dy, 6),
                           round(v.dxf.get("bulge", 0), 6),
                           round(v.dxf.get("start_width", entity.dxf.get("default_start_width", 0)), 6),
                           round(v.dxf.get("end_width", entity.dxf.get("default_end_width", 0)), 6))
                          for v in entity.vertices))
        assert Counter(signature(e, 2, 3) for e in original_entities) == Counter(signature(e) for e in edited_entities)
        auditor = edited.audit()
        assert not auditor.has_errors, auditor.errors
        assert not auditor.has_fixes, auditor.fixes
        print(f"Production DXF verified: {len(edited_entities)} polylines, every vertex/bulge/width/layer retained after translation; bounds={bbox.extents(original_entities)}")
