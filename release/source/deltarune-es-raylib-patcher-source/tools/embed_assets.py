#!/usr/bin/env python3
import argparse
from pathlib import Path


ASSETS = [
    ("embedded_logo_png", "assets/logo.png"),
    ("embedded_music_wav", "assets/music.wav"),
    ("embedded_strings_old_bin", "payload/tables/ch5_es_strings_old.bin"),
    ("embedded_strings_new_bin", "payload/tables/ch5_es_strings_new.bin"),
    ("embedded_strings_steam_bin", "payload/tables/ch5_es_strings_steam.bin"),
    ("embedded_strings_20260701_bin", "payload/tables/ch5_es_strings_20260701.bin"),
    ("embedded_dwords_old_bin", "payload/tables/ch5_es_dword_patches_old.bin"),
    ("embedded_dwords_new_bin", "payload/tables/ch5_es_dword_patches_new.bin"),
    ("embedded_dwords_steam_bin", "payload/tables/ch5_es_dword_patches_steam.bin"),
    ("embedded_dwords_20260701_bin", "payload/tables/ch5_es_dword_patches_20260701.bin"),
]


def write_array(out, name: str, data: bytes) -> None:
    out.write(f"const uint8_t {name}[] = {{\n")
    for pos in range(0, len(data), 12):
        chunk = data[pos : pos + 12]
        out.write("    ")
        out.write(", ".join(f"0x{byte:02x}" for byte in chunk))
        out.write(",\n")
    out.write("};\n")
    out.write(f"const size_t {name}_len = sizeof({name});\n\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate embedded asset C source for the standalone patcher.")
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--out", default=None, type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    out_path = args.out.resolve() if args.out else root / "src" / "embedded_assets.c"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", encoding="ascii", newline="\n") as out:
        out.write('#include "embedded_assets.h"\n\n')
        for name, rel_path in ASSETS:
            path = root / rel_path
            if not path.exists():
                raise SystemExit(f"missing asset: {path}")
            write_array(out, name, path.read_bytes())

    print(f"Generated {out_path} ({out_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
