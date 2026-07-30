
#include "world/level/storage/external_servers.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "platform/path.h"

static const char* kFileName = "external_servers.txt";

static bool copyTrimmed(char* dst, int n, const char* src) {
    int len = (int)strlen(src);
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\n' ||
                       src[len - 1] == '\r' || src[len - 1] == '\t'))
        len--;
    if (len <= 0) return false;
    if (len > n - 1) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return true;
}

void externalServersLoadFrom(ExternalServerList* list, const char* path) {
    list->count = 0;
    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[128];
    while (list->count < MCPSP_MAX_SERVERS && fgets(line, sizeof(line), f)) {
        if (strlen(line) <= 2) continue;

        char* tok[4];
        int n = 0;
        char* p = line;
        while (n < 4) {
            tok[n++] = p;
            char* colon = strchr(p, ':');
            if (!colon) break;
            *colon = '\0';
            p = colon + 1;
        }
        if (n != 4) continue;

        ExternalServer sv;
        memset(&sv, 0, sizeof(sv));
        char idBuf[16], portBuf[16];
        if (!copyTrimmed(idBuf, sizeof(idBuf), tok[0])) continue;
        if (!copyTrimmed(sv.name, sizeof(sv.name), tok[1])) continue;
        if (!copyTrimmed(sv.addr, sizeof(sv.addr), tok[2])) continue;
        if (!copyTrimmed(portBuf, sizeof(portBuf), tok[3])) continue;
        sv.id   = (int)strtol(idBuf, 0, 0);
        sv.port = (int)strtol(portBuf, 0, 0);
        if (sv.id == 0 || sv.port == 0) continue;

        list->servers[list->count++] = sv;
    }
    fclose(f);
}

void externalServersSaveTo(const ExternalServerList* list, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < list->count; i++) {
        const ExternalServer& sv = list->servers[i];
        fprintf(f, "%d:%s:%s:%d\n", sv.id, sv.name, sv.addr, sv.port);
    }
    fclose(f);
}

void externalServersLoad(ExternalServerList* list) {
    externalServersLoadFrom(list, assetPath(kFileName));
}

void externalServersSave(const ExternalServerList* list) {
    externalServersSaveTo(list, assetPath(kFileName));
}

bool externalServersAdd(ExternalServerList* list, const char* name, const char* addr, int port) {
    if (list->count >= MCPSP_MAX_SERVERS) return false;
    if (!name || !name[0] || !addr || !addr[0] || port <= 0) return false;

    int id = 1;
    for (;;) {
        bool taken = false;
        for (int i = 0; i < list->count; i++)
            if (list->servers[i].id == id) { taken = true; break; }
        if (!taken) break;
        id++;
    }

    ExternalServer& sv = list->servers[list->count];
    memset(&sv, 0, sizeof(sv));
    sv.id = id;
    sv.port = port;
    if (!copyTrimmed(sv.name, sizeof(sv.name), name)) return false;
    if (!copyTrimmed(sv.addr, sizeof(sv.addr), addr)) return false;
    list->count++;
    externalServersSave(list);
    return true;
}

bool externalServersRemove(ExternalServerList* list, int index) {
    if (index < 0 || index >= list->count) return false;
    for (int i = index; i < list->count - 1; i++)
        list->servers[i] = list->servers[i + 1];
    list->count--;
    externalServersSave(list);
    return true;
}
