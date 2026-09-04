#include "world/entity/monster/spider.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include <cmath>

Spider::Spider(Level* level)
:   Monster(level), climbing(false) {
    setSize(1.4f, 0.9f);
    entityRendererId = ER_SPIDER_RENDERER;
    runSpeed = 0.5f;
    attackDamage = 2;
    health = getMaxHealth();

}

void Spider::tick() {
    Monster::tick();
    climbing = horizontalCollision;
}

bool Spider::onLadder()      { return climbing; }
void Spider::makeStuckInWeb() {  }

int Spider::getEntityTypeId() const { return EntityTypes::IdSpider; }
int Spider::getDeathLoot()          { return ITEM_STRING; }

// Spiders only pick a fight in the dark.
bool Spider::canAttack(Entity* target) { return getBrightness(1.0f) < 0.5f; }

// Daylight makes them lose interest, but slowly rather than all at once.
bool Spider::shouldKeepTarget(Entity* target) {
    if (getBrightness(1.0f) > 0.5f && sharedRandom.nextInt(100) == 0) return false;
    return Monster::shouldKeepTarget(target);
}
