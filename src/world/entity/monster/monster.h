
#ifndef MCPSP_WORLD_ENTITY_MONSTER_MONSTER_H
#define MCPSP_WORLD_ENTITY_MONSTER_MONSTER_H

#include "world/entity/path_finder_mob.h"
#include "rs/rs.h"

class Monster : public PathfinderMob {
public:
    Monster(Level* level);

    virtual void tick();
    virtual int  getCreatureBaseType() const;

    bool canSpawn();

    virtual bool doHurtTarget(Entity* target);
    virtual int  getAttackTime() { return 20; }

protected:
    bool  isDarkEnoughToSpawn();

    int attackDamage;

    int frozenTicks;
};

#endif
