"""Exercise real multi-version DWGs through the deployed GUI open pipeline.

Uses upstream LibreDWG fixtures included in its corresponding source archive.
Does not imply every producer, entity, layout or proxy class has been verified.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    exe = Path(sys.argv[1]).resolve()
    samples = Path(sys.argv[2]).resolve()
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = [samples / "r11" / "entities-2d.dwg"]
    paths += [samples / f"example_{version}.dwg" for version in
              ("r13", "r14", "2000", "2004", "2007", "2010", "2013", "2018")]
    paths += [Path(p).resolve() for p in sys.argv[4:]]
    results = []
    env = os.environ.copy()
    env.pop("CAD_LIBREDWG_READER", None)
    env["CAD_ODA_CONVERTER"] = ""
    env["PATH"] = str(Path(os.environ.get("SystemRoot", os.environ.get("SYSTEMROOT", r"C:\Windows"))) / "System32")
    for index, path in enumerate(paths):
        report = output / f"open-{index:02}.json"
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        run = subprocess.run([str(exe), "--check-open", str(path), str(report)],
                             env=env, timeout=160)
        result = json.loads(report.read_text(encoding="utf-8"))
        result["exitCode"] = run.returncode
        result["sourceUnchanged"] = before == hashlib.sha256(path.read_bytes()).hexdigest()
        result["header"] = path.read_bytes()[:6].decode("ascii")
        result["inputSha256"] = before
        # The upstream "2d" AC1009 fixture has nonzero Z throughout; our 2D
        # renderer deliberately does not flatten it. Record, don't hide, this limit.
        result["displayStatus"] = "displayable" if result["entities"] else "not-rendered"
        result["expectedNo2dDisplay"] = index == 0
        results.append(result)
        print(f"{result['header']} {path.name}: exit={run.returncode}, "
              f"entities={result['entities']}, actual={result['loadedPath']}", flush=True)
    (output / "matrix.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    failures = [r for r in results if r["exitCode"] != 0 or not r["success"] or
                not r["sourceUnchanged"] or (r["entities"] == 0 and not r["expectedNo2dDisplay"]) or
                Path(r["loadedPath"]).resolve() != Path(r["requestedPath"]).resolve() or
                not any("LibreDWG" in w for w in r["warnings"])]
    if failures:
        raise SystemExit(f"Failed {len(failures)}/{len(results)} DWG opens; see matrix.json")
    rendered = sum(r["entities"] > 0 for r in results)
    print(f"PASS: {len(results)} real DWGs parsed directly, {rendered} have displayable "
          "2D entities; original hashes unchanged. See explicit not-rendered results.")


if __name__ == "__main__":
    main()
