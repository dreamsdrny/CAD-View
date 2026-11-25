"""Independently audit a text-only dimension edit without changing either file."""
import sys
from collections import Counter

import ezdxf
from ezdxf.lldxf.encoding import decode_dxf_unicode
from ezdxf.lldxf.tagwriter import TagCollector


def tags(entity, ignored=()):
    return [(tag.code, tag.value) for tag in TagCollector.dxftags(entity) if tag.code not in ignored]


def main(original_path, edited_path):
    original = ezdxf.readfile(original_path)
    edited = ezdxf.readfile(edited_path)
    for name, drawing in (("original", original), ("edited", edited)):
        audit = drawing.audit()
        print(f"{name}: {len(audit.errors)} audit errors, {len(audit.fixes)} fixes")
        assert not audit.errors and not audit.fixes

    assert Counter(e.dxftype() for e in original.modelspace()) == Counter(
        e.dxftype() for e in edited.modelspace()
    )
    changed = []
    for before in original.modelspace():
        after = edited.entitydb[before.dxf.handle]
        if tags(before) != tags(after):
            assert before.dxftype() == "DIMENSION"
            assert tags(before, (1, 2)) == tags(after, (1, 2))
            changed.append((before, after))
    assert len(changed) == 1, f"Expected one text-only change, got {len(changed)}"
    before, after = changed[0]
    print("Edited override:", decode_dxf_unicode(after.dxf.text))
    old_block = original.blocks[before.dxf.geometry]
    new_block = edited.blocks[after.dxf.geometry]
    assert len(old_block) == len(new_block)
    labels = 0
    for old, new in zip(old_block, new_block):
        assert old.dxftype() == new.dxftype()
        ignored = (5, 330)
        if old.dxftype() in ("MTEXT", "TEXT"):
            ignored += (1, 3)
            labels += 1
        assert tags(old, ignored) == tags(new, ignored)
    assert labels == 1
    # The shared original block and all other model entities must remain intact.
    for old, new in zip(old_block, edited.blocks[before.dxf.geometry]):
        assert tags(old) == tags(new)
    print("PASS: original block, styles, arrows, definition points and other model entities unchanged")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: DimensionTextInterop.py ORIGINAL.dxf EDITED.dxf")
    main(*sys.argv[1:])
