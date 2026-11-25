"""Audit supplied reference and generated fixed SET without changing either source."""
import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

import ezdxf
from ezdxf import bbox


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("generated", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("--cleaned", type=Path)
    args = parser.parse_args()
    reference = args.reference.resolve()
    digest = hashlib.sha256(reference.read_bytes()).hexdigest()
    original = ezdxf.readfile(reference)
    baseline_audit=ezdxf.readfile(reference).audit()
    assert not baseline_audit.errors
    inherited_fixes={(int(item.code), item.message) for item in baseline_audit.fixes}
    model = original.modelspace()
    pcs = [e for e in model.query("INSERT") if e.dxf.name == "12331"]
    assert len(pcs) == 10
    assert all(e.dxf.xscale == 1 and e.dxf.yscale == 1 for e in pcs)
    assert sorted(round(e.dxf.rotation) for e in pcs) == [90] * 5 + [270] * 5
    frames = [e for e in model.query("LWPOLYLINE") if e.closed and
              abs(bbox.extents([e]).size.x - 120) < 1e-6 and abs(bbox.extents([e]).size.y - 150) < 1e-6]
    assert len(frames) == 1
    print("Reference: 120 x 150 mm, 10 PCS, rotations 90/270, no negative scale/mirror")
    generated = ezdxf.readfile(args.generated)
    audit = generated.audit()
    assert not audit.errors and not audit.fixes
    actual_pcs = [e for e in generated.modelspace() if e.dxf.layer == "PANEL_PCS"]
    assert len(actual_pcs) == 8  # current regular array, not the reference's nested placement
    for dim in generated.modelspace().query("DIMENSION"):
        text = list(generated.blocks[dim.dxf.geometry].query("TEXT"))[0].dxf.text
        assert abs(float(text.split()[-1]) - dim.get_measurement()) < 0.000501
    set_values = [e.get_measurement() for e in generated.modelspace().query("DIMENSION") if e.dxf.layer == "PANEL_DIM_SET"]
    assert sorted(round(v, 6) for v in set_values) == [120, 150]
    print("Generated: 120 x 150 mm, 8 PCS regular fit; six dimensions match measured geometry; 0 audit errors/fixes")
    if args.cleaned:
        cleaned = ezdxf.readfile(args.cleaned)
        audit = cleaned.audit()
        assert not audit.errors and {(int(item.code),item.message) for item in audit.fixes}<=inherited_fixes
        assert not [e for e in cleaned.modelspace().query("LINE") if e.dxf.layer == "图框"]
        assert len([e for e in cleaned.modelspace().query("INSERT") if e.dxf.name == "12331"]) == 10
        assert not list(cleaned.modelspace().query('MTEXT'))
        print("White frame cleanup: engineering frame lines/text removed; 10 reference PCS instances intact; no new audit errors/fixes")
    with tempfile.TemporaryDirectory(prefix="white-delete-", dir=Path.cwd() / "build") as folder:
        output = Path(folder) / "delete-all.dxf"
        subprocess.run([str(args.probe.resolve()), str(reference), str(output), "all"], check=True)
        erased = ezdxf.readfile(output)
        audit = erased.audit()
        assert not audit.errors and {(int(item.code),item.message) for item in audit.fixes}<=inherited_fixes
        remaining = list(erased.modelspace())
        assert all(e.dxftype() == "LEADER" for e in remaining), [(e.dxftype(), e.dxf.handle) for e in remaining]
        print("Delete all: all displayed lines/text/dimensions/INSERTs removed; only unsupported non-displayed LEADER source records retained")
    assert hashlib.sha256(reference.read_bytes()).hexdigest() == digest
    print("Inherited source audit fixes (not introduced by cleanup):",sorted(inherited_fixes))
    print("PASS: reference source hash unchanged")


if __name__ == "__main__":
    main()
