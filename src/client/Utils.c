#if defined(_WIN32)
#include <client/Utils.h>

bool __read_client__(const char *path, CharVector *content) {
    if (!path || !content) return false;

    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    rewind(file);

    CharVector_clear(content);
    if (size > 0) {
        if (!CharVector_reserve(content, (size_t)size)) {
            fclose(file); return false;
        }
        size_t bytes = fread(content->data, 1, (size_t)size, file);
        if (bytes != (size_t)size) {
            fclose(file);
            return false;
        }
        content->size = bytes;
    }
    fclose(file);
    return true;
}

bool __write_client__(const char *path, const CharVector *content) {
    if (!path || !content) return false;

    FILE *file = fopen(path, "wb");
    if (!file) return false;

    if (content->size > 0) {
        size_t written = fwrite(content->data, 1, content->size, file);
        if (written != content->size) {
            fclose(file);
            return false;
        }
    }
    fclose(file);
    return true;
}

unsigned int __virtual_address_2_raw_Offset__(const CharVector *image, unsigned int address) {
    if (!image || image->size < sizeof(IMAGE_DOS_HEADER)) return 0;

    const IMAGE_DOS_HEADER *dos_header = (const IMAGE_DOS_HEADER*)image->data;
    if (dos_header->e_lfanew < 0 ||
        (size_t)dos_header->e_lfanew + sizeof(IMAGE_NT_HEADERS) > image->size) {
        return 0;
    }

    const IMAGE_NT_HEADERS* nt_headers = (const IMAGE_NT_HEADERS*)&image->data[dos_header->e_lfanew];
    size_t sections_offset = (size_t)dos_header->e_lfanew +
                             offsetof(IMAGE_NT_HEADERS, OptionalHeader) +
                             nt_headers->FileHeader.SizeOfOptionalHeader;
    size_t section_count = nt_headers->FileHeader.NumberOfSections;

    if (sections_offset + section_count * sizeof(IMAGE_SECTION_HEADER) > image->size) {
        return 0;
    }

    const IMAGE_SECTION_HEADER* sections = (const IMAGE_SECTION_HEADER*)&image->data[sections_offset];
    for (size_t i = 0; i < section_count; ++i) {
        DWORD begin = nt_headers->OptionalHeader.ImageBase + sections[i].VirtualAddress;
        DWORD end = begin + sections[i].Misc.VirtualSize;
        if (address >= begin && address < end) {
            return address - begin + sections[i].PointerToRawData;
        }
    }
    return 0;
}

int __hex_digi_value__(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

bool __convert_hex_string_2_byte_array__(const char *hex, CharVector *result) {
    if (!hex || !result) return false;

    CharVector_clear(result);
    size_t len = strlen(hex);
    if (len % 2 != 0) return false;

    if (!CharVector_reserve(result, len / 2)) return false;

    for (size_t i = 0; i < len; i += 2) {
        const int high = __hex_digi_value__(hex[i]);
        const int low = __hex_digi_value__(hex[i + 1]);
        if (high < 0 || low < 0) return false;
        if (!CharVector_push_back(result, (char)((high << 4) | low))) return false;
    }
    return true;
}

bool __apply_laa__(CharVector *image) {
    if (!image || image->size < sizeof(IMAGE_DOS_HEADER)) return false;

    PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)image->data;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew < 0 ||
        (size_t)dos_header->e_lfanew + sizeof(IMAGE_NT_HEADERS) > image->size) {
        return false;
    }

    PIMAGE_NT_HEADERS nt_headers = (PIMAGE_NT_HEADERS)&image->data[dos_header->e_lfanew];
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) return false;

    nt_headers->FileHeader.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
    return true;
}

#endif

