#pragma once

// A CPU fault leaves the display controller scanning out the last framebuffer,
// so hex written straight into it is the only crash dump this build can take.
void fbTextFrameBegin(void);
void fbTextHex(int row, int col, unsigned int value, int digits);
void fbTextClear(int row);
