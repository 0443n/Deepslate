
#include <pspctrl.h>
#include <pspgu.h>

#include "client/gui/screens/menu.h"
#include "client/gui/screens/screen.h"
#include "gpu/sprite.h"
#include "gpu/widgets.h"

#include <cmath>
#include <ctime>
#include <pspkernel.h>

#include "client/gui/screens/splashes.h"

static int s_splash = -1;

static const float btnSizeV = 75.0f;
static const float BTN_PX   = btnSizeV * UI_SCALE;
static const float BTN_Y    = 95.0f;
static const float BTN_X0   = 7.0f;
static const float BTN_X1   = BTN_X0 + BTN_PX + 8.0f;
static const float BTN_X2   = BTN_X1 + BTN_PX + 8.0f;
static PocketButton buttons[3] = {
    { BTN_X0, BTN_Y, BTN_PX, 0.0f, 176.0f, 75.0f, "Join Game",  true },
    { BTN_X1, BTN_Y, BTN_PX, 0.0f, 101.0f, 75.0f, "Start Game", true },
    { BTN_X2, BTN_Y, BTN_PX, 0.0f,  26.0f, 75.0f, "Options",    true },
};
static const int numButtons = 3;

static const unsigned int kTitleSeed[3] = {
    0x0251B8B0u, 0x1360B0C0u, 0x00000275u
};
#define TITLE_SEED_LEN 14
static int s_seedHold = 0;

struct TitleScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void TitleScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int held) {

    static const unsigned int SEED_MASK = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_UP;
    s_seedHold = ((held & SEED_MASK) == SEED_MASK) ? (s_seedHold + 1) : 0;

    int& selected = s.selected;
    AppScreen& screen = s.screen;
    char (&statusMsg)[128] = s.statusMsg;
    int& optFocus = s.optFocus;
    int& optTabHighlight = s.optTabHighlight;
    int& optItemHighlight = s.optItemHighlight;
    int& optCategory = s.optCategory;

    if (pressed & PSP_CTRL_RIGHT)
        selected = (selected < 0) ? 1 : (selected + 1) % numButtons;
    if (pressed & PSP_CTRL_LEFT)
        selected = (selected < 0) ? 1 : (selected + numButtons - 1) % numButtons;

    if ((pressed & PSP_CTRL_CROSS) && selected >= 0) {
        if (selected == 1) {
            screen = SCREEN_WORLDS;
            statusMsg[0] = '\0';
        } else if (selected == 0) {
            joinListReset(s);
            screen = SCREEN_JOIN;
            statusMsg[0] = '\0';
        } else {
            optFocus = 1;
            optTabHighlight = optCategory;
            optItemHighlight = 0;
            screen = SCREEN_OPTIONS;
            statusMsg[0] = '\0';
        }
    }
}

void TitleScreen::renderContent(MenuState& s) {
    Font& font = s.font; bool haveFont = s.haveFont;
    Texture& guiAtlas = s.guiAtlas; bool haveGui = s.haveGui;
    Texture& logo = s.logo; bool haveLogo = s.haveLogo;
    Texture& touchGui = s.touchGui; bool haveTouch = s.haveTouch;
    int& selected = s.selected;

    float logoYV = 6.0f;

    float logoWV = (float)logo.realW / UI_SCALE;
    float logoHV = (float)logo.realH / UI_SCALE;
    float logoXV = (VW - logoWV) / 2.0f;
    if (haveLogo) {
        textureBind(&logo);
        sceGuDisable(GU_DEPTH_TEST);
        spriteDraw(&logo, logoXV * UI_SCALE, logoYV * UI_SCALE,
                  logoWV * UI_SCALE, logoHV * UI_SCALE,
                  0, 0, (float)logo.realW, (float)logo.realH, WHITE);
        sceGuEnable(GU_DEPTH_TEST);
    }

    if (haveFont) {

        if (s_splash < 0) {
            unsigned seed = (unsigned)time(0) * 2654435761u + sceKernelGetSystemTimeLow();
            s_splash = (int)(seed % (unsigned)kSplashCount);
        }
        const char* splash = kSplashes[s_splash];

        float t = (float)sceKernelGetSystemTimeLow() * 1e-6f;
        float scale = powf(sinf(t * 3.14f * 2.3f), 4.0f) * 0.06f + 1.3f;

        float len = (float)fontTextWidth(&font, splash);
        float fit = (VW * 0.3125f) / (len * 1.3f);
        if (fit > 1.0f) fit = 1.0f;

        sceGuDisable(GU_DEPTH_TEST);
        fontDrawTransformed(&font, (logoXV + logoWV) * 0.71f * UI_SCALE,
                            (logoYV + logoHV - 15.0f) * UI_SCALE,
                            splash, 0xFF00FFFFu ,
                            -20.0f, scale * fit * UI_SCALE, true);
        sceGuEnable(GU_DEPTH_TEST);
    }

    if (haveGui && haveTouch && haveFont) {
        sceGuDisable(GU_DEPTH_TEST);
        for (int i = 0; i < numButtons; i++)
            pocketButtonDraw(&font, &guiAtlas, &touchGui, &buttons[i], i == selected, UI_SCALE);
        sceGuEnable(GU_DEPTH_TEST);
    }

    if (haveFont) {
        sceGuDisable(GU_DEPTH_TEST);
        const char* copyright = "\xffMojang AB";
        float cw = fontTextWidth(&font, copyright) * UI_SCALE;
        fontDrawTextShadow(&font, 480.0f - cw - 4.0f, 272.0f - 9.0f * UI_SCALE, copyright, WHITE, UI_SCALE);
        if (s_seedHold > 30) {
            char line[TITLE_SEED_LEN + 1];
            for (int i = 0; i < TITLE_SEED_LEN; i++) {
                unsigned int v = (kTitleSeed[i / 6] >> (5 * (i % 6))) & 31u;
                line[i] = v ? (char)(0x40u + v) : ' ';
            }
            line[TITLE_SEED_LEN] = '\0';
            float lw = fontTextWidth(&font, line) * UI_SCALE;
            fontDrawTextShadow(&font, (480.0f - lw) * 0.5f, 272.0f - 20.0f * UI_SCALE,
                               line, 0xFF80FFFFu, UI_SCALE);
        }
        sceGuEnable(GU_DEPTH_TEST);
    }
}

static TitleScreen s_titleScreen;
Screen& titleScreen() { return s_titleScreen; }
