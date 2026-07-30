
#include <pspctrl.h>
#include <pspgu.h>
#include <cstdio>
#include <cstring>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "world/level/levelgen/level_source.h"
#include "gpu/sprite.h"
#include "client/gui/screens/world_icons.h"

struct WorldsScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void WorldsScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int ) {
    WorldList& worlds = s.worlds;
    int& worldSelected = s.worldSelected;
    int& uiRow = s.uiRow;
    int& topSelected = s.topSelected;
    int& deleteSelected = s.deleteSelected;
    char (&statusMsg)[128] = s.statusMsg;
    AppScreen& screen = s.screen;

    int numEntries = worlds.count + 1;

    if (pressed & PSP_CTRL_DOWN) {
        if (uiRow == 0) uiRow = 1;
        else if (uiRow == 1 && worldSelected < worlds.count) uiRow = 2;
    }
    if (pressed & PSP_CTRL_UP) {
        if (uiRow == 2) uiRow = 1;
        else if (uiRow == 1) uiRow = 0;
    }
    if (pressed & PSP_CTRL_RIGHT) {
        if (uiRow == 0) topSelected = 1;
        else if (uiRow == 1) {
            if (worldSelected < numEntries - 1) worldSelected++;
        }
    }
    if (pressed & PSP_CTRL_LEFT) {
        if (uiRow == 0) topSelected = 0;
        else if (uiRow == 1) {
            if (worldSelected > 0) worldSelected--;
        }
    }

    if (pressed & PSP_CTRL_CIRCLE)
        screen = SCREEN_TITLE;

    if (pressed & PSP_CTRL_CROSS) {
        if (uiRow == 0) {
            if (topSelected == 0) screen = SCREEN_TITLE;
            else {
                createFormReset(s);
                screen = SCREEN_CREATE;
            }
        } else if (uiRow == 1) {
            if (worldSelected < worlds.count) {
                snprintf(statusMsg, sizeof(statusMsg), "Loading: %s", worlds.names[worldSelected]);
                screen = SCREEN_GAME;
            } else {
                createFormReset(s);
                screen = SCREEN_CREATE;
            }
        } else if (uiRow == 2) {
            screen = SCREEN_DELETE;
            deleteSelected = 1;
        }
    }
}

void WorldsScreen::renderContent(MenuState& s) {
    Font& font = s.font; bool haveFont = s.haveFont;
    bool haveGui = s.haveGui;
    bool haveBg = s.haveBg;
    Texture& touchGui = s.touchGui; bool haveTouch = s.haveTouch;
    Texture& defaultWorld = s.defaultWorld; bool haveDefaultWorld = s.haveDefaultWorld;
    WorldList& worlds = s.worlds;
    int& worldSelected = s.worldSelected;
    int& uiRow = s.uiRow;
    int& topSelected = s.topSelected;
    float& listScrollX = s.listScrollX;

    if (haveGui && haveTouch && haveFont && haveDefaultWorld && haveBg) {
        sceGuDisable(GU_DEPTH_TEST);

        const float listScale = 0.88f;
        const float itemWidthV = 120.0f * listScale;
        float targetScrollX = worldSelected * itemWidthV;
        listScrollX += (targetScrollX - listScrollX) * 0.3f;
        if (listScrollX > targetScrollX - 0.5f && listScrollX < targetScrollX + 0.5f) {
            listScrollX = targetScrollX;
        }

        int numEntries = worlds.count + 1;
        float rowY = 42.0f;

        for (int i = 0; i < numEntries; i++) {
            float xOffsetV = (i * itemWidthV) - listScrollX;
            float xCenterV = VW / 2.0f + xOffsetV;
            float xV = xCenterV - itemWidthV / 2.0f;

            float dist = (xCenterV > VW / 2.0f) ? (xCenterV - VW / 2.0f) : (VW / 2.0f - xCenterV);
            float a0 = 1.1f - dist * 0.0055f;
            if (a0 > 1.0f) a0 = 1.0f;
            if (a0 < 0.2f) a0 = 0.2f;

            bool isSelected = (i == worldSelected);
            bool isListActive = (uiRow == 1);
            bool isHovered = (isSelected && isListActive);

            unsigned int alpha = (unsigned int)(255.0f * a0);
            unsigned int colWhite = (alpha << 24) | 0xFFFFFFu;
            unsigned int colGrey  = (alpha << 24) | 0xA0A0A0u;
            unsigned int colTitle = (alpha << 24) | (isHovered ? 0xA0FFFFu : 0xFFFFFFu);

            if (isSelected) {
                float y0V = rowY - 16.0f * listScale;
                float y1V = rowY + 92.0f * listScale;
                unsigned int borderRGB = isHovered ? 0xFFFFFFu : 0x808080u;
                unsigned int borderColor = (alpha << 24) | borderRGB;

                const unsigned int ROW_FILL_ALPHA = 0xC8;
                unsigned int bgColor = ((alpha * ROW_FILL_ALPHA / 255) << 24) | 0x2D2D2Du;

                const float bw = 1.0f;
                const float cx = xV - bw, cy = y0V;
                const float cw = itemWidthV + bw * 2.0f, ch = y1V - y0V;
                drawRect(cx * UI_SCALE, cy * UI_SCALE, cw * UI_SCALE, bw * UI_SCALE, borderColor);
                drawRect(cx * UI_SCALE, (cy + ch - bw) * UI_SCALE, cw * UI_SCALE, bw * UI_SCALE, borderColor);
                drawRect(cx * UI_SCALE, cy * UI_SCALE, bw * UI_SCALE, ch * UI_SCALE, borderColor);
                drawRect((cx + cw - bw) * UI_SCALE, cy * UI_SCALE, bw * UI_SCALE, ch * UI_SCALE, borderColor);

                drawRect(xV * UI_SCALE, (y0V + bw) * UI_SCALE, itemWidthV * UI_SCALE,
                         (ch - bw * 2.0f) * UI_SCALE, bgColor);
            }

            if (i < worlds.count) {

                int iconDist = i > worldSelected ? i - worldSelected : worldSelected - i;
                Texture* img = iconDist <= 1 ? worldIcon(worlds.names[i]) : 0;
                if (!img) img = &defaultWorld;
                textureBind(img);
                float imgW = 64.0f * listScale, imgH = 48.0f * listScale;
                float imgX = xCenterV - imgW / 2.0f;
                float imgY = rowY - 8.0f * listScale;

                float srcY = img->realH * 0.125f;
                float srcH = img->realH * 0.75f;

                spriteDraw(img, imgX * UI_SCALE, imgY * UI_SCALE, imgW * UI_SCALE, imgH * UI_SCALE,
                           0.0f, srcY, (float)img->realW, srcH, colWhite);

                float xText = xCenterV - 55.0f * listScale;

                const float rowTextMaxW = 120.0f - 10.0f;

                const char* t0 = worlds.displayNames[i];
                fontDrawTextClipped(&font, xText * UI_SCALE, (rowY + 44.0f * listScale) * UI_SCALE, t0, colTitle, UI_SCALE * listScale, rowTextMaxW);

                const char* t1 = worlds.dates[i];
                fontDrawTextShadow(&font, xText * UI_SCALE, (rowY + 54.0f * listScale) * UI_SCALE, t1, colGrey, UI_SCALE * listScale);

                const char* t2 = worlds.names[i];
                fontDrawTextClipped(&font, xText * UI_SCALE, (rowY + 64.0f * listScale) * UI_SCALE, t2, colGrey, UI_SCALE * listScale, rowTextMaxW);

                const char* t3 = worlds.gameModes[i] == 1 ? "Creative" : "Survival";
                fontDrawTextShadow(&font, xText * UI_SCALE, (rowY + 74.0f * listScale) * UI_SCALE, t3, colGrey, UI_SCALE * listScale);
            } else {

                textureBind(&touchGui);
                float w = 54.0f * listScale, h = 54.0f * listScale;
                float iconX = xCenterV - w / 2.0f;
                float iconY = rowY + 4.0f * listScale;
                float iconV = isHovered ? 86.0f : 32.0f;
                spriteDraw(&touchGui, iconX * UI_SCALE, iconY * UI_SCALE, w * UI_SCALE, h * UI_SCALE,
                           168.0f, iconV, 54.0f, 54.0f, colWhite);

                const char* cn = "Create new";
                float cnw = fontTextWidth(&font, cn) * listScale;
                fontDrawTextShadow(&font, (xCenterV - cnw / 2.0f) * UI_SCALE, (rowY + 62.0f * listScale) * UI_SCALE, cn, colTitle, UI_SCALE * listScale);
            }
        }

        if (worlds.count > 0 && worldSelected < worlds.count) {
            bool delHovered = (uiRow == 2);
            textureBind(&touchGui);

            float trashW = 24.0f, trashH = 18.0f;
            float trashX = (VW - trashW) / 2.0f;
            float trashY = VH - 22.0f;

            float scale = delHovered ? 0.95f : 1.0f;
            float drawW = trashW * scale;
            float drawH = trashH * scale;
            float drawX = trashX + (trashW - drawW) / 2.0f;
            float drawY = trashY + (trashH - drawH) / 2.0f;

            float trashU = delHovered ? 184.0f : 150.0f;

            spriteDraw(&touchGui, drawX * UI_SCALE, drawY * UI_SCALE, drawW * UI_SCALE, drawH * UI_SCALE,
                       trashU, 0.0f, 34.0f, 26.0f, WHITE);
        }

        {

            float lb = 4.0f * MENU_PX + menuBarButtonW(s, "Back");
            float rb = VW - 4.0f * MENU_PX - menuBarButtonW(s, "Create new");
            drawMenuHeader(s, "Select world", 0.0f, VW, MENU_BAR_H, MENU_BAR_TEXT, lb, rb - lb);
        }
        {
            float bw = menuBarButtonW(s, "Back");
            menuBarButton(s, 4.0f * MENU_PX, bw, "Back", uiRow == 0 && topSelected == 0);
            float cw = menuBarButtonW(s, "Create new");
            menuBarButton(s, VW - cw - 4.0f * MENU_PX, cw, "Create new",
                          uiRow == 0 && topSelected == 1);
        }

        sceGuEnable(GU_DEPTH_TEST);
    }
}

static WorldsScreen s_worldsScreen;
Screen& worldsScreen() { return s_worldsScreen; }
