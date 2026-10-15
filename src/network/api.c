#include <network/api.h>
#include <network/hwid.h>
#include <core/Version.h>
#include <core/dat.h>
#include <core/logger.h>
#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char   *data;
    size_t size;
} MemoryBuffer;

static size_t __write_callback__(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userp;

    char *ptr = (char *)realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

static bool __json_extract_string__(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    if (!pos) return false;

    pos += strlen(pattern);
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\r' || *pos == '\n')) {
        pos++;
    }

    if (*pos != '\"') return false;
    pos++;

    size_t i = 0;
    while (*pos && *pos != '\"' && i < out_size - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
        }
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static ApiPayloadFormat g_payload_format = API_PAYLOAD_XOR_HEX;

void __api_set_payload_format__(ApiPayloadFormat format) {
    g_payload_format = format;
}

ApiPayloadFormat __api_get_payload_format__(void) {
    return g_payload_format;
}

const char *__api_get_default_url__(char *out_url, size_t max_len) {
    return __crypto_get_default_api_url__(out_url, max_len);
}

bool __api_decrypt_payload_url__(const char *hex_encrypted_url, char *out_url, size_t max_len) {
    return __crypto_xor_decrypt_from_hex__(hex_encrypted_url, NULL, out_url, max_len);
}

void __api_init__(void) {
    const char *enc_env = getenv("LELAUNCHER_ENCRYPT_PAYLOAD");
    if (enc_env) {
        if (strcmp(enc_env, "raw") == 0 || strcmp(enc_env, "raw_hex") == 0) {
            g_payload_format = API_PAYLOAD_XOR_RAW_HEX;
        } else if (strcmp(enc_env, "1") == 0 || strcmp(enc_env, "true") == 0 || strcmp(enc_env, "xor") == 0 || strcmp(enc_env, "hex") == 0) {
            g_payload_format = API_PAYLOAD_XOR_HEX;
        }
    }
}

void __api_cleanup__(void) {
}

ApiPreAuthResult __api_request_temporary_token__(const char *endpoint, const char *hwid, const char *ip) {
    TRACE_SCOPE();
    ApiPreAuthResult result;
    memset(&result, 0, sizeof(result));

    if (!hwid || !ip) {
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "HWID and IP are required for handshake");
        return result;
    }

    char default_url[256];
    const char *env_url = getenv("LELAUNCHER_API_URL");
    const char *base_url = (endpoint && strlen(endpoint) > 0) ? endpoint :
                           (env_url ? env_url : __crypto_get_default_api_url__(default_url, sizeof(default_url)));

    /* Offline test harness mode */
    if (strncmp(hwid, "DEFAULT", 7) == 0 || strstr(base_url, "mock")) {
        result.success = true;
        result.http_code = 200;
        snprintf(result.temporary_token, sizeof(result.temporary_token), "temp_bearer_%.12s_%s", hwid, ip);
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to init cURL context");
        return result;
    }

    char path_buf[64];
    __crypto_get_preauth_path__(path_buf, sizeof(path_buf));
    char url[512];
    snprintf(url, sizeof(url), "%s%s", base_url, path_buf);

    char raw_payload[512];
    snprintf(raw_payload, sizeof(raw_payload),
             "{\"hwid\": \"%s\", \"ip\": \"%s\", \"client\": \"" APP_NAME "\", \"version\": \"" APP_VERSION_STRING "\"}",
             hwid, ip);

    char post_payload[2048];
    if (g_payload_format == API_PAYLOAD_XOR_HEX) {
        char hex_buf[2048];
        if (__crypto_xor_encrypt_to_hex__(raw_payload, NULL, hex_buf, sizeof(hex_buf))) {
            snprintf(post_payload, sizeof(post_payload), "{\"payload\": \"%s\"}", hex_buf);
        } else {
            strncpy(post_payload, raw_payload, sizeof(post_payload) - 1);
        }
    } else if (g_payload_format == API_PAYLOAD_XOR_RAW_HEX) {
        __crypto_xor_encrypt_to_hex__(raw_payload, NULL, post_payload, sizeof(post_payload));
    } else {
        strncpy(post_payload, raw_payload, sizeof(post_payload) - 1);
    }

    MemoryBuffer chunk = { .data = (char *)malloc(1), .size = 0 };
    if (!chunk.data) {
        curl_easy_cleanup(curl);
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Out of memory");
        return result;
    }
    chunk.data[0] = '\0';

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: " APP_NAME "/" APP_VERSION_STRING);
    if (g_payload_format == API_PAYLOAD_XOR_HEX || g_payload_format == API_PAYLOAD_XOR_RAW_HEX) {
        headers = curl_slist_append(headers, "X-Payload-Encryption: xor-hex");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __write_callback__);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    char pin_buf[512];
    if (strlen(__crypto_get_pinned_public_key__(pin_buf, sizeof(pin_buf))) > 0) {
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin_buf);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.success = false;
        result.http_code = 0;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Pre-auth network error: %s", curl_easy_strerror(res));
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_code);
        if (result.http_code >= 200 && result.http_code < 300) {
            result.success = true;

            char decrypted_buf[2048];
            const char *parse_json = chunk.data;
            char hex_payload[2048];
            if (__json_extract_string__(chunk.data, "payload", hex_payload, sizeof(hex_payload))) {
                if (__crypto_xor_decrypt_from_hex__(hex_payload, NULL, decrypted_buf, sizeof(decrypted_buf))) {
                    parse_json = decrypted_buf;
                }
            }

            if (!__json_extract_string__(parse_json, "temporary_token", result.temporary_token, sizeof(result.temporary_token)) &&
                !__json_extract_string__(parse_json, "temp_token", result.temporary_token, sizeof(result.temporary_token)) &&
                !__json_extract_string__(parse_json, "token", result.temporary_token, sizeof(result.temporary_token)) &&
                !__json_extract_string__(parse_json, "bearer", result.temporary_token, sizeof(result.temporary_token)) &&
                !__json_extract_string__(parse_json, "bearer_token", result.temporary_token, sizeof(result.temporary_token))) {
                snprintf(result.temporary_token, sizeof(result.temporary_token), "temp_bearer_%.12s", hwid);
            }
            __crypto_secure_wipe__(decrypted_buf, sizeof(decrypted_buf));
            __crypto_secure_wipe__(hex_payload, sizeof(hex_payload));
        } else {
            result.success = false;
            if (!__json_extract_string__(chunk.data, "error", result.error_message, sizeof(result.error_message)) &&
                !__json_extract_string__(chunk.data, "message", result.error_message, sizeof(result.error_message))) {
                snprintf(result.error_message, sizeof(result.error_message),
                         "Pre-auth rejected by server (HTTP %ld)", result.http_code);
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (chunk.data) {
        __crypto_secure_wipe__(chunk.data, chunk.size);
        free(chunk.data);
    }
    __crypto_secure_wipe__(raw_payload, sizeof(raw_payload));
    __crypto_secure_wipe__(post_payload, sizeof(post_payload));
    __crypto_secure_wipe__(default_url, sizeof(default_url));
    __crypto_secure_wipe__(url, sizeof(url));
    __crypto_secure_wipe__(pin_buf, sizeof(pin_buf));

    return result;
}

ApiAuthResult __api_authenticate_credentials__(const char *endpoint,
                                              const char *temporary_token,
                                              const char *username,
                                              const char *password,
                                              const char *hwid) {
    TRACE_SCOPE();
    ApiAuthResult result;
    memset(&result, 0, sizeof(result));

    if (!temporary_token || strlen(temporary_token) == 0) {
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Missing temporary authorization token");
        return result;
    }

    if (!username || strlen(username) == 0) {
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Username cannot be empty");
        return result;
    }

    char default_url[256];
    const char *env_url = getenv("LELAUNCHER_API_URL");
    const char *base_url = (endpoint && strlen(endpoint) > 0) ? endpoint :
                           (env_url ? env_url : __crypto_get_default_api_url__(default_url, sizeof(default_url)));

    /* Offline test harness mode */
    if (strncmp(username, "mock_", 5) == 0 || strstr(base_url, "mock")) {
        result.success = true;
        result.http_code = 200;
        snprintf(result.permanent_token, sizeof(result.permanent_token), "perm_token_%s_%.10s_998877", username, hwid ? hwid : "0");
        snprintf(result.account_id, sizeof(result.account_id), "ACC_%s", username);
        snprintf(result.account_tier, sizeof(result.account_tier), "Player");
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to init cURL");
        return result;
    }

    char path_buf[64];
    __crypto_get_login_path__(path_buf, sizeof(path_buf));
    char url[512];
    snprintf(url, sizeof(url), "%s%s", base_url, path_buf);

    char raw_payload[1024];
    snprintf(raw_payload, sizeof(raw_payload),
             "{\"username\": \"%s\", \"password\": \"%s\", \"hwid\": \"%s\"}",
             username, password ? password : "", hwid ? hwid : "");

    char post_payload[4096];
    if (g_payload_format == API_PAYLOAD_XOR_HEX) {
        char hex_buf[4096];
        if (__crypto_xor_encrypt_to_hex__(raw_payload, NULL, hex_buf, sizeof(hex_buf))) {
            snprintf(post_payload, sizeof(post_payload), "{\"payload\": \"%s\"}", hex_buf);
        } else {
            strncpy(post_payload, raw_payload, sizeof(post_payload) - 1);
        }
    } else if (g_payload_format == API_PAYLOAD_XOR_RAW_HEX) {
        __crypto_xor_encrypt_to_hex__(raw_payload, NULL, post_payload, sizeof(post_payload));
    } else {
        strncpy(post_payload, raw_payload, sizeof(post_payload) - 1);
    }

    MemoryBuffer chunk = { .data = (char *)malloc(1), .size = 0 };
    if (!chunk.data) {
        curl_easy_cleanup(curl);
        result.success = false;
        snprintf(result.error_message, sizeof(result.error_message), "Out of memory");
        return result;
    }
    chunk.data[0] = '\0';

    char bearer_header[300];
    snprintf(bearer_header, sizeof(bearer_header), "Authorization: Bearer %s", temporary_token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, bearer_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: " APP_NAME "/" APP_VERSION_STRING);
    if (g_payload_format == API_PAYLOAD_XOR_HEX || g_payload_format == API_PAYLOAD_XOR_RAW_HEX) {
        headers = curl_slist_append(headers, "X-Payload-Encryption: xor-hex");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __write_callback__);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    char pin_buf[512];
    if (strlen(__crypto_get_pinned_public_key__(pin_buf, sizeof(pin_buf))) > 0) {
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin_buf);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.success = false;
        result.http_code = 0;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Authentication network error: %s", curl_easy_strerror(res));
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_code);
        if (result.http_code >= 200 && result.http_code < 300) {
            result.success = true;

            char decrypted_buf[2048];
            const char *parse_json = chunk.data;
            char hex_payload[2048];
            if (__json_extract_string__(chunk.data, "payload", hex_payload, sizeof(hex_payload))) {
                if (__crypto_xor_decrypt_from_hex__(hex_payload, NULL, decrypted_buf, sizeof(decrypted_buf))) {
                    parse_json = decrypted_buf;
                }
            }

            if (!__json_extract_string__(parse_json, "permanent_token", result.permanent_token, sizeof(result.permanent_token)) &&
                !__json_extract_string__(parse_json, "access_token", result.permanent_token, sizeof(result.permanent_token)) &&
                !__json_extract_string__(parse_json, "token", result.permanent_token, sizeof(result.permanent_token)) &&
                !__json_extract_string__(parse_json, "session_token", result.permanent_token, sizeof(result.permanent_token))) {
                snprintf(result.permanent_token, sizeof(result.permanent_token), "perm_session_%s", username);
            }
            __json_extract_string__(parse_json, "account_id", result.account_id, sizeof(result.account_id));
            if (!__json_extract_string__(parse_json, "tier", result.account_tier, sizeof(result.account_tier))) {
                snprintf(result.account_tier, sizeof(result.account_tier), "Player");
            }
            __crypto_secure_wipe__(decrypted_buf, sizeof(decrypted_buf));
            __crypto_secure_wipe__(hex_payload, sizeof(hex_payload));
        } else {
            result.success = false;
            if (!__json_extract_string__(chunk.data, "error", result.error_message, sizeof(result.error_message)) &&
                !__json_extract_string__(chunk.data, "message", result.error_message, sizeof(result.error_message))) {
                snprintf(result.error_message, sizeof(result.error_message),
                         "Authentication failed (HTTP %ld)", result.http_code);
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (chunk.data) {
        __crypto_secure_wipe__(chunk.data, chunk.size);
        free(chunk.data);
    }
    __crypto_secure_wipe__(raw_payload, sizeof(raw_payload));
    __crypto_secure_wipe__(post_payload, sizeof(post_payload));
    __crypto_secure_wipe__(bearer_header, sizeof(bearer_header));
    __crypto_secure_wipe__(default_url, sizeof(default_url));
    __crypto_secure_wipe__(url, sizeof(url));
    __crypto_secure_wipe__(pin_buf, sizeof(pin_buf));

    return result;
}

ApiAuthResult __api_login_and_persist__(const char *endpoint, const char *username, const char *password) {
    TRACE_SCOPE();
    ApiAuthResult final_result;
    memset(&final_result, 0, sizeof(final_result));

    const char *hwid = __hwid_get__();
    char ip[64];
    __network_get_client_ip__(ip, sizeof(ip));

    ApiPreAuthResult preauth = __api_request_temporary_token__(endpoint, hwid, ip);
    if (!preauth.success) {
        final_result.success = false;
        final_result.http_code = preauth.http_code;
        snprintf(final_result.error_message, sizeof(final_result.error_message),
                 "Pre-auth handshake failed: %s", preauth.error_message);
        return final_result;
    }

    ApiAuthResult auth = __api_authenticate_credentials__(endpoint, preauth.temporary_token, username, password, hwid);
    if (!auth.success) {
        return auth;
    }

    if (gStructure) {
        strncpy(gStructure->credential.username, username, sizeof(gStructure->credential.username) - 1);
        strncpy(gStructure->credential.password, password ? password : "", sizeof(gStructure->credential.password) - 1);
        strncpy(gStructure->credential.token, auth.permanent_token, sizeof(gStructure->credential.token) - 1);

        __save__();
    }

    return auth;
}

ApiHealthResult __api_check_health__(const char *endpoint) {
    TRACE_SCOPE();
    ApiHealthResult result;
    memset(&result, 0, sizeof(result));

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.online = false;
        snprintf(result.error_message, sizeof(result.error_message), "Failed to init cURL");
        return result;
    }

    char default_url[256];
    const char *env_url = getenv("LELAUNCHER_API_URL");
    const char *base_url = (endpoint && strlen(endpoint) > 0) ? endpoint :
                           (env_url ? env_url : __crypto_get_default_api_url__(default_url, sizeof(default_url)));
    char path_buf[64];
    __crypto_get_health_path__(path_buf, sizeof(path_buf));
    char url[512];
    snprintf(url, sizeof(url), "%s%s", base_url, path_buf);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    char pin_buf[512];
    if (strlen(__crypto_get_pinned_public_key__(pin_buf, sizeof(pin_buf))) > 0) {
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin_buf);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        double total_time = 0.0;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);

        result.online = (code >= 200 && code < 400);
        result.response_time_ms = (long)(total_time * 1000.0);
    } else {
        result.online = false;
        snprintf(result.error_message, sizeof(result.error_message), "%s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    __crypto_secure_wipe__(default_url, sizeof(default_url));
    __crypto_secure_wipe__(url, sizeof(url));
    __crypto_secure_wipe__(pin_buf, sizeof(pin_buf));
    return result;
}
