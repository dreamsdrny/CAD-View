"""Create a second-producer test DWG using ezdxf + upstream LibreDWG writer.

Developer fixture only: the shipped application does not expose this writer.
"""
from pathlib import Path
import subprocess
import sys
import ezdxf

writer = Path(sys.argv[1]).resolve()
folder = Path(sys.argv[2]).resolve()
folder.mkdir(parents=True, exist_ok=True)
source = folder / "secondary-input.dxf"
target = folder / "secondary-r2000.dwg"
doc = ezdxf.new("R2000")
model = doc.modelspace()
model.add_line((0, 0), (25, 10))
model.add_circle((10, 10), 3)
model.add_lwpolyline([(0, 0), (25, 0), (25, 20), (0, 20)], close=True)
doc.saveas(source)
run = subprocess.run([str(writer), "-y", "-a", "r2000", "-o", str(target), str(source)],
                     capture_output=True, timeout=120)
(folder / "producer-log.txt").write_bytes(run.stdout + run.stderr)
if run.returncode != 0 or not target.is_file() or target.read_bytes()[:6] != b"AC1015":
    raise SystemExit("Secondary-producer DWG generation failed; see producer-log.txt")
print(target)
