#include "world/entity/monster/creeper.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/item/item.h"
#include "world/item/item_instance.h"
#include "world/entity/player.h"
#include "world/entity/local_player.h"
#include "world/inventory/inventory.h"
#include "client/renderer/particle.h"

Creeper::Creeper(Level* level)
:   Monster(level), swell(0), oldSwell(0), swellDir(-1) {
    setSize(0.6f, 1.8f);
    entityRendererId = ER_CREEPER_RENDERER;
    health = getMaxHealth();

}

void Creeper::explode() {
    remove();
    worldExplode(level->w, x, y + bbHeight * 0.5f, z, 2.4f);
}

int Creeper::getEntityTypeId() const { return EntityTypes::IdCreeper; }
int Creeper::getDeathLoot()          { return ITEM_GUNPOWDER; }

void Creeper::tick() {
    oldSwell = swell;
    Monster::tick();
    if (removed) return;

    // swellDir is 2 once flint and steel lit it, otherwise SwellGoal drives it.
    if (swell == 0 && swellDir > 0) level->playSound(this, "random.fuse", 1.0f, 0.5f);

    swell += (swellDir == 2) ? 1 : swellDir;
    if (swell < 0) swell = 0;
    if (swell >= MAX_SWELL) { swell = MAX_SWELL; explode(); }
}

float Creeper::getSwelling(float a) const {
    return (oldSwell + (swell - oldSwell) * a) / (float)(MAX_SWELL - 2);
}

bool Creeper::playerInteract() {
    ItemInstance* sel = g_level.player->inventory->getSelected();
    if (!sel || sel->id != ITEM_FLINT_AND_STEEL) return Monster::playerInteract();
    if (!level->isClientSide && swellDir != 2) {

        level->playSound(g_level.player->x + 0.5f, g_level.player->y + 0.5f,
                         g_level.player->z + 0.5f, "fire.ignite",
                         1.0f, sharedRandom.nextFloat() * 0.4f + 0.8f);
        swellDir = 2;
    }

    g_level.player->inventory->hurtSelected(1);
    return true;
}
