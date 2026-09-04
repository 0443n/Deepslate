
#ifndef MCPSP_WORLD_ENTITY_ANIMAL_CHICKEN_H
#define MCPSP_WORLD_ENTITY_ANIMAL_CHICKEN_H

#include "world/entity/animal/animal.h"
#include "world/item/item.h"
#include "rs/rs.h"

class Chicken : public Animal {
public:
    Chicken(Level* level);
    virtual int  getAiKind() const { return DS_MOB_CHICKEN; }
    virtual int  getTemptItem() const { return ITEM_SEEDS_WHEAT; }

    virtual int  getEntityTypeId() const;
    virtual int  getMaxHealth() { return 4; }
    virtual void aiStep();
    virtual void causeFallDamage(float) {}
    virtual void dropDeathLoot();

    virtual const char* getAmbientSound() { return "mob.chicken"; }
    virtual const char* getHurtSound()    { return "mob.chickenhurt"; }
    virtual const char* getDeathSound()   { return "mob.chickenhurt"; }

    float flap, oFlap;
    float flapSpeed, oFlapSpeed;
    float flapping;

private:
    int eggTime;
};

#endif
