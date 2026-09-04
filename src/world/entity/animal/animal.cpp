#include "world/entity/animal/animal.h"
#include "world/item/item.h"
#include "world/level/level.h"
#include "world/level/chunk/chunk.h"
#include "world/entity/entity_types.h"
#include "nbt/compound_tag.h"

Animal::Animal(Level* level) : PathfinderMob(level) {}

void Animal::baseTick() {
    if (age < 0) age++;
    Mob::baseTick();
}

void Animal::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
    tag->putInt("Age", age);
}

void Animal::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
    age = tag->getInt("Age");
}

bool Animal::hurt(Entity* source, int damage) {
    fleeTime = 3 * TicksPerSecond;
    attackTargetId = 0;
    return PathfinderMob::hurt(source, damage);
}

int Animal::getCreatureBaseType() const { return EntityTypes::BaseCreature; }
