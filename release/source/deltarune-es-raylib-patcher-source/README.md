# DELTARUNE Chapter 5 ES Raylib Patcher

Patcher no oficial en C + raylib.

No incluye `data.win`, ejecutables del juego ni UndertaleModTool. El usuario debe tener una copia oficial instalada.

## Uso

1. Ejecuta `deltarune-es-patcher`.
2. Arrastra la carpeta del Capitulo 5 o su `data.win` original a la ventana.
3. Pulsa `APLICAR PARCHE`, `ENTER` o `ESPACIO`.

El programa busca `data.win`, comprueba que sea la version verificada, crea `data.win.original` como backup y sustituye `data.win` por la version parcheada.

Tambien puede usarse desde terminal:

```bash
./deltarune-es-patcher --apply "/ruta/a/DELTARUNE/Chapter 5"
```

## Que contiene

- `src/main.c`: patcher raylib.
- `assets/logo.png`: logo mostrado en la ventana.
- `assets/music.wav`: musica en loop.
- `payload/tables/ch5_es_strings.bin`: textos traducidos.
- `payload/tables/ch5_es_dword_patches.bin`: offsets internos necesarios para reconstruir `data.win`.
- `tools/make_payload_tables.py`: herramienta usada para regenerar las tablas desde el entorno de trabajo.

El patcher reconstruye el bloque de textos `STRG` directamente y aplica una tabla de punteros. No llama a `.sh`, `.bat`, `.exe`, `dotnet` ni `UndertaleModToolCli` durante el parcheo.

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
