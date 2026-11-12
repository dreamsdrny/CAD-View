"""Developer-only independent DXF validation; not a runtime dependency."""
import pathlib
import subprocess
import sys
import tempfile
import ezdxf

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
