
#ifndef MCPSP_PLATFORM_PNG_LOADER_H
#define MCPSP_PLATFORM_PNG_LOADER_H

struct PngReader;

PngReader* pngOpen(const char* path, int* outW, int* outH);

bool pngReadRow(PngReader* r, unsigned char* rgbaRow);
void pngClose(PngReader* r);

#endif
