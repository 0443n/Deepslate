#ifndef PLATFORM_CANARY_H
#define PLATFORM_CANARY_H

#define CANARY_WORDS 16
#define CANARY_FILL  0xC0DEFACEu

static inline void canaryArm(volatile unsigned int* guard) {
    for (int i = 0; i < CANARY_WORDS; i++) guard[i] = CANARY_FILL;
}

static inline int canaryCheck(const volatile unsigned int* guard) {
    for (int i = 0; i < CANARY_WORDS; i++)
        if (guard[i] != CANARY_FILL) return i + 1;
    return 0;
}

#endif
