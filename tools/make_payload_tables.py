#!/usr/bin/env python3
import argparse
import hashlib
import json
import struct
from pathlib import Path


def u32(data: bytes, pos: int) -> int:
    return struct.unpack_from("<I", data, pos)[0]


def write_string_table(json_path: Path, out_path: Path) -> None:
    rows = json.loads(json_path.read_text(encoding="utf-8"))
    translations: dict[int, bytes] = {}
    for row in rows:
        index = int(row["index"])
        text = row.get("translation")
        if text is None or text == "":
            text = row.get("source", "")
        translations[index] = text.encode("utf-8")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(b"DRSP")
        f.write(struct.pack("<II", 1, len(translations)))
        for index in sorted(translations):
            blob = translations[index]
            f.write(struct.pack("<II", index, len(blob)))
            f.write(blob)


def write_dword_table(base_path: Path, target_path: Path, out_path: Path) -> None:
    base = base_path.read_bytes()
    target = target_path.read_bytes()
    if len(base) != len(target):
        raise SystemExit(f"size mismatch: {base_path}={len(base)} {target_path}={len(target)}")

    changes: list[tuple[int, int, int]] = []
    for pos in range(8, len(base) - 3, 4):
        old = u32(base, pos)
        new = u32(target, pos)
        if old != new:
            changes.append((pos, old, new))

    patched = bytearray(base)
    for pos, _old, new in changes:
        struct.pack_into("<I", patched, pos, new)
    if patched != target:
        for pos, (a, b) in enumerate(zip(patched, target)):
            if a != b:
                raise SystemExit(f"non-dword diff remains at 0x{pos:x}: {a:02x} != {b:02x}")
        raise SystemExit("non-dword diff remains after EOF")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(b"DRDP")
        f.write(struct.pack("<II", 1, len(changes)))
        for pos, old, new in changes:
            f.write(struct.pack("<III", pos, old, new))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build runtime payload tables for the raylib patcher.")
    parser.add_argument("--translation-json", default="../translation_export_ch5/localized_text.humanfix.ascii.json")
    parser.add_argument("--direct-base", default="/tmp/direct_patch3.win")
    parser.add_argument("--target-win", default="../data_es_optionsfix_ascii.win")
    parser.add_argument("--out-dir", default="payload/tables")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    repo_root = root.parent
    translation_json = (root / args.translation_json).resolve()
    if not translation_json.exists():
        translation_json = (repo_root / args.translation_json).resolve()

    direct_base = Path(args.direct_base).expanduser()
    if not direct_base.is_absolute():
        direct_base = (root / direct_base).resolve()
    target_win = Path(args.target_win).expanduser()
    if not target_win.is_absolute():
        target_win = (root / target_win).resolve()
    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir

    strings_out = out_dir / "ch5_es_strings.bin"
    dwords_out = out_dir / "ch5_es_dword_patches.bin"
    write_string_table(translation_json, strings_out)
    write_dword_table(direct_base, target_win, dwords_out)

    print(f"strings: {strings_out} ({strings_out.stat().st_size} bytes)")
    print(f"dwords : {dwords_out} ({dwords_out.stat().st_size} bytes)")
    print(f"target : {hashlib.sha256(target_win.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
