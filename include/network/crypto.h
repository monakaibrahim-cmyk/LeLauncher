#ifndef __CRYPTO_H__
#define __CRYPTO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_XOR_HEX_KEY                                            "E47B912A5F83C61D3EA072B54968D1FA"

/* Hex and byte conversion */
size_t __crypto_hex_to_bytes__(const char *hex, uint8_t *out_bytes, size_t max_len);
bool __crypto_bytes_to_hex__(const uint8_t *in_bytes, size_t len, char *out_hex, size_t max_hex_len);
size_t __crypto_get_active_key__(uint8_t *out_key, size_t max_len);

/* Stream cipher primitives */
void __crypto_xor_cipher__(const uint8_t *data, size_t data_len, const uint8_t *key, size_t key_len, uint8_t *out_buf);
bool __crypto_xor_encrypt_to_hex__(const char *plaintext, const char *hex_key, char *out_hex_buf, size_t max_hex_len);
bool __crypto_xor_decrypt_from_hex__(const char *hex_ciphertext, const char *hex_key, char *out_plaintext, size_t max_plain_len);

/* Obfuscated endpoint string getters */
const char *__crypto_get_default_api_url__(char *out_url, size_t max_len);
const char *__crypto_get_preauth_path__(char *out_path, size_t max_len);
const char *__crypto_get_login_path__(char *out_path, size_t max_len);
const char *__crypto_get_health_path__(char *out_path, size_t max_len);
const char *__crypto_get_live_path__(char *out_path, size_t max_len);

/* Memory sanitization and TLS pinning */
void __crypto_secure_wipe__(void *ptr, size_t len);
void __crypto_set_pinned_public_key__(const char *pin);
const char *__crypto_get_pinned_public_key__(char *out_buf, size_t max_len);

#endif /* __CRYPTO_H__ */
