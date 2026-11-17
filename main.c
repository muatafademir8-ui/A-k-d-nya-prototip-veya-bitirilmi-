// Acik Dunya Surus - PSP portu (PSPSDK / pspdev). Web surumunun sade hali.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("AcikDunya", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BW 512
#define SW 480
#define SH 272
#define CS 80.0f
#define PI 3.14159265f
#define RGB(r,g,b) (0xff000000u|((b)<<16)|((g)<<8)|(r))

static unsigned int __attribute__((aligned(16))) list[262144];
typedef struct { unsigned int c; float x, y, z; } V;

static unsigned vramOff = 0;
static void *vramAlloc(unsigned w, unsigned h, int psm) {
    unsigned s = w * h * (psm == GU_PSM_4444 ? 2 : 4);
    void *r = (void *)vramOff; vramOff += s; return r;
}

static int running = 1;
static int exitCb(int a, int b, void *c) { running = 0; return 0; }
static int cbThread(SceSize args, void *argp) {
    int cb = sceKernelCreateCallback("exit", exitCb, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static const unsigned pal[6] = {
    RGB(217,138,108), RGB(127,166,201), RGB(201,179,127),
    RGB(157,140,196), RGB(143,196,160), RGB(224,196,196) };

static unsigned shade(unsigned c, float k) {
    unsigned r = (unsigned)((c & 255) * k), g = (unsigned)(((c >> 8) & 255) * k), b = (unsigned)(((c >> 16) & 255) * k);
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

// Merkezi (x,y,z) olan kutu cizer.
static void box(float x, float y, float z, float w, float h, float d, unsigned col) {
    static const float cx[8] = {-1,1,1,-1,-1,1,1,-1}, cy[8] = {-1,-1,1,1,-1,-1,1,1}, cz[8] = {-1,-1,-1,-1,1,1,1,1};
    static const int fi[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    static const float fs[6] = {.8f,.8f,.65f,.65f,1.0f,.5f};
    V *v = (V *)sceGuGetMemory(36 * sizeof(V));
    float hx = w / 2, hy = h / 2, hz = d / 2; int n = 0;
    for (int f = 0; f < 6; f++) {
        unsigned c = shade(col, fs[f]);
        int a = fi[f][0], b = fi[f][1], cc = fi[f][2], dd = fi[f][3], idx[6] = {a,b,cc,a,cc,dd};
        for (int k = 0; k < 6; k++) {
            int i = idx[k];
            v[n].c = c; v[n].x = x + cx[i] * hx; v[n].y = y + cy[i] * hy; v[n].z = z + cz[i] * hz; n++;
        }
    }
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, v);
}

static unsigned hsh(int a, int b, int c) {
    unsigned x = (unsigned)a * 73856093u ^ (unsigned)b * 19349663u ^ (unsigned)c * 83492791u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15; return x;
}
// Blok (i,j) icindeki q. bina (varsa).
static int bld(int i, int j, int q, float *x, float *z, float *w, float *d, float *h, unsigned *col) {
    unsigned a = hsh(i, j, q);
    if (a % 6 == 0) return 0;
    *w = 9 + (a >> 3) % 13; *d = 9 + (a >> 8) % 13;
    *h = 6 + (float)((a >> 13) % 48) * (((a >> 20) % 3 == 0) ? 2.0f : 1.0f);
    *x = i * CS + 11 + (q & 1) * 29 + 14.5f; *z = j * CS + 11 + (q >> 1) * 29 + 14.5f;
    *col = pal[a % 6]; return 1;
}

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// Oyuncu
static float px = 0, pz = 0, h = 0, ch = 0, v = 0, lv = 0, s = 0, alt = 0, vy = 0;
static int fly = 0, hits = 0;

// Trafik
#define TCN 16
typedef struct { int ax, d, st; float x, y, z, sp, vx, vy, vz, rx, rz, t; unsigned col; } T;
static T tc[TCN];
static void tplace(T *t, int init) {
    t->ax = rand() & 1; t->d = (rand() & 1) ? 1 : -1; t->sp = 14 + rand() % 24; t->st = 0; t->y = 0; t->rx = t->rz = 0;
    float off = ((rand() & 1) ? -1 : 1) * (init ? 40 + rand() % 130 : 130 + rand() % 30);
    int k = rand() % 3 - 1; float ln = t->d * 3.6f;
    if (t->ax == 0) { t->x = px + off; t->z = (floorf(pz / CS + .5f) + k) * CS + ln; }
    else { t->x = (floorf(px / CS + .5f) + k) * CS - ln; t->z = pz + off; }
    t->col = pal[rand() % 6];
}
static float tyaw(T *t) { return t->ax == 0 ? -t->d * PI / 2 : (t->d > 0 ? PI : 0); }

static void carModel(unsigned body) {
    box(0, .7f, 0, 2, .6f, 4.3f, body);
    box(0, 1.25f, .3f, 1.7f, .5f, 2.2f, RGB(30,45,65));
    box(-1.05f, .45f, -1.35f, .35f, .9f, .9f, RGB(20,20,24)); box(1.05f, .45f, -1.35f, .35f, .9f, .9f, RGB(20,20,24));
    box(-1.05f, .45f, 1.35f, .35f, .9f, .9f, RGB(20,20,24));  box(1.05f, .45f, 1.35f, .35f, .9f, .9f, RGB(20,20,24));
    box(0, .8f, -2.2f, 1.6f, .12f, .1f, RGB(240,250,255));
    box(0, .8f, 2.2f, 1.8f, .12f, .1f, RGB(255,40,40));
}

static int onRoad(float x, float z) {
    float rx = fmodf(fmodf(x, CS) + CS, CS), rz = fmodf(fmodf(z, CS) + CS, CS);
    return rx < 7 || rx > CS - 7 || rz < 7 || rz > CS - 7;
}

int main(void) {
    int th = sceKernelCreateThread("cb", cbThread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    pspDebugScreenInit();
    srand(12345);

    void *fbp0 = vramAlloc(BW, SH, GU_PSM_8888), *fbp1 = vramAlloc(BW, SH, GU_PSM_8888), *zbp = vramAlloc(BW, SH, GU_PSM_4444);
    sceGuInit(); sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BW); sceGuDispBuffer(SW, SH, fbp1, BW); sceGuDepthBuffer(zbp, BW);
    sceGuOffset(2048 - (SW / 2), 2048 - (SH / 2)); sceGuViewport(2048, 2048, SW, SH);
    sceGuDepthRange(65535, 0); sceGuScissor(0, 0, SW, SH); sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL); sceGuEnable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE); sceGuShadeModel(GU_SMOOTH);
    sceGuFinish(); sceGuSync(0, 0); sceDisplayWaitVblankStart(); sceGuDisplay(GU_TRUE);

    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    for (int i = 0; i < TCN; i++) tplace(&tc[i], 1);
    unsigned lastBtn = 0; const float dt = 1.0f / 60.0f;

    while (running) {
        SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
        int gas = pad.Buttons & PSP_CTRL_CROSS, brk = pad.Buttons & PSP_CTRL_SQUARE, hb = pad.Buttons & PSP_CTRL_CIRCLE, nt = pad.Buttons & PSP_CTRL_RTRIGGER;
        if ((pad.Buttons & PSP_CTRL_TRIANGLE) && !(lastBtn & PSP_CTRL_TRIANGLE)) { fly = !fly; if (fly && alt < .1f) vy = 10; }
        lastBtn = pad.Buttons;
        float tgt = -((float)pad.Lx - 128.0f) / 128.0f; if (fabsf(tgt) < .15f) tgt = 0;
        if (pad.Buttons & PSP_CTRL_LEFT) tgt = 1; if (pad.Buttons & PSP_CTRL_RIGHT) tgt = -1;

        int on = onRoad(px, pz) || alt > 1;
        float mx = (on ? 62.0f : 26.0f) * (nt ? 1.85f : 1.0f);
        if (gas || nt) v += (nt ? 70 + fmaxf(0, v) * .35f : (v < mx ? 26 * (1 - fmaxf(0, v) / 70) : 0)) * dt;
        if (brk) { v -= (v > 1 ? 48 : 16) * dt; if (v < -16) v = -16; }
        if (!gas && !brk && !nt) v *= 1 - .7f * dt;
        if (v > mx && !nt) v -= (v - mx) * (on ? .15f : 1.2f) * dt;
        if (hb) v *= 1 - .25f * dt;
        s += (tgt - s) * fminf(1, dt * 8);
        float dh = s * (2.1f / (1 + fabsf(v) / 22)) * clampf(v / 8, -1, 1) * dt * ((hb && fabsf(v) > 8) ? 1.5f : 1);
        h += dh;
        { float c = cosf(dh), sn = sinf(dh), v2 = v * c - lv * sn; lv = v * sn + lv * c; v = v2; }
        int dr = hb || (fabsf(v) > 38 && fabsf(s) > .7f);
        lv *= expf(-(dr ? 1.4f : 9.0f) * dt);
        if (dr && fabsf(lv) > 3) v -= fabsf(lv) * .12f * dt;
        px += (-sinf(h) * v + cosf(h) * lv) * dt; pz += (-cosf(h) * v - sinf(h) * lv) * dt;

        if (fly) {
            float tv = ((pad.Buttons & PSP_CTRL_UP) ? 1.0f : 0.0f) - ((pad.Buttons & PSP_CTRL_DOWN) ? 1.0f : 0.0f);
            vy += (tv * 26 - vy) * fminf(1, dt * 5); if (alt > 1) v -= vy * .12f * dt;
            alt += vy * dt; if (alt < 0) { alt = 0; vy = 0; }
        } else if (alt > 0) { vy -= 35 * dt; alt += vy * dt; if (alt <= 0) { alt = 0; vy = 0; } }

        // Bina carpismasi (3x3 blok)
        int ci = (int)floorf(px / CS), cj = (int)floorf(pz / CS);
        for (int i = ci - 1; i <= ci + 1; i++) for (int j = cj - 1; j <= cj + 1; j++) for (int q = 0; q < 4; q++) {
            float bx, bz, bw, bd, bh; unsigned bc;
            if (!bld(i, j, q, &bx, &bz, &bw, &bd, &bh, &bc) || alt > bh + .8f) continue;
            float dx = px - bx, dz = pz - bz, ox = bw / 2 + 1.4f - fabsf(dx), oz = bd / 2 + 1.4f - fabsf(dz);
            if (ox > 0 && oz > 0) { if (ox < oz) px += dx > 0 ? ox : -ox; else pz += dz > 0 ? oz : -oz; if (fabsf(v) > 3) { v *= .5f; lv *= .5f; } }
        }

        // Trafik
        float pvx = -sinf(h) * v + cosf(h) * lv, pvz = -cosf(h) * v - sinf(h) * lv;
        for (int i = 0; i < TCN; i++) {
            T *t = &tc[i];
            if (t->st == 0) {
                float dir = t->d * t->sp * dt; if (t->ax == 0) t->x += dir; else t->z += dir;
                float dx = t->x - px, dz = t->z - pz;
                if (fabsf(dx) > 190 || fabsf(dz) > 190) { tplace(t, 0); continue; }
                float tvx = t->ax == 0 ? t->d * t->sp : 0, tvz = t->ax == 1 ? t->d * t->sp : 0;
                float rel = sqrtf((pvx - tvx) * (pvx - tvx) + (pvz - tvz) * (pvz - tvz));
                if (alt < 3 && dx * dx + dz * dz < 3.4f * 3.4f && rel > 2) {
                    t->st = 1; t->t = 4; hits++;
                    t->vx = pvx * 1.1f + tvx * .3f; t->vz = pvz * 1.1f + tvz * .3f; t->vy = 10 + (rand() % 80) / 10.0f + rel * .12f;
                    t->rx = ((rand() % 100) - 50) / 5.0f; t->rz = ((rand() % 100) - 50) / 5.0f;
                }
            } else {
                t->vy -= 25 * dt; t->x += t->vx * dt; t->y += t->vy * dt; t->z += t->vz * dt;
                if (t->y < 0) { t->y = 0; t->vy *= -.35f; t->vx *= .6f; t->vz *= .6f; t->rx *= .6f; t->rz *= .6f; }
                t->t -= dt; if (t->t <= 0) tplace(t, 0);
            }
        }

        // Kamera
        float dd = atan2f(sinf(h - ch), cosf(h - ch)); ch += dd * fminf(1, dt * 3.5f);
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(RGB(168,208,234)); sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity(); sceGumPerspective(68.0f + fminf(40.0f, fabsf(v) * .3f), 16.0f / 9.0f, .5f, 320.0f);
        sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
        { ScePspFVector3 eye = { px + sinf(ch) * 9.5f, 4.3f + alt + fabsf(v) * .03f, pz + cosf(ch) * 9.5f };
          ScePspFVector3 ctr = { px - sinf(h) * 4, 1.3f + alt + vy * .2f, pz - cosf(h) * 4 };
          ScePspFVector3 up = { 0, 1, 0 }; sceGumLookAt(&eye, &ctr, &up); }
        sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

        box(px, -.5f, pz, 700, 1, 700, RGB(111,174,85));
        for (int i = ci - 3; i <= ci + 4; i++) {
            box(i * CS, .05f, pz, 14, .1f, 700, RGB(58,61,68));
            float z0 = floorf(pz / 20) * 20;
            for (int k = -5; k <= 5; k++) { float zc = z0 + k * 20, rz = fmodf(fmodf(zc, CS) + CS, CS); if (rz < 9 || rz > CS - 9) continue; box(i * CS, .12f, zc, .5f, .1f, 5, RGB(243,232,176)); }
        }
        for (int j = cj - 3; j <= cj + 4; j++) {
            box(px, .06f, j * CS, 700, .1f, 14, RGB(58,61,68));
            float x0 = floorf(px / 20) * 20;
            for (int k = -5; k <= 5; k++) { float xc = x0 + k * 20, rx = fmodf(fmodf(xc, CS) + CS, CS); if (rx < 9 || rx > CS - 9) continue; box(xc, .13f, j * CS, 5, .1f, .5f, RGB(243,232,176)); }
        }
        for (int i = ci - 3; i <= ci + 3; i++) for (int j = cj - 3; j <= cj + 3; j++) for (int q = 0; q < 4; q++) {
            float bx, bz, bw, bd, bh; unsigned bc;
            if (bld(i, j, q, &bx, &bz, &bw, &bd, &bh, &bc)) box(bx, bh / 2, bz, bw, bh, bd, bc);
        }
        for (int i = 0; i < TCN; i++) {
            T *t = &tc[i]; ScePspFVector3 p = { t->x, t->y, t->z };
            sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); sceGumTranslate(&p); sceGumRotateY(tyaw(t));
            if (t->st) { sceGumRotateX(t->rx * (4 - t->t) * .3f); sceGumRotateZ(t->rz * (4 - t->t) * .3f); }
            carModel(t->col);
        }
        { ScePspFVector3 p = { px, alt, pz };
          sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity(); sceGumTranslate(&p); sceGumRotateY(h);
          sceGumRotateZ(s * (alt > 1 ? .5f : .035f)); if (alt > .5f) sceGumRotateX(atan2f(vy, fmaxf(fabsf(v), 14)) * .9f);
          carModel(RGB(244,244,246)); }

        sceGuFinish(); sceGuSync(0, 0);
        pspDebugScreenSetOffset((int)fbp0); pspDebugScreenSetXY(1, 1);
        pspDebugScreenSetTextColor(0xffffffff);
        pspDebugScreenPrintf("HIZ %d km/s  %s  CARPISMA %d", (int)(fabsf(v) * 3.6f), fly ? "UCUS" : "YER", hits);
        pspDebugScreenSetXY(1, 33);
        pspDebugScreenPrintf("X gaz  []fren  O drift  R nitro  /\\ ucus  UP/DOWN yuksel/alcal");
        sceDisplayWaitVblankStart();
        fbp0 = sceGuSwapBuffers();
    }
    sceGuTerm(); sceKernelExitGame();
    return 0;
}
