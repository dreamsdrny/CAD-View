"""Independent material-layout and CSV arithmetic verification (development only).
Usage: python -B tests/native/FpcMaterialInterop.py <base>
Requires ezdxf; actual application is native C++ without Python runtime.
"""
import argparse
import csv
import math
from pathlib import Path
import ezdxf
from ezdxf import bbox


def check_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as file:
        rows = list(csv.DictReader(file))
    assert rows
    for row in rows:
        width = float(row["大料宽mm"])
        height = float(row["大料高mm"])
        sw, sh = float(row["SET宽mm"]), float(row["SET高mm"])
        units = int(row["PCS每SET"])
        sets = int(row["SET每Panel"])
        pieces = int(row["PCS每Panel"])
        assert sets * units == pieces
        expected = 100 * sets * sw * sh / (width * height)
        assert abs(expected - float(row["SET面积占大料%"])) < 0.000051
        panels = int(row["所需大料张数"])
        order = int(row["订单PCS"])
        rate = float(row["预计良率%"]) / 100
        shipment = (math.floor(panels * pieces * rate + 1e-9) if row["出货规则"] == "按PCS预计良率" else math.floor(panels * sets * rate + 1e-9) * units)
        assert shipment == int(row["预计出货PCS"])
        assert shipment >= order
        if panels:
            prior = (math.floor((panels - 1) * pieces * rate + 1e-9) if row["出货规则"] == "按PCS预计良率" else math.floor((panels - 1) * sets * rate + 1e-9) * units)
            assert prior < order
        assert int(row["预计超交PCS"]) == shipment - order
    print(path.name, f"PASS: {len(rows)} rows; SET counts, utilization, minimum required panels and shipment rounding match")
    return rows


def check_dxf(path, row):
    document = ezdxf.readfile(path)
    audit = document.audit()
    assert not audit.errors and not audit.fixes
    model = document.modelspace()
    boxes = [bbox.extents([e]) for e in model if e.dxf.layer == "FPC_SET"]
    work = bbox.extents([e for e in model if e.dxf.layer == "FPC_WORK"])
    gx=float(row["SET在材料上X间距mm"]);gy=float(row["SET在材料上Y间距mm"])
    assert len(boxes) == int(row["SET每Panel"])
    for i, a in enumerate(boxes):
        assert a.extmin.x >= work.extmin.x - 1e-6 and a.extmax.x <= work.extmax.x + 1e-6
        assert a.extmin.y >= work.extmin.y - 1e-6 and a.extmax.y <= work.extmax.y + 1e-6
        for b in boxes[:i]:
            assert (a.extmax.x+gx <= b.extmin.x + 1e-6 or b.extmax.x+gx <= a.extmin.x + 1e-6 or a.extmax.y+gy <= b.extmin.y + 1e-6 or b.extmax.y+gy <= a.extmin.y + 1e-6)
    for dimension in model.query("DIMENSION"):
        text = list(document.blocks[dimension.dxf.geometry].query("TEXT"))[0].dxf.text
        assert abs(float(text.split()[-1]) - dimension.get_measurement()) < 0.000501
    assert document.units == 4
    detailed = [e for e in model if e.dxf.layer == "FPC_PCS"]
    if detailed:
        assert len(detailed) == int(row["PCS每Panel"])
        for e in detailed:
            p=bbox.extents([e])
            assert any(p.extmin.x>=s.extmin.x-1e-6 and p.extmax.x<=s.extmax.x+1e-6 and p.extmin.y>=s.extmin.y-1e-6 and p.extmax.y<=s.extmax.y+1e-6 for s in boxes)
    print(path.name, "PASS: no audit repairs; each SET in work area, no overlaps; material dimensions match")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=Path)
    args = parser.parse_args()
    for suffix in ("", "-ui", "-E2"):
        csv_path, dxf_path = Path(str(args.base) + suffix + ".csv"), Path(str(args.base) + suffix + ".dxf")
        if csv_path.exists():
            rows = check_csv(csv_path)
            if dxf_path.exists():
                check_dxf(dxf_path, rows[0])
            if suffix=="-ui":
                detailed=Path(str(args.base)+"-detailed.dxf")
                if detailed.exists():check_dxf(detailed,rows[0])


if __name__ == "__main__":
    main()
