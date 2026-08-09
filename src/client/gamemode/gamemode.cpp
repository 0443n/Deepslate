#include "client/gamemode/gamemode.h"
#include "world/level/level.h"
#include "world/entity/local_player.h"
#include "client/player/player.h"
#include "client/gui/hud.h"
#include "platform/audio/sound.h"
#include "world/entity/tripod_camera.h"
#include "client/player/player_state.h"
#include "world/level/world.h"
#include "world/level/tile/material.h"
#include "world/level/level.h"
#include "world/entity/entity.h"
#include "world/entity/arrow.h"
#include "world/entity/throwable.h"
#include "world/entity/animal/pig.h"
#include "world/entity/animal/sheep.h"
#include "world/entity/animal/cow.h"
#include "world/item/bucket_item.h"
#include "world/entity/entity_types.h"
#include "world/item/item.h"
#include "world/item/tile_item.h"
#include "world/item/food_item.h"
#include "world/level/chunk/chunk.h"
#include "world/level/tile/entity/sign_tile_entity.h"
#include "world/level/tile/entity/chest_tile_entity.h"
#include "world/item/crafting/recipe.h"
#include "world/level/tile/entity/furnace_tile_entity.h"
#include "world/level/tile/entity/reactor_tile_entity.h"
#include "world/level/tile/fire.h"
#include "world/entity/item_entity.h"
#include "platform/time.h"
#include "client/gamemode/click_repeat.h"

static Mob* nearbyTripodCamera() {
    if (!g_level.player) return 0;

    static EntityList nearby;
    g_level.getEntities(0, g_level.player->bb.grow(2.5f, 2.5f, 2.5f), nearby);
    for (size_t i = 0; i < nearby.size(); i++) {
        Entity* e = nearby[i];
        if (e->entityRendererId != ER_TRIPODCAMERA_RENDERER) continue;
        float dx = e->x - g_level.player->x;
        float dy = e->y - g_level.player->y;
        float dz = e->z - g_level.player->z;
        if (dx * dx + dy * dy + dz * dz < 2.5f * 2.5f) return (Mob*)e;
    }
    return 0;
}

static bool s_drawing = false;

static bool canDrawBow() {
    if (g_level.player->inventory->isCreative()) return true;
    for (int i = 0; i < g_level.player->inventory->getContainerSize(); i++) {
        ItemInstance* it = g_level.player->inventory->getItem(i);
        if (it && it->id == ITEM_ARROW && it->count > 0) return true;
    }
    return false;
}

static bool tileNeedsTool(unsigned char id, unsigned char ) {
    return !materialOf(id).isAlwaysDestroyable();
}

MiningState g_mining = { false, 0, 0, 0, 0.0f };
int g_useItemDelay = 0;

void playerDropSelected(bool all) {
    if (g_level.player->inventory->isCreative() || !g_level.player) return;
    ItemInstance* held = g_level.player->inventory->getSelected();
    if (!held || held->isNull()) return;

    ItemInstance piece = g_level.player->inventory->removeSelected(all ? held->count : 1);
    if (piece.isNull()) return;

    LocalPlayer* p = g_level.player;
    const float D2R = 3.14159265f / 180.0f;
    float cy = cosf(p->yRot * D2R), sy = sinf(p->yRot * D2R);
    float cp = cosf(p->xRot * D2R), sp = sinf(p->xRot * D2R);

    const float pow = 0.3f;

    ItemEntity* e = new ItemEntity(&g_level, p->x, p->y - 0.3f, p->z, piece);
    e->xd = cp * sy * pow;
    e->yd = sp * pow + 0.1f;
    e->zd = cp * cy * pow;

    float dir = (rand() / (float)RAND_MAX) * 6.2831853f;
    float j = 0.02f * (rand() / (float)RAND_MAX);
    e->xd += cosf(dir) * j;
    e->zd += sinf(dir) * j;
    e->yd += ((rand() / (float)RAND_MAX) - (rand() / (float)RAND_MAX)) * 0.1f;
    e->throwTime = 20 * 2;
    g_level.addEntity(e);
    soundPlay("random.pop", 0.3f, 1.0f);
}

static void spillContainer(Container* c, int x, int y, int z) {

    if (g_gameMode->isCreative()) return;
    for (int i = 0; i < c->getContainerSize(); i++) {
        ItemInstance* it = c->getItem(i);
        if (it && !it->isNull()) Tile::popResource(x, y, z, *it);
    }
}
#include "client/renderer/item_hand.h"
#include "client/renderer/particle.h"
#include <cmath>
#include <pspkernel.h>
#include <cstdio>
#include <cstdlib>
#include <pspctrl.h>

extern World g_world;
extern Level g_level;
extern bool  g_worldBuilt;

void signStartEdit(SignTileEntity* ste);

static void playTileSoundAt(const char* name, const SoundType& s, int x, int y, int z) {
    if (!name) return;
    g_level.playSound(x + 0.5f, y + 0.5f, z + 0.5f, name,
                      (s.volume + 1.0f) / 2.0f, s.pitch * 0.8f);
}

static void playTileBreakSound(int tileId, int x, int y, int z) {
    const SoundType& s = g_tileSounds[Tile::tiles[tileId & 0xFF]->soundType];
    playTileSoundAt(s.breakSound, s, x, y, z);
}

static Entity* pickEntityOnViewRay(float range, float maxT, bool mobsOnly) {
    if (!g_level.player) return 0;
    const float DEG2RAD = 3.14159265f / 180.0f;
    float cy = cosf(g_level.player->yRot * DEG2RAD), sy = sinf(g_level.player->yRot * DEG2RAD);
    float cp = cosf(g_level.player->xRot * DEG2RAD), sp = sinf(g_level.player->xRot * DEG2RAD);
    float dx = cp * sy, dy = sp, dz = cp * cy;

    float px = g_level.player->x, py = g_level.player->y, pz = g_level.player->z;

    static EntityList candidates;
    g_level.getEntities(0, AABB(px - range, py - range, pz - range,
                                px + range, py + range, pz + range), candidates);

    Entity* best = 0;
    float bestT = maxT;
    for (size_t i = 0; i < candidates.size(); i++) {
        Entity* e = candidates[i];
        if (mobsOnly ? !e->isMob() : !e->isPickable()) continue;
        float t;
        if (e->bb.clip(px, py, pz, dx, dy, dz, range, t) && t < bestT) {
            bestT = t; best = e;
        }
    }
    return best;
}

static bool breakHangingEntityUnderCrosshair() {
    const float range = 5.0f;

    float blockT = range;
    BlockHit bh = worldPick(&g_world, g_level.player->x, g_level.player->y, g_level.player->z, g_level.player->yRot, g_level.player->xRot, range);
    if (bh.hit) {
        float hx = bh.x + bh.clickX, hy = bh.y + bh.clickY, hz = bh.z + bh.clickZ;
        float ddx = hx - g_level.player->x, ddy = hy - g_level.player->y, ddz = hz - g_level.player->z;
        blockT = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);
    }

    Entity* best = pickEntityOnViewRay(range, blockT, false);
    if (best) {

        int dmg = 1;
        ItemInstance* held = g_level.player->inventory->getSelected();
        if (held && !held->isNull() && Item::items[held->id])
            dmg = Item::items[held->id]->getAttackDamage();
        best->hurt(g_level.player, dmg);
        playerSwing();

        if (!g_gameMode->isCreative() && held && !held->isNull() &&
            Item::items[held->id] && Item::items[held->id]->maxDamage > 0)
            if (g_level.player->inventory->hurtSelected(Item::items[held->id]->getHurtEnemyDurabilityCost()))
                g_level.playSound(g_level.player, "random.break", 1.0f, 1.0f);
        return true;
    }
    return false;
}

static bool interactMobUnderCrosshair() {
    const float range = 3.0f;
    Entity* best = pickEntityOnViewRay(range, range, true);
    return best ? ((Mob*)best)->playerInteract() : false;
}

static void spawnEatParticles(int iconCell, int count) {
    particlesEat(g_level.player->x, g_level.player->y, g_level.player->z,
                 g_level.player->yRot, g_level.player->xRot, iconCell, count);

    float r1 = rand() / (float)RAND_MAX, r2 = rand() / (float)RAND_MAX;
    g_level.playSound(g_level.player, "random.eat",
                      0.5f + 0.5f * (rand() % 2), (r1 - r2) * 0.2f + 1.0f);
}

static void breakTargetedBlock(const BlockHit& hit) {

    unsigned char brokenId = worldBlock(&g_world, hit.x, hit.y, hit.z);
    unsigned char brokenData = worldData(&g_world, hit.x, hit.y, hit.z);

    if ((brokenId == BLOCK_BEDROCK && hit.y <= 0) ||
        brokenId == BLOCK_INVISIBLE_BEDROCK ||
        !worldReady(&g_world, hit.x, hit.z) ||
        hit.y < 0 || hit.y >= WORLD_H) {
        return;
    }

    if (brokenId == BLOCK_CHEST || brokenId == BLOCK_FURNACE || brokenId == BLOCK_FURNACE_LIT) {
        TileEntity* te = g_level.getTileEntity(hit.x, hit.y, hit.z);
        if (te && te->type == TE_CHEST)
            spillContainer(&((ChestTileEntity*)te)->container, hit.x, hit.y, hit.z);
        else if (te && te->type == TE_FURNACE)
            spillContainer((FurnaceTileEntity*)te, hit.x, hit.y, hit.z);
    }
    if (isSign(brokenId) || brokenId == BLOCK_CHEST ||
        brokenId == BLOCK_FURNACE || brokenId == BLOCK_FURNACE_LIT)
        g_level.removeTileEntity(hit.x, hit.y, hit.z);
    {

        ItemInstance* sel = g_level.player->inventory->getSelected();
        bool shearedLeaf = (brokenId == BLOCK_LEAVES && sel && sel->id == ITEM_SHEARS &&
                            !g_gameMode->isCreative());
        if (shearedLeaf)
            Tile::popResource(hit.x, hit.y, hit.z,
                              ItemInstance(BLOCK_LEAVES, 1, (short)(brokenData & 3)));
        bool couldDestroy = true;
        if (!g_gameMode->isCreative() && tileNeedsTool(brokenId, brokenData)) {
            Item* it = (sel && sel->id > 0 && sel->id < 4096) ? Item::items[sel->id] : nullptr;
            couldDestroy = it && it->canDestroySpecial(brokenId);
        }
        if (couldDestroy && !shearedLeaf)
            worldSpawnResources(&g_world, hit.x, hit.y, hit.z, brokenId, brokenData);

        if (couldDestroy && brokenId == BLOCK_TOPSNOW && !g_gameMode->isCreative())
            Tile::popResource(hit.x, hit.y, hit.z, ItemInstance(ITEM_SNOWBALL, 1, 0));

        if (!g_gameMode->isCreative() && sel && !sel->isNull()) {
            Item* tool = Item::items[sel->id];
            if (tool && tool->maxDamage > 0 &&
                g_level.player->inventory->hurtSelected(tool->getMineDurabilityCost()))
                g_level.playSound(g_level.player, "random.break", 1.0f, 1.0f);
        }

        particlesDestroyBlock(&g_world, hit.x, hit.y, hit.z, brokenId, brokenData);
        playTileBreakSound(brokenId, hit.x, hit.y, hit.z);

        unsigned char leaves = BLOCK_AIR;
        if (brokenId == BLOCK_ICE) {
            unsigned char below = worldBlock(&g_world, hit.x, hit.y - 1, hit.z);
            if (isSolidPhys(below) || isLiquidId(below)) leaves = BLOCK_WATER;
        }
        worldSetBlockAndData(&g_world, hit.x, hit.y, hit.z, leaves, 0);
        if (isLiquidId(leaves)) worldScheduleTick(&g_world, hit.x, hit.y, hit.z, leaves, 5);
        worldNotifyNeighborsChanged(&g_world, hit.x, hit.y, hit.z);

        worldUpdateLights(&g_world);
        worldRebuildAroundNow(&g_world, hit.x, hit.y, hit.z);
    }
}

static bool continueMining(const BlockHit& hit) {
    static unsigned int s_lastUs = 0;
    static float s_digTicks = 0.0f;

    static unsigned int s_breakCooldownUs = 0;
    unsigned int now = sceKernelGetSystemTimeLow();

    unsigned char id   = worldBlock(&g_world, hit.x, hit.y, hit.z);
    unsigned char data = worldData(&g_world, hit.x, hit.y, hit.z);

    if (!g_mining.active || hit.x != g_mining.x || hit.y != g_mining.y || hit.z != g_mining.z) {
        g_mining.active = true;
        g_mining.x = hit.x; g_mining.y = hit.y; g_mining.z = hit.z;
        g_mining.progress = 0.0f;
        s_lastUs = now; s_digTicks = 0.0f;
        playerSwing();
        Tile::tiles[id]->attack(&g_world, hit.x, hit.y, hit.z, g_level.player);
    }

    float dt = Tile::tiles[id]->destroySpeed;
    if (id == BLOCK_AIR || dt < 0.0f) { g_mining.progress = 0.0f; return false; }
    if (dt == 0.0f) return true;

    playerSwing();

    ItemInstance* sel = g_level.player->inventory->getSelected();
    Item* it = (sel && sel->id > 0 && sel->id < 4096) ? Item::items[sel->id] : nullptr;
    bool canDestroy = !tileNeedsTool(id, data) || (it && it->canDestroySpecial(id));
    float speed = canDestroy ? (it ? it->getDestroySpeed(id) : 1.0f) : 1.0f;

    LocalPlayer* p = g_level.player;
    if (p) {
        unsigned char eyeBlk = worldBlock(&g_world, Mth::floor(p->x), Mth::floor(p->y), Mth::floor(p->z));
        if (isWaterId(eyeBlk)) speed /= 5.0f;
        if (!p->onGround)      speed /= 5.0f;
    }
    float perTick = (speed / dt) / (canDestroy ? 30.0f : 100.0f);

    float ticks = (now - s_lastUs) / 50000.0f;
    s_lastUs = now;

    if (s_breakCooldownUs && !timeReached(now, s_breakCooldownUs)) return false;
    g_mining.progress += perTick * ticks;

    s_digTicks += ticks;
    if (s_digTicks >= 4.0f) {
        s_digTicks -= 4.0f;
        particlesCrackHit(&g_world, hit.x, hit.y, hit.z, id, data, hit.face);
        const SoundType& s = g_tileSounds[Tile::tiles[id]->soundType];
        if (s.stepSound)
            g_level.playSound(hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f, s.stepSound,
                              (s.volume + 1.0f) / 8.0f, s.pitch * 0.5f);
    }

    bool done = g_mining.progress >= 1.0f;
    if (done) {
        s_breakCooldownUs = now + 250000;
        if (!s_breakCooldownUs) s_breakCooldownUs = 1;
    }
    return done;
}

bool GameMode::useItemOn(ItemInstance* item, const BlockHit& hit, bool* usedItem) {
    if (usedItem) *usedItem = false;
    unsigned char t = worldBlock(&g_world, hit.x, hit.y, hit.z);

    if (t == BLOCK_INVISIBLE_BEDROCK) return true;

    if (t > 0 && Tile::tiles[t]->use(&g_world, hit.x, hit.y, hit.z, g_level.player)) return true;
    if (!item || item->isNull()) return false;
    if (!item->getItem()) return false;

    if (isCreative()) {

        short aux = item->data, count = item->count;
        bool ok = item->useOn(g_level.player, &g_world, hit.x, hit.y, hit.z, hit.face,
                              hit.clickX, hit.clickY, hit.clickZ);
        item->data = aux; item->count = count;
        if (usedItem) *usedItem = ok;
        return ok;
    }
    bool ok = item->useOn(g_level.player, &g_world, hit.x, hit.y, hit.z, hit.face,
                          hit.clickX, hit.clickY, hit.clickZ);
    if (usedItem) *usedItem = ok;
    return ok;
}

static ClickRepeat s_click[2];

static void clickPlacedBlock(bool placed) {
    s_click[1].placed(placed, g_level.player ? g_level.player->x : 0.0f,
                              g_level.player ? g_level.player->y : 0.0f,
                              g_level.player ? g_level.player->z : 0.0f);
}

static unsigned int autoRepeatClicks(unsigned int pressed, unsigned int held) {
    const unsigned int kMask[2] = { PSP_CTRL_RTRIGGER, PSP_CTRL_LTRIGGER };
    unsigned int now = sceKernelGetSystemTimeLow();
    LocalPlayer* p = g_level.player;
    unsigned int out = 0;
    for (int b = 0; b < 2; b++) {
        if (!(held & kMask[b]))  { s_click[b].release(); continue; }
        if (pressed & kMask[b])  { s_click[b].pressed(now); continue; }
        if (p && p->isSleeping()) continue;
        if (p && s_click[b].repeatDue(now, p->x, p->y, p->z)) out |= kMask[b];
    }
    return out;
}

void GameMode::handleInput(unsigned int pressed, unsigned int held) {

    if (g_worldBuilt) pressed |= autoRepeatClicks(pressed, held);

    if (g_worldBuilt) {
        static unsigned int s_startUs = 0;
        ItemInstance* sel = g_level.player->inventory->getSelected();
        if (sel && sel->id == ITEM_BOW) {
            bool lHeld = (held & PSP_CTRL_LTRIGGER) != 0;

            bool hasArrow = canDrawBow();
            if (lHeld && !s_drawing && (pressed & PSP_CTRL_LTRIGGER) && hasArrow) {
                s_drawing = true; s_startUs = sceKernelGetSystemTimeLow();
            }
            if (lHeld && s_drawing) {

                float ticks = (sceKernelGetSystemTimeLow() - s_startUs) / 50000.0f;
                g_level.player->bowTimeHeld = ticks;
                float p = ticks / 20.0f;
                p = ((p * p) + p * 2) / 3.0f;
                if (p > 1) p = 1;
                g_level.player->bowPull = p;
                pressed &= ~PSP_CTRL_RTRIGGER;
            } else {
                if (s_drawing) {
                    s_drawing = false;
                    float pow = g_level.player->bowPull;

                    if (pow >= 0.1f &&
                        g_level.player->inventory->removeResource(ItemInstance(ITEM_ARROW, 1, 0), true) == 0) {

                        g_level.addEntity(new Arrow(&g_level, g_level.player->x,
                                                    g_level.player->y + g_level.player->getHeadHeight(),
                                                    g_level.player->z,
                                                    g_level.player->yRot, g_level.player->xRot, pow * 2.0f, pow >= 1.0f,
                                                     true));

                        g_level.playSound(g_level.player, "random.bow", 1.0f,
                                          1.0f / ((rand() / (float)RAND_MAX) * 0.4f + 1.2f) + pow * 0.5f);

                        if (g_level.player->inventory->hurtSelected(1))
                            g_level.playSound(g_level.player, "random.break", 1.0f, 1.0f);
                    }
                }
                g_level.player->bowPull = 0.0f;
                g_level.player->bowTimeHeld = 0.0f;
            }
            pressed &= ~PSP_CTRL_LTRIGGER;
        } else {

            s_drawing = false;
            g_level.player->bowPull = 0.0f;
            g_level.player->bowTimeHeld = 0.0f;
        }
    }

    if (g_worldBuilt) {
        ItemInstance* sel = g_level.player->inventory->getSelected();
        if (sel && (sel->id == ITEM_SNOWBALL || sel->id == ITEM_EGG)) {
            if ((pressed & PSP_CTRL_LTRIGGER) && !g_useItemDelay) {
                g_useItemDelay = USE_ITEM_DELAY_TICKS;
                int type = (sel->id == ITEM_EGG) ? EntityTypes::IdThrownEgg
                                                 : EntityTypes::IdSnowball;
                g_level.addEntity(new Throwable(&g_level,
                    g_level.player->x, g_level.player->y, g_level.player->z,
                    g_level.player->yRot, g_level.player->xRot, type));
                playerSwing();

                g_level.playSound(g_level.player, "random.bow", 0.5f,
                                  0.4f / ((rand() / (float)RAND_MAX) * 0.4f + 0.8f));

                if (!isCreative()) g_level.player->inventory->consumeSelected();
            }
            pressed &= ~PSP_CTRL_LTRIGGER;
        }

        else if (sel && sel->id == ITEM_CAMERA) {
            if ((pressed & PSP_CTRL_LTRIGGER) && !g_useItemDelay) {
                g_useItemDelay = USE_ITEM_DELAY_TICKS;

                bool handled = interactMobUnderCrosshair();
                if (!handled) {
                    Mob* near = nearbyTripodCamera();
                    if (near) handled = near->playerInteract();
                }
                if (!handled) {
                    g_level.addEntity(new TripodCamera(&g_level,
                        g_level.player->x, g_level.player->y, g_level.player->z,
                        g_level.player->yRot, g_level.player->xRot));
                }
                playerSwing();
            }
            pressed &= ~PSP_CTRL_LTRIGGER;
        }
    }

    if (g_worldBuilt) {
        static bool s_eating = false;
        static unsigned int s_eatStart = 0;
        static int s_lastEmit = 0;
        ItemInstance* sel = g_level.player->inventory->getSelected();
        bool isFoodSel = sel && sel->getItem() && sel->getItem()->isFood();
        if (isFoodSel) {
            bool lHeld = (held & PSP_CTRL_LTRIGGER) != 0;

            bool canEat = g_gameMode->isCreative() ||
                          g_level.player->health < g_level.player->getMaxHealth();
            if (lHeld && !s_eating && (pressed & PSP_CTRL_LTRIGGER) && canEat) {
                s_eating = true; s_eatStart = sceKernelGetSystemTimeLow(); s_lastEmit = 0;
            }
            if (lHeld && s_eating) {
                float ticks = (sceKernelGetSystemTimeLow() - s_eatStart) / 50000.0f;
                float progress = ticks / (float)FoodItem::EAT_TICKS;
                if (progress > 1.0f) progress = 1.0f;
                g_level.player->eatAnim = progress;
                int icon = itemFlatIcon(sel->id, (unsigned char)sel->data);

                int t4 = (int)ticks / 4;
                if ((int)ticks >= 4 && t4 != s_lastEmit) {
                    s_lastEmit = t4;
                    spawnEatParticles(icon, 5);
                }
                if (ticks >= (float)FoodItem::EAT_TICKS) {
                    spawnEatParticles(icon, 10);

                    if (!g_gameMode->isCreative()) {
                        int nutrition = ((FoodItem*)sel->getItem())->getNutrition();
                        g_level.player->heal(nutrition);
                        g_level.playSound(g_level.player, "random.burp", 0.5f,
                                          (rand() / (float)RAND_MAX) * 0.1f + 0.9f);

                        short remainder = ((FoodItem*)sel->getItem())->getFoodRemainder();
                        g_level.player->inventory->consumeSelected();
                        if (remainder) g_level.player->inventory->setSelectedIfEmpty(remainder, 0);

                        ItemInstance* next = g_level.player->inventory->getSelected();
                        bool moreFood = next && next->getItem() && next->getItem()->isFood();
                        bool stillHurt = g_level.player->health < g_level.player->getMaxHealth();
                        if (lHeld && moreFood && stillHurt) {
                            s_eatStart = sceKernelGetSystemTimeLow(); s_lastEmit = 0;
                            g_level.player->eatAnim = 0.0f;
                        } else {
                            s_eating = false;
                            g_level.player->eatAnim = 0.0f;
                        }
                    } else {
                        s_eatStart = sceKernelGetSystemTimeLow(); s_lastEmit = 0;
                        g_level.player->eatAnim = 0.0f;
                    }
                }
                pressed &= ~PSP_CTRL_LTRIGGER;
            } else {
                s_eating = false;
                g_level.player->eatAnim = 0.0f;
            }
        } else {
            s_eating = false;
            g_level.player->eatAnim = 0.0f;
        }
    }

    if (g_worldBuilt && g_gameMode && !g_gameMode->isCreative()) {
        ItemInstance* mineSel = g_level.player->inventory->getSelected();
        bool bow = mineSel && mineSel->id == ITEM_BOW;
        if ((held & PSP_CTRL_RTRIGGER) && !bow) {
            BlockHit hit = worldPick(&g_world, g_level.player->x, g_level.player->y, g_level.player->z,
                                     g_level.player->yRot, g_level.player->xRot, 5.0f);
            if (hit.hit && continueMining(hit)) {
                breakTargetedBlock(hit);
                g_mining.active = false;
                g_mining.progress = 0.0f;
            } else if (!hit.hit) {
                g_mining.active = false;
                g_mining.progress = 0.0f;
            }
        } else {
            g_mining.active = false;
            g_mining.progress = 0.0f;
        }
    }

    if (g_worldBuilt && (pressed & (PSP_CTRL_RTRIGGER | PSP_CTRL_LTRIGGER))) {

        if ((pressed & PSP_CTRL_RTRIGGER) && breakHangingEntityUnderCrosshair()) {
            return;
        }

        if ((pressed & PSP_CTRL_LTRIGGER) && interactMobUnderCrosshair()) {
            playerSwing();
            return;
        }
        if (pressed & PSP_CTRL_RTRIGGER) {
            playerSwing();
        }

        ItemInstance* held = g_level.player->inventory->getSelected();
        bool clipLiquids = (pressed & PSP_CTRL_LTRIGGER) && held && held->getItem() &&
                           held->getItem()->isLiquidClipItem(held->data);
        BlockHit hit = worldPick(&g_world, g_level.player->x, g_level.player->y, g_level.player->z, g_level.player->yRot, g_level.player->xRot, 5.0f, clipLiquids);
        if (hit.hit) {
            if (pressed & PSP_CTRL_RTRIGGER) {

                bool putOut = fireExtinguishAt(&g_world, hit.x, hit.y, hit.z, hit.face);

                if (g_gameMode->isCreative() && !putOut) breakTargetedBlock(hit);
            } else {

                bool placedBlock = false;
                bool consumed = g_gameMode->useItemOn(g_level.player->inventory->getSelected(),
                                                      hit, &placedBlock);

                clickPlacedBlock(g_gameMode->isCreative() && placedBlock);
                if (consumed) {
                    playerSwing();
                    return;
                }
            }
        }
    }
}

GameMode* g_gameMode = 0;

void gameModeInit(int gameType) {
    delete g_gameMode;
    bool creative = (gameType == 1);
    g_gameMode = creative ? (GameMode*)new CreativeMode()
                          : (GameMode*)new SurvivalMode();

    g_level.player->inventory->reinit(creative);
}
void gameModeShutdown() { delete g_gameMode; g_gameMode = 0; }

void gameModeHandleInput(unsigned int pressed, unsigned int held) {
    if (g_gameMode) g_gameMode->handleInput(pressed, held);
}

static bool isUsableBlockId(unsigned char id) {
    if (isDoor(id) || isTrapdoor(id) || isFenceGate(id) || isSign(id)) return true;
    switch (id) {
        case BLOCK_CRAFTING_TABLE: case BLOCK_STONECUTTER:
        case BLOCK_FURNACE:        case BLOCK_FURNACE_LIT:
        case BLOCK_CHEST:
            return true;
        default:
            return false;
    }
}

CrosshairTarget gameModeCrosshairTarget() {
    CrosshairTarget t = { 0, 0 };
    if (!g_worldBuilt || !g_level.player) return t;

    ItemInstance* sel = g_level.player->inventory->getSelected();
    if (sel && sel->id == ITEM_BOW) {
        if (s_drawing)    { t.useLabel = "Release"; return t; }
        if (canDrawBow())   t.useLabel = "Draw";
    }

    if (sel && sel->getItem() && sel->getItem()->isFood() && g_gameMode &&
        (g_gameMode->isCreative() ||
         g_level.player->health < g_level.player->getMaxHealth())) {
        t.useLabel = "Eat";
        return t;
    }

    if (sel && sel->id == ITEM_CAMERA) {
        t.useLabel = nearbyTripodCamera() ? "Take Picture" : "Place";
        return t;
    }

    if (sel) {
        Entity* m = pickEntityOnViewRay(3.0f, 3.0f, true);
        switch (m ? m->getEntityTypeId() : 0) {
            case EntityTypes::IdCreeper:
                if (sel->id == ITEM_FLINT_AND_STEEL) t.useLabel = "Ignite";
                break;
            case EntityTypes::IdSheep: {
                Sheep* s = (Sheep*)m;
                if (s->isBaby()) break;
                if (sel->id == ITEM_BONEMEAL)              t.useLabel = "Dye";
                else if (sel->id == ITEM_SHEARS && !s->isSheared()) t.useLabel = "Shear";
                break;
            }
            case EntityTypes::IdCow:
                if (sel->id == ITEM_BUCKET && sel->data == BUCKET_EMPTY &&
                    !g_level.player->inventory->isCreative() && ((Cow*)m)->canBeMilked())
                    t.useLabel = "Milk";
                break;
        }
        if (t.useLabel) return t;
    }

    BlockHit hit = worldPick(&g_world, g_level.player->x, g_level.player->y, g_level.player->z,
                             g_level.player->yRot, g_level.player->xRot, 5.0f);
    if (!hit.hit) return t;
    unsigned char id = worldBlock(&g_world, hit.x, hit.y, hit.z);

    if (id == BLOCK_INVISIBLE_BEDROCK) return t;

    t.breakLabel = (id == BLOCK_LEAVES && sel && sel->id == ITEM_SHEARS) ? "Shear" : "Mine";

    if (!t.useLabel) {

        if (sel && sel->id == ITEM_BONEMEAL && sel->data == DYE_WHITE &&
            (id == BLOCK_SAPLING || id == BLOCK_WHEAT || id == BLOCK_MELON_STEM ||
             id == BLOCK_GRASS   || id == BLOCK_REEDS))                t.useLabel = "Grow";
        else if (id == BLOCK_TNT && sel && sel->id == ITEM_FLINT_AND_STEEL) t.useLabel = "Ignite";

        else if (sel && sel->id == ITEM_FLINT_AND_STEEL &&
                 worldBlock(&g_world, hit.x + kFaceNeighbor[hit.face][0],
                                      hit.y + kFaceNeighbor[hit.face][1],
                                      hit.z + kFaceNeighbor[hit.face][2]) == BLOCK_AIR &&
                 fireMayPlace(&g_world, hit.x + kFaceNeighbor[hit.face][0],
                                        hit.y + kFaceNeighbor[hit.face][1],
                                        hit.z + kFaceNeighbor[hit.face][2]))
                                                                       t.useLabel = "Ignite";
        else if (id == BLOCK_BED)                                      t.useLabel = "Sleep";
        else if (id == BLOCK_NETHER_REACTOR)                           t.useLabel = "Activate";
        else if (isUsableBlockId(id))                                  t.useLabel = "Use";

        else if (sel && sel->id == ITEM_PAINTING)                      t.useLabel = "Hang";
        else if (sel && (sel->id < 256 || sel->id == ITEM_SIGN ||
                         sel->id == ITEM_SPAWN_EGG))                   t.useLabel = "Place";
    }
    return t;
}
