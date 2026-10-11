// Generates Pop's art and sounds into ../assets and ../icon.png. Run once (or after tweaking): ./tools/gen-assets.sh
// The game just loads the files, so any of them can be swapped for hand-made ones.
// Built against Termux's desktop raylib: only Image/Wave functions, no window.
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float smooth(float a, float b, float x) { x = clamp01((x - a) / (b - a)); return x * x * (3 - 2 * x); }

static unsigned rng = 12345;
static float rnd(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

// value noise + fbm for the nebula
static float hash2(int x, int y) { unsigned h = x * 374761393u + y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return (h & 0xffffff) / 16777216.0f; }
static float vnoise(float x, float y)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float x, float y) { float s = 0, a = 0.5f; for (int i = 0; i < 6; i++, x *= 2.03f, y *= 2.03f, a *= 0.5f) s += a * vnoise(x, y); return s; }

static Image canvas(int w, int h) { return (Image){ calloc(w * h, 4), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 }; }
static void put(Image *im, int x, int y, float r, float g, float b, float a)
{
    unsigned char *p = (unsigned char *)im->data + 4 * (y * im->width + x);
    p[0] = 255 * clamp01(r); p[1] = 255 * clamp01(g); p[2] = 255 * clamp01(b); p[3] = 255 * clamp01(a);
}
static void save(Image im, const char *path) { if (!ExportImage(im, path)) { fprintf(stderr, "failed: %s\n", path); exit(1); } UnloadImage(im); }

// A glossy sphere in near-white, so the game can tint it any color
static Image orb(int n)
{
    Image im = canvas(n, n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1, d = sqrtf(u * u + v * v);
            float a = smooth(1, 1 - 3.0f / n, d);
            if (a <= 0) continue;
            float z = sqrtf(fmaxf(0, 1 - d * d));
            float lx = -0.45f, ly = -0.55f, lz = 0.7f;
            float diff = fmaxf(0, u * lx + v * ly + z * lz);
            float rim = powf(1 - z, 3) * 0.6f;
            float spec = powf(fmaxf(0, 1 - hypotf(u + 0.38f, v + 0.42f) / 0.28f), 2) * 0.9f;
            float c = 0.45f + 0.5f * diff + rim;
            put(&im, x, y, c + spec, c + spec, c + spec, a);
        }
    return im;
}

// soft radial glow (white, for additive blending)
static Image glow(int n, float power)
{
    Image im = canvas(n, n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float d = hypotf((x + 0.5f) / n * 2 - 1, (y + 0.5f) / n * 2 - 1);
            put(&im, x, y, 1, 1, 1, powf(clamp01(1 - d), power));
        }
    return im;
}

// a 4-pointed sparkle
static Image spark(int n)
{
    Image im = canvas(n, n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float u = fabsf((x + 0.5f) / n * 2 - 1), v = fabsf((y + 0.5f) / n * 2 - 1);
            float a = powf(clamp01(1 - hypotf(u, v)), 3) + clamp01(1 - u * 14 - v) * clamp01(1 - v) + clamp01(1 - v * 14 - u) * clamp01(1 - u);
            put(&im, x, y, 1, 1, 1, a);
        }
    return im;
}

static void nebulaPixel(float u, float v, float *r, float *g, float *b)
{
    float n1 = fbm(u * 3 + 10, v * 3), n2 = fbm(u * 5 - 4, v * 5 + 7);
    float neb = smooth(0.45f, 0.85f, n1), neb2 = smooth(0.5f, 0.9f, n2);
    *r = 0.05f + 0.35f * neb * (0.6f + 0.4f * v) + 0.10f * neb2;
    *g = 0.03f + 0.08f * neb + 0.20f * neb2 * (1 - v);
    *b = 0.10f + 0.40f * neb * (1 - 0.5f * v) + 0.25f * neb2;
    float vig = 1 - 0.55f * powf(hypotf(u - 0.5f, (v - 0.5f) * 0.7f) * 1.6f, 2);
    *r *= vig; *g *= vig; *b *= vig;
}

static Image background(int w, int h)
{
    Image im = canvas(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float r, g, b;
            nebulaPixel((float)x / w, (float)y / h * h / w, &r, &g, &b);
            put(&im, x, y, r, g, b, 1);
        }
    // stars
    for (int i = 0; i < w * h / 900; i++) {
        int sx = rnd() * w, sy = rnd() * h;
        float br = powf(rnd(), 3) * 0.9f + 0.15f, rad = br > 0.6f ? 2.2f : 1.2f;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int px = sx + dx, py = sy + dy;
                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                float k = br * clamp01(1 - hypotf(dx, dy) / rad);
                unsigned char *p = (unsigned char *)im.data + 4 * (py * w + px);
                for (int c = 0; c < 3; c++) p[c] = fminf(255, p[c] + 255 * k * (c == 2 ? 1 : 0.85f));
            }
    }
    return im;
}

static Image icon(int n)
{
    Image im = canvas(n, n);
    Image o = orb(512), g = glow(512, 2.2f);
    unsigned char *op = o.data, *gp = g.data;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float r, gg, b;
            nebulaPixel((float)x / n * 0.8f + 0.1f, (float)y / n * 0.8f + 0.3f, &r, &gg, &b);
            // orb (pink tint) with glow, centered and inside the launcher's safe zone
            float u = (x - n * 0.5f) / (n * 0.30f), v = (y - n * 0.5f) / (n * 0.30f);
            int ox = (int)((u * 0.5f + 0.5f) * 511), oy = (int)((v * 0.5f + 0.5f) * 511);
            float gu = (x - n * 0.5f) / (n * 0.48f), gv = (y - n * 0.5f) / (n * 0.48f);
            int gx = (int)((gu * 0.5f + 0.5f) * 511), gy = (int)((gv * 0.5f + 0.5f) * 511);
            if (gx >= 0 && gy >= 0 && gx < 512 && gy < 512) {
                float k = gp[4 * (gy * 512 + gx) + 3] / 255.0f * 0.55f;
                r += k * 1.0f; gg += k * 0.37f; b += k * 0.49f;
            }
            if (ox >= 0 && oy >= 0 && ox < 512 && oy < 512) {
                unsigned char *p = op + 4 * (oy * 512 + ox);
                float a = p[3] / 255.0f, c = p[0] / 255.0f;
                r = r * (1 - a) + a * c * 1.0f; gg = gg * (1 - a) + a * c * 0.37f; b = b * (1 - a) + a * c * 0.49f;
                float sp = fmaxf(0, c - 0.95f) * 8;  // keep the highlight white
                r += a * sp; gg += a * sp; b += a * sp;
            }
            put(&im, x, y, r, gg, b, 1);
        }
    UnloadImage(o); UnloadImage(g);
    return im;
}

// ---- sound ----

typedef struct { float *s; int n; } Buf;
static Buf buf(float secs) { int n = secs * RATE; return (Buf){ calloc(n, sizeof(float)), n }; }

static void saveWave(Buf b, const char *path, float gain)
{
    float peak = 1e-6f;
    for (int i = 0; i < b.n; i++) peak = fmaxf(peak, fabsf(b.s[i]));
    short *pcm = malloc(b.n * sizeof(short));
    for (int i = 0; i < b.n; i++) pcm[i] = (short)(b.s[i] / peak * gain * 32000);
    Wave w = { b.n, RATE, 16, 1, pcm };
    if (!ExportWave(w, path)) { fprintf(stderr, "failed: %s\n", path); exit(1); }
    free(pcm);
    free(b.s);
}

static float note(int semis) { return 440 * powf(2, semis / 12.0f); }  // semitones from A4

// plucked/bell-ish tone added into b at time t
static void pluck(Buf b, float t, float f, float dur, float amp, float bright)
{
    int i0 = t * RATE, n = dur * RATE;
    for (int i = 0; i < n && i0 + i < b.n; i++) {
        float x = (float)i / RATE, env = expf(-x * 6 / dur) * fminf(1, i / 60.0f);
        float s = sinf(2 * PI * f * x) + bright * 0.5f * sinf(2 * PI * f * 2 * x) * expf(-x * 12) + bright * 0.25f * sinf(2 * PI * f * 3.01f * x) * expf(-x * 20);
        b.s[i0 + i] += amp * env * s;
    }
}

static Buf popSound(void)
{
    Buf b = buf(0.18f);
    float ph = 0;
    for (int i = 0; i < b.n; i++) {
        float t = (float)i / b.n, f = 380 + 900 * powf(t, 0.4f);
        ph += 2 * PI * f / RATE;
        float env = expf(-t * 7) * fminf(1, i / 40.0f);
        b.s[i] = env * (sinf(ph) + 0.3f * sinf(ph * 2.5f)) + (i < 300 ? (rnd() * 2 - 1) * (1 - i / 300.0f) * 0.6f : 0);
    }
    return b;
}

static Buf missSound(void)
{
    Buf b = buf(0.1f);
    float lp = 0;
    for (int i = 0; i < b.n; i++) {
        float t = (float)i / b.n;
        lp += 0.15f * ((rnd() * 2 - 1) - lp);
        b.s[i] = expf(-t * 8) * (lp * 2 + 0.4f * sinf(2 * PI * 160 * i / RATE));
    }
    return b;
}

static Buf loseSound(void)
{
    Buf b = buf(0.6f);
    float ph = 0;
    for (int i = 0; i < b.n; i++) {
        float t = (float)i / b.n, f = 330 * powf(0.35f, t);
        ph += 2 * PI * f / RATE;
        b.s[i] = expf(-t * 4) * fminf(1, i / 100.0f) * (sinf(ph) + 0.4f * sinf(ph * 0.5f + sinf(ph * 3) * 0.8f));
    }
    return b;
}

static Buf startSound(void)
{
    Buf b = buf(0.9f);
    int arp[] = { 3, 7, 10, 15 };  // C E G C (from A4)
    for (int i = 0; i < 4; i++) pluck(b, i * 0.07f, note(arp[i]), 0.7f, 1, 1);
    return b;
}

// 16 s ambient loop, Am F C G: pad + arpeggio + a circular echo, so the end flows into the start
static Buf music(void)
{
    const float BAR = 4;
    Buf b = buf(BAR * 4);
    int chords[4][3] = { { -12, -9, -5 }, { -16, -12, -9 }, { -9, -5, -2 }, { -14, -10, -7 } };  // A2-ish voicings
    for (int c = 0; c < 4; c++) {
        // pad: detuned sines, slow swell per bar (crossfades into the next)
        for (int k = 0; k < 3; k++)
            for (int d = -1; d <= 1; d += 2) {
                float f = note(chords[c][k]) * (1 + d * 0.003f);
                for (int i = 0; i < (BAR + 1) * RATE; i++) {
                    float x = (float)i / RATE;
                    float env = smooth(0, 1.2f, x) * (1 - smooth(BAR - 0.3f, BAR + 1, x));
                    int j = ((int)(c * BAR * RATE) + i) % b.n;
                    b.s[j] += 0.05f * env * (sinf(2 * PI * f * x) + 0.2f * sinf(2 * PI * f * 2 * x));
                }
            }
        // arpeggio: eighth notes up and down the chord, an octave up
        int pat[8] = { 0, 1, 2, 1, 0, 2, 1, 2 };
        for (int s = 0; s < 8; s++) pluck(b, c * BAR + s * BAR / 8, note(chords[c][pat[s]] + 24), 0.9f, s % 4 == 0 ? 0.22f : 0.14f, 0.6f);
    }
    // echo, wrapped around the loop
    int dl = 0.375f * RATE;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < b.n; i++) b.s[i] += 0.35f * b.s[(i - dl + b.n) % b.n] * (pass ? 0.5f : 1);
    return b;
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    save(orb(256), "assets/images/orb.png");
    save(glow(256, 2.5f), "assets/images/glow.png");
    save(spark(64), "assets/images/spark.png");
    save(background(720, 1440), "assets/images/background.png");
    save(icon(1024), "icon.png");
    saveWave(popSound(), "assets/sounds/pop.wav", 0.8f);
    saveWave(missSound(), "assets/sounds/miss.wav", 0.5f);
    saveWave(loseSound(), "assets/sounds/lose.wav", 0.8f);
    saveWave(startSound(), "assets/sounds/start.wav", 0.7f);
    saveWave(music(), "assets/sounds/music.qoa", 0.6f);
    printf("assets written\n");
    return 0;
}
