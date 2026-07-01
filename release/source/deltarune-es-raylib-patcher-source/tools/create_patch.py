#!/usr/bin/env python3
import argparse
import collections
import difflib
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from datetime import date
from pathlib import Path


STEAM_DATA_WIN = Path.home() / ".local/share/Steam/steamapps/common/DELTARUNE/chapter5_windows/data.win"
TRANSLATION_REL = Path("translation_export_ch5/localized_text.humanfix.ascii.json")
UMT_IMPORT_REL = Path("tools/translation-kit/ImportTranslationKit.csx")
CRITICAL_PATTERNS = ("DELTARUNE", ".ogg", "gml_", "scr_", "mus_", "snd_", "room_", "obj_")


def u32(data: bytes, pos: int) -> int:
    return struct.unpack_from("<I", data, pos)[0]


def write_u32(buf: bytearray, pos: int, value: int) -> None:
    struct.pack_into("<I", buf, pos, value)


def sha256_path(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(str(part) for part in cmd))
    subprocess.run([str(part) for part in cmd], cwd=cwd, env=env, check=True)


def c_ident(value: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not ident or ident[0].isdigit():
        ident = f"b{ident}"
    return ident


def find_chunk(data: bytes, name: str) -> tuple[int, int]:
    if len(data) < 16 or data[:4] != b"FORM":
        raise SystemExit("data.win no parece un FORM valido")
    pos = 8
    target = name.encode("ascii")
    while pos + 8 <= len(data):
        size = u32(data, pos + 4)
        end = pos + 8 + size
        if end < pos or end > len(data):
            raise SystemExit(f"chunk invalido en 0x{pos:x}")
        if data[pos : pos + 4] == target:
            return pos, size
        pos = end
    raise SystemExit(f"no se encontro chunk {name}")


def read_strings(path: Path) -> tuple[bytes, int, int, int, list[str]]:
    data = path.read_bytes()
    strg_off, strg_size = find_chunk(data, "STRG")
    count = u32(data, strg_off + 8)
    strings: list[str] = []
    for index in range(count):
        entry = u32(data, strg_off + 12 + index * 4)
        length = u32(data, entry)
        raw = data[entry + 4 : entry + 4 + length]
        strings.append(raw.decode("utf-8", "replace"))
    return data, strg_off, strg_size, count, strings


def resolve_work_root(patcher_root: Path, explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get("DELTARUNE_WORK_ROOT"):
        candidates.append(Path(os.environ["DELTARUNE_WORK_ROOT"]))
    candidates.extend([
        patcher_root.parent,
        Path.home() / "Desktop/undertalemodtool",
    ])
    for candidate in candidates:
        candidate = candidate.expanduser().resolve()
        if (candidate / "UndertaleModCli").exists() and (candidate / "data.win.original").exists():
            return candidate
    raise SystemExit("No encuentro el entorno de UndertaleModTool. Usa --work-root o DELTARUNE_WORK_ROOT.")


def map_translation(base_path: Path, steam_path: Path, translation_json: Path, out_path: Path) -> dict:
    _old_data, old_off, old_size, old_count, old_strings = read_strings(base_path)
    _new_data, new_off, new_size, new_count, new_strings = read_strings(steam_path)
    matcher = difflib.SequenceMatcher(None, old_strings, new_strings, autojunk=False)
    index_map: dict[int, int] = {}
    for tag, old_start, old_end, new_start, _new_end in matcher.get_opcodes():
        if tag == "equal":
            for delta in range(old_end - old_start):
                index_map[old_start + delta] = new_start + delta

    new_positions: dict[str, list[int]] = collections.defaultdict(list)
    for index, text in enumerate(new_strings):
        new_positions[text].append(index)

    rows = json.loads(translation_json.read_text(encoding="utf-8"))
    mapped: list[dict] = []
    stats: collections.Counter[str] = collections.Counter()
    unmapped: list[tuple[int, str]] = []
    for row in rows:
        translation = row.get("translation")
        if translation is None or translation == "":
            stats["empty_skipped"] += 1
            continue
        old_index = int(row["index"])
        source = row.get("source", "")
        new_index = index_map.get(old_index)
        method = "equal"
        if new_index is None:
            positions = new_positions.get(source, [])
            if len(positions) == 1:
                new_index = positions[0]
                method = "unique_source"
            elif positions:
                new_index = min(positions, key=lambda pos: abs(pos - old_index))
                method = "nearest_duplicate_source"
            else:
                stats["unmapped"] += 1
                unmapped.append((old_index, source[:100]))
                continue

        if new_index >= len(new_strings) or new_strings[new_index] != source:
            stats["source_mismatch_new_skipped"] += 1
            seen = new_strings[new_index][:80] if new_index < len(new_strings) else "OOB"
            unmapped.append((old_index, f"{seen!r} != {source[:80]!r}"))
            continue

        mapped_row = dict(row)
        mapped_row["old_index"] = old_index
        mapped_row["index"] = new_index
        mapped_row["map_method"] = method
        mapped.append(mapped_row)
        stats[method] += 1

    duplicates = [idx for idx, count in collections.Counter(int(row["index"]) for row in mapped).items() if count > 1]
    critical_hits = []
    for row in mapped:
        source = new_strings[int(row["index"])]
        translation = row.get("translation", "")
        if source != translation and any(pattern in source for pattern in CRITICAL_PATTERNS):
            critical_hits.append((row["index"], source[:160], translation[:160]))
    if critical_hits:
        for hit in critical_hits[:20]:
            print("CRITICAL", hit)
        raise SystemExit(f"Abortado: {len(critical_hits)} strings criticos cambiarian")

    out_path.write_text(json.dumps(mapped, ensure_ascii=False, indent=2), encoding="utf-8")
    return {
        "old_layout": (old_off, old_size, old_count),
        "new_layout": (new_off, new_size, new_count),
        "rows_in": len(rows),
        "rows_out": len(mapped),
        "stats": dict(stats),
        "duplicates": len(duplicates),
        "unmapped_sample": unmapped[:10],
        "critical_hits": len(critical_hits),
    }


def write_string_table(mapped_json: Path, out_path: Path) -> int:
    rows = json.loads(mapped_json.read_text(encoding="utf-8"))
    translations: dict[int, bytes] = {}
    for row in rows:
        translation = row.get("translation")
        if translation is None or translation == "":
            continue
        translations[int(row["index"])] = translation.encode("utf-8")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as out:
        out.write(b"DRSP")
        out.write(struct.pack("<II", 1, len(translations)))
        for index in sorted(translations):
            text = translations[index]
            out.write(struct.pack("<II", index, len(text)))
            out.write(text)
    return len(translations)


def build_direct_patch(steam_path: Path, string_table: Path, out_path: Path) -> tuple[int, int, int, int]:
    file = steam_path.read_bytes()
    strg_off, old_strg_size = find_chunk(file, "STRG")
    string_count = u32(file, strg_off + 8)

    table = string_table.read_bytes()
    if len(table) < 12 or table[:4] != b"DRSP" or u32(table, 4) != 1:
        raise SystemExit("tabla de strings invalida")
    replacements: dict[int, bytes] = {}
    pos = 12
    for _ in range(u32(table, 8)):
        index = u32(table, pos)
        length = u32(table, pos + 4)
        pos += 8
        replacements[index] = table[pos : pos + length]
        pos += length

    table_len = 4 + string_count * 4
    new_content_len = table_len
    old_entries: list[int] = []
    for index in range(string_count):
        entry = u32(file, strg_off + 12 + index * 4)
        length = len(replacements[index]) if index in replacements else u32(file, entry)
        old_entries.append(entry)
        new_content_len += (4 + length + 1 + 3) & ~3
    while (strg_off + 8 + new_content_len) % 128 != 0:
        new_content_len += 1

    old_total = 8 + old_strg_size
    new_total = 8 + new_content_len
    old_tail = strg_off + old_total
    new_tail = strg_off + new_total
    new_len = len(file) - old_total + new_total
    patched = bytearray(new_len)
    patched[:strg_off] = file[:strg_off]
    patched[strg_off : strg_off + 4] = b"STRG"
    write_u32(patched, strg_off + 4, new_content_len)
    content_off = strg_off + 8
    write_u32(patched, content_off, string_count)

    cursor = table_len
    for index, entry in enumerate(old_entries):
        if index in replacements:
            text = replacements[index]
            length = len(text)
        else:
            length = u32(file, entry)
            text = file[entry + 4 : entry + 4 + length]
        new_entry = strg_off + 8 + cursor
        write_u32(patched, content_off + 4 + index * 4, new_entry)
        write_u32(patched, content_off + cursor, length)
        patched[content_off + cursor + 4 : content_off + cursor + 4 + length] = text
        patched[content_off + cursor + 4 + length] = 0
        cursor += (4 + length + 1 + 3) & ~3

    patched[new_tail:] = file[old_tail:]
    write_u32(patched, 4, new_len - 8)
    out_path.write_bytes(patched)
    return strg_off, old_strg_size, string_count, new_content_len


def write_dword_table(base_path: Path, target_path: Path, out_path: Path) -> int:
    base = base_path.read_bytes()
    target = target_path.read_bytes()
    if len(base) != len(target):
        raise SystemExit(f"size mismatch direct={len(base)} target={len(target)}")

    changes: list[tuple[int, int, int]] = []
    for pos in range(8, len(base) - 3, 4):
        old = u32(base, pos)
        new = u32(target, pos)
        if old != new:
            changes.append((pos, old, new))

    patched = bytearray(base)
    for pos, _old, new in changes:
        write_u32(patched, pos, new)
    if patched != target:
        for pos, (a, b) in enumerate(zip(patched, target)):
            if a != b:
                raise SystemExit(f"queda diff no-dword en 0x{pos:x}: {a:02x}!={b:02x}")
        raise SystemExit("queda diff no-dword al final")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as out:
        out.write(b"DRDP")
        out.write(struct.pack("<II", 1, len(changes)))
        for patch in changes:
            out.write(struct.pack("<III", *patch))
    return len(changes)


def load_manifest(root: Path) -> dict:
    path = root / "payload/profiles.json"
    if not path.exists():
        return {"profiles": []}
    return json.loads(path.read_text(encoding="utf-8"))


def save_manifest(root: Path, manifest: dict) -> None:
    path = root / "payload/profiles.json"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def update_manifest(root: Path, sha: str, layout: tuple[int, int, int], profile_name: str | None, profile_id: str | None) -> tuple[dict, bool]:
    manifest = load_manifest(root)
    profiles = manifest.setdefault("profiles", [])
    existing = next((profile for profile in profiles if profile.get("input_sha256") == sha), None)
    if existing:
        return existing, False

    today_name = profile_name or date.today().isoformat()
    used_names = {profile.get("name") for profile in profiles}
    name = today_name if today_name not in used_names else f"{today_name}-{sha[:8]}"
    raw_id = profile_id or f"b{name.replace('-', '')}"
    ident = c_ident(raw_id)
    used_ids = {profile.get("id") for profile in profiles}
    if ident in used_ids:
        ident = c_ident(f"{raw_id}_{sha[:8]}")

    strings = f"payload/tables/ch5_es_strings_{ident}.bin"
    dwords = f"payload/tables/ch5_es_dword_patches_{ident}.bin"
    strg_off, strg_size, string_count = layout
    profile = {
        "id": ident,
        "name": name,
        "input_sha256": sha,
        "strg_offset": strg_off,
        "string_count": string_count,
        "strg_size": strg_size,
        "strings": strings,
        "dwords": dwords,
    }
    profiles.append(profile)
    return profile, True


def copy_latest_aliases(root: Path, profile: dict) -> None:
    shutil.copy2(root / profile["strings"], root / "payload/tables/ch5_es_strings.bin")
    shutil.copy2(root / profile["dwords"], root / "payload/tables/ch5_es_dword_patches.bin")


def zip_dir(zip_path: Path, source_dir: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(source_dir.rglob("*")):
            arcname = path.relative_to(source_dir.parent)
            if path.is_dir():
                zf.write(path, arcname.as_posix() + "/")
            else:
                info = zipfile.ZipInfo.from_file(path, arcname.as_posix())
                if os.access(path, os.X_OK):
                    info.external_attr = (0o100755 & 0xFFFF) << 16
                with path.open("rb") as f:
                    zf.writestr(info, f.read(), compress_type=zipfile.ZIP_DEFLATED)


def make_final_folders(root: Path) -> None:
    for folder in ("final_linux", "final_windows"):
        shutil.rmtree(root / folder, ignore_errors=True)
        (root / folder).mkdir(parents=True, exist_ok=True)
    for archive in ("final_linux.zip", "final_windows.zip"):
        (root / archive).unlink(missing_ok=True)

    shutil.copy2(root / "build/linux/deltarune-es-patcher", root / "final_linux/deltarune-es-patcher")
    shutil.copy2(root / "FINAL_README.txt", root / "final_linux/README.txt")
    os.chmod(root / "final_linux/deltarune-es-patcher", 0o755)
    shutil.copy2(root / "build/windows/deltarune-es-patcher.exe", root / "final_windows/deltarune-es-patcher.exe")
    shutil.copy2(root / "FINAL_README.txt", root / "final_windows/README.txt")
    zip_dir(root / "final_linux.zip", root / "final_linux")
    zip_dir(root / "final_windows.zip", root / "final_windows")


def cleanup_generated(root: Path) -> None:
    shutil.rmtree(root / "build/create_patch", ignore_errors=True)
    for pattern in ("patcher.log", "*.log", "*.ppdb"):
        for path in root.rglob(pattern):
            if ".git" not in path.parts:
                path.unlink(missing_ok=True)
    (root / "src/embedded_assets.c").unlink(missing_ok=True)


def audit(root: Path) -> None:
    suspicious_names = {
        "data.win",
        "underanalyzer.dll",
        "raylib.dll",
    }
    suspicious_suffixes = (".win", ".unx", ".ios", ".droid", ".ppdb", ".log")
    bad: list[Path] = []
    for path in root.rglob("*"):
        if ".git" in path.parts or "third_party" in path.parts or not path.is_file():
            continue
        lower = path.name.lower()
        if lower in suspicious_names or lower.startswith("undertalemodcli") or lower.startswith("undertalemodlib"):
            bad.append(path)
        elif lower.endswith(suspicious_suffixes):
            bad.append(path)
        elif path.stat().st_size > 90 * 1024 * 1024:
            bad.append(path)
    if bad:
        for path in bad:
            print(f"SOSPECHOSO: {path} {path.stat().st_size} bytes")
        raise SystemExit("Auditoria fallida")

    for archive in (
        root / "final_linux.zip",
        root / "final_windows.zip",
        root / "release/deltarune-es-raylib-patcher-linux.zip",
        root / "release/deltarune-es-raylib-patcher-source.zip",
    ):
        with zipfile.ZipFile(archive) as zf:
            for name in zf.namelist():
                lower = Path(name).name.lower()
                if lower == "embedded_assets.c" or lower in suspicious_names:
                    raise SystemExit(f"Archivo sospechoso dentro de {archive}: {name}")
                if lower.startswith("undertalemodcli") or lower.startswith("undertalemodlib"):
                    raise SystemExit(f"Archivo sospechoso dentro de {archive}: {name}")
                if lower.endswith(suspicious_suffixes):
                    raise SystemExit(f"Archivo sospechoso dentro de {archive}: {name}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a new DELTARUNE Chapter 5 ES patcher profile from the current Steam data.win.")
    parser.add_argument("--patcher-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--steam-data-win", type=Path, default=Path(os.environ.get("STEAM_DATA_WIN", STEAM_DATA_WIN)))
    parser.add_argument("--base-data-win", type=Path)
    parser.add_argument("--translation-json", type=Path)
    parser.add_argument("--umt-cli", type=Path)
    parser.add_argument("--umt-import-script", type=Path)
    parser.add_argument("--profile-name")
    parser.add_argument("--profile-id")
    parser.add_argument("--skip-windows", action="store_true")
    args = parser.parse_args()

    root = args.patcher_root.resolve()
    work_root = resolve_work_root(root, args.work_root)
    steam_data = args.steam_data_win.expanduser().resolve()
    base_data = (args.base_data_win or work_root / "data.win.original").expanduser().resolve()
    translation_json = (args.translation_json or work_root / TRANSLATION_REL).expanduser().resolve()
    umt_cli = (args.umt_cli or work_root / "UndertaleModCli").expanduser().resolve()
    import_script = (args.umt_import_script or work_root / UMT_IMPORT_REL).expanduser().resolve()

    for path, label in (
        (steam_data, "Steam data.win"),
        (base_data, "data.win original base"),
        (translation_json, "translation json"),
        (umt_cli, "UndertaleModCli"),
        (import_script, "ImportTranslationKit.csx"),
    ):
        if not path.exists():
            raise SystemExit(f"No existe {label}: {path}")

    build_dir = root / "build/create_patch"
    shutil.rmtree(build_dir, ignore_errors=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    steam_sha = sha256_path(steam_data)
    _steam_bytes, strg_off, strg_size, string_count, _steam_strings = read_strings(steam_data)
    print(f"Steam data.win: {steam_data}")
    print(f"SHA256 entrada: {steam_sha}")
    print(f"STRG: off=0x{strg_off:x} size=0x{strg_size:x} count={string_count}")

    current_manifest = load_manifest(root)
    patched_match = next((profile for profile in current_manifest.get("profiles", []) if profile.get("output_sha256") == steam_sha), None)
    input_match = next((profile for profile in current_manifest.get("profiles", []) if profile.get("input_sha256") == steam_sha), None)
    if patched_match and not input_match:
        raise SystemExit(
            "Ese data.win parece estar ya parcheado con el perfil "
            f"{patched_match.get('name')}. Para crear un perfil nuevo hace falta el data.win original "
            "de Steam: verifica archivos en Steam o pasa --steam-data-win a una copia original."
        )

    mapped_json = build_dir / "mapped_translation.json"
    mapping_stats = map_translation(base_data, steam_data, translation_json, mapped_json)
    print(f"Traducciones mapeadas: {mapping_stats['rows_out']} / {mapping_stats['rows_in']}")
    print(f"Stats: {mapping_stats['stats']} duplicados={mapping_stats['duplicates']} criticos={mapping_stats['critical_hits']}")

    target_win = build_dir / "data_safe_umt_es.win"
    umt_log = build_dir / "umt_import.log"
    env = os.environ.copy()
    env["UMT_TRANSLATION_JSON"] = str(mapped_json)
    with umt_log.open("wb") as log:
        print("+", umt_cli, "load", steam_data, "-s", import_script, "-o", target_win)
        subprocess.run(
            [str(umt_cli), "load", str(steam_data), "-s", str(import_script), "-o", str(target_win)],
            cwd=work_root,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    target_hash = sha256_path(target_win)
    print(f"SHA256 UMT salida: {target_hash}")

    profile, is_new = update_manifest(root, steam_sha, (strg_off, strg_size, string_count), args.profile_name, args.profile_id)
    profile["output_sha256"] = target_hash
    strings_out = root / profile["strings"]
    dwords_out = root / profile["dwords"]

    translation_count = write_string_table(mapped_json, strings_out)
    direct_win = build_dir / "data_direct_es.win"
    build_direct_patch(steam_data, strings_out, direct_win)
    dword_count = write_dword_table(direct_win, target_win, dwords_out)
    copy_latest_aliases(root, profile)

    # update_manifest works on an in-memory manifest; reload/update/save cleanly here.
    manifest = load_manifest(root)
    existing = next((entry for entry in manifest["profiles"] if entry["input_sha256"] == steam_sha), None)
    if existing:
        existing.update(profile)
    else:
        manifest["profiles"].append(profile)
    save_manifest(root, manifest)

    print(f"Perfil {'nuevo' if is_new else 'existente'}: {profile['name']} ({profile['id']})")
    print(f"Strings: {translation_count} -> {strings_out}")
    print(f"Dwords : {dword_count} -> {dwords_out}")

    run([sys.executable, root / "tools/embed_assets.py", "--root", root, "--headers-only"])

    build_env = os.environ.copy()
    if "RAYLIB_WIN_DIR" not in build_env:
        fallback = work_root / "raylib-patcher/third_party/raylib-5.5_win64_mingw-w64/raylib-5.5_win64_mingw-w64"
        if fallback.exists():
            build_env["RAYLIB_WIN_DIR"] = str(fallback)

    if not args.skip_windows:
        run([root / "build_windows.sh"], cwd=root, env=build_env)
    run([root / "package_linux.sh"], cwd=root, env=build_env)
    if args.skip_windows and not (root / "build/windows/deltarune-es-patcher.exe").exists():
        raise SystemExit("No hay build Windows existente y se uso --skip-windows")
    make_final_folders(root)

    with tempfile.TemporaryDirectory(prefix="deltarune_es_patch_") as tmp:
        game_dir = Path(tmp) / "game"
        game_dir.mkdir()
        shutil.copy2(steam_data, game_dir / "data.win")
        run([root / "build/linux/deltarune-es-patcher", "--apply", game_dir], cwd=root)
        patched_hash = sha256_path(game_dir / "data.win")
        if (game_dir / "data.win").read_bytes() != target_win.read_bytes():
            raise SystemExit("El patcher final no coincide byte a byte con UMT")
        print(f"Prueba final OK: {patched_hash}")

    cleanup_generated(root)
    audit(root)
    print("\nParche generado correctamente.")
    print(f"final_linux.zip : {root / 'final_linux.zip'}")
    print(f"final_windows.zip: {root / 'final_windows.zip'}")
    print("No se ha hecho commit ni push.")


if __name__ == "__main__":
    main()
