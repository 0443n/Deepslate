
#ifndef MCPSP_CLIENT_ENTITY_RENDERER_H
#define MCPSP_CLIENT_ENTITY_RENDERER_H

extern float g_relBaseX, g_relBaseY, g_relBaseZ;

class Entity;
class EntityRenderDispatcher;

// The shadow sheet is read once, off the frame that first needs it.
void entityShadowPreload(void);

void renderEntityShadow(float x, float y, float z, float off, float radius, float pow);

void renderEntityFlame(float x, float y, float z, float bbY0, float w, float h);

class EntityRenderer {
public:
    virtual ~EntityRenderer() {}

    virtual void render(Entity* entity, float x, float y, float z, float rot, float a) = 0;
    virtual void init(EntityRenderDispatcher* ) {}

    // Loading a texture means libpng, malloc and the memory stick, and the
    // render pass runs all three with a display list open.
    virtual void preload() {}

    void postRender(Entity* entity, float x, float y, float z, float a);

protected:

    float shadowRadius;
    float shadowStrength;
    EntityRenderer() : shadowRadius(0.0f), shadowStrength(1.0f) {}
};

#endif
