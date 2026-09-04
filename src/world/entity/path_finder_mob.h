
#ifndef MCPSP_WORLD_ENTITY_PATH_FINDER_MOB_H
#define MCPSP_WORLD_ENTITY_PATH_FINDER_MOB_H

#include "world/entity/mob.h"

struct DsMobAi;
struct DsMobIn;
struct DsMobOut;

// The AI itself is Rust, see rust/src/mob. This class is the snapshot boundary,
// it describes the mob to the Rust side each tick and applies what comes back.
class PathfinderMob : public Mob {
public:
    PathfinderMob(Level* level);
    virtual ~PathfinderMob();

    virtual void updateAi();
    virtual void travel(float xs, float yf);

    // Which set of goals the Rust side builds, one of the DS_MOB_ constants.
    virtual int getAiKind() const = 0;
    virtual int getTemptItem() const { return 0; }

    virtual bool hurt(Entity* source, int damage);

    // Per mob gates the target goals ask about, on top of their own checks.
    virtual bool canAttack(Entity* target) { return true; }
    virtual bool shouldKeepTarget(Entity* target) { return true; }

    // Implemented by the mobs that can actually land a hit or fire a shot.
    virtual bool doHurtTarget(Entity* target) { return false; }
    virtual int  getAttackTime() { return 20; }
    virtual void performRangedAttack(Entity* target, float dist) {}

    // The mob has a grudge now, either from its own hurt or from a neighbour's.
    virtual void alerted() {}
    void alertNearby(float range);

    // Called once the block under the mob is eaten.
    virtual void ate() {}
    virtual void setSwellDir(int dir) {}

    int  getEatAnimationTick() const { return eatAnimTick; }

    Entity* getTarget() const;
    void    setTarget(Entity* e);
    int     getTargetId() const { return attackTargetId; }

    float runSpeed;
    int   fleeTime;
    // Movement basis. yRot carries the head, which looks where it likes.
    float yMoveRot;

protected:
    void traceAi();
    void buildSnapshot(DsMobIn& in);
    void applyIntent(const DsMobOut& out);
    Entity* resolveSlot(int slot) const;

    DsMobAi* ai;
    int  attackTargetId;
    int  lastHurtByMobId;
    int  aiTickCount;
    int  lastHurtTick;
    int  targetSlot;
    int  eatAnimTick;
};

#endif
