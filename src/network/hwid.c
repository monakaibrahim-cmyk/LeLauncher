#include <network/hwid.h>
#include <core/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

static char s_cached_hwid[128] = {0};

const char *__hwid_get__(void) {
    TRACE_SCOPE();
    if (s_cached_hwid[0] != '\0') {
        return s_cached_hwid;
    }

#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        char guid[128] = {0};
        DWORD size = sizeof(guid);
        if (RegQueryValueExA(hKey, "MachineGuid", NULL, NULL, (LPBYTE)guid, &size) == ERROR_SUCCESS) {
            strncpy(s_cached_hwid, guid, sizeof(s_cached_hwid) - 1);
        }
        RegCloseKey(hKey);
    }
#else
    FILE *f = fopen("/etc/machine-id", "r");
    if (!f) {
        f = fopen("/var/lib/dbus/machine-id", "r");
    }
    if (!f) {
        f = fopen("/sys/class/dmi/id/product_uuid", "r");
    }

    if (f) {
        if (fgets(s_cached_hwid, sizeof(s_cached_hwid), f)) {
            size_t len = strlen(s_cached_hwid);
            while (len > 0 && (s_cached_hwid[len - 1] == '\n' || s_cached_hwid[len - 1] == '\r' || isspace((unsigned char)s_cached_hwid[len - 1]))) {
                s_cached_hwid[--len] = '\0';
            }
        }
        fclose(f);
    }
#endif

    if (s_cached_hwid[0] == '\0') {
        strncpy(s_cached_hwid, "DEFAULT-HWID-0000-1111-2222-333344445555", sizeof(s_cached_hwid) - 1);
    }

    return s_cached_hwid;
}

void __network_get_client_ip__(char *out_ip, size_t max_len) {
    TRACE_SCOPE();
    if (!out_ip || max_len == 0) return;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        strncpy(out_ip, "127.0.0.1", max_len - 1);
        out_ip[max_len - 1] = '\0';
        return;
    }
#endif

    int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        strncpy(out_ip, "127.0.0.1", max_len - 1);
        out_ip[max_len - 1] = '\0';
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8");
    serv.sin_port = htons(53);

    /* Connect UDP socket to resolve route interface without sending packets */
    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        strncpy(out_ip, "127.0.0.1", max_len - 1);
    } else {
        struct sockaddr_in name;
        socklen_t namelen = sizeof(name);
        if (getsockname(sock, (struct sockaddr *)&name, &namelen) < 0) {
            strncpy(out_ip, "127.0.0.1", max_len - 1);
        } else {
            inet_ntop(AF_INET, &name.sin_addr, out_ip, (socklen_t)max_len);
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    out_ip[max_len - 1] = '\0';
}
