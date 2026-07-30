
#include <pspctrl.h>
#include <pspgu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "gpu/sprite.h"
#include "gpu/gui_atlas.h"

struct AddServerScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void addServerFormReset(MenuState& s) {
    s.addSelected = 1;
    s.addName[0] = '\0';
    snprintf(s.addAddr, sizeof(s.addAddr), "127.0.0.1");
    snprintf(s.addPort, sizeof(s.addPort), "19132");
}

static const float COL_X = 12.0f;
static const float BOX_H = 16.0f;
static float colW() { return VW / 2.0f - COL_X - 2.0f; }
static float formTop()    { return MENU_BAR_H + 4.0f; }
static float formBottom() { return UI_HINTS_Y / UI_SCALE - 4.5f; }

static float labelY(int field) {
    return formTop() + 3.5f + field * (BOX_H + 16.0f);
}
static float boxY(int field) { return labelY(field) + 12.0f; }

static float addButtonW(MenuState& s) { return menuBarButtonW(s, "Add Server"); }
static float addButtonY() { return labelY(1); }

static bool formValid(const MenuState& s) {
    return s.addName[0] && s.addAddr[0] && strtol(s.addPort, 0, 0) > 0;
}

void AddServerScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int ) {
    int& sel = s.addSelected;
    AppScreen& screen = s.screen;

    static int lastField = 1;
    if (sel >= 1 && sel <= 3) lastField = sel;

    if (pressed & PSP_CTRL_DOWN) {
        if (sel == 0) sel = lastField;
        else if (sel >= 1 && sel < 3) sel++;
    }
    if (pressed & PSP_CTRL_UP) {
        if (sel == 4 || sel == 1) sel = 0;
        else if (sel > 1) sel--;
    }
    if (pressed & PSP_CTRL_RIGHT) {
        if (sel >= 1 && sel <= 3) sel = 4;
    }
    if (pressed & PSP_CTRL_LEFT) {
        if (sel == 4 || sel == 0) sel = lastField;
    }

    if (pressed & PSP_CTRL_CIRCLE)
        screen = SCREEN_JOIN;

    if (pressed & PSP_CTRL_CROSS) {
        switch (sel) {
            case 0: screen = SCREEN_JOIN; break;
            case 1: startOsk(OSK_TARGET_SRV_NAME, "Server Name:", s.addName, 16); break;
            case 2: startOsk(OSK_TARGET_SRV_ADDR, "Server Address:", s.addAddr, 63); break;
            case 3: startOsk(OSK_TARGET_SRV_PORT, "Server Port:", s.addPort, 6); break;
            case 4:
                if (formValid(s)) {
                    externalServersAdd(&s.servers, s.addName, s.addAddr,
                                       (int)strtol(s.addPort, 0, 0));
                    s.joinRow = s.servers.count - 1;
                    if (s.joinRow < 0) s.joinRow = 0;
                    screen = SCREEN_JOIN;
                }
                break;
        }
    }
}

void AddServerScreen::renderContent(MenuState& s) {
    if (!s.haveTouch || !s.haveFont || !s.haveGui) return;
    int sel = s.addSelected;

    sceGuDisable(GU_DEPTH_TEST);

    drawNinePatch(s, GA_SS_PANEL, 3.0f,
                  5.0f, formTop(), VW - 10.0f, formBottom() - formTop());

    const char* labels[3] = { "Server Name", "Address", "Port" };
    const char* values[3] = { s.addName, s.addAddr, s.addPort };
    for (int i = 0; i < 3; i++) {
        fontDrawTextShadow(&s.font, COL_X * UI_SCALE, labelY(i) * UI_SCALE,
                           labels[i], 0xFFBBBBBBu, UI_SCALE);
        drawTextField(s, COL_X, boxY(i), colW(), BOX_H, values[i], labels[i], sel == i + 1);
    }

    fontDrawTextWrapped(&s.font, (VW / 2.0f + 10.0f) * UI_SCALE, labelY(0) * UI_SCALE,
                        "Add server by IP/Address.", 0xFFBBBBBBu, UI_SCALE,
                        VW / 2.0f - 20.0f);

    {
        float bw = addButtonW(s), bh = MENU_BAR_BTNH;
        float bx = 3.0f * ((VW - 10.0f) / 4.0f) - bw / 2.0f;
        bool active = formValid(s), hovered = (sel == 4);
        guiTButton(s, bx, addButtonY(), bw, bh, hovered, MENU_BEVEL);
        guiTButtonLabel(s, bx, addButtonY(), bw, bh, "Add Server", hovered, active, MENU_BAR_TEXT);
    }

    {
        float bw = menuBarButtonW(s, "Back");
        float rb = VW - 4.0f * MENU_PX - bw;
        drawMenuHeader(s, "Add External Server", 0.0f, VW, MENU_BAR_H, MENU_BAR_TEXT, 0.0f, rb);
        menuBarButton(s, rb, bw, "Back", sel == 0);
    }

    sceGuEnable(GU_DEPTH_TEST);
}

static AddServerScreen s_addServerScreen;
Screen& addServerScreen() { return s_addServerScreen; }
