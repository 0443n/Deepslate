
#ifndef MCPSP_WORLD_ENTITY_ANIMAL_PIG_H
#define MCPSP_WORLD_ENTITY_ANIMAL_PIG_H

#include "world/entity/animal/animal.h"
#include "world/item/item.h"
#include "rs/rs.h"

class Pig : public Animal {
public:
    Pig(Level* level);

    virtual int  getAiKind() const { return DS_MOB_PIG; }
    virtual int  getTemptItem() const { return ITEM_WHEAT; }

    virtual int  getEntityTypeId() const;
    virtual int  getMaxHealth() { return 10; }
    virtual int  getDeathLoot();

    virtual const char* getAmbientSound() { return "mob.pig"; }
    virtual const char* getHurtSound()    { return "mob.pig"; }
    virtual const char* getDeathSound()   { return "mob.pigdeath"; }
};

#endif
