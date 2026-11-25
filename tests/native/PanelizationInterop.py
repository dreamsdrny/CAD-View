"""Development-only independent audit of the native panel sample exports.

Run after CAD_TEST_PANEL_SOURCE/CAD_TEST_PANEL_OUTPUT generate native fixtures:
    python -B tests/native/PanelizationInterop.py build/native/E2-panel-check
ezdxf is a test dependency, not an application runtime dependency.
"""
import argparse
import math
from pathlib import Path

import ezdxf
from ezdxf import bbox


def check(filename: Path) -> None:
    doc = ezdxf.readfile(filename)
    audit = doc.audit()
    assert not audit.errors and not audit.fixes, (filename, audit.errors, audit.fixes)
    assert doc.units == 4
    model = doc.modelspace()
    measurements = {}
    for dim in model.query("DIMENSION"):
        measured = dim.get_measurement()
        labels = list(doc.blocks[dim.dxf.geometry].query("TEXT"))
        assert len(labels) == 1
        prefix, number = labels[0].dxf.text.split()
        assert abs(float(number) - measured) <= 0.00050001
        measurements.setdefault(prefix, []).append(measured)
        print(filename.name, prefix, f"{measured:.6f}", labels[0].dxf.text)
    assert set(measurements) == {"PCS", "ARRAY", "SET"}
    assert all(len(values) == 2 for values in measurements.values())
    pcs = list(model.query('*[layer=="PANEL_PCS"]'))
    if not pcs:
        pcs = list(model.query('*[layer=="PANEL_REFERENCE"]'))
    assert len(pcs) == 4
    extents = bbox.extents(pcs)
    actual = [extents.size.x, extents.size.y]
    assert all(math.isclose(a, b, abs_tol=1e-6) for a, b in zip(actual, measurements["ARRAY"]))
    assert all(math.isclose(a, b, abs_tol=0.001) for a, b in zip(measurements["PCS"], [65.3, 22.9]))
    assert any(abs(p[4]) > 1e-8 for p in pcs[0].get_points()), "Lost original PCS circular arcs"
    assert not list(model.query("INSERT")), "Unexpected implicit scale or mirror"
    print(filename.name, "PASS: no audit errors/fixes; dimensions match geometric extents")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=Path)
    args = parser.parse_args()
    for suffix in ("-design.dxf", "-fused.dxf"):
        check(Path(str(args.base) + suffix))


if __name__ == "__main__":
    main()
