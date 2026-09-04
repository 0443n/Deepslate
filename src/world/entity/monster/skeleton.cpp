#include "world/entity/monster/skeleton.h"
#include "world/entity/entity_types.h"
#include "world/entity/arrow.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include <cmath>

static const float SK_RADDEG = 180.0f / 3.14159265f;

Skeleton::Skeleton(Level* level)
:   Monster(level) {
    setSize(0.6f, 1.8f);
    entityRendererId = ER_SKELETON_RENDERER;
    attackDamage = 2;
    health = getMaxHealth();

}

int Skeleton::getEntityTypeId() const { return EntityTypes::IdSkeleton; }

void Skeleton::performRangedAttack(Entity* target, float dist) {
    float myEyeY = y + getHeadHeight();
    float ex = target->x;
    float ey = target->y + target->getHeadHeight() - 0.7f;
    float ez = target->z;
    float dx = ex - x, dz = ez - z;
    float horiz = sqrtf(dx * dx + dz * dz);

    float dy = (ey - (myEyeY - 0.1f)) + horiz * 0.2f;
    float yaw   = atan2f(dz, dx) * SK_RADDEG - 90.0f;
    float pitch = atan2f(dy, horiz) * SK_RADDEG;

    xRot = pitch;

    Arrow* a = new Arrow(level, x, myEyeY, z, yaw, pitch, 1.6f / 1.5f, false,
                          false,  32.0f);
    a->ownerId = entityId;
    level->addEntity(a);
}

void Skeleton::dropDeathLoot() {
    int arrows = sharedRandom.nextInt(3);
    for (int i = 0; i < arrows; i++) spawnAtLocation(ITEM_ARROW, 1);
    int bones = sharedRandom.nextInt(3);
    for (int i = 0; i < bones; i++) spawnAtLocation(ITEM_BONE, 1);
}
