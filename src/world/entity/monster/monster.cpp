
#include "world/entity/monster/monster.h"
#include "world/entity/local_player.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/difficulty.h"
#include "world/inventory/inventory.h"
#include "client/renderer/particle.h"
#include <cmath>

Monster::Monster(Level* level)
:   PathfinderMob(level),
    attackDamage(2),
    frozenTicks(0)
{
    entityRendererId = ER_HUMANOID_RENDERER;
    heightOffset = 0.0f;
    walkingSpeed = 0.1f;
}

static const int DESPAWN_FROZEN = 5 * 60 * 20;

void Monster::tick() {
    PathfinderMob::tick();

    if (!level->isClientSide && level->getDifficulty() == Difficulty::PEACEFUL) { remove(); return; }

    if (level->player) {
        float dx = x - level->player->x, dy = y - level->player->y, dz = z - level->player->z;
        if (dx * dx + dy * dy + dz * dz > mobAiRange() * mobAiRange()) {
            if (++frozenTicks >= DESPAWN_FROZEN) remove();
        } else {
            frozenTicks = 0;
        }
    }
}

int Monster::getCreatureBaseType() const { return EntityTypes::BaseEnemy; }

bool Monster::canSpawn() {
    return isDarkEnoughToSpawn() && Mob::canSpawn();
}

bool Monster::isDarkEnoughToSpawn() {
    int xt = (int)floorf(x);
    int yt = (int)floorf(bb.y0);
    int zt = (int)floorf(z);

    int sky = lightSkyGet(level->w, xt, yt, zt) - g_skyDarken;
    if (sky < 0) sky = 0;
    if (sky > sharedRandom.nextInt(32)) return false;

    return lightRawAt(level->w, xt, yt, zt) <= sharedRandom.nextInt(8);
}

bool Monster::doHurtTarget(Entity* target) {
    swing();
    return target->hurt(this, attackDamage);
}
