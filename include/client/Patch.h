#ifndef __PATCH_H__
#define __PATCH_H__

#if defined(_WIN32)

#include <windows.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Utils.h"
#include <core/Version.h>

#define K_PATCHES_COUNT 3

struct PatchDetails {
    unsigned int virtual_address;
    const char* hex_bytes;
};

static const struct PatchDetails __patches__[3] = {
    {
        .virtual_address = 0x004DCCF0,                                                              /* lua_ScanDllStart */
        .hex_bytes = "B8"
                     "00000000"                                                                     /* mov eax, 0 */
                     "C3"                                                                           /* ret */
    },
    {
        .virtual_address = 0x004E5CB0,                                                              /* ScanDllStart */
        .hex_bytes = "B8"
                     "01000000"                                                                     /* mov eax, 1 */
                     "A3"
                     "74B4B600"                                                                     /* mov isScanDllFinished, eax */
                     "68"
                     "E05C4E00"                                                                     /* push offset aAwesomeWotlkLib_dll */
                     "E8"
                     "1C683800"                                                                     /* call _loadddll */
                     "83C4"
                     "04"                                                                           /* add esp, 4 */
                     "55"                                                                           /* push ebp */
                     "8BEC"                                                                         /* mov ebp, esp */
                     "E8"
                     "A110F2FF"                                                                     /* call 0x00406D70 */
                     "E9"
                     "045BF2FF"                                                                     /* jmp 0x0040B7D8 */
                     "CCCCCCCCCCCCCCCCCCCCCCCC"                                                     /* int3 x12 (padding) */
                     "4C654C61756E636865722E646C6C00"                                               /* "LeLauncher.dll" at 0x004E5CE0 */
    },
    {
        .virtual_address = 0x0040B7D0,                                                              /* StartAddress */
        .hex_bytes = "E9"
                     "DBA40D00"                                                                     /* jmp 0x004E5CB0 */
                     "909090"                                                                       /* nop x3 (pads out the 8 bytes replaced) */
    }
};

/*
 * Expected MD5 checksum of the modified (patched) WoW.exe (optional override).
 * The primary detection checks the official MFIL manifest checksum (e.g. from client_partial).
 * If the file matches the MFIL checksum, it is known to be unmodified and needs patching.
 * If it has been patched, its binary signature (LAA + DLL injection hook) or modified checksum is recognized.
 */
#ifndef EXPECTED_MODIFIED_WOW_MD5
#define EXPECTED_MODIFIED_WOW_MD5 "00000000000000000000000000000000"
#endif

bool __is_regular_file__(const char* path);
const char *__get__file_name__(const char *path);
void __build_sibling_file_path__(const char *base_path, const char *sibling_name, char *buffer, size_t size);
const char *__find_game_client_executable__(void);
bool __is_binary_patched__(const char *path);
bool __is_client_patched__(const char *path);
bool __check_and_patch_client__(const char *path);
bool __apply_patches__(const char *path);
bool __patch_main_thread__(int argc, char* argv[]);

#endif

#endif // __PATCH_H__
