#include "world/level/pathfinder/path.h"
#include "world/entity/entity.h"

Path::Path() : length(0), index(0) {}

bool Path::isEmpty() const { return length == 0; }

void Path::copyPoints(const short* xyz, int len) {
    if (len > MAX_PATH) len = MAX_PATH;
    length = (short)len;
    index = 0;
    for (int i = 0; i < len; ++i) {
        points[i].x = xyz[i * 3];
        points[i].y = xyz[i * 3 + 1];
        points[i].z = xyz[i * 3 + 2];
    }
}

void Path::destroy() { index = length = 0; }

Vec3 Path::currentPos(Entity* e) const { return getPos(e, index); }
void Path::next() { index++; }
int  Path::getSize() const { return length; }
bool Path::isDone() const { return index >= length; }
int  Path::getIndex() const { return index; }
void Path::setIndex(int i) { index = (short)i; }

void Path::getPoint(int i, int& x, int& y, int& z) const {
    x = points[i].x; y = points[i].y; z = points[i].z;
}

void Path::lastPoint(int& x, int& y, int& z) const {
    getPoint(length > 0 ? length - 1 : 0, x, y, z);
}

Vec3 Path::getPos(Entity* e, int i) const {
    float half = (int)(e->bbWidth + 1) * 0.5f;
    return Vec3(points[i].x + half, (float)points[i].y, points[i].z + half);
}
