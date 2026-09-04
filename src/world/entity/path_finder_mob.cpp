#include "world/entity/path_finder_mob.h"
#include "world/entity/local_player.h"
#include "world/entity/arrow.h"
#include "world/entity/entity_types.h"
#include "world/inventory/inventory.h"
#include "world/item/item_instance.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "client/renderer/particle.h"
#include "world/level/chunk/chunk.h"
#include "rs/rs.h"
#include "util/mth.h"
#include "platform/trace.h"
#include <cmath>
#include <cstring>

// The two halves of each struct are written by hand, so a field added on one
// side and forgotten on the other has to break the build rather than the game.
static_assert(sizeof(DsEntityView) == 44, "DsEntityView layout changed");
static_assert(sizeof(DsMobIn) == 264, "DsMobIn layout changed");
static_assert(sizeof(DsMobOut) == 80, "DsMobOut layout changed");

PathfinderMob::PathfinderMob(Level* level)
:   Mob(level), runSpeed(0.7f), fleeTime(0), yMoveRot(0.0f), ai(0),
    attackTargetId(0), lastHurtByMobId(0), aiTickCount(0),
    lastHurtTick(0), targetSlot(DS_TARGET_NONE), eatAnimTick(0) {}

// Mob::travel steers off yRot, which is the head here, so the body's own angle
// stands in for the length of the call.
void PathfinderMob::travel(float xs, float yf) {
    float head = yRot;
    yRot = yMoveRot;
    Mob::travel(xs, yf);
    yRot = head;
}

PathfinderMob::~PathfinderMob() {
    if (ai) ds_mob_free(ai);
}

Entity* PathfinderMob::getTarget() const {
    if (attackTargetId == 0) return 0;
    Entity* e = level->getEntity(attackTargetId);
    return (e && e->isAlive()) ? e : 0;
}

void PathfinderMob::setTarget(Entity* e) {
    attackTargetId = e ? e->entityId : 0;
    // The Rust side owns the slot, an id set from C++ has no view behind it.
    targetSlot = DS_TARGET_NONE;
}

Entity* PathfinderMob::resolveSlot(int slot) const {
    if (slot == DS_TARGET_PLAYER)   return (Entity*)level->player;
    if (slot == DS_TARGET_ATTACKER) return level->getEntity(lastHurtByMobId);
    return 0;
}

// Arrows and other projectiles are not worth chasing, only their shooter is.
static Entity* attackerOf(Level* level, Entity* source) {
    if (!source) return 0;
    if (source->isEntityType(EntityTypes::IdArrow)) {
        Arrow* ar = (Arrow*)source;
        return ar->ownerId ? level->getEntity(ar->ownerId) : 0;
    }
    return (source->isPlayer() || source->isMob()) ? source : 0;
}

bool PathfinderMob::hurt(Entity* source, int damage) {
    if (!Mob::hurt(source, damage)) return false;
    Entity* by = attackerOf(level, source);
    if (by && by != this) {
        lastHurtByMobId = by->entityId;
        lastHurtTick = aiTickCount + 1;
        alerted();
    }
    return true;
}

// The Rust revenge goal asks for this on the tick it takes the grudge up, which
// is where vanilla's HurtByTargetGoal alerts the neighbours too.
void PathfinderMob::alertNearby(float range) {
    AABB box = bb.grow(range, range, range);
    static EntityList nearby;
    level->getEntities(this, box, nearby);
    for (size_t i = 0; i < nearby.size(); i++) {
        Entity* e = nearby[i];
        if (!e->isMob()) continue;
        PathfinderMob* m = (PathfinderMob*)e;
        if (m->getAiKind() == getAiKind()) m->alerted();
    }
}

// --- snapshot ------------------------------------------------------------

static void clearView(DsEntityView& v) {
    memset(&v, 0, sizeof(v));
}

static void fillView(DsEntityView& v, Entity* e) {
    if (!e) { clearView(v); return; }
    v.valid = 1;
    v.alive = e->isAlive() ? 1 : 0;
    v.canSee = 0;
    v.extra = 0;
    v.x = e->x; v.y = e->y; v.z = e->z;
    v.bbY0 = e->bb.y0; v.bbY1 = e->bb.y1;
    v.head = e->getHeadHeight();
    v.width = e->bbWidth;
}

// canSee is a raycast, so it is only worth paying for on the mobs whose goals
// actually read it and only once the entity is close enough to matter.
static const float SIGHT_RANGE = 32.0f;

void PathfinderMob::buildSnapshot(DsMobIn& in) {
    memset(&in, 0, sizeof(in));

    in.kind = getAiKind();
    in.age = isBaby() ? -1 : 0;
    in.noActionTime = noActionTime;
    in.fleeTime = fleeTime;
    in.attackTime = attackTime;
    in.health = health;
    in.onFire = onFire;
    in.inWater = isInWater() ? 1 : 0;
    in.inLava = isInLava() ? 1 : 0;
    in.onGround = onGround ? 1 : 0;
    in.horizCollision = horizontalCollision ? 1 : 0;
    in.lastHurtTime = lastHurtTick;
    in.targetSlot = targetSlot;
    in.isDay = worldIsDay() ? 1 : 0;
    in.fireImmune = fireImmune ? 1 : 0;

    in.x = x; in.y = y; in.z = z;
    in.bbX0 = bb.x0; in.bbY0 = bb.y0; in.bbZ0 = bb.z0;
    in.bbWidth = bbWidth; in.bbHeight = bbHeight;
    in.headHeight = getHeadHeight();
    in.yRot = yRot; in.xRot = xRot;
    in.yBodyRot = yBodyRot;
    in.xd = xd; in.yd = yd; in.zd = zd;
    in.runSpeed = runSpeed;

    bool wantsSight = getAiKind() >= DS_MOB_ZOMBIE;

    LocalPlayer* p = level->player;
    bool creative = p && p->inventory && p->inventory->isCreative();
    fillView(in.player, (Entity*)p);
    if (p) {
        ItemInstance* sel = p->inventory ? p->inventory->getSelected() : 0;
        in.player.extra = (sel && !sel->isNull()) ? sel->id : 0;
        if (wantsSight && distanceTo((Entity*)p) <= SIGHT_RANGE)
            in.player.canSee = canSee((Entity*)p) ? 1 : 0;
    }

    Entity* attacker = lastHurtByMobId ? level->getEntity(lastHurtByMobId) : 0;
    fillView(in.attacker, attacker);
    if (attacker && wantsSight && distanceTo(attacker) <= SIGHT_RANGE)
        in.attacker.canSee = canSee(attacker) ? 1 : 0;

    if (isBaby()) {
        static EntityList nearby;
        nearby.clear();
        level->getEntitiesOfType(getEntityTypeId(), bb.grow(8.0f, 4.0f, 8.0f), nearby);
        Entity* best = 0;
        float bestDist = 1e9f;
        for (unsigned int i = 0; i < nearby.size(); i++) {
            Entity* e = nearby[i];
            if (!e || e == this || e->isBaby() || !e->isAlive()) continue;
            float d = distanceTo(e);
            if (d < bestDist) { bestDist = d; best = e; }
        }
        fillView(in.parent, best);
    }

    Entity* target = resolveSlot(targetSlot);
    in.gateCanAttack = (p && !creative && canAttack((Entity*)p)) ? 1 : 0;
    in.gateKeepTarget = (target && shouldKeepTarget(target)) ? 1 : 0;
}

// --- intent --------------------------------------------------------------

void PathfinderMob::applyIntent(const DsMobOut& out) {
    xxa = out.xxa;
    yya = out.yya;
    // yRot is the head and yMoveRot steers the body, so a mob can watch the
    // player while walking somewhere else. Mob::tick drags the body along.
    yMoveRot = out.yRot;
    yRot = out.yHeadRot;
    xRot = out.xRot;
    jumping = out.jumping != 0;

    if (out.setVel) { xd = out.xd; yd = out.yd; zd = out.zd; }

    targetSlot = out.targetSlot;
    Entity* target = resolveSlot(targetSlot);
    attackTargetId = target ? target->entityId : 0;

    if (out.ignite) setOnFire(out.ignite);
    for (int i = 0; i < out.smoke; i++) {
        float ox = x + sharedRandom.nextFloat() * bbWidth * 2.0f - bbWidth;
        float oy = (y - heightOffset) + sharedRandom.nextFloat() * bbHeight;
        float oz = z + sharedRandom.nextFloat() * bbWidth * 2.0f - bbWidth;
        particlesSmoke(ox, oy, oz);
    }

    if (out.alertOthers) alertNearby((float)out.alertOthers);

    if (out.swellDir) setSwellDir(out.swellDir);
    eatAnimTick = out.eatTick;

    if (out.attack && target) {
        attackTime = getAttackTime();
        doHurtTarget(target);
    }

    if (out.ranged && target) performRangedAttack(target, out.rangedDist);

    if (out.eatBlock) {
        int bx = Mth::floor(x), by = Mth::floor(y), bz = Mth::floor(z);
        // Tall grass is cropped where the mob stands, a grass block is scraped
        // down to dirt. See EAT_ in rust/src/mob/goals/animal.rs.
        int ey = (out.eatBlock == DS_EAT_TALL_GRASS) ? by : by - 1;
        int want = (out.eatBlock == DS_EAT_TALL_GRASS) ? BLOCK_TALLGRASS : BLOCK_GRASS;
        int into = (out.eatBlock == DS_EAT_TALL_GRASS) ? BLOCK_AIR : BLOCK_DIRT;
        if (worldBlock(level->w, bx, ey, bz) == want) {
            worldSetBlockAndData(level->w, bx, ey, bz, into, 0);
            worldNotifyNeighborsChanged(level->w, bx, ey, bz);
            worldRebuildAroundNow(level->w, bx, ey, bz);
            ate();
        }
    }
}

// Mobs near the player say what they are doing, so a console session comes back
// as goal names instead of a description. traceMark reopens the file per line
// and the memory stick is slow, so the whole world shares one budget rather
// than every mob paying it.
static const float TRACE_RANGE = 12.0f;
static const int   TRACE_GATE = 60;
static int s_traceGate = 0;

void PathfinderMob::traceAi() {
    LocalPlayer* p = level->player;
    if (!p || distanceTo((Entity*)p) > TRACE_RANGE) return;
    if (++s_traceGate < TRACE_GATE) return;
    s_traceGate = 0;

    // A missing ai is the one failure the goal names cannot describe, so it gets
    // said out loud instead of dropping the line.
    if (!ai) {
        traceMark("MOB %d k%d NO-AI", entityId, getAiKind());
        return;
    }

    unsigned char buf[80];
    buf[0] = 0;
    int packed = ds_mob_debug(ai, buf, (int)sizeof(buf) - 1);
    traceMark("MOB %d k%d %s tgt%d nav%d/%d y%d fl%d na%d %s",
              entityId, getAiKind(), (const char*)buf,
              (packed >> 24) & 0xff, (packed >> 16) & 0xff, packed & 0xffff,
              (int)(yya * 100.0f), fleeTime, noActionTime,
              onGround ? "gnd" : "air");
}

void PathfinderMob::updateAi() {
    noActionTime++;
    checkDespawn();
    if (removed) return;

    if (fleeTime > 0) fleeTime--;
    aiTickCount++;

    xxa = 0; yya = 0; jumping = false;

    if (!ai) {
        ai = ds_mob_new(getAiKind(), entityId, getTemptItem());
        if (!ai) return;
    }

    DsMobIn in;
    DsMobOut out;
    buildSnapshot(in);
    memset(&out, 0, sizeof(out));
    ds_mob_tick(ai, level->w, &in, &out);
    applyIntent(out);
    traceAi();
}
