#include "world/entity/monster/zombie.h"
#include "world/entity/entity_types.h"
#include "world/item/item.h"

Zombie::Zombie(Level* level) : Monster(level) {
    setSize(0.6f, 1.8f);
    entityRendererId = ER_ZOMBIE_RENDERER;
    runSpeed = 0.5f;
    attackDamage = 4;
    health = getMaxHealth();

}

Zombie::Zombie(Level* level, int rendererId) : Monster(level) {
    setSize(0.6f, 1.8f);
    entityRendererId = (EntityRendererId)rendererId;
    runSpeed = 0.5f;
    attackDamage = 4;
    health = getMaxHealth();

}

int Zombie::getEntityTypeId() const { return EntityTypes::IdZombie; }

void Zombie::dropDeathLoot() {
    if (sharedRandom.nextInt(4) == 0) spawnAtLocation(ITEM_FEATHER, 1);
}
