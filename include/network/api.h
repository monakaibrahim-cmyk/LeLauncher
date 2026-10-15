#ifndef __API_H__
#define __API_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <network/crypto.h>

typedef enum {
    API_PAYLOAD_PLAIN = 0,
    API_PAYLOAD_XOR_HEX,
    API_PAYLOAD_XOR_RAW_HEX
} ApiPayloadFormat;

void __api_set_payload_format__(ApiPayloadFormat format);
ApiPayloadFormat __api_get_payload_format__(void);

typedef struct {
    bool    success;
    long    http_code;
    char    temporary_token[256];
    char    error_message[256];
} ApiPreAuthResult;

typedef struct {
    bool    success;
    long    http_code;
    char    permanent_token[256];
    char    account_id[64];
    char    account_tier[32];
    char    error_message[256];
} ApiAuthResult;

typedef struct {
    bool    online;
    long    response_time_ms;
    char    version[32];
    char    error_message[256];
} ApiHealthResult;

void __api_init__(void);
void __api_cleanup__(void);

/* Payload decrypt and URL helpers */
bool __api_decrypt_payload_url__(const char *hex_encrypted_url, char *out_url, size_t max_len);
const char *__api_get_default_url__(char *out_url, size_t max_len);

/* Handshake and authentication endpoints */
ApiPreAuthResult __api_request_temporary_token__(const char *endpoint, const char *hwid, const char *ip);
ApiAuthResult __api_authenticate_credentials__(const char *endpoint,
                                              const char *temporary_token,
                                              const char *username,
                                              const char *password,
                                              const char *hwid);
ApiAuthResult __api_login_and_persist__(const char *endpoint, const char *username, const char *password);
ApiHealthResult __api_check_health__(const char *endpoint);

#endif /* __API_H__ */
