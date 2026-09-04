#include "world/entity/mob_factory.h"
#include "world/entity/entity_types.h"
#include "world/entity/mob.h"
#include "world/entity/entity.h"
#include "world/entity/animal/pig.h"
#include "world/entity/animal/cow.h"
#include "world/entity/animal/chicken.h"
#include "world/entity/animal/sheep.h"
#include "world/entity/monster/zombie.h"
#include "world/entity/monster/skeleton.h"
#include "world/entity/monster/creeper.h"
#include "world/entity/monster/spider.h"
#include "world/entity/monster/pig_zombie.h"
#include <cstdlib>

// Entity::operator new silently falls back to malloc for anything that does
// not fit a pool slot, so an oversized mob would only show up as heap churn.
#define CHECK_FITS_POOL(T) \
    static_assert(sizeof(T) <= Entity::ENTITY_SLOT, #T " outgrew the entity pool slot")

CHECK_FITS_POOL(Pig);
CHECK_FITS_POOL(Cow);
CHECK_FITS_POOL(Chicken);
CHECK_FITS_POOL(Sheep);
CHECK_FITS_POOL(Zombie);
CHECK_FITS_POOL(Skeleton);
CHECK_FITS_POOL(Creeper);
CHECK_FITS_POOL(Spider);
CHECK_FITS_POOL(PigZombie);

namespace MobFactory {

static const int MOB_SLOT_RESERVE = 24;

Mob* createMob(int mobType, Level* level) {
    if (Entity::freeSlots() <= MOB_SLOT_RESERVE) return 0;
    Mob* r = 0;
    switch (mobType) {
        case EntityTypes::IdPig:      r = new Pig(level); break;
        case EntityTypes::IdCow:      r = new Cow(level); break;
        case EntityTypes::IdChicken:  r = new Chicken(level); break;
        case EntityTypes::IdSheep:    r = new Sheep(level); break;
        case EntityTypes::IdZombie:   r = new Zombie(level); break;
        case EntityTypes::IdSkeleton: r = new Skeleton(level); break;
        case EntityTypes::IdCreeper:  r = new Creeper(level); break;
        case EntityTypes::IdSpider:   r = new Spider(level); break;
        case EntityTypes::IdPigZombie:r = new PigZombie(level); break;
    }
    return r;
}

}
