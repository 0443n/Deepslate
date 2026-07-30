
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
static const float yBaseV   = 2.0f + VH / 3.0f;
static const float spacingV = (VW - 3.0f * btnSizeV) / 4.0f;
static PocketButton buttons[3] = {
    { (spacingV + 0 * (btnSizeV + spacingV)) * UI_SCALE, yBaseV * UI_SCALE, btnSizeV * UI_SCALE, 0.0f, 176.0f, 75.0f, "Join Game",  true },
    { (spacingV + 1 * (btnSizeV + spacingV)) * UI_SCALE, yBaseV * UI_SCALE, btnSizeV * UI_SCALE, 0.0f, 101.0f, 75.0f, "Start Game", true },
    { (spacingV + 2 * (btnSizeV + spacingV)) * UI_SCALE, yBaseV * UI_SCALE, btnSizeV * UI_SCALE, 0.0f,  26.0f, 75.0f, "Options",    true },
};
static const int numButtons = 3;
struct TitleScreen : Screen {
    void renderContent(MenuState& s);
    void handleInput(MenuState& s, unsigned int pressed, unsigned int held);
};

void TitleScreen::handleInput(MenuState& s, unsigned int pressed, unsigned int ) {

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

    const float LOGO_SCALE = 1.15f;
    float logoYV = 4.0f;

    float logoWV = ((VW < (float)logo.realW) ? VW : (float)logo.realW) * LOGO_SCALE;
    float logoHV = logoWV / (float)logo.realW * (float)logo.realH;
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
        sceGuEnable(GU_DEPTH_TEST);
    }
}

static TitleScreen s_titleScreen;
Screen& titleScreen() { return s_titleScreen; }
