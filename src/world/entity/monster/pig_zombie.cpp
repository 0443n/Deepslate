#include "world/entity/monster/pig_zombie.h"
#include "world/entity/entity_types.h"
#include "world/level/level.h"
#include "world/item/item.h"
#include "nbt/compound_tag.h"

PigZombie::PigZombie(Level* level)
:   Zombie(level, ER_PIGZOMBIE_RENDERER),
    angerTime(0),
    playAngrySoundIn(0),
    stunedTime(TicksPerSecond * 3)
{
    runSpeed = 0.7f;
    attackDamage = 5;
    fireImmune = true;
    health = getMaxHealth();
}

int PigZombie::getEntityTypeId() const { return EntityTypes::IdPigZombie; }

void PigZombie::tick() {
    if (stunedTime > 0) stunedTime--;
    if (angerTime > 0) angerTime--;
    if (playAngrySoundIn > 0) {
        if (--playAngrySoundIn == 0)

            level->playSound(this, "mob.zombiepig.zpigangry",
                             getSoundVolume() * 2.0f, getVoicePitch() * 1.8f);
    }
    Zombie::tick();
}

// Freshly spawned pig zombies stay calm, and an unangered one only reacts to
// something practically on top of it.
bool PigZombie::canAttack(Entity* target) {
    if (stunedTime != 0) return false;
    if (angerTime != 0) return true;
    return target && target->distanceTo(x, y, z) < 5.0f;
}

// Anger only opens the canAttack gate, the target goal picks the player up on
// its next scan. Writing the id here would be undone by the AI's own answer.
void PigZombie::alerted() {
    angerTime = 400 + sharedRandom.nextInt(400);
    playAngrySoundIn = sharedRandom.nextInt(40);
}

void PigZombie::dropDeathLoot() {
    int count = sharedRandom.nextInt(2);
    for (int i = 0; i < count; i++) spawnAtLocation(ITEM_GOLD_INGOT, 1);
}

void PigZombie::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
    tag->putShort("Anger", (short)angerTime);
}

void PigZombie::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
    angerTime = tag->getShort("Anger");
}
