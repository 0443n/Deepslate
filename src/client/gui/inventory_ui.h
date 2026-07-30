#pragma once
#include "client/gui/screens/menu.h"

void inventoryDraw(MenuState& s);

enum { INV_BTN_BACK = 0, INV_BTN_CRAFT, INV_BTN_ARMOR, INV_BTN_COUNT };
bool invHeaderButton(MenuState& s, int i, float* x = 0, float* w = 0);
