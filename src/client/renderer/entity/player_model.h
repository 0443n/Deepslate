
#ifndef MCPSP_CLIENT_PLAYER_MODEL_H
#define MCPSP_CLIENT_PLAYER_MODEL_H

void playerModelRender(float a);

// The skin and all ten armour sheets, read before any frame needs them.
void playerModelPreload(void);

void playerModelRenderPreview(float sx, float sy, float scale);

#endif
