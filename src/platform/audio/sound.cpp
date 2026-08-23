#include "platform/audio/sound.h"

#include <pspkernel.h>
#include <pspaudio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>

#include "platform/path.h"
#include <pspmp3.h>
#include <psputility.h>

#define SAMPLE_RATE   44100
#define SAMPLE_COUNT   1024
#define MAX_VOICES       16

#define NAME_LEN         24
#define PACK_MAGIC 0x4753434DU
#define MUSIC_MAGIC 0x34554D4DU

struct Entry {
    char         name[NAME_LEN];
    unsigned int offset;
    unsigned int frames;
};

struct Voice {
    const signed char* frames;
    unsigned int frameCount;

    unsigned int frame;
    unsigned int frac;
    unsigned int step;
    int          vol;
    unsigned char cat;
    volatile int playing;
};

static Entry*             g_index;
static int                g_count;
static const signed char* g_pcm;
static unsigned int       g_srcRate = 22050;
static Voice              g_voices[MAX_VOICES];
static int                g_channel = -1;

extern volatile bool      g_guDialogActive;
static float              g_master  = 1.0f;

static float              g_catVol[SND_CAT_COUNT];
static struct CatVolDefaults {
    CatVolDefaults() { for (int i = 0; i < SND_CAT_COUNT; i++) g_catVol[i] = 1.0f; }
} g_catVolDefaults;

static Entry*             g_caveIndex;
static int                g_caveCount;
static unsigned int       g_caveRate;
static unsigned int       g_cavePcmBase;
static unsigned int       g_caveMaxFrames;
static signed char*       g_caveBuf;
static char               g_cavePath[64];

#define MUSIC_HALF_SAMPLES 16384
#define MUSIC_IN_BUF       (16 * 1024)
#define MUSIC_PCM_BUF      (9216)
static Entry*             g_musIndex;
static int                g_musCount;
static unsigned int       g_musRate;
static unsigned int       g_musPcmBase;
static char               g_musPath[64];
static bool*              g_musHeard;
static bool               g_musResource;

static short*             g_musBuf;
static volatile int       g_musReady[2];
static volatile int       g_musPlaying;
static volatile int       g_musEnded;
static int                g_musHalf;
static unsigned int       g_musPos;
static unsigned int       g_musFilled;
static FILE*              g_musFile;
static unsigned int       g_musNextAt;
static int                g_musHandle = -1;
static unsigned char*     g_musInBuf;
static unsigned char*     g_musPcmBuf;
static short*             g_musPcmPtr;
static int                g_musPcmLeft;
static int                g_thid    = -1;
static volatile int       g_mixerQuit = 0;

static bool loadPack(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    unsigned int header[4];
    if (fread(header, sizeof(header), 1, f) != 1 || header[0] != PACK_MAGIC) {
        fclose(f);
        return false;
    }

    int count = (int)header[1];
    unsigned int pcmBytes = header[2];
    unsigned int rate     = header[3];
    if (count <= 0 || !rate) { fclose(f); return false; }

    Entry* index      = (Entry*)malloc(sizeof(Entry) * count);
    signed char* pcm  = (signed char*)malloc(pcmBytes);
    if (!index || !pcm ||
        fread(index, sizeof(Entry), count, f) != (size_t)count ||
        fread(pcm, 1, pcmBytes, f) != pcmBytes) {
        free(index);
        free(pcm);
        fclose(f);
        return false;
    }
    fclose(f);

    g_index   = index;
    g_count   = count;
    g_pcm     = pcm;
    g_srcRate = rate;
    return true;
}

static int findFirst(const char* name) {
    int lo = 0, hi = g_count - 1, hit = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(g_index[mid].name, name);
        if (cmp < 0)      lo = mid + 1;
        else if (cmp > 0) hi = mid - 1;
        else { hit = mid; hi = mid - 1; }
    }
    return hit;
}

void soundMixBlock(short* out) {

    bool any = (g_musPlaying && !g_guDialogActive);
    for (int v = 0; v < MAX_VOICES && !any; v++) {
        if (g_voices[v].playing) any = true;
    }
    if (!any) {
        memset(out, 0, SAMPLE_COUNT * 2 * sizeof(short));
        return;
    }

    static int mix[SAMPLE_COUNT];

    memset(mix, 0, sizeof(mix));

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* s = &g_voices[v];
        if (!s->playing) continue;

        unsigned int frame = s->frame, frac = s->frac;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            if (frame >= s->frameCount) { s->playing = 0; break; }

            int sample1 = s->frames[frame] << 8;
            int sample2 = (frame + 1 < s->frameCount) ? (s->frames[frame + 1] << 8) : sample1;
            int interp = sample1 + (((sample2 - sample1) * (int)frac) >> 16);

            mix[i] += (interp * s->vol) >> 12;

            frac += s->step;
            frame += frac >> 16;
            frac  &= 0xFFFF;
        }
        s->frame = frame; s->frac = frac;
    }

    if (g_musPlaying && !g_guDialogActive) {
        int vol = (int)(g_catVol[SND_CAT_MUSIC] * 4096.0f);
        unsigned int pos = g_musPos, halfN = MUSIC_HALF_SAMPLES;
        int half = g_musHalf;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            if (!g_musReady[half]) {
                if (g_musEnded) g_musPlaying = 0;
                break;
            }
            const short* h = g_musBuf + half * halfN;
            int s1 = h[pos];

            mix[i] += ((s1 * vol) >> 12);
            if (++pos >= halfN) {
                pos = 0;
                g_musReady[half] = 0;
                half ^= 1;
            }
        }
        g_musPos = pos; g_musHalf = half;
    }

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int sample = mix[i];
        if (sample >  32767) sample =  32767;
        if (sample < -32768) sample = -32768;
        out[i * 2] = out[i * 2 + 1] = (short)sample;
    }
}

static int mixerThread(SceSize , void* ) {

    static short out[2][SAMPLE_COUNT * 2];
    int buf = 0;

    static const int MIX_PRIO_GAME   = 0x12;
    static const int MIX_PRIO_DIALOG = 0x22;
    bool loweredForDialog = false;

    while (!g_mixerQuit) {
        if (g_guDialogActive != loweredForDialog) {
            loweredForDialog = g_guDialogActive;
            sceKernelChangeThreadPriority(0, loweredForDialog ? MIX_PRIO_DIALOG
                                                              : MIX_PRIO_GAME);
        }
        soundMixBlock(out[buf]);

        int vol = (int)(g_master * PSP_AUDIO_VOLUME_MAX);
        sceAudioOutputPannedBlocking(g_channel, vol, vol, out[buf]);
        buf ^= 1;
    }
    return 0;
}

static bool loadCaveIndex(const char* path);
static bool loadMusicIndex(const char* path);
static void musicReleaseResource(void);

void soundInit(void) {

    srand(sceKernelGetSystemTimeLow());

    extern int g_lowMemPsp;
    const char* want  = g_lowMemPsp ? "data/sound/sounds_lo.bin" : "data/sound/sounds.bin";
    const char* other = g_lowMemPsp ? "data/sound/sounds.bin"    : "data/sound/sounds_lo.bin";
    if (!loadPack(assetPath(want))  && !loadPack(want) &&
        !loadPack(assetPath(other)) && !loadPack(other)) {
        return;
    }

    if (!loadCaveIndex(assetPath("data/sound/caves.bin")))
        loadCaveIndex("data/sound/caves.bin");

    if (!loadMusicIndex(assetPath("data/sound/music.bin")))
        loadMusicIndex("data/sound/music.bin");

    g_channel = sceAudioChReserve(0, SAMPLE_COUNT, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return;

    g_mixerQuit = 0;
    g_thid = sceKernelCreateThread("sound_thread", mixerThread, 0x12, 0x10000,
                                   PSP_THREAD_ATTR_USER, 0);
    if (g_thid < 0) return;
    sceKernelStartThread(g_thid, 0, 0);
}

void soundShutdown(void) {
    if (g_thid >= 0) {
        g_mixerQuit = 1;

        SceUInt timeout = 1000 * 1000;
        sceKernelWaitThreadEnd(g_thid, &timeout);
        sceKernelDeleteThread(g_thid);
        g_thid = -1;
    }
    if (g_channel >= 0) { sceAudioChRelease(g_channel); g_channel = -1; }
    free((void*)g_pcm);  g_pcm = 0;
    free(g_index);       g_index = 0;
    g_count = 0;
    free(g_caveBuf);     g_caveBuf = 0;
    free(g_caveIndex);   g_caveIndex = 0;
    g_caveCount = 0;
    soundMusicStop();
    musicReleaseResource();
    free(g_musBuf);      g_musBuf = 0;
    free(g_musInBuf);    g_musInBuf = 0;
    free(g_musPcmBuf);   g_musPcmBuf = 0;
    free(g_musIndex);    g_musIndex = 0;
    free(g_musHeard);    g_musHeard = 0;
    g_musCount = 0;
}

void soundSetVolume(float volume) {
    g_master = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

void soundSetCategoryVolume(int cat, float volume) {
    if (cat < 0 || cat >= SND_CAT_COUNT) return;
    g_catVol[cat] = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);

    if (cat == SND_CAT_MUSIC && g_catVol[cat] <= 0.0f) {
        g_musPlaying = 0;
        g_musReady[0] = 0; g_musReady[1] = 0;
        musicReleaseResource();
    }
}

float soundAttenuate(float distSq, float volume) {

    float dd = 16.0f;
    if (volume > 1.0f) dd *= volume;
    if (distSq >= dd * dd) return 0.0f;

    float mult = 1.1f - sqrtf(distSq) / 20.0f;
    if (mult < -1.0f) mult = -1.0f; else if (mult > 1.0f) mult = 1.0f;
    float v = volume * mult;
    if (v <= 0.0f) return 0.0f;
    return v > 1.0f ? 1.0f : v;
}

static bool loadCaveIndex(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    unsigned int header[4];
    if (fread(header, sizeof(header), 1, f) != 1 || header[0] != PACK_MAGIC || !header[1] || !header[3]) {
        fclose(f);
        return false;
    }
    int count = (int)header[1];
    Entry* index = (Entry*)malloc(sizeof(Entry) * count);
    if (!index || fread(index, sizeof(Entry), count, f) != (size_t)count) {
        free(index);
        fclose(f);
        return false;
    }
    fclose(f);

    g_caveMaxFrames = 0;
    for (int i = 0; i < count; i++)
        if (index[i].frames > g_caveMaxFrames) g_caveMaxFrames = index[i].frames;

    g_caveIndex   = index;
    g_caveCount   = count;
    g_caveRate    = header[3];
    g_cavePcmBase = sizeof(header) + sizeof(Entry) * count;
    snprintf(g_cavePath, sizeof(g_cavePath), "%s", path);
    return true;
}

static void playCave(float volume, float pitch) {
    if (!g_caveCount) return;

    for (int v = 0; v < MAX_VOICES; v++)
        if (g_voices[v].playing && g_voices[v].frames == g_caveBuf && g_caveBuf) return;

    Voice* s = 0;
    for (int v = 0; v < MAX_VOICES; v++)
        if (!g_voices[v].playing) { s = &g_voices[v]; break; }
    if (!s) return;

    if (!g_caveBuf) {
        g_caveBuf = (signed char*)malloc(g_caveMaxFrames);
        if (!g_caveBuf) return;
    }

    const Entry* e = &g_caveIndex[rand() % g_caveCount];
    FILE* f = fopen(g_cavePath, "rb");
    if (!f) return;
    bool ok = fseek(f, (long)(g_cavePcmBase + e->offset), SEEK_SET) == 0 &&
              fread(g_caveBuf, 1, e->frames, f) == e->frames;
    fclose(f);
    if (!ok) return;

    s->frames     = g_caveBuf;
    s->frameCount = e->frames;
    s->frame      = 0;
    s->frac       = 0;
    s->step       = (unsigned int)(((float)g_caveRate / SAMPLE_RATE) * pitch * 65536.0f);
    s->vol        = (int)(volume * 4096.0f);
    s->playing    = 1;
}

static bool loadMusicIndex(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    unsigned int header[4];
    if (fread(header, sizeof(header), 1, f) != 1 || header[0] != MUSIC_MAGIC ||
        !header[1] || header[3] != SAMPLE_RATE) {
        fclose(f);
        return false;
    }
    int count = (int)header[1];
    Entry* index = (Entry*)malloc(sizeof(Entry) * count);
    bool*  heard = (bool*)calloc(count, sizeof(bool));
    if (!index || !heard || fread(index, sizeof(Entry), count, f) != (size_t)count) {
        free(index);
        free(heard);
        fclose(f);
        return false;
    }
    fclose(f);

    g_musIndex   = index;
    g_musHeard   = heard;
    g_musCount   = count;
    g_musRate    = header[3];
    g_musPcmBase = sizeof(header) + sizeof(Entry) * count;
    snprintf(g_musPath, sizeof(g_musPath), "%s", path);
    return true;
}

static int g_musLast = -1;

static int musicPickTrack(void) {
    int unheard = 0;
    for (int i = 0; i < g_musCount; i++) if (!g_musHeard[i]) unheard++;
    if (unheard == 0) {
        for (int i = 0; i < g_musCount; i++) g_musHeard[i] = false;
        unheard = g_musCount;
    }

    const bool skipLast = (g_musCount > 1 && g_musLast >= 0 && !g_musHeard[g_musLast]);
    int avail = unheard - (skipLast ? 1 : 0);
    int k = rand() % avail;
    for (int i = 0; i < g_musCount; i++) {
        if (g_musHeard[i]) continue;
        if (skipLast && i == g_musLast) continue;
        if (k-- == 0) { g_musHeard[i] = true; g_musLast = i; return i; }
    }
    return 0;
}

static void musicFeed(void) {
    while (sceMp3CheckStreamDataNeeded(g_musHandle) > 0) {
        unsigned char* dst = 0;
        SceInt32 towrite = 0, srcpos = 0;
        if (sceMp3GetInfoToAddStreamData(g_musHandle, &dst, &towrite, &srcpos) < 0) return;
        if (towrite <= 0) return;
        if (fseek(g_musFile, srcpos, SEEK_SET) != 0) return;
        int got = (int)fread(dst, 1, towrite, g_musFile);

        sceMp3NotifyAddStreamData(g_musHandle, got > 0 ? got : 0);
        if (got <= 0) return;
    }
}

#define MUSIC_CHUNKS_PER_CALL 4
static int          g_musFillHalf = -1;
static unsigned int g_musFillPos  = 0;

static bool musicFillStep(int half) {
    if (g_musFillHalf != half) { g_musFillHalf = half; g_musFillPos = 0; }

    short* out = g_musBuf + half * MUSIC_HALF_SAMPLES;
    int chunks = 0;

    while (g_musFillPos < MUSIC_HALF_SAMPLES) {
        if (g_musPcmLeft <= 0) {
            if (chunks >= MUSIC_CHUNKS_PER_CALL) return false;
            musicFeed();
            SceShort16* pcm = 0;
            SceInt32 bytes = sceMp3Decode(g_musHandle, &pcm);
            if (bytes <= 0 || !pcm) {
                g_musEnded = 1;
                break;
            }
            g_musPcmPtr  = (short*)pcm;
            g_musPcmLeft = bytes / 4;
            chunks++;
        }
        unsigned int take = MUSIC_HALF_SAMPLES - g_musFillPos;
        if (take > (unsigned int)g_musPcmLeft) take = (unsigned int)g_musPcmLeft;
        short* dst = out + g_musFillPos;
        for (unsigned int i = 0; i < take; i++)
            dst[i] = (short)((g_musPcmPtr[i * 2] + g_musPcmPtr[i * 2 + 1]) >> 1);
        g_musPcmPtr  += take * 2;
        g_musPcmLeft -= (int)take;
        g_musFillPos += take;
    }

    if (g_musFillPos < MUSIC_HALF_SAMPLES)
        memset(out + g_musFillPos, 0, (MUSIC_HALF_SAMPLES - g_musFillPos) * sizeof(short));
    g_musFilled      = g_musFillPos;
    g_musReady[half] = 1;
    g_musFillHalf    = -1;
    return true;
}

static bool musicInitResource(void) {
    if (g_musResource) return true;
    if (sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC) < 0) return false;
    if (sceUtilityLoadModule(PSP_MODULE_AV_MP3) < 0)     return false;
    if (sceMp3InitResource() < 0)                        return false;
    g_musResource = true;
    return true;
}

static void musicRelease(void) {
    if (g_musHandle >= 0) { sceMp3ReleaseMp3Handle(g_musHandle); g_musHandle = -1; }
    if (g_musFile) { fclose(g_musFile); g_musFile = 0; }
    g_musPcmPtr = 0; g_musPcmLeft = 0;
}

static void musicReleaseResource(void) {
    musicRelease();
    if (!g_musResource) return;
    sceMp3TermResource();
    sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    g_musResource = false;
}

static void musicStart(void) {
    if (!g_musCount || g_channel < 0) return;
    if (!musicInitResource()) { g_musCount = 0; return; }

    if (!g_musBuf) {
        g_musBuf    = (short*)malloc(2 * MUSIC_HALF_SAMPLES * sizeof(short));

        g_musInBuf  = (unsigned char*)memalign(64, MUSIC_IN_BUF);
        g_musPcmBuf = (unsigned char*)memalign(64, MUSIC_PCM_BUF);
        if (!g_musBuf || !g_musInBuf || !g_musPcmBuf) {
            free(g_musBuf);    g_musBuf = 0;
            free(g_musInBuf);  g_musInBuf = 0;
            free(g_musPcmBuf); g_musPcmBuf = 0;
            g_musCount = 0;
            return;
        }
    }

    const Entry* e = &g_musIndex[musicPickTrack()];
    g_musFile = fopen(g_musPath, "rb");
    if (!g_musFile) return;

    SceMp3InitArg args;
    memset(&args, 0, sizeof(args));
    args.mp3StreamStart = g_musPcmBase + e->offset;
    args.mp3StreamEnd   = args.mp3StreamStart + e->frames;
    args.mp3Buf         = g_musInBuf;
    args.mp3BufSize     = MUSIC_IN_BUF;
    args.pcmBuf         = g_musPcmBuf;
    args.pcmBufSize     = MUSIC_PCM_BUF;

    g_musHandle = sceMp3ReserveMp3Handle(&args);
    if (g_musHandle < 0) { musicRelease(); return; }
    musicFeed();
    if (sceMp3Init(g_musHandle) < 0) { musicRelease(); return; }

    g_musEnded = 0;
    g_musHalf = 0; g_musPos = 0;
    g_musReady[0] = 0; g_musReady[1] = 0;
    g_musFillHalf = -1; g_musFillPos = 0;

    while (!musicFillStep(0)) {}
    while (!musicFillStep(1)) {}
    if (g_musEnded && !g_musFilled) { musicRelease(); return; }
    g_musPlaying = 1;
}

static void musicArmGap(void) {
    g_musNextAt = sceKernelGetSystemTimeLow() + (unsigned int)(rand() % (20 * 60 * 3)) * 50000u;
    if (!g_musNextAt) g_musNextAt = 1;
}

void soundMusicStop(void) {
    g_musPlaying = 0;
    g_musReady[0] = 0; g_musReady[1] = 0;
    g_musEnded = 1;
    musicRelease();
    musicArmGap();
}

void soundMusicUpdate(void) {
    if (!g_musCount || g_channel < 0) return;
    if (g_catVol[SND_CAT_MUSIC] <= 0.0f) return;

    if (g_guDialogActive) return;

    if (g_musPlaying) {

        if (g_musFillHalf >= 0) { musicFillStep(g_musFillHalf); return; }
        for (int h = 0; h < 2; h++)
            if (!g_musReady[h] && !g_musEnded) { musicFillStep(h); break; }
        return;
    }

    if (g_musHandle >= 0 || g_musFile) {
        musicRelease();
        musicArmGap();
        return;
    }

    if (g_musNextAt && (int)(sceKernelGetSystemTimeLow() - g_musNextAt) < 0)
        return;
    g_musNextAt = 0;
    musicStart();
}

static const struct { const char* prefix; unsigned char cat; } kCatPrefix[] = {

    { "step.",         SND_CAT_BLOCK    },
    { "fire.",         SND_CAT_BLOCK    },
    { "liquid.",       SND_CAT_BLOCK    },
    { "random.fizz",   SND_CAT_BLOCK    },

    { "mob.zombie",    SND_CAT_HOSTILE  },
    { "mob.skeleton",  SND_CAT_HOSTILE  },
    { "mob.spider",    SND_CAT_HOSTILE  },
    { "mob.creeper",   SND_CAT_HOSTILE  },

    { "mob.",          SND_CAT_FRIENDLY },

    { "random.bowhit", SND_CAT_FRIENDLY },

    { "damage.",       SND_CAT_PLAYER   },
    { "random.hurt",   SND_CAT_PLAYER   },
    { "random.eat",    SND_CAT_PLAYER   },
    { "random.burp",   SND_CAT_PLAYER   },
    { "random.splash", SND_CAT_PLAYER   },
    { "random.bow",    SND_CAT_PLAYER   },
    { "random.pop",    SND_CAT_PLAYER   },

    { "ambient.cave",  SND_CAT_AMBIENT  },

    { "random.click",  SND_CAT_UI       },
};

int categoryOf(const char* name) {
    for (unsigned i = 0; i < sizeof(kCatPrefix) / sizeof(*kCatPrefix); i++)
        if (strncmp(name, kCatPrefix[i].prefix, strlen(kCatPrefix[i].prefix)) == 0)
            return kCatPrefix[i].cat;
    return SND_CAT_BLOCK;
}

void soundPlay(const char* name, float volume, float pitch, int catOverride) {
    if (g_channel < 0 || !name || !name[0] || g_master <= 0.0f) return;
    if (volume <= 0.0f) return;

    const int cat = (catOverride >= 0 && catOverride < SND_CAT_COUNT) ? catOverride
                                                                       : categoryOf(name);
    volume *= g_catVol[cat];
    if (volume <= 0.0f) return;

    if (strcmp(name, "ambient.cave") == 0) { playCave(volume, pitch); return; }

    int first = findFirst(name);
    if (first < 0) return;

    int variants = 1;
    while (first + variants < g_count &&
           strcmp(g_index[first + variants].name, name) == 0)
        variants++;
    const Entry* e = &g_index[first + (variants > 1 ? rand() % variants : 0)];

    Voice* s = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].playing) { s = &g_voices[v]; break; }
    }
    if (!s) return;

    if (volume > 1.0f) volume = 1.0f;
    if (pitch < 0.1f) pitch = 0.1f;
    if (pitch > 4.0f) pitch = 4.0f;

    s->frames     = g_pcm + e->offset;
    s->frameCount = e->frames;
    s->frame      = 0;
    s->frac       = 0;
    s->step       = (unsigned int)(((float)g_srcRate / SAMPLE_RATE) * pitch * 65536.0f);
    s->vol        = (int)(volume * 4096.0f);
    s->cat        = (unsigned char)cat;
    s->playing    = 1;
}

void soundStopWorld(void) {
    for (int v = 0; v < MAX_VOICES; v++)
        if (g_voices[v].cat != SND_CAT_UI) g_voices[v].playing = 0;
}

void soundStopAll(void) {

    for (int v = 0; v < MAX_VOICES; v++) g_voices[v].playing = 0;
}
