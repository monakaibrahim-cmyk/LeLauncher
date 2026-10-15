#include <dat.h>
#include <Globals.h>
#include <core/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

Structure *gStructure = NULL;

void __generate_garbage__(unsigned char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (unsigned char)(rand() % 256);
    }
}

void __transform_buffer__(uint8_t *buffer, size_t size, bool forward) {
    for (size_t i = 0; i < size; i++) {
        uint8_t b = buffer[i];

        if (forward) {
            b = ((b & 0x0F) << 4) | ((b & 0xF0) >> 4);
            b = (b << 3) | (b >> 5);
            b = -b;
        } else {
            b = -b;
            b = (b >> 3) | (b << 5); /* Inverse rotate right by 3 */
            b = ((b & 0x0F) << 4) | ((b & 0xF0) >> 4);
        }

        buffer[i] = b;
    }
}

void __calculate_checksum__(Structure *s, char out_checksum[256]) {
    TRACE_SCOPE();
    if (!s || !out_checksum) {
        return;
    }

    memset(out_checksum, 0, 256);

    /* 64-bit FNV-1a hash across critical payload fields */
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t fnv_prime = 1099511628211ULL;

    uint8_t *p = (uint8_t *)&s->header.version;

    for (size_t i = 0; i < sizeof(int); i++) {
        hash ^= p[i];
        hash *= fnv_prime;
    }

    for (size_t i = 0; i < sizeof(s->credential.username); i++) {
        hash ^= (uint8_t)s->credential.username[i];
        hash *= fnv_prime;
    }

    for (size_t i = 0; i < sizeof(s->credential.password); i++) {
        hash ^= (uint8_t)s->credential.password[i];
        hash *= fnv_prime;
    }

    for (size_t i = 0; i < sizeof(s->credential.token); i++) {
        hash ^= (uint8_t)s->credential.token[i];
        hash *= fnv_prime;
    }

    p = (uint8_t *)&s->options.debug;

    for (size_t i = 0; i < sizeof(int); i++) {
        hash ^= p[i];
        hash *= fnv_prime;
    }

    snprintf(out_checksum, 256, "%016llx", (unsigned long long)hash);
}

void __default__(Structure *s) {
    if (!s) {
        return;
    }

    /* Free any previously allocated buffers */
    __free__(s);

    s->header.version = 1;

    /* Allocate and generate 8-byte garbage blocks */
    s->garbage_one.size = 8;
    s->garbage_one.values = (char *)malloc(s->garbage_one.size);

    if (s->garbage_one.values) {
        __generate_garbage__((unsigned char *)s->garbage_one.values, s->garbage_one.size);
    }

    s->garbage_two.size = 8;
    s->garbage_two.values = (char *)malloc(s->garbage_two.size);

    if (s->garbage_two.values) {
        __generate_garbage__((unsigned char *)s->garbage_two.values, s->garbage_two.size);
    }

    s->garbage_three.size = 8;
    s->garbage_three.values = (char *)malloc(s->garbage_three.size);

    if (s->garbage_three.values) {
        __generate_garbage__((unsigned char *)s->garbage_three.values, s->garbage_three.size);
    }

    memset(s->credential.username, 0, sizeof(s->credential.username));
    memset(s->credential.password, 0, sizeof(s->credential.password));
    memset(s->credential.token, 0, sizeof(s->credential.token));

    s->options.debug = 0;

    __calculate_checksum__(s, s->header.checksum);
}

void __free__(Structure *s) {
    if (!s) {
        return;
    }

    if (s->garbage_one.values) {
        free(s->garbage_one.values);
        s->garbage_one.values = NULL;
    }
    s->garbage_one.size = 0;

    if (s->garbage_two.values) {
        free(s->garbage_two.values);
        s->garbage_two.values = NULL;
    }
    s->garbage_two.size = 0;

    if (s->garbage_three.values) {
        free(s->garbage_three.values);
        s->garbage_three.values = NULL;
    }
    s->garbage_three.size = 0;
}

bool __save__(void) {
    TRACE_SCOPE();
    if (!gStructure) {
        LOG_WARN("DAT", "__save__ called with NULL gStructure");
        return false;
    }

    __calculate_checksum__(gStructure, gStructure->header.checksum);

    size_t total_size = sizeof(int) + 256 +
                        sizeof(uint32_t) + gStructure->garbage_one.size +
                        sizeof(gStructure->credential.username) + sizeof(gStructure->credential.password) +
                        sizeof(gStructure->credential.token) +
                        sizeof(uint32_t) + gStructure->garbage_two.size +
                        sizeof(int) +
                        sizeof(uint32_t) + gStructure->garbage_three.size;

    uint8_t *buffer = (uint8_t *)malloc(total_size);

    if (!buffer) {
        return false;
    }

    uint8_t *pointer = buffer;

    memcpy(pointer, &gStructure->header.version, sizeof(int));
    pointer += sizeof(int);

    memcpy(pointer, gStructure->header.checksum, 256);
    pointer += 256;

    uint32_t garbage_one_size = (uint32_t)gStructure->garbage_one.size;
    memcpy(pointer, &garbage_one_size, sizeof(uint32_t));
    pointer += sizeof(uint32_t);

    if (garbage_one_size > 0 && gStructure->garbage_one.values) {
        memcpy(pointer, gStructure->garbage_one.values, garbage_one_size);
        pointer += garbage_one_size;
    }

    memcpy(pointer, gStructure->credential.username, sizeof(gStructure->credential.username));
    pointer += sizeof(gStructure->credential.username);
    memcpy(pointer, gStructure->credential.password, sizeof(gStructure->credential.password));
    pointer += sizeof(gStructure->credential.password);
    memcpy(pointer, gStructure->credential.token, sizeof(gStructure->credential.token));
    pointer += sizeof(gStructure->credential.token);

    uint32_t garbage_two_size = (uint32_t)gStructure->garbage_two.size;
    memcpy(pointer, &garbage_two_size, sizeof(uint32_t));
    pointer += sizeof(uint32_t);

    if (garbage_two_size > 0 && gStructure->garbage_two.values) {
        memcpy(pointer, gStructure->garbage_two.values, garbage_two_size);
        pointer += garbage_two_size;
    }

    memcpy(pointer, &gStructure->options.debug, sizeof(int));
    pointer += sizeof(int);

    uint32_t garbage_three_size = (uint32_t)gStructure->garbage_three.size;
    memcpy(pointer, &garbage_three_size, sizeof(uint32_t));
    pointer += sizeof(uint32_t);

    if (garbage_three_size > 0 && gStructure->garbage_three.values) {
        memcpy(pointer, gStructure->garbage_three.values, garbage_three_size);
        pointer += garbage_three_size;
    }

    /* Obfuscate buffer before writing to disk */
    __forward__(buffer, total_size);

    FILE *f = fopen(__dat__, "wb");
    if (!f) {
        free(buffer);
        return false;
    }

    size_t written = fwrite(buffer, 1, total_size, f);
    fclose(f);
    free(buffer);

    return written == total_size;
}

bool __load__(void) {
    TRACE_SCOPE();
    if (!gStructure) {
        LOG_WARN("DAT", "__load__ called with NULL gStructure");
        return false;
    }

    FILE *f = fopen(__dat__, "rb");

    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long sz = ftell(f);

    if (sz <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    size_t file_size = (size_t)sz;
    uint8_t *buffer = (uint8_t *)malloc(file_size);

    if (!buffer) {
        fclose(f);
        return false;
    }

    if (fread(buffer, 1, file_size, f) != file_size) {
        free(buffer);
        fclose(f);
        return false;
    }

    fclose(f);

    /* De-obfuscate buffer */
    __backward__(buffer, file_size);

    /* Free any previously allocated buffers in gStructure before loading */
    __free__(gStructure);

    uint8_t *pointer = buffer;
    uint8_t *end = buffer + file_size;

    #define SAFE_READ(dst, size) do {                                       \
        if ((size_t)(end - pointer) < (size_t)(size)) {                     \
            __free__(gStructure);                                           \
            __default__(gStructure);                                        \
            free(buffer);                                                   \
            return false;                                                   \
        }                                                                   \
        memcpy((dst), pointer, (size));                                     \
        pointer += (size);                                                  \
    } while (0)

    SAFE_READ(&gStructure->header.version, sizeof(int));
    SAFE_READ(gStructure->header.checksum, 256);

    uint32_t g1_sz = 0;
    SAFE_READ(&g1_sz, sizeof(uint32_t));
    if ((size_t)g1_sz > (size_t)(end - pointer)) {
        __free__(gStructure);
        __default__(gStructure);
        free(buffer);
        return false;
    }
    gStructure->garbage_one.size = g1_sz;

    if (g1_sz > 0) {
        gStructure->garbage_one.values = (char *)malloc(g1_sz);

        if (!gStructure->garbage_one.values) {
            __free__(gStructure);
            __default__(gStructure);
            free(buffer);
            return false;
        }

        SAFE_READ(gStructure->garbage_one.values, g1_sz);
    } else {
        gStructure->garbage_one.values = NULL;
    }

    SAFE_READ(gStructure->credential.username, sizeof(gStructure->credential.username));
    SAFE_READ(gStructure->credential.password, sizeof(gStructure->credential.password));
    SAFE_READ(gStructure->credential.token, sizeof(gStructure->credential.token));
    gStructure->credential.username[sizeof(gStructure->credential.username) - 1] = '\0';
    gStructure->credential.password[sizeof(gStructure->credential.password) - 1] = '\0';
    gStructure->credential.token[sizeof(gStructure->credential.token) - 1] = '\0';

    uint32_t g2_sz = 0;
    SAFE_READ(&g2_sz, sizeof(uint32_t));
    if ((size_t)g2_sz > (size_t)(end - pointer)) {
        __free__(gStructure);
        __default__(gStructure);
        free(buffer);
        return false;
    }
    gStructure->garbage_two.size = g2_sz;

    if (g2_sz > 0) {
        gStructure->garbage_two.values = (char *)malloc(g2_sz);

        if (!gStructure->garbage_two.values) {
            __free__(gStructure);
            __default__(gStructure);
            free(buffer);
            return false;
        }

        SAFE_READ(gStructure->garbage_two.values, g2_sz);
    } else {
        gStructure->garbage_two.values = NULL;
    }

    SAFE_READ(&gStructure->options.debug, sizeof(int));

    uint32_t g3_sz = 0;
    SAFE_READ(&g3_sz, sizeof(uint32_t));
    if ((size_t)g3_sz > (size_t)(end - pointer)) {
        __free__(gStructure);
        __default__(gStructure);
        free(buffer);
        return false;
    }
    gStructure->garbage_three.size = g3_sz;

    if (g3_sz > 0) {
        gStructure->garbage_three.values = (char *)malloc(g3_sz);

        if (!gStructure->garbage_three.values) {
            __free__(gStructure);
            __default__(gStructure);
            free(buffer);
            return false;
        }

        SAFE_READ(gStructure->garbage_three.values, g3_sz);
    } else {
        gStructure->garbage_three.values = NULL;
    }

    #undef SAFE_READ
    free(buffer);

    /* Verify checksum */
    char calculated_checksum[256];
    __calculate_checksum__(gStructure, calculated_checksum);
    if (strncmp(gStructure->header.checksum, calculated_checksum, 256) != 0) {
        __free__(gStructure);
        __default__(gStructure);
        return false;
    }

    return true;
}

INITIALIZER(__seed__) {
    srand((unsigned int)time(NULL));
}

INITIALIZER(__allocate_dat_struct__) {
    srand((unsigned int)time(NULL));
    gStructure = (Structure *)calloc(1, sizeof(Structure));

    if (gStructure == NULL) {
        fprintf(stderr, "Failed to allocate Memory to Dat Struct!\n");
        return;
    }

    /* Attempt to load persisted container, or initialize with defaults */
    if (!__load__()) {
        __default__(gStructure);
    }
}

DESTRUCTOR(__deallocate_dat_struct__) {
    if (gStructure == NULL) {
        fprintf(stderr, "Failed to deallocate an unallocated Dat Struct!\n");
        return;
    }

    /* Save container to disk before exit */
    __save__();

    /* Cleanup memories of Struct and sub structs */
    __free__(gStructure);
    free(gStructure);
    gStructure = NULL;
}
