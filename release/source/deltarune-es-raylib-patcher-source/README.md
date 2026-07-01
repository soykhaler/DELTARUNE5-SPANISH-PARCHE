# DELTARUNE Chapter 5 ES Raylib Patcher

Patcher no oficial en C + raylib.

No incluye `data.win`, ejecutables del juego ni UndertaleModTool. El usuario debe tener una copia oficial instalada.

## Uso

1. Ejecuta `deltarune-es-patcher`.
2. Arrastra la carpeta del Capitulo 5 o su `data.win` original a la ventana.
3. Pulsa `APLICAR PARCHE`, `ENTER` o `ESPACIO`.

El programa busca `data.win`, reconoce el layout de las builds soportadas, crea `data.win.original` como backup y sustituye `data.win` por la version parcheada. Si el layout no esta soportado, aborta antes de escribir punteros para evitar romper el juego.

Tambien puede usarse desde terminal:

```bash
./deltarune-es-patcher --apply "/ruta/a/DELTARUNE/Chapter 5"
```

## Que contiene

- `src/main.c`: patcher raylib.
- `src/embedded_assets.h`: declaraciones de assets embebidos.
- `assets/logo.png`: logo mostrado en la ventana.
- `assets/music.wav`: musica en loop.
- `payload/tables/ch5_es_strings_old.bin`: textos traducidos alineados con la build del 25 de junio de 2026.
- `payload/tables/ch5_es_strings_new.bin`: textos traducidos alineados con la build del 27 de junio de 2026.
- `payload/tables/ch5_es_strings_steam.bin`: textos traducidos alineados con la build de Steam del 29 de junio de 2026.
- `payload/tables/ch5_es_strings_20260701.bin`: textos traducidos alineados con la build de Steam del 1 de julio de 2026.
- `payload/tables/ch5_es_dword_patches_old.bin`: offsets internos de la build del 25 de junio de 2026.
- `payload/tables/ch5_es_dword_patches_new.bin`: offsets internos de la build del 27 de junio de 2026.
- `payload/tables/ch5_es_dword_patches_steam.bin`: offsets internos de la build de Steam del 29 de junio de 2026.
- `payload/tables/ch5_es_dword_patches_20260701.bin`: offsets internos de la build de Steam del 1 de julio de 2026.
- `tools/embed_assets.py`: genera `src/embedded_assets.c` antes de compilar.
- `tools/make_payload_tables.py`: herramienta usada para regenerar las tablas desde el entorno de trabajo.

El patcher reconstruye el bloque de textos `STRG` directamente y aplica la tabla de punteros que corresponde al layout detectado. No llama a `.sh`, `.bat`, `.exe`, `dotnet` ni `UndertaleModToolCli` durante el parcheo.

`src/embedded_assets.c` no se versiona porque es un archivo generado muy grande. Los scripts de build lo recrean desde `assets/` y `payload/tables/`; el ejecutable final sigue llevando todo embebido y no necesita payload externo.

## Build Linux

```bash
./build_linux.sh
./package_linux.sh
```

## Build Windows

Compila en Windows con raylib instalado o usa MinGW cross-compile si tienes raylib para Windows:

```bash
./build_windows.sh
```

## Creditos

No afiliado con Toby Fox, 8-4, Fangamer ni el equipo oficial.
DELTARUNE es propiedad de Toby Fox.
Parche no oficial por soykhaler.
