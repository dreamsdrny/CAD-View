"""Independent DXF source-instance deletion audit; ezdxf is development-only."""
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path
import ezdxf


def run(probe: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cad-delete-") as folder:
        root = Path(folder)
        doc = ezdxf.new("R2010")
        model = doc.modelspace()
        dim = model.add_linear_dim(base=(0, 10), p1=(0, 0), p2=(65.3, 0))
        dim.render()
        first = dim.dimension
        model.add_entity(first.copy())
        model.add_line((0, -2), (65.3, -2))
        model.add_mtext("REMOVE-ME", dxfattribs={"insert": (10, -10)})
        source = root / "source.dxf"
        doc.saveas(source)
        saved=ezdxf.readfile(source);saved_first=list(saved.modelspace().query("DIMENSION"))[0]
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        original_entities = len(model)
        for selector in ("dimension", "REMOVE-ME"):
            output = root / (selector + ".dxf")
            subprocess.run([str(probe), str(source), str(output), selector], check=True)
            reopened = ezdxf.readfile(output)
            audit = reopened.audit()
            assert not audit.errors and not audit.fixes, (audit.errors, audit.fixes)
            assert len(reopened.modelspace()) == original_entities - 1
            if selector == "dimension":
                remaining = list(reopened.modelspace().query("DIMENSION"))
                assert len(remaining) == 1
                expected_blocks={e.dxf.handle:e.dxf.geometry for e in saved.modelspace().query("DIMENSION")}
                assert remaining[0].dxf.geometry == expected_blocks[remaining[0].dxf.handle]
                assert remaining[0].get_measurement() == saved_first.get_measurement()
                assert remaining[0].dxf.geometry in reopened.blocks
            else:
                assert not list(reopened.modelspace().query("MTEXT"))
                assert len(reopened.modelspace().query("DIMENSION")) == 2
            assert hashlib.sha256(source.read_bytes()).hexdigest() == before
            print(selector, "PASS: exactly one instance deleted, shared block/other entities/source intact; 0 audit errors/fixes")


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve())
