#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <Globals.h>
#include <downloader.h>
#include <mfil.h>
#include <core/logger.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define mfil_strcasecmp _stricmp
#else
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#define mfil_strcasecmp strcasecmp
#endif

/* MD5 context and transformation routines (RFC 1321) */
typedef struct {
    uint32_t                                                                state[4];
    uint32_t                                                                count[2];
    uint8_t                                                                 buffer[64];
} MD5Context;

#define S11                                                             7
#define S12                                                             12
#define S13                                                             17
#define S14                                                             22
#define S21                                                             5
#define S22                                                             9
#define S23                                                             14
#define S24                                                             20
#define S31                                                             4
#define S32                                                             11
#define S33                                                             16
#define S34                                                             23
#define S41                                                             6
#define S42                                                             10
#define S43                                                             15
#define S44                                                             21

#define MD5_F(x, y, z)                                                  (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z)                                                  (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z)                                                  ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z)                                                  ((y) ^ ((x) | (~z)))

#define MD5_ROTATE_LEFT(x, n)                                           (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) {                                  \
    (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac);                 \
    (a) = MD5_ROTATE_LEFT((a), (s));                                    \
    (a) += (b);                                                         \
}

#define MD5_GG(a, b, c, d, x, s, ac) {                                  \
    (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac);                 \
    (a) = MD5_ROTATE_LEFT((a), (s));                                    \
    (a) += (b);                                                         \
}

#define MD5_HH(a, b, c, d, x, s, ac) {                                  \
    (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac);                 \
    (a) = MD5_ROTATE_LEFT((a), (s));                                    \
    (a) += (b);                                                         \
}

#define MD5_II(a, b, c, d, x, s, ac) {                                  \
    (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac);                 \
    (a) = MD5_ROTATE_LEFT((a), (s));                                    \
    (a) += (b);                                                         \
}

static void __copy_string__(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }

    if (!src) {
        dest[0]                     = '\0';
        return;
    }

    size_t len                      = strlen(src);

    if (len >= dest_size) {
        len                         = dest_size - 1;
    }

    memcpy(dest, src, len);
    dest[len]                       = '\0';
}

static void __md5_transform__(uint32_t state[4], const uint8_t *block) {
    uint32_t a                      = state[0];
    uint32_t b                      = state[1];
    uint32_t c                      = state[2];
    uint32_t d                      = state[3];
    uint32_t x[16];

    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        x[i]                        = ((uint32_t)block[j]) | (((uint32_t)block[j + 1]) << 8) |
                                      (((uint32_t)block[j + 2]) << 16) | (((uint32_t)block[j + 3]) << 24);
    }

    /* Round 1 */
    MD5_FF(a, b, c, d, x[ 0], S11, 0xd76aa478);
    MD5_FF(d, a, b, c, x[ 1], S12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], S13, 0x242070db);
    MD5_FF(b, c, d, a, x[ 3], S14, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4], S11, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[ 5], S12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], S13, 0xa8304613);
    MD5_FF(b, c, d, a, x[ 7], S14, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8], S11, 0x698098d8);
    MD5_FF(d, a, b, c, x[ 9], S12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], S13, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], S14, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12], S11, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], S12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], S13, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], S14, 0x49b40821);

    /* Round 2 */
    MD5_GG(a, b, c, d, x[ 1], S21, 0xf61e2562);
    MD5_GG(d, a, b, c, x[ 6], S22, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], S23, 0x265e5a51);
    MD5_GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5], S21, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10], S22, 0x02441453);
    MD5_GG(c, d, a, b, x[15], S23, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9], S21, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14], S22, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], S23, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[ 8], S24, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13], S21, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], S23, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

    /* Round 3 */
    MD5_HH(a, b, c, d, x[ 5], S31, 0xfffa3942);
    MD5_HH(d, a, b, c, x[ 8], S32, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], S33, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], S34, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1], S31, 0xa4beea44);
    MD5_HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], S34, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13], S31, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[ 0], S32, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], S33, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[ 6], S34, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9], S31, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], S32, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[ 2], S34, 0xc4ac5665);

    /* Round 4 */
    MD5_II(a, b, c, d, x[ 0], S41, 0xf4292244);
    MD5_II(d, a, b, c, x[ 7], S42, 0x432aff97);
    MD5_II(c, d, a, b, x[14], S43, 0xab9423a7);
    MD5_II(b, c, d, a, x[ 5], S44, 0xfc93a039);
    MD5_II(a, b, c, d, x[12], S41, 0x655b59c3);
    MD5_II(d, a, b, c, x[ 3], S42, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], S43, 0xffeff47d);
    MD5_II(b, c, d, a, x[ 1], S44, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8], S41, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], S43, 0xa3014314);
    MD5_II(b, c, d, a, x[13], S44, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4], S41, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], S42, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[ 9], S44, 0xeb86d391);

    state[0]                       += a;
    state[1]                       += b;
    state[2]                       += c;
    state[3]                       += d;
}

static void __md5_init__(MD5Context *context) {
    context->count[0]               = 0;
    context->count[1]               = 0;
    context->state[0]               = 0x67452301;
    context->state[1]               = 0xefcdab89;
    context->state[2]               = 0x98badcfe;
    context->state[3]               = 0x10325476;
}

static void __md5_update__(MD5Context *context, const uint8_t *input, size_t input_len) {
    size_t index                    = (size_t)((context->count[0] >> 3) & 0x3F);

    if ((context->count[0] += ((uint32_t)input_len << 3)) < ((uint32_t)input_len << 3)) {
        context->count[1]++;
    }
    context->count[1]              += ((uint32_t)input_len >> 29);

    size_t part_len                 = 64 - index;
    size_t i                        = 0;

    if (input_len >= part_len) {
        memcpy(&context->buffer[index], input, part_len);
        __md5_transform__(context->state, context->buffer);

        for (i = part_len; i + 63 < input_len; i += 64) {
            __md5_transform__(context->state, &input[i]);
        }
        index                       = 0;
    }

    memcpy(&context->buffer[index], &input[i], input_len - i);
}

static void __md5_final__(uint8_t digest[16], MD5Context *context) {
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];

    for (int i = 0; i < 4; i++) {
        bits[i]                     = (uint8_t)((context->count[0] >> (i * 8)) & 0xFF);
        bits[i + 4]                 = (uint8_t)((context->count[1] >> (i * 8)) & 0xFF);
    }

    size_t index                    = (size_t)((context->count[0] >> 3) & 0x3F);
    size_t pad_len                  = (index < 56) ? (56 - index) : (120 - index);

    __md5_update__(context, padding, pad_len);
    __md5_update__(context, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4]               = (uint8_t)((context->state[i]) & 0xFF);
        digest[i * 4 + 1]           = (uint8_t)((context->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2]           = (uint8_t)((context->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3]           = (uint8_t)((context->state[i] >> 24) & 0xFF);
    }
}

/* Ensure directory components exist along a given file path */
static void __make_parent_dirs__(const char *filepath) {
    if (!filepath || !*filepath) {
        return;
    }

    char path[2048];
    size_t len                      = strlen(filepath);

    if (len >= sizeof(path)) {
        return;
    }

    memcpy(path, filepath, len + 1);

    for (char *p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char temp               = *p;
            *p                      = '\0';
#if defined(_WIN32)
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
            *p                      = temp;
        }
    }
}

void __mfil_free_master__(MfilMaster *master) {
    if (!master) {
        return;
    }

    if (master->servers) {
        free(master->servers);
        master->servers             = NULL;
    }

    master->server_count            = 0;
    master->version                 = 0;
    master->manifest_partial[0]     = '\0';
    master->client_partial[0]       = '\0';
}

void __mfil_free_manifest__(MfilManifest *manifest) {
    if (!manifest) {
        return;
    }

    if (manifest->serverpaths) {
        free(manifest->serverpaths);
        manifest->serverpaths       = NULL;
    }
    manifest->serverpath_count      = 0;

    if (manifest->files) {
        free(manifest->files);
        manifest->files             = NULL;
    }
    manifest->file_count            = 0;
    manifest->version               = 0;
}

bool __mfil_parse_master_string__(const char *content, size_t length, MfilMaster *master) {
    if (!content || !master) {
        return false;
    }

    memset(master, 0, sizeof(*master));

    size_t server_cap               = 0;
    MfilServer *current_server      = NULL;

    const char *ptr                 = content;
    const char *end                 = content + length;

    while (ptr < end) {
        const char *line_start      = ptr;
        while (ptr < end && *ptr != '\n') {
            ptr++;
        }
        const char *line_end        = ptr;
        if (ptr < end && *ptr == '\n') {
            ptr++;
        }

        bool is_indented            = false;
        if (line_start < line_end && (*line_start == '\t' || *line_start == ' ')) {
            is_indented             = true;
        }

        const char *key_start       = line_start;
        while (key_start < line_end && (*key_start == ' ' || *key_start == '\t')) {
            key_start++;
        }
        while (line_end > key_start && (line_end[-1] == '\r' || line_end[-1] == '\n' || line_end[-1] == ' ')) {
            line_end--;
        }

        if (key_start >= line_end) {
            continue;
        }

        const char *eq              = NULL;
        for (const char *p = key_start; p < line_end; p++) {
            if (*p == '=') {
                eq                  = p;
                break;
            }
        }
        if (!eq) {
            continue;
        }

        const char *key_end         = eq;
        while (key_end > key_start && key_end[-1] == ' ') {
            key_end--;
        }

        char key[128];
        size_t klen                 = (size_t)(key_end - key_start);
        if (klen >= sizeof(key)) {
            klen                    = sizeof(key) - 1;
        }
        memcpy(key, key_start, klen);
        key[klen]                   = '\0';

        const char *val_start       = eq + 1;
        char val[512];
        size_t vlen                 = (size_t)(line_end - val_start);
        if (vlen >= sizeof(val)) {
            vlen                    = sizeof(val) - 1;
        }
        memcpy(val, val_start, vlen);
        val[vlen]                   = '\0';

        if (is_indented) {
            if (current_server && strcmp(key, "location") == 0) {
                __copy_string__(current_server->location, sizeof(current_server->location), val);
            }
        } else {
            current_server          = NULL;
            if (strcmp(key, "version") == 0) {
                master->version     = atoi(val);
            } else if (strcmp(key, "server") == 0) {
                if (master->server_count >= server_cap) {
                    size_t new_cap  = server_cap == 0 ? 4 : server_cap * 2;
                    MfilServer *tmp = (MfilServer *)realloc(master->servers, new_cap * sizeof(MfilServer));
                    if (!tmp) {
                        __mfil_free_master__(master);
                        return false;
                    }
                    master->servers = tmp;
                    server_cap      = new_cap;
                }
                current_server      = &master->servers[master->server_count++];
                memset(current_server, 0, sizeof(*current_server));
                __copy_string__(current_server->name, sizeof(current_server->name), val);
            } else if (strcmp(key, "manifest_partial") == 0) {
                __copy_string__(master->manifest_partial, sizeof(master->manifest_partial), val);
            } else if (strcmp(key, "client_partial") == 0) {
                __copy_string__(master->client_partial, sizeof(master->client_partial), val);
            }
        }
    }

    return true;
}

bool __mfil_parse_master_file__(const char *filepath, MfilMaster *master) {
    TRACE_SCOPE();
    if (!filepath || !master) {
        return false;
    }

    FILE *f                         = fopen(filepath, "rb");

    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long size                       = ftell(f);

    if (size < 0) {
        fclose(f);
        return false;
    }

    rewind(f);

    char *buffer                    = (char *)malloc((size_t)size + 1);

    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_bytes               = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    buffer[read_bytes]              = '\0';

    bool success                    = __mfil_parse_master_string__(buffer, read_bytes, master);
    free(buffer);

    return success;
}

bool __mfil_parse_manifest_string__(const char *content, size_t length, MfilManifest *manifest) {
    if (!content || !manifest) {
        return false;
    }

    memset(manifest, 0, sizeof(*manifest));

    size_t sp_cap                   = 0;
    size_t file_cap                 = 0;
    MfilServerPath *current_sp      = NULL;
    MfilFile *current_file          = NULL;

    const char *ptr                 = content;
    const char *end                 = content + length;

    while (ptr < end) {
        const char *line_start      = ptr;
        while (ptr < end && *ptr != '\n') {
            ptr++;
        }
        const char *line_end        = ptr;
        if (ptr < end && *ptr == '\n') {
            ptr++;
        }

        bool is_indented            = false;
        if (line_start < line_end && (*line_start == '\t' || *line_start == ' ')) {
            is_indented             = true;
        }

        const char *key_start       = line_start;
        while (key_start < line_end && (*key_start == ' ' || *key_start == '\t')) {
            key_start++;
        }
        while (line_end > key_start && (line_end[-1] == '\r' || line_end[-1] == '\n' || line_end[-1] == ' ')) {
            line_end--;
        }

        if (key_start >= line_end) {
            continue;
        }

        const char *eq              = NULL;
        for (const char *p = key_start; p < line_end; p++) {
            if (*p == '=') {
                eq                  = p;
                break;
            }
        }
        if (!eq) {
            continue;
        }

        const char *key_end         = eq;
        while (key_end > key_start && key_end[-1] == ' ') {
            key_end--;
        }

        char key[128];
        size_t klen                 = (size_t)(key_end - key_start);
        if (klen >= sizeof(key)) {
            klen                    = sizeof(key) - 1;
        }
        memcpy(key, key_start, klen);
        key[klen]                   = '\0';

        const char *val_start       = eq + 1;
        char val[512];
        size_t vlen                 = (size_t)(line_end - val_start);
        if (vlen >= sizeof(val)) {
            vlen                    = sizeof(val) - 1;
        }
        memcpy(val, val_start, vlen);
        val[vlen]                   = '\0';

        if (is_indented) {
            if (current_sp) {
                if (strcmp(key, "path") == 0) {
                    __copy_string__(current_sp->path, sizeof(current_sp->path), val);
                }
            } else if (current_file) {
                if (strcmp(key, "name") == 0) {
                    __copy_string__(current_file->name, sizeof(current_file->name), val);
                } else if (strcmp(key, "size") == 0) {
                    current_file->size = (int64_t)strtoll(val, NULL, 10);
                } else if (strcmp(key, "checksum") == 0) {
                    __copy_string__(current_file->checksum, sizeof(current_file->checksum), val);
                } else if (strcmp(key, "fileversion") == 0) {
                    current_file->fileversion = atoi(val);
                } else if (strcmp(key, "flags") == 0) {
                    current_file->flags = atoi(val);
                } else if (strcmp(key, "path") == 0) {
                    __copy_string__(current_file->path, sizeof(current_file->path), val);
                }
            }
        } else {
            current_sp              = NULL;
            current_file            = NULL;
            if (strcmp(key, "version") == 0) {
                manifest->version   = atoi(val);
            } else if (strcmp(key, "serverpath") == 0) {
                if (manifest->serverpath_count >= sp_cap) {
                    size_t new_cap  = sp_cap == 0 ? 4 : sp_cap * 2;
                    MfilServerPath *tmp = (MfilServerPath *)realloc(manifest->serverpaths, new_cap * sizeof(MfilServerPath));
                    if (!tmp) {
                        __mfil_free_manifest__(manifest);
                        return false;
                    }
                    manifest->serverpaths = tmp;
                    sp_cap          = new_cap;
                }
                current_sp          = &manifest->serverpaths[manifest->serverpath_count++];
                memset(current_sp, 0, sizeof(*current_sp));
                __copy_string__(current_sp->name, sizeof(current_sp->name), val);
            } else if (strcmp(key, "file") == 0) {
                if (manifest->file_count >= file_cap) {
                    size_t new_cap  = file_cap == 0 ? 64 : file_cap * 2;
                    MfilFile *tmp   = (MfilFile *)realloc(manifest->files, new_cap * sizeof(MfilFile));
                    if (!tmp) {
                        __mfil_free_manifest__(manifest);
                        return false;
                    }
                    manifest->files = tmp;
                    file_cap        = new_cap;
                }
                current_file        = &manifest->files[manifest->file_count++];
                memset(current_file, 0, sizeof(*current_file));
                __copy_string__(current_file->file, sizeof(current_file->file), val);
                __copy_string__(current_file->name, sizeof(current_file->name), val);
            }
        }
    }

    return true;
}

bool __mfil_parse_manifest_file__(const char *filepath, MfilManifest *manifest) {
    TRACE_SCOPE();
    if (!filepath || !manifest) {
        return false;
    }

    FILE *f                         = fopen(filepath, "rb");

    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long size                       = ftell(f);

    if (size < 0) {
        fclose(f);
        return false;
    }

    rewind(f);

    char *buffer                    = (char *)malloc((size_t)size + 1);

    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_bytes               = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    buffer[read_bytes]              = '\0';

    bool success                    = __mfil_parse_manifest_string__(buffer, read_bytes, manifest);
    free(buffer);

    return success;
}

const char *__mfil_get_master_location__(const MfilMaster *master, const char *server_name) {
    if (!master || master->server_count == 0 || !master->servers) {
        return NULL;
    }

    if (server_name && *server_name) {
        for (size_t i = 0; i < master->server_count; i++) {
            if (strcmp(master->servers[i].name, server_name) == 0) {
                return master->servers[i].location;
            }
        }
        return NULL;
    }

    return master->servers[0].location;
}

const char *__mfil_get_serverpath__(const MfilManifest *manifest, const char *path_key) {
    if (!manifest || !path_key || !manifest->serverpaths) {
        return "";
    }

    for (size_t i = 0; i < manifest->serverpath_count; i++) {
        if (strcmp(manifest->serverpaths[i].name, path_key) == 0) {
            return manifest->serverpaths[i].path;
        }
    }

    return "";
}

bool __mfil_build_url__(const char *base_location, const char *subpath, const char *filename, char *out_url, size_t out_url_size) {
    if (!base_location || !filename || !out_url || out_url_size == 0) {
        return false;
    }

    size_t base_len                 = strlen(base_location);
    size_t written                  = 0;

    if (base_len >= out_url_size) {
        return false;
    }

    memcpy(out_url, base_location, base_len);
    written                         = base_len;

    /* Append subpath if present and non-empty */
    if (subpath && *subpath) {
        bool base_has_slash         = (written > 0 && out_url[written - 1] == '/');
        bool sub_has_slash          = (subpath[0] == '/');

        if (!base_has_slash && !sub_has_slash) {
            if (written + 1 >= out_url_size) {
                return false;
            }
            out_url[written++]      = '/';
        } else if (base_has_slash && sub_has_slash) {
            subpath++;
        }

        size_t sub_len              = strlen(subpath);
        if (written + sub_len >= out_url_size) {
            return false;
        }
        memcpy(out_url + written, subpath, sub_len);
        written                    += sub_len;
    }

    /* Append filename */
    bool current_has_slash          = (written > 0 && out_url[written - 1] == '/');
    bool file_has_slash             = (filename[0] == '/');

    if (!current_has_slash && !file_has_slash) {
        if (written + 1 >= out_url_size) {
            return false;
        }
        out_url[written++]          = '/';
    } else if (current_has_slash && file_has_slash) {
        filename++;
    }

    size_t file_len                 = strlen(filename);
    if (written + file_len >= out_url_size) {
        return false;
    }
    memcpy(out_url + written, filename, file_len);
    written                        += file_len;

    out_url[written]                = '\0';
    return true;
}

bool __mfil_calculate_file_checksum__(const char *filepath, char out_md5[33]) {
    TRACE_SCOPE();
    if (!filepath || !out_md5) {
        return false;
    }

    FILE *f                         = fopen(filepath, "rb");
    if (!f) {
        return false;
    }

    MD5Context ctx;
    __md5_init__(&ctx);

    uint8_t buffer[65536];
    size_t bytes_read               = fread(buffer, 1, sizeof(buffer), f);

    while (bytes_read > 0) {
        __md5_update__(&ctx, buffer, bytes_read);
        bytes_read                  = fread(buffer, 1, sizeof(buffer), f);
    }

    fclose(f);

    uint8_t digest[16];
    __md5_final__(digest, &ctx);

    for (int i = 0; i < 16; i++) {
        snprintf(out_md5 + i * 2, 3, "%02x", digest[i]);
    }
    out_md5[32]                     = '\0';
    return true;
}

bool __mfil_verify_file_checksum__(const char *filepath, const char *expected_md5) {
    TRACE_SCOPE();
    if (!filepath || !expected_md5 || !*expected_md5) {
        return false;
    }

    char actual_hex[33];
    if (!__mfil_calculate_file_checksum__(filepath, actual_hex)) {
        return false;
    }

    return (mfil_strcasecmp(actual_hex, expected_md5) == 0);
}

CURLStatus __mfil_download_file__(const char *base_location, const MfilManifest *manifest, const MfilFile *file, const char *destination_dir, const CURLOptions *options, CURLResult *result) {
    TRACE_SCOPE();
    if (!base_location || !file) {
        return CURLDL_ERR_BAD_ARG;
    }

    const char *subpath             = __mfil_get_serverpath__(manifest, file->path);
    char url[2048];

    if (!__mfil_build_url__(base_location, subpath, file->name, url, sizeof(url))) {
        return CURLDL_ERR_BAD_ARG;
    }

    char full_dest_path[2048];

    if (destination_dir && *destination_dir) {
        size_t dlen                 = strlen(destination_dir);
        bool d_has_slash            = (destination_dir[dlen - 1] == '/' || destination_dir[dlen - 1] == '\\');
        bool f_has_slash            = (file->name[0] == '/' || file->name[0] == '\\');
        const char *fn              = file->name;

        if (d_has_slash && f_has_slash) {
            fn++;
        }

        __copy_string__(full_dest_path, sizeof(full_dest_path), destination_dir);
        size_t cur_len              = strlen(full_dest_path);

        if (!d_has_slash && !f_has_slash) {
            if (cur_len + 1 < sizeof(full_dest_path)) {
                full_dest_path[cur_len]     = '/';
                full_dest_path[cur_len + 1] = '\0';
                cur_len++;
            }
        }

        if (cur_len < sizeof(full_dest_path)) {
            __copy_string__(full_dest_path + cur_len, sizeof(full_dest_path) - cur_len, fn);
        }
    } else {
        __copy_string__(full_dest_path, sizeof(full_dest_path), file->name);
    }

    __make_parent_dirs__(full_dest_path);

    CURLOptions opt;

    if (options) {
        opt                         = *options;
    } else {
        __init_options__(&opt, url);
    }

    opt.url                         = url;
    opt.output_path                 = full_dest_path;

    CURLStatus status               = __download__(&opt, result);

    if (status == CURLDL_OK || status == CURLDL_ALREADY_COMPLETE) {
        if (file->checksum[0] != '\0') {
            if (!__mfil_verify_file_checksum__(full_dest_path, file->checksum)) {
                if (result) {
                    result->status  = CURLDL_ERR_FILE_IO;
                    snprintf(result->error_message, sizeof(result->error_message),
                             "MD5 checksum verification failed for %.*s", 400, full_dest_path);
                }
                return CURLDL_ERR_FILE_IO;
            }
        }
    }

    return status;
}

CURLStatus __mfil_download_partial__(const char *base_location, const char *partial_filename, const char *destination_path, const CURLOptions *options, CURLResult *result) {
    if (!base_location || !partial_filename || !destination_path) {
        return CURLDL_ERR_BAD_ARG;
    }

    char url[2048];

    if (!__mfil_build_url__(base_location, NULL, partial_filename, url, sizeof(url))) {
        return CURLDL_ERR_BAD_ARG;
    }

    __make_parent_dirs__(destination_path);

    CURLOptions opt;

    if (options) {
        opt                         = *options;
    } else {
        __init_options__(&opt, url);
    }

    opt.url                         = url;
    opt.output_path                 = destination_path;

    return __download__(&opt, result);
}

const MfilFile *__mfil_find_file__(const MfilManifest *manifest, const char *filename) {
    if (!manifest || !manifest->files || !filename || !*filename) {
        return NULL;
    }

    const char *target_base = filename;
    const char *slash = strrchr(filename, '/');
    const char *bslash = strrchr(filename, '\\');
    if (slash && (!bslash || slash > bslash)) {
        target_base = slash + 1;
    } else if (bslash) {
        target_base = bslash + 1;
    }

    for (size_t i = 0; i < manifest->file_count; i++) {
        const MfilFile *f = &manifest->files[i];
        if (mfil_strcasecmp(f->name, filename) == 0 ||
            mfil_strcasecmp(f->file, filename) == 0) {
            return f;
        }

        const char *f_base = f->name;
        const char *f_slash = strrchr(f->name, '/');
        const char *f_bslash = strrchr(f->name, '\\');
        if (f_slash && (!f_bslash || f_slash > f_bslash)) {
            f_base = f_slash + 1;
        } else if (f_bslash) {
            f_base = f_bslash + 1;
        }

        if (mfil_strcasecmp(f_base, target_base) == 0) {
            return f;
        }
    }

    return NULL;
}

bool __mfil_get_client_unmodified_checksum__(const char *client_filename, char out_checksum[64]) {
    if (!out_checksum) return false;
    out_checksum[0] = '\0';

    if (!client_filename || !*client_filename) {
        client_filename = "WoW.exe";
    }

    const char *master_candidates[] = {
        "WoW.mfil",
        "data/WoW.mfil"
    };

    MfilMaster master;
    bool has_master = false;
    for (size_t i = 0; i < sizeof(master_candidates)/sizeof(master_candidates[0]); i++) {
        if (__mfil_parse_master_file__(master_candidates[i], &master)) {
            has_master = true;
            break;
        }
    }

    const char *manifest_paths[8];
    size_t path_count = 0;

    char client_p1[512] = {0};
    char client_p2[512] = {0};
    char client_p3[512] = {0};
    char manifest_p1[512] = {0};
    char manifest_p2[512] = {0};

    if (has_master) {
        if (master.client_partial[0] != '\0') {
            snprintf(client_p1, sizeof(client_p1), "%s", master.client_partial);
            manifest_paths[path_count++] = client_p1;

            snprintf(client_p2, sizeof(client_p2), "www/wow-pod-retail/US/12340.direct/%s", master.client_partial);
            manifest_paths[path_count++] = client_p2;

            snprintf(client_p3, sizeof(client_p3), "data/%s", master.client_partial);
            manifest_paths[path_count++] = client_p3;
        }
        if (master.manifest_partial[0] != '\0') {
            snprintf(manifest_p1, sizeof(manifest_p1), "%s", master.manifest_partial);
            manifest_paths[path_count++] = manifest_p1;

            snprintf(manifest_p2, sizeof(manifest_p2), "www/wow-pod-retail/US/12340.direct/%s", master.manifest_partial);
            manifest_paths[path_count++] = manifest_p2;
        }
    }

    /* Fallback candidate locations for client manifests */
    manifest_paths[path_count++] = "www/wow-pod-retail/US/12340.direct/wow-client-12340-8ccae76521675ea1479f44048a865f1e.mfil";
    manifest_paths[path_count++] = "wow-client-12340-8ccae76521675ea1479f44048a865f1e.mfil";

    for (size_t i = 0; i < path_count; i++) {
        MfilManifest manifest;
        if (__mfil_parse_manifest_file__(manifest_paths[i], &manifest)) {
            const MfilFile *file = __mfil_find_file__(&manifest, client_filename);
            if (file && file->checksum[0] != '\0') {
                __copy_string__(out_checksum, 64, file->checksum);
                __mfil_free_manifest__(&manifest);
                if (has_master) __mfil_free_master__(&master);
                return true;
            }
            __mfil_free_manifest__(&manifest);
        }
    }

    if (has_master) {
        __mfil_free_master__(&master);
    }

    /* Standard default unmodified WoW 3.3.5a 12340 checksum as ultimate fallback */
    __copy_string__(out_checksum, 64, "45892bdedd0ad70aed4ccd22d9fb5984");
    return true;
}

