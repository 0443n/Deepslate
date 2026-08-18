
#ifndef MCPSP_WORLD_LEVEL_TILE_TILE_SHAPES_H
#define MCPSP_WORLD_LEVEL_TILE_TILE_SHAPES_H

#include "client/player/physics.h"

int tileShapeBoxes(const World* w, int x, int y, int z, unsigned char id,
                   unsigned char data, float out[3][6]);

void trapdoorShape(unsigned char data, float out[6]);

int  stairShapeBoxes(const World* w, int gx, int y, int gz, unsigned char data, float out[3][6]);

float stairTopAt(const World* w, int gx, int y, int gz, unsigned char data, float fx, float fz);

float stairTopEntering(const World* w, int gx, int y, int gz, unsigned char data,
                       float xa, float za, float wx, float wz);

float stairTopAt(const World* w, int gx, int y, int gz, unsigned char data, float fx, float fz);

float stairTopEntering(const World* w, int gx, int y, int gz, unsigned char data,
                       float xa, float za, float wx, float wz);

void chestShapeBox(const World* w, int gx, int y, int gz, float out[6]);

#endif
