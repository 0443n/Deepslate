
#ifndef MCPSP_WORLD_ENTITY_ANIMAL_ANIMAL_H
#define MCPSP_WORLD_ENTITY_ANIMAL_ANIMAL_H

#include "world/entity/path_finder_mob.h"

class CompoundTag;

class Animal : public PathfinderMob {
public:

    // Herds are capped at 15 and cost real entity slots, so one you found stays
    // found. Nothing here ever despawns on distance.
    virtual bool removeWhenFarAway() { return false; }
    Animal(Level* level);

    virtual bool  hurt(Entity* source, int damage);
    virtual int   getCreatureBaseType() const;

    virtual int   getAmbientSoundInterval() { return 12 * TicksPerSecond; }

    virtual bool  isBaby() { return age < 0; }
    virtual void  baseTick();
    int  getAge() const { return age; }
    void setAge(int a) { age = a; }

protected:
    virtual void addAdditonalSaveData(CompoundTag* tag);
    virtual void readAdditionalSaveData(CompoundTag* tag);

    int age = 0;
};

#endif
