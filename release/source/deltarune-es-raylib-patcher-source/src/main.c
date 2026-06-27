#define _POSIX_C_SOURCE 200809L

#include "raylib.h"
#include "embedded_assets.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#endif

#define WINDOW_W 960
#define WINDOW_H 640
#define LOG_LIMIT 32768
#define PATH_LIMIT 4096
#define MAX_SEARCH_DEPTH 5
#define STRG_ALIGN 128
#define KNOWN_INPUT_SHA256_OLD "9ed07de5e437de3b7bac0000c6374166198ec337f5ad761de9d5fed393cf326e"
#define KNOWN_INPUT_SHA256_NEW "cd940118eff1a3f24dd8dca3932901f85bfc83761e175e36736622316e7ed556"

typedef struct Sha256 {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} Sha256;

typedef struct StringPatch {
    uint32_t index;
    uint32_t len;
    uint8_t *text;
} StringPatch;

typedef struct StringPatchSet {
    uint32_t count;
    StringPatch *items;
} StringPatchSet;

typedef struct DwordPatchProfile {
    const char *name;
    size_t strg_offset;
    uint32_t string_count;
    uint32_t strg_size;
    const uint8_t *strings_blob;
    size_t strings_blob_len;
    const uint8_t *blob;
    size_t blob_len;
} DwordPatchProfile;

typedef struct PatchJob {
    volatile int running;
    volatile int done;
    volatile int success;
    char data_win[PATH_LIMIT];
    char app_dir[PATH_LIMIT];
    char log_path[PATH_LIMIT];
    char status[256];
} PatchJob;

static PatchJob g_job = {0};

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32 - count));
}

static void sha256_transform(Sha256 *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | data[j + 3];
    }
    for (; i < 64; ++i) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + s1 + ch + K[i] + m[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(Sha256 *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(Sha256 *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(Sha256 *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xff);
        hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xff);
        hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xff);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xff);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xff);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xff);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xff);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xff);
    }
}

static void sha256_buffer_hex(const uint8_t *data, size_t len, char out_hex[65]) {
    Sha256 ctx;
    uint8_t hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
    for (int i = 0; i < 32; ++i) sprintf(out_hex + i * 2, "%02x", hash[i]);
    out_hex[64] = '\0';
}

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static int is_sep(char c) {
    return c == '/' || c == '\\';
}

static const char *base_name(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (is_sep(*p)) base = p + 1;
    }
    return base;
}

static int equals_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void normalize_input_path(const char *input, char *out, size_t out_size) {
    while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') input++;

    char temp[PATH_LIMIT];
    snprintf(temp, sizeof(temp), "%s", input);
    size_t len = strlen(temp);
    while (len > 0 && (temp[len - 1] == ' ' || temp[len - 1] == '\t' || temp[len - 1] == '\r' || temp[len - 1] == '\n')) {
        temp[--len] = '\0';
    }
    if (len >= 2 && ((temp[0] == '"' && temp[len - 1] == '"') || (temp[0] == '\'' && temp[len - 1] == '\''))) {
        temp[len - 1] = '\0';
        memmove(temp, temp + 1, len - 1);
    }

    const char *src = temp;
    if (strncmp(src, "file://localhost/", 17) == 0) src += 16;
    else if (strncmp(src, "file://", 7) == 0) src += 7;

    char decoded[PATH_LIMIT];
    size_t w = 0;
    for (size_t r = 0; src[r] && w + 1 < sizeof(decoded); ++r) {
        if (src[r] == '%' && isxdigit((unsigned char)src[r + 1]) && isxdigit((unsigned char)src[r + 2])) {
            int hi = hex_digit_value(src[r + 1]);
            int lo = hex_digit_value(src[r + 2]);
            decoded[w++] = (char)((hi << 4) | lo);
            r += 2;
        } else {
            decoded[w++] = src[r];
        }
    }
    decoded[w] = '\0';

#if defined(_WIN32)
    if (decoded[0] == '/' && isalpha((unsigned char)decoded[1]) && decoded[2] == ':') {
        memmove(decoded, decoded + 1, strlen(decoded));
    }
#endif

    snprintf(out, out_size, "%s", decoded);
}

static void path_join(char *out, size_t out_size, const char *base, const char *child) {
    if (!base || !base[0]) {
        snprintf(out, out_size, "%s", child);
        return;
    }
    char last = base[strlen(base) - 1];
    const char sep =
#if defined(_WIN32)
        '\\';
#else
        '/';
#endif
    if (last == '/' || last == '\\') snprintf(out, out_size, "%s%s", base, child);
    else snprintf(out, out_size, "%s%c%s", base, sep, child);
}

static int path_with_suffix(char *out, size_t out_size, const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (path_len + suffix_len + 1 > out_size) return 0;
    memcpy(out, path, path_len);
    memcpy(out + path_len, suffix, suffix_len + 1);
    return 1;
}

static int file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode);
#endif
}

static int dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static void dirname_in_place(char *path) {
    size_t len = strlen(path);
    while (len > 0 && !is_sep(path[len - 1])) len--;
    if (len == 0) {
        strcpy(path, ".");
    } else if (len == 1) {
        path[1] = '\0';
    } else {
        path[len - 1] = '\0';
    }
}

static void get_executable_dir(char *out, size_t out_size, const char *argv0) {
#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)out_size);
    if (len == 0 || len >= out_size) snprintf(out, out_size, "%s", argv0 ? argv0 : ".");
    dirname_in_place(out);
#else
    ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
    if (len > 0) {
        out[len] = '\0';
    } else {
        snprintf(out, out_size, "%s", argv0 ? argv0 : ".");
    }
    dirname_in_place(out);
#endif
}

static int find_data_win_in_dir(const char *dir, int depth, char *out, size_t out_size) {
    char direct[PATH_LIMIT];
    path_join(direct, sizeof(direct), dir, "data.win");
    if (file_exists(direct)) {
        snprintf(out, out_size, "%s", direct);
        return 1;
    }
    if (depth <= 0) return 0;

#if defined(_WIN32)
    char pattern[PATH_LIMIT];
    path_join(pattern, sizeof(pattern), dir, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char child[PATH_LIMIT];
        path_join(child, sizeof(child), dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (find_data_win_in_dir(child, depth - 1, out, out_size)) {
                FindClose(h);
                return 1;
            }
        } else if (equals_ignore_case(fd.cFileName, "data.win") && file_exists(child)) {
            snprintf(out, out_size, "%s", child);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[PATH_LIMIT];
        path_join(child, sizeof(child), dir, entry->d_name);
        if (dir_exists(child)) {
            if (find_data_win_in_dir(child, depth - 1, out, out_size)) {
                closedir(d);
                return 1;
            }
        } else if (equals_ignore_case(entry->d_name, "data.win") && file_exists(child)) {
            snprintf(out, out_size, "%s", child);
            closedir(d);
            return 1;
        }
    }
    closedir(d);
#endif
    return 0;
}

static int resolve_data_win_path(const char *input, char *out, size_t out_size) {
    char normalized[PATH_LIMIT];
    normalize_input_path(input, normalized, sizeof(normalized));
    if (file_exists(normalized)) {
        snprintf(out, out_size, "%s", normalized);
        return 1;
    }
    if (dir_exists(normalized)) {
        return find_data_win_in_dir(normalized, MAX_SEARCH_DEPTH, out, out_size);
    }
    return 0;
}

static int read_file_alloc(const char *path, uint8_t **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    rewind(f);

    uint8_t *data = (uint8_t *)malloc((size_t)size ? (size_t)size : 1);
    if (!data) {
        fclose(f);
        return 0;
    }
    size_t got = fread(data, 1, (size_t)size, f);
    int ok = got == (size_t)size && !ferror(f);
    fclose(f);
    if (!ok) {
        free(data);
        return 0;
    }
    *out_data = data;
    *out_len = (size_t)size;
    return 1;
}

static int write_file_all(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (!ok) remove(path);
    return ok;
}

static int copy_file_binary(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }

    uint8_t buffer[65536];
    size_t got = 0;
    int ok = 1;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) remove(dst);
    return ok;
}

static void append_log(const char *path, const char *fmt, ...) {
    FILE *f = fopen(path, "ab");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

static int sha256_file_hex(const char *path, char out_hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    Sha256 ctx;
    sha256_init(&ctx);
    uint8_t buffer[65536];
    size_t got = 0;
    while ((got = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        sha256_update(&ctx, buffer, got);
    }
    int ok = !ferror(f);
    fclose(f);
    if (!ok) return 0;

    uint8_t hash[32];
    sha256_final(&ctx, hash);
    for (int i = 0; i < 32; ++i) sprintf(out_hex + i * 2, "%02x", hash[i]);
    out_hex[64] = '\0';
    return 1;
}

static void free_string_patches(StringPatchSet *set) {
    if (!set || !set->items) return;
    for (uint32_t i = 0; i < set->count; ++i) free(set->items[i].text);
    free(set->items);
    set->items = NULL;
    set->count = 0;
}

static int load_string_patches_from_memory(const uint8_t *blob, size_t len, StringPatchSet *set, char *err, size_t err_size) {
    if (len < 12 || memcmp(blob, "DRSP", 4) != 0 || read_u32le(blob + 4) != 1) {
        snprintf(err, err_size, "Tabla de textos invalida");
        return 0;
    }

    uint32_t count = read_u32le(blob + 8);
    StringPatch *items = (StringPatch *)calloc(count ? count : 1, sizeof(StringPatch));
    if (!items) {
        snprintf(err, err_size, "Sin memoria para la tabla de textos");
        return 0;
    }

    size_t pos = 12;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 8 > len) {
            free(items);
            snprintf(err, err_size, "Tabla de textos truncada");
            return 0;
        }
        uint32_t index = read_u32le(blob + pos);
        uint32_t text_len = read_u32le(blob + pos + 4);
        pos += 8;
        if (pos + text_len > len) {
            free(items);
            snprintf(err, err_size, "Texto truncado en tabla");
            return 0;
        }
        items[i].index = index;
        items[i].len = text_len;
        items[i].text = (uint8_t *)malloc(text_len ? text_len : 1);
        if (!items[i].text) {
            for (uint32_t j = 0; j < i; ++j) free(items[j].text);
            free(items);
            snprintf(err, err_size, "Sin memoria para textos");
            return 0;
        }
        if (text_len) memcpy(items[i].text, blob + pos, text_len);
        pos += text_len;
    }

    set->count = count;
    set->items = items;
    return 1;
}

static int find_chunk(const uint8_t *data, size_t len, const char name[4], size_t *chunk_offset, uint32_t *chunk_size) {
    if (len < 16 || memcmp(data, "FORM", 4) != 0) return 0;
    size_t pos = 8;
    while (pos + 8 <= len) {
        uint32_t size = read_u32le(data + pos + 4);
        size_t end = pos + 8 + (size_t)size;
        if (end < pos || end > len) return 0;
        if (memcmp(data + pos, name, 4) == 0) {
            *chunk_offset = pos;
            *chunk_size = size;
            return 1;
        }
        pos = end;
    }
    return 0;
}

static const DwordPatchProfile *find_dword_profile(size_t strg_offset, uint32_t string_count, uint32_t strg_size) {
    static const DwordPatchProfile profiles[] = {
        {"2025-06-25", 0x014e8cf0u, 93214u, 0x0041ee08u, embedded_strings_old_bin, sizeof(embedded_strings_old_bin), embedded_dwords_old_bin, sizeof(embedded_dwords_old_bin)},
        {"2026-06-27", 0x014e9330u, 93223u, 0x0041ef48u, embedded_strings_new_bin, sizeof(embedded_strings_new_bin), embedded_dwords_new_bin, sizeof(embedded_dwords_new_bin)},
    };

    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (profiles[i].strg_offset == strg_offset &&
            profiles[i].string_count == string_count &&
            profiles[i].strg_size == strg_size) {
            return &profiles[i];
        }
    }
    return NULL;
}

static int apply_dword_patches_from_memory(const DwordPatchProfile *profile,
                                           uint8_t *patched, size_t patched_len,
                                           uint32_t *applied, uint32_t *skipped,
                                           char *err, size_t err_size) {
    const uint8_t *blob = profile->blob;
    size_t blob_len = profile->blob_len;
    if (blob_len < 12 || memcmp(blob, "DRDP", 4) != 0 || read_u32le(blob + 4) != 1) {
        snprintf(err, err_size, "Tabla de punteros invalida");
        return 0;
    }
    uint32_t count = read_u32le(blob + 8);
    if (blob_len != 12u + (size_t)count * 12u) {
        snprintf(err, err_size, "Tabla de punteros truncada");
        return 0;
    }

    *applied = 0;
    *skipped = 0;

    size_t pos = 12;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t patch_pos = read_u32le(blob + pos);
        uint32_t old_value = read_u32le(blob + pos + 4);
        uint32_t new_value = read_u32le(blob + pos + 8);
        pos += 12;

        if ((size_t)patch_pos + 4 > patched_len) {
            (*skipped)++;
            continue;
        }

        uint32_t current = read_u32le(patched + patch_pos);
        if (current == old_value) {
            write_u32le(patched + patch_pos, new_value);
            (*applied)++;
        } else {
            (*skipped)++;
        }
    }

    return 1;
}

static int build_patched_data(const char *data_path,
                              uint8_t **out_data, size_t *out_len, char output_hash[65],
                              uint32_t *dwords_applied, uint32_t *dwords_skipped,
                              const char **profile_name,
                              char *err, size_t err_size) {
    uint8_t *file = NULL;
    size_t file_len = 0;
    if (!read_file_alloc(data_path, &file, &file_len)) {
        snprintf(err, err_size, "No se pudo leer data.win");
        return 0;
    }

    size_t strg_offset = 0;
    uint32_t old_strg_size = 0;
    if (!find_chunk(file, file_len, "STRG", &strg_offset, &old_strg_size)) {
        free(file);
        snprintf(err, err_size, "No se encontro el bloque STRG");
        return 0;
    }
    if (strg_offset + 12 > file_len || strg_offset + 8u + old_strg_size > file_len) {
        free(file);
        snprintf(err, err_size, "Bloque STRG invalido");
        return 0;
    }

    uint32_t string_count = read_u32le(file + strg_offset + 8);
    const DwordPatchProfile *profile = find_dword_profile(strg_offset, string_count, old_strg_size);
    if (!profile) {
        free(file);
        snprintf(err, err_size, "Build de data.win no soportada todavia; no aplico punteros a ciegas");
        return 0;
    }
    *profile_name = profile->name;

    StringPatchSet patches = {0};
    if (!load_string_patches_from_memory(profile->strings_blob, profile->strings_blob_len, &patches, err, err_size)) {
        free(file);
        return 0;
    }

    size_t table_len = 4u + (size_t)string_count * 4u;
    if (strg_offset + 8u + table_len > file_len) {
        free_string_patches(&patches);
        free(file);
        snprintf(err, err_size, "Tabla STRG fuera de rango");
        return 0;
    }

    const uint8_t **replacement = (const uint8_t **)calloc(string_count ? string_count : 1, sizeof(uint8_t *));
    uint32_t *replacement_len = (uint32_t *)calloc(string_count ? string_count : 1, sizeof(uint32_t));
    if (!replacement || !replacement_len) {
        free(replacement);
        free(replacement_len);
        free_string_patches(&patches);
        free(file);
        snprintf(err, err_size, "Sin memoria para traducciones");
        return 0;
    }
    for (uint32_t i = 0; i < patches.count; ++i) {
        if (patches.items[i].index >= string_count) {
            free(replacement);
            free(replacement_len);
            free_string_patches(&patches);
            free(file);
            snprintf(err, err_size, "Indice de texto fuera de rango: %u", patches.items[i].index);
            return 0;
        }
        replacement[patches.items[i].index] = patches.items[i].text;
        replacement_len[patches.items[i].index] = patches.items[i].len;
    }

    size_t new_content_len = table_len;
    for (uint32_t i = 0; i < string_count; ++i) {
        uint32_t entry_offset = read_u32le(file + strg_offset + 12u + (size_t)i * 4u);
        if ((size_t)entry_offset + 4 > file_len) {
            free(replacement);
            free(replacement_len);
            free_string_patches(&patches);
            free(file);
            snprintf(err, err_size, "Entrada STRG fuera de rango");
            return 0;
        }
        uint32_t len = replacement[i] ? replacement_len[i] : read_u32le(file + entry_offset);
        if (!replacement[i] && (size_t)entry_offset + 4u + len > file_len) {
            free(replacement);
            free(replacement_len);
            free_string_patches(&patches);
            free(file);
            snprintf(err, err_size, "Texto STRG fuera de rango");
            return 0;
        }
        size_t entry_size = 4u + (size_t)len + 1u;
        entry_size = (entry_size + 3u) & ~(size_t)3u;
        if (new_content_len + entry_size < new_content_len) {
            free(replacement);
            free(replacement_len);
            free_string_patches(&patches);
            free(file);
            snprintf(err, err_size, "STRG demasiado grande");
            return 0;
        }
        new_content_len += entry_size;
    }
    while (((strg_offset + 8u + new_content_len) % STRG_ALIGN) != 0) new_content_len++;
    if (new_content_len > UINT32_MAX) {
        free(replacement);
        free(replacement_len);
        free_string_patches(&patches);
        free(file);
        snprintf(err, err_size, "STRG supera 4GB");
        return 0;
    }

    size_t old_total = 8u + (size_t)old_strg_size;
    size_t new_total = 8u + new_content_len;
    size_t old_tail = strg_offset + old_total;
    size_t new_tail = strg_offset + new_total;
    size_t new_file_len = file_len - old_total + new_total;
    if (new_file_len > UINT32_MAX) {
        free(replacement);
        free(replacement_len);
        free_string_patches(&patches);
        free(file);
        snprintf(err, err_size, "data.win supera 4GB");
        return 0;
    }

    uint8_t *patched = (uint8_t *)calloc(new_file_len ? new_file_len : 1, 1);
    if (!patched) {
        free(replacement);
        free(replacement_len);
        free_string_patches(&patches);
        free(file);
        snprintf(err, err_size, "Sin memoria para data.win parcheado");
        return 0;
    }

    memcpy(patched, file, strg_offset);
    memcpy(patched + strg_offset, "STRG", 4);
    write_u32le(patched + strg_offset + 4, (uint32_t)new_content_len);
    uint8_t *content = patched + strg_offset + 8;
    write_u32le(content, string_count);

    size_t cursor = table_len;
    for (uint32_t i = 0; i < string_count; ++i) {
        uint32_t old_entry_offset = read_u32le(file + strg_offset + 12u + (size_t)i * 4u);
        uint32_t len = replacement[i] ? replacement_len[i] : read_u32le(file + old_entry_offset);
        const uint8_t *text = replacement[i] ? replacement[i] : file + old_entry_offset + 4;
        uint32_t new_entry_offset = (uint32_t)(strg_offset + 8u + cursor);

        write_u32le(content + 4u + (size_t)i * 4u, new_entry_offset);
        write_u32le(content + cursor, len);
        if (len) memcpy(content + cursor + 4u, text, len);
        content[cursor + 4u + len] = 0;
        cursor += (4u + (size_t)len + 1u + 3u) & ~(size_t)3u;
    }

    if (old_tail < file_len) memcpy(patched + new_tail, file + old_tail, file_len - old_tail);
    write_u32le(patched + 4, (uint32_t)new_file_len - 8u);

    free(replacement);
    free(replacement_len);
    free_string_patches(&patches);

    if (!apply_dword_patches_from_memory(profile, patched, new_file_len,
                                         dwords_applied, dwords_skipped, err, err_size)) {
        free(file);
        free(patched);
        return 0;
    }
    if (*dwords_skipped != 0) {
        snprintf(err, err_size, "La build coincide con %s, pero %u punteros no cuadran", profile->name, *dwords_skipped);
        free(file);
        free(patched);
        return 0;
    }

    free(file);
    sha256_buffer_hex(patched, new_file_len, output_hash);

    *out_data = patched;
    *out_len = new_file_len;
    return 1;
}

static int replace_file(const char *src, const char *dst) {
#if defined(_WIN32)
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

static void set_job_status(PatchJob *job, const char *text) {
    snprintf(job->status, sizeof(job->status), "%s", text);
}

static int patch_job_run(PatchJob *job) {
    FILE *clear = fopen(job->log_path, "wb");
    if (clear) fclose(clear);

    set_job_status(job, "Leyendo data.win...");
    append_log(job->log_path, "DELTARUNE Chapter 5 ES patcher\n");
    append_log(job->log_path, "data.win:\n%s\n", job->data_win);
    append_log(job->log_path, "payload: embebido en el ejecutable\n\n");

    if (!file_exists(job->data_win)) {
        append_log(job->log_path, "No existe el archivo seleccionado.\n");
        return 0;
    }

    char hash[65];
    hash[0] = '\0';
    if (!sha256_file_hex(job->data_win, hash)) {
        append_log(job->log_path, "No se pudo calcular SHA256; continuo igualmente.\n");
    } else {
        append_log(job->log_path, "SHA256 entrada: %s\n", hash);
        if (strcmp(hash, KNOWN_INPUT_SHA256_OLD) != 0 && strcmp(hash, KNOWN_INPUT_SHA256_NEW) != 0) {
            append_log(job->log_path, "Aviso: hash no reconocido; intentare parchear solo si el layout esta soportado.\n");
        }
    }

    char backup[PATH_LIMIT], tmp_out[PATH_LIMIT];
    if (!path_with_suffix(backup, sizeof(backup), job->data_win, ".original") ||
        !path_with_suffix(tmp_out, sizeof(tmp_out), job->data_win, ".es.tmp")) {
        append_log(job->log_path, "La ruta de data.win es demasiado larga.\n");
        return 0;
    }

    set_job_status(job, "Creando backup...");
    if (!file_exists(backup)) {
        append_log(job->log_path, "Creando backup: %s\n", backup);
        if (!copy_file_binary(job->data_win, backup)) {
            append_log(job->log_path, "No se pudo crear el backup.\n");
            return 0;
        }
    } else {
        append_log(job->log_path, "Backup existente: %s\n", backup);
    }

    set_job_status(job, "Aplicando parche...");
    append_log(job->log_path, "Reconstruyendo STRG y offsets internos...\n");
    uint8_t *patched = NULL;
    size_t patched_len = 0;
    char output_hash[65];
    uint32_t dwords_applied = 0;
    uint32_t dwords_skipped = 0;
    const char *profile_name = "";
    char err[256];
    err[0] = '\0';
    if (!build_patched_data(job->data_win, &patched, &patched_len, output_hash,
                            &dwords_applied, &dwords_skipped, &profile_name, err, sizeof(err))) {
        append_log(job->log_path, "%s\n", err[0] ? err : "No se pudo aplicar el parche.");
        remove(tmp_out);
        return 0;
    }

    remove(tmp_out);
    if (!write_file_all(tmp_out, patched, patched_len)) {
        free(patched);
        append_log(job->log_path, "No se pudo escribir el archivo temporal.\n");
        remove(tmp_out);
        return 0;
    }
    free(patched);

    set_job_status(job, "Sustituyendo data.win...");
    if (!replace_file(tmp_out, job->data_win)) {
        append_log(job->log_path, "No se pudo sustituir data.win: %s\n", strerror(errno));
        remove(tmp_out);
        return 0;
    }

    set_job_status(job, "Parche aplicado");
    append_log(job->log_path, "Perfil de offsets: %s\n", profile_name);
    append_log(job->log_path, "Punteros actualizados: %u", dwords_applied);
    if (dwords_skipped > 0) append_log(job->log_path, " (%u omitidos por cambios en la build)", dwords_skipped);
    append_log(job->log_path, "\n");
    append_log(job->log_path, "SHA256 salida: %s\n", output_hash);
    append_log(job->log_path, "\nParche aplicado correctamente.\n");
    append_log(job->log_path, "Backup: %s\n", backup);
    return 1;
}

#if defined(_WIN32)
static DWORD WINAPI patch_thread_proc(LPVOID data) {
    PatchJob *job = (PatchJob *)data;
    job->success = patch_job_run(job);
    job->done = 1;
    job->running = 0;
    return 0;
}
#else
static void *patch_thread_proc(void *data) {
    PatchJob *job = (PatchJob *)data;
    job->success = patch_job_run(job);
    job->done = 1;
    job->running = 0;
    return NULL;
}
#endif

static int start_patch_thread(PatchJob *job) {
    job->running = 1;
    job->done = 0;
    job->success = 0;
    set_job_status(job, "Preparando...");
#if defined(_WIN32)
    HANDLE thread = CreateThread(NULL, 0, patch_thread_proc, job, 0, NULL);
    if (!thread) {
        job->running = 0;
        return 0;
    }
    CloseHandle(thread);
    return 1;
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, patch_thread_proc, job) != 0) {
        job->running = 0;
        return 0;
    }
    pthread_detach(thread);
    return 1;
#endif
}

static void read_log_tail(const char *path, char *out, size_t out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        out[0] = '\0';
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long start = size > (long)(out_size - 1) ? size - (long)(out_size - 1) : 0;
    fseek(f, start, SEEK_SET);
    size_t got = fread(out, 1, out_size - 1, f);
    out[got] = '\0';
    fclose(f);
    if (start > 0) {
        char *first_newline = strchr(out, '\n');
        if (first_newline) memmove(out, first_newline + 1, strlen(first_newline + 1) + 1);
    }
}

static void abbreviate_path(const char *path, char *out, size_t out_size, size_t max_chars) {
    size_t len = strlen(path);
    if (len + 1 <= out_size && len <= max_chars) {
        snprintf(out, out_size, "%s", path);
        return;
    }
    if (max_chars < 8 || out_size < 8) {
        snprintf(out, out_size, "%s", base_name(path));
        return;
    }
    size_t tail = max_chars - 3;
    if (tail + 4 > out_size) tail = out_size - 4;
    snprintf(out, out_size, "...%s", path + (len > tail ? len - tail : 0));
}

static void draw_button(Rectangle rect, const char *text, int disabled) {
    Color border = disabled ? GRAY : WHITE;
    DrawRectangleLinesEx(rect, 2.0f, border);
    int font_size = 22;
    int text_w = MeasureText(text, font_size);
    DrawText(text, (int)(rect.x + rect.width / 2 - text_w / 2), (int)(rect.y + rect.height / 2 - font_size / 2), font_size, border);
}

static int button_clicked(Rectangle rect, int disabled) {
    if (disabled) return 0;
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), rect);
}

static void draw_log_text(const char *text, Rectangle rect) {
    DrawRectangleLinesEx(rect, 1.0f, WHITE);
    int font_size = 15;
    int line_height = 18;
    int max_lines = (int)(rect.height / line_height);
    if (max_lines <= 0) return;

    const char *line_starts[128];
    int cap = (int)(sizeof(line_starts) / sizeof(line_starts[0]));
    int count = 0;
    line_starts[count++] = text;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n' && p[1] != '\0') {
            if (count < cap) {
                line_starts[count++] = p + 1;
            } else {
                memmove(line_starts, line_starts + 1, sizeof(line_starts[0]) * (cap - 1));
                line_starts[cap - 1] = p + 1;
            }
        }
    }

    int start = count > max_lines ? count - max_lines : 0;
    float y = rect.y + 10.0f;
    for (int i = start; i < count && y < rect.y + rect.height - line_height; ++i) {
        char line[512];
        const char *start_p = line_starts[i];
        const char *end_p = strchr(start_p, '\n');
        size_t len = end_p ? (size_t)(end_p - start_p) : strlen(start_p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, start_p, len);
        line[len] = '\0';
        DrawTextEx(GetFontDefault(), line, (Vector2){rect.x + 10.0f, y}, (float)font_size, 1.0f, RAYWHITE);
        y += (float)line_height;
    }
}

static void select_input_path(const char *path) {
    char resolved[PATH_LIMIT];
    if (resolve_data_win_path(path, resolved, sizeof(resolved))) {
        snprintf(g_job.data_win, sizeof(g_job.data_win), "%s", resolved);
        set_job_status(&g_job, "Listo");
    } else {
        g_job.data_win[0] = '\0';
        set_job_status(&g_job, "No encuentro data.win");
    }
}

static void get_current_dir(char *out, size_t out_size) {
#if defined(_WIN32)
    DWORD len = GetCurrentDirectoryA((DWORD)out_size, out);
    if (len == 0 || len >= out_size) snprintf(out, out_size, ".");
#else
    if (!getcwd(out, out_size)) snprintf(out, out_size, ".");
#endif
}

static int try_autolocate_data_win(void) {
    if (g_job.data_win[0] && file_exists(g_job.data_win)) return 1;

    char cwd[PATH_LIMIT];
    get_current_dir(cwd, sizeof(cwd));
    if (resolve_data_win_path(cwd, g_job.data_win, sizeof(g_job.data_win))) {
        set_job_status(&g_job, "Listo");
        return 1;
    }
    if (resolve_data_win_path(g_job.app_dir, g_job.data_win, sizeof(g_job.data_win))) {
        set_job_status(&g_job, "Listo");
        return 1;
    }

    char parent[PATH_LIMIT];
    snprintf(parent, sizeof(parent), "%s", g_job.app_dir);
    dirname_in_place(parent);
    if (resolve_data_win_path(parent, g_job.data_win, sizeof(g_job.data_win))) {
        set_job_status(&g_job, "Listo");
        return 1;
    }

    set_job_status(&g_job, "Suelta carpeta o pon el exe junto al juego");
    return 0;
}

static void start_patch_or_locate(void) {
    if (g_job.running) return;
    if (!try_autolocate_data_win()) return;
    start_patch_thread(&g_job);
}

int main(int argc, char **argv) {
    char app_dir[PATH_LIMIT];
    get_executable_dir(app_dir, sizeof(app_dir), argc > 0 ? argv[0] : NULL);
    snprintf(g_job.app_dir, sizeof(g_job.app_dir), "%s", app_dir);
    path_join(g_job.log_path, sizeof(g_job.log_path), app_dir, "patcher.log");
    set_job_status(&g_job, "Suelta carpeta o data.win");

    if (argc > 2 && strcmp(argv[1], "--apply") == 0) {
        select_input_path(argv[2]);
        if (!g_job.data_win[0]) {
            fprintf(stderr, "No encuentro data.win en: %s\n", argv[2]);
            return 1;
        }
        int ok = patch_job_run(&g_job);
        return ok ? 0 : 1;
    }

    if (argc > 1) select_input_path(argv[1]);
    if (!g_job.data_win[0]) try_autolocate_data_win();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W, WINDOW_H, "DELTARUNE Chapter 5 ES Patcher");
    InitAudioDevice();
    SetTargetFPS(60);

    Texture2D logo = {0};
    Music music = {0};
    int has_logo = 0, has_music = 0;

    Image logo_image = LoadImageFromMemory(".png", embedded_logo_png, (int)embedded_logo_png_len);
    if (logo_image.data != NULL) {
        SetWindowIcon(logo_image);
        logo = LoadTextureFromImage(logo_image);
        has_logo = logo.id != 0;
        UnloadImage(logo_image);
    }

    music = LoadMusicStreamFromMemory(".wav", embedded_music_wav, (int)embedded_music_wav_len);
    if (music.stream.buffer != NULL) {
        music.looping = true;
        PlayMusicStream(music);
        has_music = 1;
    }

    char log_text[LOG_LIMIT];
    log_text[0] = '\0';

    while (!WindowShouldClose()) {
        if (has_music) UpdateMusicStream(music);

        if (IsFileDropped() && !g_job.running) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0) {
                select_input_path(dropped.paths[0]);
                if (g_job.data_win[0]) start_patch_or_locate();
            }
            UnloadDroppedFiles(dropped);
        }

        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) && !g_job.running) {
            start_patch_or_locate();
        }

        read_log_tail(g_job.log_path, log_text, sizeof(log_text));

        BeginDrawing();
        ClearBackground(BLACK);

        int w = GetScreenWidth();
        int h = GetScreenHeight();
        DrawText("PATCHER", 36, 32, 54, WHITE);
        DrawText("RAYLIB", 42, 88, 28, WHITE);
        DrawText("by soykhaler", 42, 122, 20, WHITE);

        if (has_logo) {
            float max_w = 260.0f;
            float max_h = 160.0f;
            float scale = max_w / (float)logo.width;
            if ((float)logo.height * scale > max_h) scale = max_h / (float)logo.height;
            Rectangle src = {0, 0, (float)logo.width, (float)logo.height};
            Rectangle dst = {(float)w - 320.0f, 28.0f, (float)logo.width * scale, (float)logo.height * scale};
            DrawTexturePro(logo, src, dst, (Vector2){0, 0}, 0, WHITE);
        }

        DrawLine(36, 174, w - 36, 174, WHITE);
        DrawText("Suelta la carpeta del Capitulo 5, data.win, o pon este exe junto al juego", 42, 194, 20, WHITE);

        char shown_path[180];
        abbreviate_path(g_job.data_win[0] ? g_job.data_win : "(ninguno)", shown_path, sizeof(shown_path), (size_t)((w - 84) / 9));
        DrawText("Archivo:", 42, 238, 18, WHITE);
        DrawText(shown_path, 42, 262, 18, g_job.data_win[0] ? RAYWHITE : GRAY);

        DrawText("Estado:", 42, 304, 18, WHITE);
        DrawText(g_job.status, 42, 328, 22, g_job.success ? GREEN : WHITE);

        Rectangle patch_btn = {42, 372, 260, 54};
        int disabled = g_job.running;
        draw_button(patch_btn, g_job.running ? "PARCHEANDO..." : "APLICAR PARCHE", disabled);
        if (button_clicked(patch_btn, disabled)) start_patch_or_locate();

        DrawText("LOG", 54, 438, 18, WHITE);
        draw_log_text(log_text, (Rectangle){42, 454, (float)w - 84, (float)h - 492});

        EndDrawing();
    }

    if (has_music) UnloadMusicStream(music);
    if (has_logo) UnloadTexture(logo);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
