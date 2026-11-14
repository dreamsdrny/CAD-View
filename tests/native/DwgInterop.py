"""Roundtrip supplied DWG samples with a locally installed ODA File Converter."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("application", type=pathlib.Path)
parser.add_argument("converter", type=pathlib.Path)
parser.add_argument("samples", type=pathlib.Path, nargs="+")
args = parser.parse_args()
for path in [args.application, args.converter, *args.samples]:
    if not path.is_file():
        parser.error(f"file does not exist: {path}")

with tempfile.TemporaryDirectory(prefix="cad-dwg-interop-") as directory:
    for index, source in enumerate(args.samples):
        output = pathlib.Path(directory) / f"roundtrip-{index}.dwg"
        result = subprocess.run(
            [str(args.application.resolve()), "--dwg-roundtrip-test",
             str(args.converter.resolve()), str(source.resolve()), str(output)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=420,
        )
        if result.returncode or not output.is_file() or output.stat().st_size == 0:
            raise SystemExit(f"FAILED {source}: {result.stderr or result.stdout}")
        print(f"PASS {source}: conversion, reopen, model entity count")
