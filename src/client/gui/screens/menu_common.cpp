
#include <pspgu.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <psputility.h>
#include <cstdio>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/panorama.h"
#include "client/gui/hud.h"
#include "gpu/gu.h"
#include "gpu/button_icons.h"
#include "world/level/tile/entity/sign_tile_entity.h"

unsigned int g_heldButtons = 0;

unsigned int menuSelectionSig(const MenuState& s) {
    unsigned int h = 2166136261u;
    const int fields[] = {
        (int)s.screen, s.worldSelected, s.deleteSelected, s.createSelected,
        s.newWorldGamemode, s.uiRow, s.topSelected, s.selected,
        s.joinUiRow, s.joinBarSel, s.joinRow, s.joinEditMode, s.addSelected,
        s.optFocus, s.optCategory, s.optTabHighlight, s.optItemHighlight,
    };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        h = (h ^ (unsigned int)fields[i]) * 16777619u;
    return (h ^ optionsValueSig()) * 16777619u;
}

void drawRect(float x, float y, float w, float h, unsigned int color) {
    guiFill(x, y, w, h, color);
}

void uiDraw(MenuState& s, float x, float y, float w, float h,
            float tsx, float tsy, float asx, float asy,
            float sw, float sh, unsigned int tint) {

    Texture* tex = s.haveTouch ? &s.touchGui : &s.guiAtlas;
    float sx = s.haveTouch ? tsx : asx, sy = s.haveTouch ? tsy : asy;
    textureBind(tex);
    spriteDraw(tex, x, y, w, h, sx, sy, sw, sh, tint);
}

void guiOptionSwitch(MenuState& s, float x, float y, float w, float h,
                     bool on, bool hovered, unsigned int tint) {
    float scale = hovered ? 0.95f : 1.0f;
    float drawW = w * scale, drawH = h * scale;
    float drawX = x + (w - drawW) / 2.0f, drawY = y + (h - drawH) / 2.0f;
    float onOff = on ? 38.0f : 0.0f;
    uiDraw(s, drawX * UI_SCALE, drawY * UI_SCALE, drawW * UI_SCALE, drawH * UI_SCALE,
           160.0f + onOff, 206.0f, GA_SS_TOGGLE_X + onOff, GA_SS_TOGGLE_Y, 38.0f, 20.0f, tint);
}

void drawMenuHeader(MenuState& s, const char* title, float x, float w, float h, float textScale,
                    float titleX, float titleW) {
    uiDraw(s, x * UI_SCALE, 0.0f, 2.0f * UI_SCALE, (h - 1.0f) * UI_SCALE,
           150.0f, 26.0f, GA_HDR_LEFT_X, GA_HDR_LEFT_Y, 2.0f, 25.0f, WHITE);
    uiDraw(s, (x + 2.0f) * UI_SCALE, 0.0f, (w - 4.0f) * UI_SCALE, (h - 1.0f) * UI_SCALE,
           153.0f, 26.0f, GA_HDR_BODY_X, GA_HDR_BODY_Y, 8.0f, 25.0f, WHITE);
    uiDraw(s, (x + w - 2.0f) * UI_SCALE, 0.0f, 2.0f * UI_SCALE, (h - 1.0f) * UI_SCALE,
           162.0f, 26.0f, GA_HDR_RIGHT_X, GA_HDR_RIGHT_Y, 2.0f, 25.0f, WHITE);
    uiDraw(s, x * UI_SCALE, (h - 1.0f) * UI_SCALE, w * UI_SCALE, 3.0f * UI_SCALE,
           153.0f, 52.0f, GA_HDR_SHADOW_X, GA_HDR_SHADOW_Y, 8.0f, 3.0f, WHITE);
    if (!title || !s.haveFont) return;

    if (titleW <= 0.0f) { titleX = x; titleW = w; }
    float tw = fontTextWidth(&s.font, title) * textScale;

    while (textScale > 1.0f && tw > titleW * UI_SCALE) {
        textScale -= 1.0f;
        tw = fontTextWidth(&s.font, title) * textScale;
    }
    fontDrawTextShadow(&s.font, (titleX + titleW / 2.0f) * UI_SCALE - tw / 2.0f,
                       h / 2.0f * UI_SCALE - 4.0f * textScale, title, 0xFFE0E0E0u, textScale);
}

float menuBarButtonW(MenuState& s, const char* label) {
    if (!s.haveFont) return 38.0f * MENU_PX;

    return (fontTextWidth(&s.font, label) * MENU_BAR_TEXT + 14.0f) * MENU_PX;
}

void menuBarButton(MenuState& s, float x, float w, const char* label, bool hovered) {
    guiTButton(s, x, MENU_BAR_BTNY, w, MENU_BAR_BTNH, hovered, MENU_BEVEL);
    guiTButtonLabel(s, x, MENU_BAR_BTNY, w, MENU_BAR_BTNH, label, hovered, true, MENU_BAR_TEXT);
}

void drawTextField(MenuState& s, float x, float y, float w, float h,
                   const char* text, const char* placeholder, bool focused, float scale) {
    const unsigned int FILL   = 0xFF353737u;
    const unsigned int BORDER = 0xFF606067u;
    drawRect(x * UI_SCALE, y * UI_SCALE, w * UI_SCALE, h * UI_SCALE, FILL);

    unsigned int edge = focused ? 0xFFFFFFFFu : BORDER;
    drawRect(x * UI_SCALE, y * UI_SCALE, w * UI_SCALE, UI_SCALE, edge);
    drawRect(x * UI_SCALE, (y + h - 1.0f) * UI_SCALE, w * UI_SCALE, UI_SCALE, edge);
    drawRect(x * UI_SCALE, y * UI_SCALE, UI_SCALE, h * UI_SCALE, edge);
    drawRect((x + w - 1.0f) * UI_SCALE, y * UI_SCALE, UI_SCALE, h * UI_SCALE, edge);
    if (!s.haveFont) return;
    bool empty = !text || !text[0];
    const char* shown = empty ? placeholder : text;
    if (!shown || !shown[0]) return;

    fontDrawTextClipped(&s.font, (x + 5.0f) * UI_SCALE, (y + h / 2.0f) * UI_SCALE - 4.0f * scale,
                        shown, empty ? 0xFF808080u : 0xFFFFFFFFu, scale,
                        (w - 10.0f) * UI_SCALE / scale);
}

void drawHeaderBar(MenuState& s, bool shadow) {
    if (!s.haveGui) { guiFill(0, 0, G(VW), G(HEADER_H), 0xFF5B535Fu); return; }
    textureBind(&s.guiAtlas);
    spriteDraw(&s.guiAtlas, 0,             0, G(2),      G(HEADER_H), GA_HDR_LEFT,  0xFFFFFFFFu);
    spriteDraw(&s.guiAtlas, G(2),          0, G(VW - 4), G(HEADER_H), GA_HDR_BODY,  0xFFFFFFFFu);
    spriteDraw(&s.guiAtlas, G(VW - 2),     0, G(2),      G(HEADER_H), GA_HDR_RIGHT, 0xFFFFFFFFu);
    if (shadow)
        spriteDraw(&s.guiAtlas, 0, G(HEADER_H), G(VW), G(3), GA_HDR_SHADOW, 0xFFFFFFFFu);
}

void drawWindowFrame(MenuState& s) {
    drawNinePatch(s, GA_SS_WINDOW_X, GA_SS_WINDOW_Y, 16, 16, 4, 0, 0, VW, VH);
    drawHeaderBar(s);
}

void drawHeaderTitle(MenuState& s, const char* title, unsigned int color,
                     float centreX, float width) {
    if (!s.haveFont) return;
    float tw = fontTextWidth(&s.font, title) * UI_SCALE;
    fontDrawTextShadow(&s.font, G(centreX) + (G(width) - tw) / 2.0f,
                       (G(HEADER_H) - 8.0f * UI_SCALE) / 2.0f, title, color, UI_SCALE);
}

void drawDirtBackground(MenuState& s, float y, float h, float uOffset, unsigned int tint) {
    if (!s.haveBg) return;
    if (h < 0.0f) h = VH * UI_SCALE - y;
    textureBind(&s.dirtBg);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    float tileScale = (float)s.dirtBg.realW / (32.0f * UI_SCALE);
    spriteDraw(&s.dirtBg, 0.0f, y, VW * UI_SCALE, h,
               uOffset * tileScale, 0.0f, (VW * UI_SCALE) * tileScale, h * tileScale, tint);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

int holdChargeUpdate(HoldCharge& c, int slotKey, int count, bool crossHeld) {
    const int MIN_CHARGE_MS = 200;
    if (crossHeld && count > 0) {
        unsigned int now = sceKernelGetSystemTimeLow();
        if (c.key != slotKey) { c.key = slotKey; c.start = now; }
        int heldMs = (int)((now - c.start) / 1000);
        float share = (heldMs - MIN_CHARGE_MS) / (700.0f + 10.0f * count);
        c.share = share < 0.0f ? 0.0f : (share > 1.0f ? 1.0f : share);
        if (count > 1 && c.share >= 1.0f) { c.reset(); return count; }
        return 0;
    }
    if (c.key == slotKey && count > 0) {
        int heldMs = (int)((sceKernelGetSystemTimeLow() - c.start) / 1000);
        int want = (int)(count * c.share);
        if (want < 1 || heldMs < MIN_CHARGE_MS) want = 1;
        c.reset();
        return want;
    }
    c.reset();
    return 0;
}

#include "gpu/sprite.h"
#include "gpu/gui_atlas.h"

void drawNinePatch(MenuState& s, float sx, float sy, float sw, float sh, float corner,
                   float gx, float gy, float gw, float gh, float destCorner,
                   unsigned int tint) {
    if (!s.haveGui) {
        drawRect(gx * UI_SCALE, gy * UI_SCALE, gw * UI_SCALE, gh * UI_SCALE, 0xFF444444u);
        return;
    }

    const float c = corner, S = UI_SCALE;
    const float dc = (destCorner < 0.0f) ? corner : destCorner;
    const float xs[4] = { 0, dc, gw - dc, gw };
    const float us[4] = { 0, c, sw - c, sw };
    const float ys[4] = { 0, dc, gh - dc, gh };
    const float vs[4] = { 0, c, sh - c, sh };
    textureBind(&s.guiAtlas);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            float dw = xs[i + 1] - xs[i], dh = ys[j + 1] - ys[j];
            if (dw <= 0 || dh <= 0) continue;
            spriteDraw(&s.guiAtlas, (gx + xs[i]) * S, (gy + ys[j]) * S, dw * S, dh * S,
                       sx + us[i], sy + vs[j], us[i + 1] - us[i], vs[j + 1] - vs[j], tint);
        }
}

void fontDrawTextClipped(const Font* font, float x, float y, const char* text,
                         unsigned int color, float scale, float maxWidthRaw) {
    if (fontTextWidth(font, text) <= maxWidthRaw) {
        fontDrawTextShadow(font, x, y, text, color, scale);
        return;
    }
    char base[68];
    int len = (int)strlen(text);
    if (len > 64) len = 64;
    memcpy(base, text, len);
    base[len] = '\0';

    char out[72];
    while (len > 0) {
        base[len] = '\0';
        snprintf(out, sizeof(out), "%s...", base);
        if (fontTextWidth(font, out) <= maxWidthRaw) {
            fontDrawTextShadow(font, x, y, out, color, scale);
            return;
        }
        len--;
    }
    fontDrawTextShadow(font, x, y, "...", color, scale);
}

void fontDrawTextWrapped(const Font* font, float x, float y, const char* text,
                         unsigned int color, float scale, float maxWidthRaw) {
    float lineH = font->lineHeight * scale;
    char line[96]; line[0] = '\0';
    char word[64];
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        int wl = 0;
        while (*p && *p != ' ' && wl < 63) word[wl++] = *p++;
        word[wl] = '\0';
        if (wl == 0) break;

        char trial[96];
        if (line[0]) snprintf(trial, sizeof(trial), "%s %s", line, word);
        else         snprintf(trial, sizeof(trial), "%s", word);

        if (!line[0] || fontTextWidth(font, trial) <= maxWidthRaw) {
            snprintf(line, sizeof(line), "%s", trial);
        } else {
            fontDrawTextShadow(font, x, y, line, color, scale);
            y += lineH;
            snprintf(line, sizeof(line), "%s", word);
        }
    }
    if (line[0]) fontDrawTextShadow(font, x, y, line, color, scale);
}

static bool oskActive = false;
static int oskTarget = 0;

static SignTileEntity* g_oskSign = 0;
static int g_oskSignLine = 0;
static unsigned short oskDesc[128];
static unsigned short oskInText[128];
static unsigned short oskOutText[128];
static SceUtilityOskData oskData;
static SceUtilityOskParams oskParams;

void startOsk(int target, const char* desc, const char* intext, int maxLen) {
    oskTarget = target;
    memset(&oskData, 0, sizeof(oskData));
    memset(&oskParams, 0, sizeof(oskParams));
    memset(oskDesc, 0, sizeof(oskDesc));
    memset(oskInText, 0, sizeof(oskInText));
    memset(oskOutText, 0, sizeof(oskOutText));

    for (int i = 0; desc[i] && i < 127; i++) oskDesc[i] = desc[i];
    for (int i = 0; intext[i] && i < 127; i++) oskInText[i] = intext[i];

    oskData.language = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
    oskData.lines = 1;
    oskData.unk_24 = 1;

    oskData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_ALL;
    oskData.desc = oskDesc;
    oskData.intext = oskInText;
    if (maxLen > 64) maxLen = 64;
    oskData.outtextlength = maxLen;
    oskData.outtextlimit = maxLen;
    oskData.outtext = oskOutText;

    oskParams.base.size = sizeof(oskParams);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &oskParams.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &oskParams.base.buttonSwap);
    oskParams.base.graphicsThread = 17;
    oskParams.base.accessThread = 19;
    oskParams.base.fontThread = 18;
    oskParams.base.soundThread = 16;
    oskParams.datacount = 1;
    oskParams.data = &oskData;

    sceUtilityOskInitStart(&oskParams);
    oskActive = true;
}

void signStartEdit(SignTileEntity* ste) {
    if (!ste) return;
    g_signEditing = ste;
    ste->selectedLine = 0;
}

void signEditLine(int line) {
    if (!g_signEditing || line < 0 || line >= SignTileEntity::NUM_LINES) return;
    g_oskSign = g_signEditing;
    g_oskSignLine = line;
    char desc[16];
    snprintf(desc, sizeof(desc), "Sign line %d:", line + 1);
    startOsk(OSK_TARGET_SIGN, desc, g_oskSign->messages[line].c_str(),
             SignTileEntity::MAX_LINE_LENGTH);
}

bool menuOskUpdate(MenuState& s) {
    if (!oskActive) return false;

    int status = sceUtilityOskGetStatus();

    if (status == PSP_UTILITY_DIALOG_QUIT ||
        status == PSP_UTILITY_DIALOG_FINISHED) {
        sceUtilityOskShutdownStart();
    } else if (status == PSP_UTILITY_DIALOG_NONE) {

        oskActive = false;

        if (oskTarget == OSK_TARGET_SIGN) {
            char line[SignTileEntity::MAX_LINE_LENGTH + 1];
            int n = 0;
            for (int i = 0; i < SignTileEntity::MAX_LINE_LENGTH && oskOutText[i]; i++)
                line[n++] = (char)oskOutText[i];
            line[n] = 0;
            if (g_oskSign) g_oskSign->messages[g_oskSignLine] = line;
            g_oskSign = 0;
            return true;
        }

        char* targetStr = 0;
        int   targetCap = 0;
        switch (oskTarget) {
            case OSK_TARGET_WORLD_NAME: targetStr = s.newWorldName; targetCap = sizeof(s.newWorldName); break;
            case OSK_TARGET_WORLD_SEED: targetStr = s.newWorldSeed; targetCap = sizeof(s.newWorldSeed); break;
            case OSK_TARGET_SRV_NAME:   targetStr = s.addName;      targetCap = sizeof(s.addName);      break;
            case OSK_TARGET_SRV_ADDR:   targetStr = s.addAddr;      targetCap = sizeof(s.addAddr);      break;
            case OSK_TARGET_SRV_PORT:   targetStr = s.addPort;      targetCap = sizeof(s.addPort);      break;
            default: return true;
        }

        for (int i = 0; i < targetCap - 1 && oskOutText[i]; i++) {
            targetStr[i] = (char)oskOutText[i];
            targetStr[i+1] = 0;
        }
        if (oskOutText[0] == 0) targetStr[0] = 0;
        return true;
    }

    guStartFrame(0xFF000000u);
    guFinishFrame();
    if (status == PSP_UTILITY_DIALOG_VISIBLE)
        sceUtilityOskUpdate(1);

    guPresent();
    return true;
}

void buttonHintsDraw(MenuState& s, const ButtonHint* hints, int n, float y, float scale) {
    if (!s.haveGui || !s.haveFont) return;

    const float BASE_H = 13.0f, TALL_DROP = 1.0f;
    float x = 6.0f;
    for (int i = 0; i < n; i++) {
        const ButtonIconRect& r = buttonIconRect(hints[i].icon);
        bool held = (g_heldButtons & hints[i].btn) != 0;
        float iy = y + (BASE_H - r.h) * scale + (r.h > BASE_H ? TALL_DROP : 0.0f);
        buttonIconDraw(&s.guiAtlas, hints[i].icon, x, iy, scale,
                       held ? 0xC8C0C0C0u : 0xC8FFFFFFu);
        x += r.w * scale + 3.0f * scale;
        if (hints[i].label[0]) {
            fontDrawTextShadow(&s.font, x, y + (BASE_H - 8.0f) * scale * 0.5f, hints[i].label,
                               0xFFE0E0E0u, scale);
            x += fontTextWidth(&s.font, hints[i].label) * scale + 10.0f * scale;
        }
    }
}

bool optionsScreenUp(const MenuState& s) {
    extern bool g_optionsOpen;
    return s.screen == SCREEN_OPTIONS || g_optionsOpen;
}

void menuHintsDraw(MenuState& s) {
    ButtonHint hints[6];
    int n = 0;
    const bool onOptions = optionsScreenUp(s);

    hints[n++] = (ButtonHint){ BTN_ICON_CROSS,  PSP_CTRL_CROSS,
                               onOptions ? "Toggle" : "Select" };
    if (s.screen != SCREEN_TITLE)
        hints[n++] = (ButtonHint){ BTN_ICON_CIRCLE, PSP_CTRL_CIRCLE, "Back" };

    if (s.screen == SCREEN_CREATE)
        hints[n++] = (ButtonHint){ BTN_ICON_TRIANGLE, PSP_CTRL_TRIANGLE, "Advanced" };
    if (onOptions) {
        hints[n++] = (ButtonHint){ BTN_ICON_LEFT,  PSP_CTRL_LEFT,  "" };
        hints[n++] = (ButtonHint){ BTN_ICON_RIGHT, PSP_CTRL_RIGHT, "Change" };
        hints[n++] = (ButtonHint){ BTN_ICON_L, PSP_CTRL_LTRIGGER, "" };
        hints[n++] = (ButtonHint){ BTN_ICON_R, PSP_CTRL_RTRIGGER, "Group" };
    }
    buttonHintsDraw(s, hints, n);
}

#include "world/level/tile/tile_gui_hooks.h"
#include "client/gamemode/gamemode.h"
#include "world/item/crafting/recipe.h"
#include "client/gui/hud.h"

void guiOpenSignEditor(SignTileEntity* te) { if (te) signStartEdit(te); }
void guiOpenFurnace(FurnaceTileEntity* te) { if (te) furnaceOpen(te); }
void guiOpenChest(ChestTileEntity* te)     { if (te) chestOpen(te); }

bool chestGuiSuppressed() { return g_gameMode && g_gameMode->isCreative(); }
void guiOpenCrafting(bool stonecutter) {
    craftOpen(Recipe::SIZE_3X3, stonecutter ? CRAFT_STONECUTTER : CRAFT_WORKBENCH);
}
void guiChatMessage(const char* msg) { hudChatMessage(msg); }
