
#ifndef MCPSP_WORLD_ENTITY_MONSTER_SKELETON_H
#define MCPSP_WORLD_ENTITY_MONSTER_SKELETON_H

#include "world/entity/monster/monster.h"

class Skeleton : public Monster {
public:
    Skeleton(Level* level);

    virtual int  getMaxHealth() { return 10; }
    virtual int  getAiKind() const { return DS_MOB_SKELETON; }

    virtual int  getEntityTypeId() const;
    virtual void dropDeathLoot();

    virtual const char* getAmbientSound() { return "mob.skeleton"; }
    virtual const char* getHurtSound()    { return "mob.skeletonhurt"; }
    virtual const char* getDeathSound()   { return "mob.skeletonhurt"; }

protected:
    virtual void performRangedAttack(Entity* target, float dist);

};

#endif
