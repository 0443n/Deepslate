#include "world/entity/animal/cow.h"
#include "world/entity/entity_types.h"
#include "world/item/item.h"
#include "world/item/bucket_item.h"
#include "world/item/item_instance.h"
#include "world/entity/player.h"
#include "world/inventory/inventory.h"
#include "world/level/level.h"

Cow::Cow(Level* level) : Animal(level) {
    setSize(0.9f, 1.3f);
    heightOffset = 0.0f;
    walkingSpeed = 0.1f;
    entityRendererId = ER_COW_RENDERER;
    health = getMaxHealth();
    milkedTicks = 0;
}

int Cow::getEntityTypeId() const { return EntityTypes::IdCow; }

void Cow::dropDeathLoot() {
    int beef = 1 + sharedRandom.nextInt(3);
    short meat = onFire > 0 ? ITEM_BEEF_COOKED : ITEM_BEEF_RAW;
    for (int i = 0; i < beef; i++) spawnAtLocation(meat, 1);
    int leather = sharedRandom.nextInt(3);
    for (int i = 0; i < leather; i++) spawnAtLocation(ITEM_LEATHER, 1);
}

void Cow::aiStep() {
    Animal::aiStep();
    ++milkedTicks;
}

bool Cow::playerInteract() {
    Player* p = (Player*)g_level.player;
    if (!p) return false;
    ItemInstance* sel = p->inventory->getSelected();
    if (milkedTicks <= 20 || !sel || sel->id != ITEM_BUCKET || sel->data != BUCKET_EMPTY ||
        p->inventory->isCreative())
        return Animal::playerInteract();
    milkedTicks = 0;
    if (--sel->count <= 0) {
        sel->id = ITEM_BUCKET; sel->count = 1; sel->data = BUCKET_MILK;
    } else {

        ItemInstance* milk = new ItemInstance(ITEM_BUCKET, 1, BUCKET_MILK);
        if (!p->inventory->add(milk)) delete milk;
        else p->inventory->ensureHotbar(ITEM_BUCKET, BUCKET_MILK);
    }
    return true;
}
