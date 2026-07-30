
#ifndef MCPSP_WORLD_EXTERNAL_SERVERS_H
#define MCPSP_WORLD_EXTERNAL_SERVERS_H

#define MCPSP_MAX_SERVERS 16

struct ExternalServer {
    int  id;
    char name[32];
    char addr[64];
    int  port;
};

struct ExternalServerList {
    ExternalServer servers[MCPSP_MAX_SERVERS];
    int count;
};

void externalServersLoad(ExternalServerList* list);
void externalServersSave(const ExternalServerList* list);

bool externalServersAdd(ExternalServerList* list, const char* name, const char* addr, int port);

bool externalServersRemove(ExternalServerList* list, int index);

void externalServersLoadFrom(ExternalServerList* list, const char* path);
void externalServersSaveTo(const ExternalServerList* list, const char* path);

#endif
