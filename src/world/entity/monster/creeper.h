
#ifndef MCPSP_WORLD_ENTITY_MONSTER_CREEPER_H
#define MCPSP_WORLD_ENTITY_MONSTER_CREEPER_H

#include "world/entity/monster/monster.h"

class Creeper : public Monster {
public:
    Creeper(Level* level);

    virtual int  getMaxHealth() { return 16; }
    virtual void tick();
    virtual int  getAiKind() const { return DS_MOB_CREEPER; }

    virtual int  getEntityTypeId() const;

    virtual bool playerInteract();

    virtual const char* getHurtSound()  { return "mob.creeper"; }
    virtual const char* getDeathSound() { return "mob.creeperdeath"; }

    float getSwelling(float a) const;
    static const int MAX_SWELL = 30;

    int  getSwellDir() const { return swellDir; }
    // 2 means flint and steel lit it, and nothing the AI says untethers that.
    virtual void setSwellDir(int dir) { if (swellDir != 2) swellDir = dir; }

protected:
    virtual int  getDeathLoot();


private:
    void explode();

    int swell, oldSwell, swellDir;
};

#endif
