#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


BASE_ASSETS = [
    ("embedded_logo_png", "assets/logo.png"),
    ("embedded_music_wav", "assets/music.wav"),
]


def c_ident(value: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not ident or ident[0].isdigit():
        ident = f"b{ident}"
    return ident


def load_profiles(root: Path) -> list[dict]:
    manifest = root / "payload" / "profiles.json"
    data = json.loads(manifest.read_text(encoding="utf-8"))
    profiles = data.get("profiles", [])
    if not profiles:
        raise SystemExit(f"no profiles in {manifest}")
    seen: set[str] = set()
    for profile in profiles:
        profile["id"] = c_ident(str(profile["id"]))
        if profile["id"] in seen:
            raise SystemExit(f"duplicate profile id: {profile['id']}")
        seen.add(profile["id"])
    return profiles


def profile_assets(profiles: list[dict]) -> list[tuple[str, str]]:
    assets = list(BASE_ASSETS)
    for profile in profiles:
        ident = profile["id"]
        assets.append((f"embedded_strings_{ident}_bin", profile["strings"]))
        assets.append((f"embedded_dwords_{ident}_bin", profile["dwords"]))
    return assets


def write_array(out, name: str, data: bytes) -> None:
    out.write(f"const uint8_t {name}[] = {{\n")
    for pos in range(0, len(data), 12):
        chunk = data[pos : pos + 12]
        out.write("    ")
        out.write(", ".join(f"0x{byte:02x}" for byte in chunk))
        out.write(",\n")
    out.write("};\n")
    out.write(f"const size_t {name}_len = sizeof({name});\n\n")


def write_assets_header(path: Path, assets: list[tuple[str, str]]) -> None:
    with path.open("w", encoding="ascii", newline="\n") as out:
        out.write("#ifndef DELTARUNE_ES_EMBEDDED_ASSETS_H\n")
        out.write("#define DELTARUNE_ES_EMBEDDED_ASSETS_H\n\n")
        out.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        for name, _rel_path in assets:
            out.write(f"extern const uint8_t {name}[];\n")
            out.write(f"extern const size_t {name}_len;\n")
        out.write("\n#endif\n")


def write_profiles_header(path: Path, profiles: list[dict]) -> None:
    with path.open("w", encoding="ascii", newline="\n") as out:
        out.write("#ifndef DELTARUNE_ES_EMBEDDED_PROFILES_H\n")
        out.write("#define DELTARUNE_ES_EMBEDDED_PROFILES_H\n\n")
        out.write(f"#define EMBEDDED_PROFILE_COUNT {len(profiles)}\n")
        out.write(f"#define KNOWN_INPUT_SHA256_COUNT {len(profiles)}\n\n")
        out.write("static const char *known_input_sha256s[KNOWN_INPUT_SHA256_COUNT] = {\n")
        for profile in profiles:
            out.write(f"    \"{profile['input_sha256']}\",\n")
        out.write("};\n\n")
        out.write("static int is_known_input_sha256(const char *hash) {\n")
        out.write("    for (size_t i = 0; i < KNOWN_INPUT_SHA256_COUNT; ++i) {\n")
        out.write("        if (strcmp(hash, known_input_sha256s[i]) == 0) return 1;\n")
        out.write("    }\n")
        out.write("    return 0;\n")
        out.write("}\n\n")
        out.write("static void init_embedded_profiles(DwordPatchProfile profiles[EMBEDDED_PROFILE_COUNT]) {\n")
        for index, profile in enumerate(profiles):
            ident = profile["id"]
            out.write(f"    profiles[{index}] = (DwordPatchProfile){{\n")
            out.write(f"        \"{profile['name']}\", 0x{int(profile['strg_offset']):08x}u, ")
            out.write(f"{int(profile['string_count'])}u, 0x{int(profile['strg_size']):08x}u,\n")
            out.write(f"        embedded_strings_{ident}_bin, embedded_strings_{ident}_bin_len,\n")
            out.write(f"        embedded_dwords_{ident}_bin, embedded_dwords_{ident}_bin_len\n")
            out.write("    };\n")
        out.write("}\n\n")
        out.write("#endif\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate embedded payload C files for the standalone patcher.")
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--out-c", default=None, type=Path)
    parser.add_argument("--headers-only", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    profiles = load_profiles(root)
    assets = profile_assets(profiles)

    src_dir = root / "src"
    src_dir.mkdir(parents=True, exist_ok=True)
    write_assets_header(src_dir / "embedded_assets.h", assets)
    write_profiles_header(src_dir / "embedded_profiles.h", profiles)

    if args.headers_only:
        print(f"Generated {src_dir / 'embedded_assets.h'}")
        print(f"Generated {src_dir / 'embedded_profiles.h'}")
        return

    out_path = args.out_c.resolve() if args.out_c else src_dir / "embedded_assets.c"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="ascii", newline="\n") as out:
        out.write('#include "embedded_assets.h"\n\n')
        for name, rel_path in assets:
            path = root / rel_path
            if not path.exists():
                raise SystemExit(f"missing asset: {path}")
            write_array(out, name, path.read_bytes())

    print(f"Generated {src_dir / 'embedded_assets.h'}")
    print(f"Generated {src_dir / 'embedded_profiles.h'}")
    print(f"Generated {out_path} ({out_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
