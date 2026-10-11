// Pop: orbs swell up and fade away; tap them before they're gone. Every pop in a row raises the combo
// (and the pitch), tapping empty space breaks it, and each orb that fades out costs a life.
// Everything that must last lives in Game: the kit hot reloads this code in dev builds (see kit/game.h).
#include "game.h"
#include <math.h>
#include <string.h>

#define MAX_ORBS 32
#define MAX_PARTS 512
#define MAX_TOUCH 10
#define LIVES 3
#define SFX_VOICES 6

typedef struct { Vector2 pos; float r, maxR, age, life; Color col; bool alive; } Orb;
typedef struct { Vector2 pos, vel; float age, life, size, spin; Color col; } Part;
typedef enum { TITLE, PLAYING, OVER } Mode;

typedef struct {
    Texture2D bg, orb, glow, spark;
    Sound pop[SFX_VOICES], miss, lose, start;
    Music music;
    int voice;

    Orb orbs[MAX_ORBS];
    Part parts[MAX_PARTS];
    int nparts;
    Mode mode;
    int score, best, combo, lives;
    float elapsed, spawnIn, shake;
    double overAt;
    int prevIds[MAX_TOUCH], prevN;
} Game;

// screen size and layout unit (1% of the short side), set every frame
static int W, H;
static float U;

static const Color PALETTE[] = {
    { 255, 94, 125, 255 }, { 255, 176, 59, 255 }, { 72, 219, 159, 255 },
    { 77, 171, 255, 255 }, { 177, 120, 255, 255 }, { 255, 230, 90, 255 },
};

static float frand(float a, float b) { return a + (b - a) * GetRandomValue(0, 10000) / 10000.0f; }

static void loadAssets(Game *g)
{
    g->bg = LoadTexture("images/background.png");
    g->orb = LoadTexture("images/orb.png");
    g->glow = LoadTexture("images/glow.png");
    g->spark = LoadTexture("images/spark.png");
    SetTextureFilter(g->bg, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g->orb, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g->glow, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(g->spark, TEXTURE_FILTER_BILINEAR);
    g->pop[0] = LoadSound("sounds/pop.wav");
    // overlapping pops. raylib crashes aliasing a sound that failed to load, so check first
    for (int i = 1; i < SFX_VOICES; i++) g->pop[i] = IsSoundValid(g->pop[0]) ? LoadSoundAlias(g->pop[0]) : g->pop[0];
    g->miss = LoadSound("sounds/miss.wav");
    g->lose = LoadSound("sounds/lose.wav");
    g->start = LoadSound("sounds/start.wav");
    g->music = LoadMusicStream("sounds/music.qoa");
    SetMusicVolume(g->music, 0.45f);
    PlayMusicStream(g->music);
}

static void unloadAssets(Game *g)
{
    UnloadTexture(g->bg);
    UnloadTexture(g->orb);
    UnloadTexture(g->glow);
    UnloadTexture(g->spark);
    if (IsSoundValid(g->pop[0]))
        for (int i = 1; i < SFX_VOICES; i++) UnloadSoundAlias(g->pop[i]);
    UnloadSound(g->pop[0]);
    UnloadSound(g->miss);
    UnloadSound(g->lose);
    UnloadSound(g->start);
    UnloadMusicStream(g->music);
}

// best score: a file in the app's private storage (raylib maps relative writes there on Android)
static void saveBest(Game *g) { SaveFileData("best.dat", &g->best, sizeof g->best); }
static void loadBest(Game *g)
{
    int n = 0;
    unsigned char *d = LoadFileData("best.dat", &n);
    if (d && n == sizeof g->best) memcpy(&g->best, d, n);
    UnloadFileData(d);
}

static void burst(Game *g, Vector2 p, Color c, int n, float speed)
{
    for (int i = 0; i < n && g->nparts < MAX_PARTS; i++) {
        float a = frand(0, 2 * PI), v = frand(0.3f, 1) * speed * U;
        g->parts[g->nparts++] = (Part){ p, { cosf(a) * v, sinf(a) * v }, 0, frand(0.4f, 0.9f),
                                        frand(1.5f, 3.5f) * U, frand(-6, 6), c };
    }
}

static void spawn(Game *g)
{
    for (int i = 0; i < MAX_ORBS; i++) {
        if (g->orbs[i].alive) continue;
        float maxR = frand(7, 11) * U;
        // later orbs live shorter: 3.2 s at the start, down to 1.4 s
        float life = fmaxf(1.4f, 3.2f - g->elapsed * 0.03f);
        g->orbs[i] = (Orb){ { frand(maxR, W - maxR), frand(maxR + 14 * U, H - maxR - 4 * U) }, 0, maxR, 0, life,
                            PALETTE[GetRandomValue(0, 5)], true };
        return;
    }
}

static void reset(Game *g)
{
    for (int i = 0; i < MAX_ORBS; i++) g->orbs[i].alive = false;
    g->score = g->combo = 0;
    g->lives = LIVES;
    g->elapsed = 0;
    g->spawnIn = 0.3f;
    g->mode = PLAYING;
    PlaySound(g->start);
}

static void tap(Game *g, Vector2 p)
{
    if (g->mode != PLAYING) {
        if (g->mode == TITLE || GetTime() - g->overAt > 0.8) reset(g);  // a short pause so a late tap doesn't restart
        return;
    }
    // hit the newest orb under the finger (the one drawn on top), with a little slack
    for (int i = MAX_ORBS - 1; i >= 0; i--) {
        Orb *o = &g->orbs[i];
        if (!o->alive || !CheckCollisionPointCircle(p, o->pos, o->r + 2.5f * U)) continue;
        o->alive = false;
        g->combo++;
        g->score += g->combo;
        burst(g, o->pos, o->col, 24 + g->combo * 2, 55 + g->combo * 3);
        Sound s = g->pop[g->voice++ % SFX_VOICES];
        SetSoundPitch(s, fminf(2.5f, 1 + g->combo * 0.06f));
        PlaySound(s);
        return;
    }
    g->combo = 0;
    burst(g, p, (Color){ 255, 255, 255, 120 }, 6, 20);
    PlaySound(g->miss);
}

static void update(Game *g, float dt)
{
    // a new finger this frame = a tap; works for several fingers at once
    int n = GetTouchPointCount();
    if (n > MAX_TOUCH) n = MAX_TOUCH;
    int ids[MAX_TOUCH];
    for (int i = 0; i < n; i++) {
        ids[i] = GetTouchPointId(i);
        bool seen = false;
        for (int j = 0; j < g->prevN; j++) seen |= g->prevIds[j] == ids[i];
        if (!seen) tap(g, GetTouchPosition(i));
    }
    memcpy(g->prevIds, ids, n * sizeof *ids);
    g->prevN = n;

    for (int i = 0; i < g->nparts; i++) {
        Part *q = &g->parts[i];
        q->age += dt;
        if (q->age >= q->life) { g->parts[i--] = g->parts[--g->nparts]; continue; }
        q->pos.x += q->vel.x * dt * 6;
        q->pos.y += q->vel.y * dt * 6;
        q->vel.x *= 0.94f;
        q->vel.y = q->vel.y * 0.94f + 4 * U * dt;
    }
    g->shake = fmaxf(0, g->shake - dt * 3);
    if (g->mode != PLAYING) return;

    g->elapsed += dt;
    g->spawnIn -= dt;
    if (g->spawnIn <= 0) {
        spawn(g);
        g->spawnIn = fmaxf(0.35f, 1.1f - g->elapsed * 0.012f) * frand(0.7f, 1.3f);
    }
    for (int i = 0; i < MAX_ORBS; i++) {
        Orb *o = &g->orbs[i];
        if (!o->alive) continue;
        o->age += dt;
        float t = o->age / o->life;
        // pop in fast, hold, then shrink away
        o->r = o->maxR * (t < 0.15f ? sinf(t / 0.15f * PI / 2) * 1.08f : t < 0.6f ? 1 : 1 - (t - 0.6f) / 0.4f);
        if (t < 1) continue;
        o->alive = false;
        g->combo = 0;
        g->shake = 1;
        burst(g, o->pos, (Color){ 120, 120, 140, 255 }, 14, 25);
        PlaySound(g->lose);
        if (--g->lives > 0) continue;
        g->mode = OVER;
        g->overAt = GetTime();
        if (g->score > g->best) { g->best = g->score; saveBest(g); }
        for (int j = 0; j < MAX_ORBS; j++)
            if (g->orbs[j].alive) { g->orbs[j].alive = false; burst(g, g->orbs[j].pos, g->orbs[j].col, 20, 40); }
    }
}

static void sprite(Texture2D t, Vector2 at, float size, float rot, Color c)
{
    DrawTexturePro(t, (Rectangle){ 0, 0, t.width, t.height }, (Rectangle){ at.x, at.y, size, size },
                   (Vector2){ size / 2, size / 2 }, rot, c);
}

static void centered(const char *s, float y, float size, Color c)
{
    int fs = (int)(size * U);
    DrawText(s, (W - MeasureText(s, fs)) / 2, (int)(y * U), fs, c);
}

static void draw(Game *g)
{
    BeginDrawing();
    // background, scaled to cover the screen
    float s = fmaxf((float)W / g->bg.width, (float)H / g->bg.height);
    DrawTextureEx(g->bg, (Vector2){ (W - g->bg.width * s) / 2, (H - g->bg.height * s) / 2 }, 0, s, WHITE);

    Camera2D cam = { .zoom = 1, .offset = { frand(-1, 1) * g->shake * 1.5f * U, frand(-1, 1) * g->shake * 1.5f * U } };
    BeginMode2D(cam);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < MAX_ORBS; i++) {
        Orb *o = &g->orbs[i];
        if (o->alive && o->r > 0) sprite(g->glow, o->pos, o->r * 4.5f, 0, Fade(o->col, 0.55f));
    }
    EndBlendMode();
    for (int i = 0; i < MAX_ORBS; i++) {
        Orb *o = &g->orbs[i];
        if (!o->alive || o->r <= 0) continue;
        sprite(g->orb, o->pos, o->r * 2, 0, o->col);
        // countdown ring: how much time this orb has left
        DrawRing(o->pos, o->r + 0.6f * U, o->r + 1.1f * U, -90, -90 + 360 * (1 - o->age / o->life), 48,
                 Fade(WHITE, 0.7f));
    }
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < g->nparts; i++) {
        Part *q = &g->parts[i];
        float k = 1 - q->age / q->life;
        sprite(g->spark, q->pos, q->size * (0.4f + k), q->spin * q->age * 57, Fade(q->col, k));
    }
    EndBlendMode();
    EndMode2D();

    if (g->mode == PLAYING) {
        DrawText(TextFormat("%d", g->score), (int)(4 * U), (int)(4 * U), (int)(9 * U), WHITE);
        if (g->combo > 1)
            DrawText(TextFormat("x%d combo!", g->combo), (int)(4 * U), (int)(13.5f * U), (int)(7 * U), PALETTE[1]);
        for (int i = 0; i < LIVES; i++)
            sprite(g->orb, (Vector2){ W - (5 + i * 6) * U, 8 * U }, 4.4f * U, 0,
                   i < g->lives ? PALETTE[0] : Fade(WHITE, 0.15f));
    } else {
        DrawRectangle(0, 0, W, H, Fade(BLACK, 0.45f));
        float y = H / U * 0.3f;
        if (g->mode == TITLE) {
            centered("POP", y, 26, WHITE);
            centered("tap the orbs before they fade", y + 32, 4.5f, Fade(WHITE, 0.8f));
            centered("taps in a row build a combo", y + 39, 4.5f, Fade(WHITE, 0.8f));
            if (g->best) centered(TextFormat("best %d", g->best), y + 63, 5, Fade(WHITE, 0.6f));
        } else {
            centered("GAME OVER", y, 12, PALETTE[0]);
            centered(TextFormat("%d", g->score), y + 15, 20, WHITE);
            centered(TextFormat("best %d", g->best), y + 37, 5, Fade(WHITE, 0.7f));
        }
        float pulse = 0.55f + 0.45f * sinf(GetTime() * 4);
        centered("tap to play", y + 52, 6, Fade(PALETTE[2], pulse));
    }
    EndDrawing();
}

static void layout(void)
{
    W = GetScreenWidth();
    H = GetScreenHeight();
    U = (W < H ? W : H) / 100.0f;
}

static void init(void *state)
{
    Game *g = state;
    layout();
    SetRandomSeed((unsigned)(GetTime() * 1e6));
    SetTargetFPS(60);
    loadAssets(g);
    loadBest(g);
}

static void frame(void *state)
{
    Game *g = state;
    layout();
    UpdateMusicStream(g->music);
    update(g, fminf(GetFrameTime(), 0.05f));
    draw(g);
}

static void reload(void *state, bool assetsChanged)
{
    if (assetsChanged) {
        unloadAssets(state);
        loadAssets(state);
    }
}

static void unload(void *state) { unloadAssets(state); }

GAME_API(sizeof(Game), init, frame, reload, unload);
