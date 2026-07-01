#ifndef DELTARUNE_ES_EMBEDDED_PROFILES_H
#define DELTARUNE_ES_EMBEDDED_PROFILES_H

#define EMBEDDED_PROFILE_COUNT 4
#define KNOWN_INPUT_SHA256_COUNT 4

static const char *known_input_sha256s[KNOWN_INPUT_SHA256_COUNT] = {
    "9ed07de5e437de3b7bac0000c6374166198ec337f5ad761de9d5fed393cf326e",
    "cd940118eff1a3f24dd8dca3932901f85bfc83761e175e36736622316e7ed556",
    "47c2c274bfb153b62d665f2343678751859f8d2417570768fca34352a57c65fe",
    "de1f204ea4a9591c8f7d0355f59065db0261592a9faf5529f7d789df295fb1cc",
};

static int is_known_input_sha256(const char *hash) {
    for (size_t i = 0; i < KNOWN_INPUT_SHA256_COUNT; ++i) {
        if (strcmp(hash, known_input_sha256s[i]) == 0) return 1;
    }
    return 0;
}

static void init_embedded_profiles(DwordPatchProfile profiles[EMBEDDED_PROFILE_COUNT]) {
    profiles[0] = (DwordPatchProfile){
        "2025-06-25", 0x014e8cf0u, 93214u, 0x0041ee08u,
        embedded_strings_old_bin, embedded_strings_old_bin_len,
        embedded_dwords_old_bin, embedded_dwords_old_bin_len
    };
    profiles[1] = (DwordPatchProfile){
        "2026-06-27", 0x014e9330u, 93223u, 0x0041ef48u,
        embedded_strings_new_bin, embedded_strings_new_bin_len,
        embedded_dwords_new_bin, embedded_dwords_new_bin_len
    };
    profiles[2] = (DwordPatchProfile){
        "2026-06-29", 0x014e9560u, 93222u, 0x0041ef18u,
        embedded_strings_steam_bin, embedded_strings_steam_bin_len,
        embedded_dwords_steam_bin, embedded_dwords_steam_bin_len
    };
    profiles[3] = (DwordPatchProfile){
        "2026-07-01", 0x014e9650u, 93223u, 0x0041eea8u,
        embedded_strings_b20260701_bin, embedded_strings_b20260701_bin_len,
        embedded_dwords_b20260701_bin, embedded_dwords_b20260701_bin_len
    };
}

#endif
