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

bool __patch_main_thread__(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const char *target = __find_game_client_executable__();
    if (!__apply_patches__(target)) {
        DWORD last_error = GetLastError();
        char* system_message = NULL;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&system_message, 0, NULL);
        LOG_ERROR("PATCH", "Failed to apply binary patches to %s: %s (code: %lu)",
                  target, system_message ? system_message : "Unknown error", (unsigned long)last_error);
        if (system_message) {
            LocalFree(system_message);
        }
        return false;
    }
    LOG_INFO("PATCH", "Binary patch successfully applied to %s (LAA + DLL loader hook)", target);
    return true;
}
#endif

