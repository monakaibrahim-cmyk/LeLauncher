#ifndef __DAT_H__
#define __DAT_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef struct {
    // padding 0x0
    size_t                                                                  size;
    char                                                                    *values;
    // padding 0x0
} Garbage;

typedef struct {
    // padding 0x0
    int                                                                     version;
    // double padding 0x0
    char                                                                    checksum[256];
} Header;

typedef struct {
    char                                                                    username[256];
    // padding 0x0
    char                                                                    password[256];
    // padding 0x0
    char                                                                    token[256];
} Credential;

typedef struct {
    int                                                                     debug;
} Options;

typedef struct {
    Header                                                                  header;
    // 8 - padding 0x0
    Garbage                                                                 garbage_one;
    // 8 - padding 0x0
    Credential                                                              credential;
    // 8 - padding 0x0
    Garbage                                                                 garbage_two;
    // 8 - padding 0x0
    Options                                                                 options;
    // 8 - padding 0x0
    Garbage                                                                 garbage_three;
} Structure;

/* Global instance of the Dat container structure */
extern Structure *gStructure;

void __generate_garbage__(unsigned char *buffer, size_t size);
void __transform_buffer__(uint8_t *buffer, size_t size, bool forward);

#define __dat__                                                                                 "Account.dat"
#define __forward__(x, y)                                                                       __transform_buffer__((uint8_t *)x, y, true)
#define __backward__(x, y)                                                                      __transform_buffer__((uint8_t *)x, y, false)

/* Functions implemented in src/dat.c */
void __calculate_checksum__(Structure *s, char checksum[256]);
void __default__(Structure *s);
void __free__(Structure *s);
bool __save__(void);
bool __load__(void);

#endif // __DAT_H__
