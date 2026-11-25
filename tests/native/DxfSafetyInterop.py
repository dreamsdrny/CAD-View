"""Create a valid shared DIMENSION fixture and audit native edit + recovery."""
import hashlib
import difflib
import pathlib
import subprocess
import sys
import tempfile

import ezdxf

from DimensionTextInterop import main as audit_text_only_edit


def main(probe):
    with tempfile.TemporaryDirectory(prefix="cad-safety-") as directory:
        root = pathlib.Path(directory)
        original, edited, recovered = (root / name for name in ("original.dxf", "edited.dxf", "recovered.dxf"))
        drawing = ezdxf.new("R2010")
        drawing.appids.new("KEEP")
        drawing.layers.new("DIMS")
        block = drawing.blocks.new("*D1")
        block.add_line((0, 10), (120, 10))
        block.add_solid([(0, 10), (2, 9), (2, 11), (2, 11)])
        block.add_mtext("120.000%%P0.2", dxfattribs={"insert": (60, 11), "char_height": 3, "attachment_point": 5})
        block.add_point((0, 0))  # Unsupported source geometry must still survive.
        for _ in range(2):
            dimension = drawing.modelspace().new_entity("DIMENSION", dxfattribs={
                "geometry": "*D1", "dimtype": 32, "dimstyle": "Standard", "layer": "DIMS",
                "defpoint": (120, 10, 0), "defpoint2": (0, 0, 0), "defpoint3": (120, 0, 0),
                "text_midpoint": (60, 11, 0), "text": "<>%%P0.2", "actual_measurement": 120,
            })
            dimension.set_xdata("KEEP", [(1000, "preserve dimension metadata")])
        drawing.modelspace().add_circle((5, 5), 1.6)
        drawing.modelspace().add_text("中文文字", dxfattribs={"insert": (15, 15), "height": 2})
        hatch = drawing.modelspace().add_hatch()
        hatch.paths.add_polyline_path([(0, 0), (120, 0), (120, 50), (0, 50)], is_closed=True)
        drawing.saveas(original)
        before = hashlib.sha256(original.read_bytes()).digest()
        subprocess.run([str(pathlib.Path(probe).resolve()), str(original), str(edited), str(recovered), str(root / "recovery")], check=True)
        assert hashlib.sha256(original.read_bytes()).digest() == before
        audit_text_only_edit(original, edited)
        audit_text_only_edit(original, recovered)
        if edited.read_bytes() != recovered.read_bytes():
            print("\n".join(difflib.unified_diff(edited.read_text(encoding="utf-8").splitlines(), recovered.read_text(encoding="utf-8").splitlines(), n=2)))
            raise AssertionError("Recovered DXF differs from the committed edit")
        print("PASS: native text edit + recovery independently audited; original hash unchanged")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: DxfSafetyInterop.py cad_dxf_safety_probe.exe")
    main(sys.argv[1])
