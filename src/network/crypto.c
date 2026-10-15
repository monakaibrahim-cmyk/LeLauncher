#include <network/crypto.h>
#include <core/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const uint8_t ENC_DEFAULT_API_URL[] = {
    0x8c, 0x0f, 0xe5, 0x5a, 0x2c, 0xb9, 0xe9, 0x32, 0x5f, 0xd0, 0x1b, 0x9b, 0x25, 0x0d, 0xbd, 0x9b,
    0x91, 0x15, 0xf2, 0x42, 0x3a, 0xf1, 0xe8, 0x7e, 0x51, 0xcd, 0x5d, 0xc3, 0x78
};

static const uint8_t ENC_PATH_PREAUTH[] = {
    0xcb, 0x1a, 0xe4, 0x5e, 0x37, 0xac, 0xb6, 0x6f, 0x5b, 0xc1, 0x07, 0xc1, 0x21
};

static const uint8_t ENC_PATH_LOGIN[] = {
    0xcb, 0x1a, 0xe4, 0x5e, 0x37, 0xac, 0xaa, 0x72, 0x59, 0xc9, 0x1c
};

static const uint8_t ENC_PATH_HEALTH[] = {
    0xcb, 0x13, 0xf4, 0x4b, 0x33, 0xf7, 0xae
};

static const uint8_t ENC_PATH_LIVE[] = {
    0xcb, 0x17, 0xf8, 0x5c, 0x3a
};

static const uint8_t EMBEDDED_KEY_BYTES[] = {
    0xE4, 0x7B, 0x91, 0x2A, 0x5F, 0x83, 0xC6, 0x1D,
    0x3E, 0xA0, 0x72, 0xB5, 0x49, 0x68, 0xD1, 0xFA
};

static char g_pinned_pubkey[512] = {0};

static int __hex_char_to_val__(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t __crypto_hex_to_bytes__(const char *hex, uint8_t *out_bytes, size_t max_len) {
    if (!hex || !out_bytes || max_len == 0) return 0;

    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return 0;

    size_t byte_count = hex_len / 2;
    if (byte_count > max_len) byte_count = max_len;

    for (size_t i = 0; i < byte_count; i++) {
        int hi = __hex_char_to_val__(hex[i * 2]);
        int lo = __hex_char_to_val__(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out_bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    return byte_count;
}

bool __crypto_bytes_to_hex__(const uint8_t *in_bytes, size_t len, char *out_hex, size_t max_hex_len) {
    if (!in_bytes || !out_hex || max_hex_len < (len * 2 + 1)) return false;

    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = hex_digits[(in_bytes[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = hex_digits[in_bytes[i] & 0x0F];
    }
    out_hex[len * 2] = '\0';
    return true;
}

size_t __crypto_get_active_key__(uint8_t *out_key, size_t max_len) {
    if (!out_key || max_len == 0) return 0;

    const char *env_key = getenv("LELAUNCHER_HEX_KEY");
    if (env_key && strlen(env_key) > 0) {
        return __crypto_hex_to_bytes__(env_key, out_key, max_len);
    }

    size_t copy_len = sizeof(EMBEDDED_KEY_BYTES);
    if (copy_len > max_len) copy_len = max_len;
    memcpy(out_key, EMBEDDED_KEY_BYTES, copy_len);
    return copy_len;
}

void __crypto_xor_cipher__(const uint8_t *data, size_t data_len, const uint8_t *key, size_t key_len, uint8_t *out_buf) {
    if (!data || !key || key_len == 0 || !out_buf) return;

    for (size_t i = 0; i < data_len; i++) {
        out_buf[i] = data[i] ^ key[i % key_len];
    }
}

bool __crypto_xor_encrypt_to_hex__(const char *plaintext, const char *hex_key, char *out_hex_buf, size_t max_hex_len) {
    TRACE_SCOPE();
    if (!plaintext || !out_hex_buf) return false;

    uint8_t key[64];
    size_t key_len = 0;

    if (hex_key && strlen(hex_key) > 0) {
        key_len = __crypto_hex_to_bytes__(hex_key, key, sizeof(key));
    } else {
        key_len = __crypto_get_active_key__(key, sizeof(key));
    }

    if (key_len == 0) return false;

    size_t plain_len = strlen(plaintext);
    if (max_hex_len < (plain_len * 2 + 1)) return false;

    uint8_t *encrypted = (uint8_t *)malloc(plain_len);
    if (!encrypted) return false;

    __crypto_xor_cipher__((const uint8_t *)plaintext, plain_len, key, key_len, encrypted);
    bool ok = __crypto_bytes_to_hex__(encrypted, plain_len, out_hex_buf, max_hex_len);

    __crypto_secure_wipe__(encrypted, plain_len);
    free(encrypted);
    return ok;
}

bool __crypto_xor_decrypt_from_hex__(const char *hex_ciphertext, const char *hex_key, char *out_plaintext, size_t max_plain_len) {
    TRACE_SCOPE();
    if (!hex_ciphertext || !out_plaintext || max_plain_len == 0) return false;

    uint8_t key[64];
    size_t key_len = 0;

    if (hex_key && strlen(hex_key) > 0) {
        key_len = __crypto_hex_to_bytes__(hex_key, key, sizeof(key));
    } else {
        key_len = __crypto_get_active_key__(key, sizeof(key));
    }

    if (key_len == 0) return false;

    size_t cipher_bytes_len = strlen(hex_ciphertext) / 2;
    if (cipher_bytes_len >= max_plain_len) return false;

    uint8_t *bytes = (uint8_t *)malloc(cipher_bytes_len);
    if (!bytes) return false;

    if (__crypto_hex_to_bytes__(hex_ciphertext, bytes, cipher_bytes_len) != cipher_bytes_len) {
        free(bytes);
        return false;
    }

    __crypto_xor_cipher__(bytes, cipher_bytes_len, key, key_len, (uint8_t *)out_plaintext);
    out_plaintext[cipher_bytes_len] = '\0';

    __crypto_secure_wipe__(bytes, cipher_bytes_len);
    free(bytes);
    return true;
}

static const char *__deobfuscate_blob__(const uint8_t *blob, size_t blob_len, char *out_buf, size_t max_len) {
    if (!blob || !out_buf || max_len <= blob_len) {
        if (out_buf && max_len > 0) out_buf[0] = '\0';
        return "";
    }

    __crypto_xor_cipher__(blob, blob_len, EMBEDDED_KEY_BYTES, sizeof(EMBEDDED_KEY_BYTES), (uint8_t *)out_buf);
    out_buf[blob_len] = '\0';
    return out_buf;
}

const char *__crypto_get_default_api_url__(char *out_url, size_t max_len) {
    return __deobfuscate_blob__(ENC_DEFAULT_API_URL, sizeof(ENC_DEFAULT_API_URL), out_url, max_len);
}

const char *__crypto_get_preauth_path__(char *out_path, size_t max_len) {
    return __deobfuscate_blob__(ENC_PATH_PREAUTH, sizeof(ENC_PATH_PREAUTH), out_path, max_len);
}

const char *__crypto_get_login_path__(char *out_path, size_t max_len) {
    return __deobfuscate_blob__(ENC_PATH_LOGIN, sizeof(ENC_PATH_LOGIN), out_path, max_len);
}

const char *__crypto_get_health_path__(char *out_path, size_t max_len) {
    return __deobfuscate_blob__(ENC_PATH_HEALTH, sizeof(ENC_PATH_HEALTH), out_path, max_len);
}

const char *__crypto_get_live_path__(char *out_path, size_t max_len) {
    return __deobfuscate_blob__(ENC_PATH_LIVE, sizeof(ENC_PATH_LIVE), out_path, max_len);
}

void __crypto_secure_wipe__(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

void __crypto_set_pinned_public_key__(const char *pin) {
    if (!pin) {
        __crypto_secure_wipe__(g_pinned_pubkey, sizeof(g_pinned_pubkey));
        g_pinned_pubkey[0] = '\0';
        return;
    }
    strncpy(g_pinned_pubkey, pin, sizeof(g_pinned_pubkey) - 1);
    g_pinned_pubkey[sizeof(g_pinned_pubkey) - 1] = '\0';
}

const char *__crypto_get_pinned_public_key__(char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return "";

    const char *env_pin = getenv("LELAUNCHER_PINNED_PUBKEY");
    if (env_pin && strlen(env_pin) > 0) {
        strncpy(out_buf, env_pin, max_len - 1);
        out_buf[max_len - 1] = '\0';
        return out_buf;
    }

    if (strlen(g_pinned_pubkey) > 0) {
        strncpy(out_buf, g_pinned_pubkey, max_len - 1);
        out_buf[max_len - 1] = '\0';
        return out_buf;
    }

    out_buf[0] = '\0';
    return "";
}
