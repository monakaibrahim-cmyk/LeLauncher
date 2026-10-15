#if defined(_WIN32)
#include <core/logger.h>
#include <client/Patch.h>

const char *__get__file_name__(const char *path) {
    const char* last_slash = strrchr(path, '\\');
    const char* last_forward_slash = strrchr(path, '/');
    if (last_forward_slash && (!last_slash || last_forward_slash > last_slash)) {
        last_slash = last_forward_slash;
    }
    return last_slash ? (last_slash + 1) : path;
}

void __build_sibling_file_path__(const char *base_path, const char *sibling_name, char *buffer, size_t size) {
    const char* filename = __get__file_name__(base_path);
    size_t directory_length = (size_t)(filename - base_path);
    if (directory_length == 0) {
        snprintf(buffer, size, "%s", sibling_name);
    } else {
        snprintf(buffer, size, "%.*s%s", (int)directory_length, base_path, sibling_name);
    }
}

bool __is_regular_file__(const char* path) {
    if (!path) return false;
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

const char *__find_game_client_executable__(void) {
    if (__is_regular_file__("WoW.exe")) return "WoW.exe";
    if (__is_regular_file__("wow.exe")) return "wow.exe";
    if (__is_regular_file__("Wow.exe")) return "Wow.exe";
    return "WoW.exe";
}

bool __apply_patches__(const char *path) {
    CharVector image = CharVector_create();
    if (!__read_client__(path, &image)) {
        CharVector_free(&image);
        return false;
    }

    if (!__apply_laa__(&image)) {
        CharVector_free(&image);
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }

    CharVector data = CharVector_create();
    for (size_t i = 0; i < K_PATCHES_COUNT; ++i) {
        unsigned int virtual_address = __patches__[i].virtual_address;
        const char* hex_bytes = __patches__[i].hex_bytes;

        unsigned int offset = __virtual_address_2_raw_Offset__(&image, virtual_address);
        if (offset == 0u) {
            CharVector_free(&data);
            CharVector_free(&image);
            SetLastError(ERROR_INVALID_ADDRESS);
            return false;
        }

        if (!__convert_hex_string_2_byte_array__(hex_bytes, &data) || CharVector_empty(&data)) {
            CharVector_free(&data);
            CharVector_free(&image);
            SetLastError(ERROR_BAD_ARGUMENTS);
            return false;
        }

        if ((size_t)offset + CharVector_size(&data) > CharVector_size(&image)) {
            CharVector_free(&data);
            CharVector_free(&image);
            SetLastError(ERROR_INVALID_ADDRESS);
            return false;
        }

        memcpy(image.data + offset, data.data, data.size);
    }
    CharVector_free(&data);
    bool written = __write_client__(path, &image);
    CharVector_free(&image);
    return written;
}

#include <network/mfil.h>

bool __is_binary_patched__(const char *path) {
    if (!path || !__is_regular_file__(path)) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    IMAGE_DOS_HEADER dos_header;
    if (fread(&dos_header, 1, sizeof(dos_header), f) != sizeof(dos_header)) {
        fclose(f);
        return false;
    }

    if (dos_header.e_magic != IMAGE_DOS_SIGNATURE || dos_header.e_lfanew <= 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, dos_header.e_lfanew, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    IMAGE_NT_HEADERS nt_headers;
    if (fread(&nt_headers, 1, sizeof(nt_headers), f) != sizeof(nt_headers)) {
        fclose(f);
        return false;
    }

    if (nt_headers.Signature != IMAGE_NT_SIGNATURE) {
        fclose(f);
        return false;
    }

    /* 1. Verify Large Address Aware (LAA) flag is set */
    if (!(nt_headers.FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE)) {
        fclose(f);
        return false;
    }

    /* 2. Locate raw offset of StartAddress hook (virtual address 0x0040B7D0) */
    size_t section_count = nt_headers.FileHeader.NumberOfSections;
    long sections_offset = dos_header.e_lfanew +
                           offsetof(IMAGE_NT_HEADERS, OptionalHeader) +
                           nt_headers.FileHeader.SizeOfOptionalHeader;

    if (fseek(f, sections_offset, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    DWORD hook_raw_offset = 0;
    const DWORD target_va = 0x0040B7D0;

    for (size_t i = 0; i < section_count; ++i) {
        IMAGE_SECTION_HEADER section;
        if (fread(&section, 1, sizeof(section), f) != sizeof(section)) {
            break;
        }
        DWORD begin = nt_headers.OptionalHeader.ImageBase + section.VirtualAddress;
        DWORD end = begin + section.Misc.VirtualSize;
        if (target_va >= begin && target_va < end) {
            hook_raw_offset = target_va - begin + section.PointerToRawData;
            break;
        }
    }

    if (hook_raw_offset == 0) {
        fclose(f);
        return false;
    }

    /* 3. Read 8 bytes at hook_raw_offset: expected "E9 DB A4 0D 00 90 90 90" */
    if (fseek(f, hook_raw_offset, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    unsigned char hook_bytes[8];
    if (fread(hook_bytes, 1, sizeof(hook_bytes), f) != sizeof(hook_bytes)) {
        fclose(f);
        return false;
    }
    fclose(f);

    const unsigned char expected_hook[8] = { 0xE9, 0xDB, 0xA4, 0x0D, 0x00, 0x90, 0x90, 0x90 };
    return (memcmp(hook_bytes, expected_hook, sizeof(expected_hook)) == 0);
}

bool __is_client_patched__(const char *path) {
    if (!path) path = __find_game_client_executable__();
    if (!__is_regular_file__(path)) return false;

    char current_md5[33] = {0};
    if (!__mfil_calculate_file_checksum__(path, current_md5)) {
        LOG_WARN("PATCH", "Failed to compute checksum for %s", path);
        return false;
    }

    char mfil_unmodified_md5[64] = {0};
    __mfil_get_client_unmodified_checksum__(path, mfil_unmodified_md5);

    LOG_INFO("PATCH", "%s checksum: %s | MFIL unmodified checksum: %s",
             path, current_md5, mfil_unmodified_md5);

    /* 1. If it matches the MFIL checksum, it is DEFINITELY an unmodified client -> needs patching */
    if (_stricmp(current_md5, mfil_unmodified_md5) == 0) {
        LOG_INFO("PATCH", "%s matches MFIL client checksum (%s): clean unmodified executable detected, patching required.",
                 path, mfil_unmodified_md5);
        return false;
    }

    /* 2. If user defined a specific modified MD5, check it */
    if (strcmp(EXPECTED_MODIFIED_WOW_MD5, "00000000000000000000000000000000") != 0 &&
        _stricmp(current_md5, EXPECTED_MODIFIED_WOW_MD5) == 0) {
        LOG_INFO("PATCH", "%s matches EXPECTED_MODIFIED_WOW_MD5 (%s). Client is patched!",
                 path, EXPECTED_MODIFIED_WOW_MD5);
        return true;
    }

    /* 3. Check internal binary patch signature (LAA header flag + StartAddress hook bytes) */
    if (__is_binary_patched__(path)) {
        LOG_INFO("PATCH", "%s binary signature verified (LAA + DLL injection hook present). Client is patched!", path);
        return true;
    }

    LOG_INFO("PATCH", "%s differs from MFIL checksum but lacks patch signature. Patching required.", path);
    return false;
}

bool __check_and_patch_client__(const char *path) {
    if (!path) path = __find_game_client_executable__();
    if (!__is_regular_file__(path)) {
        LOG_WARN("PATCH", "Game client not found at '%s'", path ? path : "null");
        return false;
    }

    /* Check file checksum: if it matches, it's already modified */
    if (__is_client_patched__(path)) {
        LOG_INFO("PATCH", "Game client %s is verified and up-to-date. Skipping patch step.", path);
        return true;
    }

    /* Not matched -> unmodified WoW.exe -> needs patching */
    LOG_INFO("PATCH", "Applying patches (LAA + DLL injection hook) to %s...", path);
    if (!__apply_patches__(path)) {
        DWORD last_error = GetLastError();
        char* system_message = NULL;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&system_message, 0, NULL);
        LOG_ERROR("PATCH", "Failed to apply binary patches to %s: %s (code: %lu)",
                  path, system_message ? system_message : "Unknown error", (unsigned long)last_error);
        if (system_message) {
            LocalFree(system_message);
        }
        return false;
    }

    /* Calculate and report new checksum after patching */
    char new_md5[33] = {0};
    if (__mfil_calculate_file_checksum__(path, new_md5)) {
        LOG_INFO("PATCH", "Successfully patched %s! New modified checksum: %s", path, new_md5);
    } else {
        LOG_INFO("PATCH", "Successfully patched %s!", path);
    }
    return true;
}

bool __patch_main_thread__(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const char *target = __find_game_client_executable__();
    return __check_and_patch_client__(target);
}
#endif

