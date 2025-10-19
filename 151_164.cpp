#include <GL/glut.h>
#include <cmath>
#include <ctime>
#include <chrono>
#include <string>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#endif

using namespace std;
using Clock = chrono::steady_clock;

// --- Window (actual) ---
int windowWidth = 800;
int windowHeight = 600;

// --- Grid (bricks) ---
const int BR_ROWS = 5;
const int BR_COLS = 10;
struct Brick { float x, y, w, h; bool alive; bool golden; bool unbreakable; };
Brick bricks[BR_ROWS * BR_COLS];
int bricks_alive = 0;

// --- Ball struct to support multiball ---
struct Ball { float x,y,r; float sx,sy; bool stuck; bool mega; bool gravitySlow; };
vector<Ball> balls;

// --- Pickup (falling powerups) ---
enum PickupType { P_NONE=0, P_EXTRA_LIFE, P_SCORE_BONUS, P_ENLARGE_PADDLE, P_SLOW_MOTION, P_FAST_MOTION,
                  P_MULTIBALL, P_LASER, P_GRAB_PADDLE, P_MEGA_BALL, P_ZAP_BRICK,
                  P_SHRINK_PADDLE, P_FAST_BALL, P_GRAVITY_BALL };
struct Pickup { PickupType type; float x,y; float vy; bool active; string emoji; };
vector<Pickup> pickups;

// --- Paddle & ball (values recomputed from window size) ---
float paddleW, paddleH, paddleX, paddleY;

// --- Game state ---
int score = 0, lives = 3, highScore = 0;
bool gameStarted = false;
int currentLevel = 1;
enum GameState { STATE_MENU, STATE_PLAYING, STATE_HIGHSCORE };
GameState state = STATE_MENU;

// --- Menu text ---
const int MENU_ITEMS = 4;
string menuText[MENU_ITEMS] = { "Start", "Resume", "High Score", "Exit" };

// --- Power-up handling (eggs) ---
bool eggActive = false;
enum EggType { EGG_NONE = 0, EGG_EXTRA_LIFE, EGG_SCORE_BONUS, EGG_ENLARGE_PADDLE, EGG_SLOW_MOTION, EGG_FAST_MOTION,
               EGG_MULTIBALL, EGG_LASER, EGG_GRAB_PADDLE, EGG_MEGA_BALL, EGG_ZAP_BRICK,
               EGG_SHRINK_PADDLE, EGG_FAST_BALL, EGG_GRAVITY_BALL };
EggType activeEgg = EGG_NONE;
Clock::time_point eggEnd;
float speedMultiplier = 1.0f;
float savedPaddleW = 0.0f;

// --- Lasers ---
struct Laser { float x,y; float h; };
vector<Laser> lasers;
float laserSpeed = 8.0f;
bool laserEnabled = false;

// --- Grab ---
bool grabActive = false; // when true, next paddle collision will stick ball

// --- Helpers ---
float clampf(float v, float a, float b) { return (v < a ? a : (v > b ? b : v)); }
bool resumeAvailable() { return gameStarted && state == STATE_MENU; }

// play a short pip sound cross-platform
void playPip() {
#ifdef _WIN32
    Beep(880, 60);
#else
    cout << '' << flush;
#endif
}

// map pickup type -> emoji string (visible in HUD and pickup label)
string emojiFor(PickupType t) {
    switch(t) {
        case P_EXTRA_LIFE: return "❤️"; // extra life
        case P_SCORE_BONUS: return "⭐"; // score
        case P_ENLARGE_PADDLE: return "🟦"; // paddle up
        case P_SLOW_MOTION: return "🐢"; // slow
        case P_FAST_MOTION: return "⚡"; // fast
        case P_MULTIBALL: return "⚪⚪"; // multiball
        case P_LASER: return "🔫"; // laser
        case P_GRAB_PADDLE: return "👐"; // grab
        case P_MEGA_BALL: return "🌕"; // mega ball
        case P_ZAP_BRICK: return "💥"; // zap
        case P_SHRINK_PADDLE: return "🔻"; // shrink
        case P_FAST_BALL: return "🚀"; // fast ball
        case P_GRAVITY_BALL: return "🌧️"; // gravity
        default: return "";
    }
}

// NOTE: GLUT bitmap fonts typically cannot render Unicode emoji glyphs. The code keeps emoji strings
// so modern terminals/GLUT implementations that support UTF-8 + fonts may show them, but on many systems
// they will appear as empty boxes. Fallback: we draw a small colored circle and an ASCII short label.

string shortLabelFor(PickupType t) {
    switch(t) {
        case P_EXTRA_LIFE: return "+1";
        case P_SCORE_BONUS: return "+100";
        case P_ENLARGE_PADDLE: return "P+";
        case P_SLOW_MOTION: return "SLOW";
        case P_FAST_MOTION: return "FAST";
        case P_MULTIBALL: return "x3";
        case P_LASER: return "LAS";
        case P_GRAB_PADDLE: return "GRB";
        case P_MEGA_BALL: return "MEGA";
        case P_ZAP_BRICK: return "ZAP";
        case P_SHRINK_PADDLE: return "-P";
        case P_FAST_BALL: return "FBL";
        case P_GRAVITY_BALL: return "GRV";
        default: return "";
    }
}

// --- Compute layout depending on current window size
void recomputeLayout() {
    // Paddle: width ~ 12.5% of width, height ~ 2.5% of height
    paddleW = windowWidth * 0.125f;
    paddleH = windowHeight * 0.025f;
    paddleY = windowHeight - paddleH - windowHeight * 0.03f; // a bit above bottom
    // Keep paddleX inside window (if previously set)
    if (paddleX < 0) paddleX = (windowWidth - paddleW) * 0.5f;
    if (paddleX + paddleW > windowWidth) paddleX = windowWidth - paddleW;

    // reposition bricks
    float marginX = windowWidth * 0.06f;   // left/right margin
    float marginTop = windowHeight * 0.08f; // top margin
    float padX = windowWidth * 0.00625f;    // brick horizontal padding
    float padY = windowHeight * 0.02f;      // brick vertical padding

    float availW = windowWidth - marginX * 2.0f - padX * (BR_COLS - 1);
    float brickW = availW / (float)BR_COLS;
    float brickH = windowHeight * 0.04f; // brick height relative to window height
    if (brickH > windowHeight * 0.08f) brickH = windowHeight * 0.08f; // cap

    // Reposition bricks (keep alive flags)
    bricks_alive = 0;
    for (int r = 0; r < BR_ROWS; ++r) {
        for (int c = 0; c < BR_COLS; ++c) {
            int idx = r * BR_COLS + c;
            bricks[idx].w = brickW;
            bricks[idx].h = brickH;
            bricks[idx].x = marginX + c * (brickW + padX);
            bricks[idx].y = marginTop + r * (brickH + padY);
            if (bricks[idx].alive) bricks_alive++;
        }
    }
}

// --- Level patterns & setup ---
void setAllBricksAlive(bool alive) {
    for (int i = 0; i < BR_ROWS * BR_COLS; ++i) {
        bricks[i].alive = alive;
        bricks[i].golden = false;
        bricks[i].unbreakable = false;
    }
    bricks_alive = alive ? BR_ROWS * BR_COLS : 0;
}

// helper to set golden bricks randomly (numGolden)
void setRandomGoldenBricks(int numGolden) {
    int tries = 0;
    while (numGolden > 0 && tries < 1000) {
        int idx = rand() % (BR_ROWS * BR_COLS);
        if (bricks[idx].alive && !bricks[idx].golden) {
            bricks[idx].golden = true;
            numGolden--;
        }
        tries++;
    }
}

void loadLevelPattern(int level) {
    recomputeLayout();
    for (int i = 0; i < BR_ROWS * BR_COLS; ++i) { bricks[i].alive = false; bricks[i].golden = false; bricks[i].unbreakable = false; }

    if (level == 1) {
        setAllBricksAlive(true);
    } else if (level == 2) {
        const char *pat[BR_ROWS] = {
            "..XXXXXX..",
            ".XXXXXXXX.",
            "XXXXXXXXXX",
            ".XX.XX.XX.",
            "..XXXXXX.."
        };
        for (int r = 0; r < BR_ROWS; ++r) for (int c = 0; c < BR_COLS; ++c) bricks[r*BR_COLS + c].alive = (pat[r][c]=='X');
    } else if (level == 3) {
        for (int r = 0; r < BR_ROWS; ++r)
            for (int c = 0; c < BR_COLS; ++c)
                bricks[r * BR_COLS + c].alive = ((r + c) % 2 == 0);
    } else if (level == 4) {
        for (int r = 0; r < BR_ROWS; ++r)
            for (int c = 0; c < BR_COLS; ++c)
                bricks[r * BR_COLS + c].alive = (c % 2 == 0);
    } else {
        setAllBricksAlive(true);
    }

    if (level == 4) {
        for (int i = 0; i < BR_ROWS * BR_COLS; ++i) if (i%7==0) bricks[i].unbreakable = true;
    }

    bricks_alive = 0;
    for (int i = 0; i < BR_ROWS * BR_COLS; ++i) if (bricks[i].alive) bricks_alive++;

    int goldCount = 1 + (level % 3);
    setRandomGoldenBricks(goldCount);
}

// --- Reset functions ---
void spawnBall(float x, float y, float dirSign=1.0f) {
    Ball b;
    b.x = x; b.y = y;
    b.r = windowHeight * 0.013f; if (b.r < 4.0f) b.r = 4.0f;
    float base = (min(windowWidth, windowHeight) / 600.0f);
    b.sx = 0.25f * dirSign * base;
    b.sy = -0.25f * base;
    b.stuck = true;
    b.mega = false;
    b.gravitySlow = false;
    balls.push_back(b);
}

void resetBallsToPaddle() {
    balls.clear();
    spawnBall(paddleX + paddleW*0.5f, paddleY - windowHeight*0.005f);
}

void startNewGame() {
    gameStarted = true;
    score = 0; lives = 3;
    currentLevel = 1;
    recomputeLayout();
    loadLevelPattern(currentLevel);
    resetBallsToPaddle();
    pickups.clear();
    state = STATE_PLAYING;
    eggActive = false; activeEgg = EGG_NONE; speedMultiplier = 1.0f; laserEnabled = false;
}

void nextLevel() {
    currentLevel++;
    if (currentLevel > 4) currentLevel = 1;
    recomputeLayout();
    loadLevelPattern(currentLevel);
    resetBallsToPaddle();
    pickups.clear();
    score += 50;
}

// --- Spawn pickup when brick breaks ---
void spawnPickupAt(float x, float y) {
    // chance to spawn: ~45%
    if ((rand()%100) > 45) return;
    Pickup p;
    int choice = rand() % 13; // choose among types
    p.type = (PickupType)(1 + choice);
    p.x = x; p.y = y;
    p.vy = windowHeight * 0.0075f + (rand()%5)/100.0f * windowHeight * 0.01f; // fall speed base
    p.active = true;
    p.emoji = emojiFor(p.type);
    pickups.push_back(p);
}

// --- Apply pickup effect when collected ---
void applyPickupEffect(PickupType t) {
    // map to existing triggerEgg logic but immediate
    playPip();
    switch (t) {
        case P_EXTRA_LIFE: lives = max(lives,0) + 1; activeEgg = EGG_EXTRA_LIFE; eggActive = true; eggEnd = Clock::now() + chrono::seconds(1); break;
        case P_SCORE_BONUS: score += 100; activeEgg = EGG_SCORE_BONUS; eggActive = true; eggEnd = Clock::now() + chrono::seconds(1); break;
        case P_ENLARGE_PADDLE: savedPaddleW = paddleW; paddleW *= 1.6f; paddleX = clampf(paddleX,0.0f,(float)windowWidth-paddleW); activeEgg = EGG_ENLARGE_PADDLE; eggActive = true; eggEnd = Clock::now()+chrono::seconds(10); break;
        case P_SLOW_MOTION: speedMultiplier = 0.55f; activeEgg = EGG_SLOW_MOTION; eggActive = true; eggEnd = Clock::now()+chrono::seconds(10); break;
        case P_FAST_MOTION: speedMultiplier = 1.55f; activeEgg = EGG_FAST_MOTION; eggActive = true; eggEnd = Clock::now()+chrono::seconds(10); break;
        case P_MULTIBALL:
            // spawn 2 extra free balls
            if (!balls.empty()) {
                Ball base = balls.front();
                for (int i=0;i<2;i++) {
                    Ball nb = base;
                    nb.sx = base.sx * (i==0?1.0f:-1.0f) * 1.2f;
                    nb.sy = base.sy * 0.9f;
                    nb.stuck = false;
                    balls.push_back(nb);
                }
            }
            activeEgg = EGG_MULTIBALL; eggActive = true; eggEnd = Clock::now()+chrono::seconds(6);
            break;
        case P_LASER: laserEnabled = true; activeEgg = EGG_LASER; eggActive = true; eggEnd = Clock::now()+chrono::seconds(12); break;
        case P_GRAB_PADDLE: grabActive = true; activeEgg = EGG_GRAB_PADDLE; eggActive = true; eggEnd = Clock::now()+chrono::seconds(12); break;
        case P_MEGA_BALL: lives += 1; for (auto &b: balls) { b.mega = true; b.r *= 1.9f; } laserEnabled=false; speedMultiplier=1.0f; activeEgg = EGG_MEGA_BALL; eggActive=true; eggEnd=Clock::now()+chrono::seconds(8); break;
        case P_ZAP_BRICK: for (int i=0;i<BR_ROWS*BR_COLS;i++) bricks[i].unbreakable = false; activeEgg = EGG_ZAP_BRICK; eggActive=true; eggEnd=Clock::now()+chrono::seconds(1); break;
        case P_SHRINK_PADDLE: savedPaddleW = paddleW; paddleW *= 0.55f; paddleX = clampf(paddleX,0.0f,(float)windowWidth-paddleW); activeEgg = EGG_SHRINK_PADDLE; eggActive=true; eggEnd=Clock::now()+chrono::seconds(10); break;
        case P_FAST_BALL: speedMultiplier *= 1.9f; activeEgg = EGG_FAST_BALL; eggActive=true; eggEnd=Clock::now()+chrono::seconds(10); break;
        case P_GRAVITY_BALL: speedMultiplier *= 0.6f; for (auto &b: balls) b.gravitySlow = true; activeEgg = EGG_GRAVITY_BALL; eggActive=true; eggEnd=Clock::now()+chrono::seconds(10); break;
        default: break;
    }
}

// --- Power-up trigger kept for compatibility (used when golden brick triggers) ---
void triggerEgg(int brickIndex) {
    // spawn a pickup at brick location (now used for any golden brick and we also spawn pickups on regular breaks)
    float bx = bricks[brickIndex].x + bricks[brickIndex].w*0.5f;
    float by = bricks[brickIndex].y + bricks[brickIndex].h*0.5f;
    spawnPickupAt(bx, by);
}

void maybeRevertEggs() {
    if (!eggActive) return;
    if (Clock::now() >= eggEnd) {
        if (activeEgg == EGG_ENLARGE_PADDLE || activeEgg == EGG_SHRINK_PADDLE) {
            paddleW = savedPaddleW;
            paddleX = clampf(paddleX, 0.0f, (float)windowWidth - paddleW);
        }
        if (activeEgg == EGG_SLOW_MOTION || activeEgg == EGG_FAST_MOTION || activeEgg == EGG_FAST_BALL || activeEgg == EGG_GRAVITY_BALL) {
            speedMultiplier = 1.0f;
            for (auto &b: balls) b.gravitySlow = false;
        }
        if (activeEgg == EGG_LASER) { laserEnabled = false; lasers.clear(); }
        if (activeEgg == EGG_GRAB_PADDLE) { grabActive = false; }
        if (activeEgg == EGG_MEGA_BALL) { for (auto &b: balls) { b.mega = false; b.r = windowHeight * 0.013f; } }
        activeEgg = EGG_NONE; eggActive = false;
    }
}

// --- Drawing helpers ---
void drawRect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
      glVertex2f(x, y);
      glVertex2f(x + w, y);
      glVertex2f(x + w, y + h);
      glVertex2f(x, y + h);
    glEnd();
}
void drawCircle(float cx, float cy, float r) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    const int SEG = 36;
    for (int i = 0; i <= SEG; ++i) {
        float a = i / (float)SEG * 2.0f * 3.14159265f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
}
void drawText(float x, float y, const string &s) {
    glRasterPos2f(x, y);
    for (char c : s) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, c);
}

// --- Draw game objects ---
void drawHUD() {
    glColor3f(1,1,1);
    string s = "Score: " + to_string(score) + "  Lives: " + to_string(lives) + "  Level: " + to_string(currentLevel) + "  High: " + to_string(highScore);
    drawText(10.0f, 20.0f, s);
    if (eggActive && activeEgg != EGG_NONE) {
        string es;
        switch (activeEgg) {
    // 💚 Beneficial pickups (Green)
    case EGG_EXTRA_LIFE:
        glColor3f(0.0f, 1.0f, 0.0f); // Green
        es = "❤️  +1 Life";
        break;

    case EGG_SCORE_BONUS:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "⭐  +100";
        break;

    case EGG_ENLARGE_PADDLE:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "🟦  Paddle Up";
        break;

    case EGG_SLOW_MOTION:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "🐢  Slow Motion";
        break;

    case EGG_MULTIBALL:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "⚪⚪  Multiball";
        break;

    case EGG_LASER:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "🔫  Laser (F)";
        break;

    case EGG_GRAB_PADDLE:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "👐  Grab";
        break;

    case EGG_MEGA_BALL:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "🌕  Mega Ball";
        break;

    case EGG_ZAP_BRICK:
        glColor3f(0.0f, 1.0f, 0.0f);
        es = "💥  Zap";
        break;

    // ❤️‍🔥 Detrimental pickups (Red)
    case EGG_SHRINK_PADDLE:
        glColor3f(1.0f, 0.0f, 0.0f); // Red
        es = "🔻  Shrunk";
        break;

    case EGG_FAST_BALL:
        glColor3f(1.0f, 0.0f, 0.0f);
        es = "🚀  Fast Ball";
        break;

    case EGG_GRAVITY_BALL:
        glColor3f(1.0f, 0.0f, 0.0f);
        es = "🌧️  Gravity";
        break;

    default:
        glColor3f(1.0f, 1.0f, 1.0f); // White (no effect)
        es = "";
        break;
}

        if (!es.empty()) drawText(10.0f, 40.0f, es);
    }
}
void drawBricks() {
    for (int i = 0; i < BR_ROWS * BR_COLS; ++i) {
        if (!bricks[i].alive) continue;
        int row = i / BR_COLS;
        if (bricks[i].golden) {
            glColor3f(0.95f,0.8f,0.18f);
        } else {
            switch (row % 5) {
                case 0: glColor3f(0.86f,0.31f,0.31f); break;
                case 1: glColor3f(0.31f,0.86f,0.47f); break;
                case 2: glColor3f(0.31f,0.55f,0.86f); break;
                case 3: glColor3f(0.86f,0.78f,0.31f); break;
                default: glColor3f(0.7f,0.31f,0.86f); break;
            }
        }
        drawRect(bricks[i].x, bricks[i].y, bricks[i].w, bricks[i].h);
        // border
        glColor3f(0.04f,0.04f,0.06f);
        glBegin(GL_LINE_LOOP);
          glVertex2f(bricks[i].x, bricks[i].y);
          glVertex2f(bricks[i].x + bricks[i].w, bricks[i].y);
          glVertex2f(bricks[i].x + bricks[i].w, bricks[i].y + bricks[i].h);
          glVertex2f(bricks[i].x, bricks[i].y + bricks[i].h);
        glEnd();

        // indicate unbreakable
        if (bricks[i].unbreakable) {
            glColor3f(0.2f,0.2f,0.2f);
            drawText(bricks[i].x + 6, bricks[i].y + bricks[i].h*0.5f, "#");
        }

        if (bricks[i].golden) {
            float cx = bricks[i].x + bricks[i].w * 0.5f;
            float cy = bricks[i].y + bricks[i].h * 0.5f;
            float r = min(bricks[i].w, bricks[i].h) * 0.18f;
            glColor3f(1.0f, 0.9f, 0.2f);
            drawCircle(cx, cy, r);
        }
    }
}

void drawPickups() {
    for (auto &p: pickups) {
        if (!p.active) continue;
        // draw a small circle plus label (emoji if supported)
        float r = 10.0f;
        glColor3f(0.95f,0.95f,0.95f);
        drawCircle(p.x, p.y, r);
        // try to draw emoji (may not render on all systems), also draw short ASCII label
        glColor3f(0,0,0);
        string txt = p.emoji.empty() ? shortLabelFor(p.type) : p.emoji + " " + shortLabelFor(p.type);
        drawText(p.x - 8.0f, p.y + 5.0f, txt);
    }
}

void drawMenu() {
    glColor4f(0.02f,0.02f,0.06f,0.9f);
    drawRect(0,0, (float)windowWidth, (float)windowHeight);

    float boxW = windowWidth * 0.30f;
    float boxH = windowHeight * 0.08f;
    float cx = (windowWidth - boxW) * 0.5f;
    float startY = windowHeight * 0.28f;

    for (int i = 0; i < MENU_ITEMS; ++i) {
        float y = startY + i * (boxH + windowHeight * 0.02f);
        bool enabled = !(i == 1 && !resumeAvailable());
        glColor3f(enabled ? 0.2f : 0.4f, 0.5f, 0.9f);
        drawRect(cx, y, boxW, boxH);
        glColor3f(1,1,1);
        drawText(cx + boxW * 0.06f, y + boxH * 0.45f, menuText[i]);
    }
}

void drawHighScoreScreen() {
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1,1,1);
    drawText(windowWidth * 0.5f - 60, windowHeight * 0.25f, "HIGH SCORE");
    drawText(windowWidth * 0.5f - 80, windowHeight * 0.35f, string("Best: ") + to_string(highScore));
    drawText(windowWidth * 0.5f - 140, windowHeight * 0.6f, "Click anywhere to return to menu.");
}

// --- Display ---
void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    drawBricks();

    // pickups
    drawPickups();

    // paddle
    glColor3f(0.78f,0.78f,0.82f);
    drawRect(paddleX, paddleY, paddleW, paddleH);

    // lasers
    if (laserEnabled) {
        for (auto &L: lasers) {
            glColor3f(1.0f,0.2f,0.2f);
            drawRect(L.x-2, L.y, 4, L.h);
        }
    }

    // balls
    for (auto &b: balls) {
        glColor3f(b.mega?0.95f:0.95f, b.mega?0.6f:0.95f, b.mega?0.2f:0.95f);
        drawCircle(b.x, b.y, b.r);
    }

    drawHUD();

    if (state == STATE_MENU) drawMenu();
    else if (state == STATE_HIGHSCORE) drawHighScoreScreen();

    glutSwapBuffers();
}

// --- Update loop ---
void update(int value) {
    maybeRevertEggs();

    if (state == STATE_PLAYING) {
        // update pickups (falling)
        for (int i = (int)pickups.size()-1; i>=0; --i) {
            Pickup &p = pickups[i];
            if (!p.active) { pickups.erase(pickups.begin()+i); continue; }
            p.y += p.vy * 1.0f; // scale speed a bit
            // check paddle collision
            if (p.y >= paddleY && p.y <= paddleY + paddleH + 20.0f && p.x >= paddleX && p.x <= paddleX + paddleW) {
                applyPickupEffect(p.type);
                pickups.erase(pickups.begin()+i);
                continue;
            }
            // remove if out of screen
            if (p.y > windowHeight + 40.0f) pickups.erase(pickups.begin()+i);
        }

        // update lasers
        if (laserEnabled) {
            for (int i = (int)lasers.size()-1; i>=0; --i) {
                lasers[i].y -= laserSpeed;
                if (lasers[i].y + lasers[i].h < 0) lasers.erase(lasers.begin()+i);
                else {
                    // laser-brick collision
                    for (int j=0;j<BR_ROWS*BR_COLS;j++) {
                        if (!bricks[j].alive) continue;
                        float bx=bricks[j].x, by=bricks[j].y, bw=bricks[j].w, bh=bricks[j].h;
                        if (lasers[i].x >= bx && lasers[i].x <= bx + bw && lasers[i].y <= by + bh && lasers[i].y >= by) {
                            if (!bricks[j].unbreakable) {
                                bool wasGolden = bricks[j].golden;
                                float spawnX = bricks[j].x + bricks[j].w*0.5f;
                                float spawnY = bricks[j].y + bricks[j].h*0.5f;
                                bricks[j].alive = false; bricks[j].golden = false; bricks_alive--; score += 10; playPip();
                                // spawn pickup for any broken brick (chance inside)
                                spawnPickupAt(spawnX, spawnY);
                            }
                            lasers.erase(lasers.begin()+i);
                            break;
                        }
                    }
                }
            }
        }

        // update balls
        for (int bi = (int)balls.size()-1; bi >= 0; --bi) {
            Ball &ball = balls[bi];
            if (ball.stuck) continue; // stays on paddle
            float effectiveSpeed = 10.0f * speedMultiplier * (ball.gravitySlow?0.7f:1.0f);
            ball.x += ball.sx * effectiveSpeed;
            ball.y += ball.sy * effectiveSpeed;

            // wall collisions
            if (ball.x - ball.r < 0.0f) { ball.x = ball.r; ball.sx = -ball.sx; }
            if (ball.x + ball.r > windowWidth) { ball.x = windowWidth - ball.r; ball.sx = -ball.sx; }
            if (ball.y - ball.r < 0.0f) { ball.y = ball.r; ball.sy = -ball.sy; }

            // paddle collision
            if (ball.y + ball.r >= paddleY && ball.y - ball.r <= paddleY + paddleH &&
                ball.x >= paddleX && ball.x <= paddleX + paddleW) {
                // if grab active, stick ball to paddle
                if (grabActive) {
                    ball.stuck = true;
                    ball.x = paddleX + paddleW*0.5f;
                    ball.y = paddleY - ball.r - windowHeight*0.005f;
                    grabActive = false; // only catch once
                } else {
                    ball.sy = -fabs(ball.sy);
                    float hitPos = (ball.x - (paddleX + paddleW * 0.5f)) / (paddleW * 0.5f);
                    ball.sx = hitPos * (0.4f * (min(windowWidth, windowHeight) / 600.0f));
                }
            }

            // bricks collision
            for (int i = 0; i < BR_ROWS * BR_COLS; ++i) {
                if (!bricks[i].alive) continue;
                float bx = bricks[i].x, by = bricks[i].y, bw = bricks[i].w, bh = bricks[i].h;
                if (ball.x + ball.r > bx && ball.x - ball.r < bx + bw &&
                    ball.y + ball.r > by && ball.y - ball.r < by + bh) {
                    if (!bricks[i].unbreakable || ball.mega || activeEgg==EGG_ZAP_BRICK) {
                        bool wasGolden = bricks[i].golden;
                        float spawnX = bricks[i].x + bricks[i].w*0.5f;
                        float spawnY = bricks[i].y + bricks[i].h*0.5f;
                        bricks[i].alive = false; bricks[i].golden = false; bricks_alive--; score += 10; playPip();
                        // spawn pickup on ANY broken brick (chance inside spawnPickupAt)
                        spawnPickupAt(spawnX, spawnY);
                    } else {
                        // bounce off unbreakable
                    }
                    ball.sy = -ball.sy;
                    break;
                }
            }

            // lose life (ball below bottom)
            if (ball.y - ball.r > windowHeight) {
                // remove this ball
                balls.erase(balls.begin() + bi);
                if (balls.empty()) {
                    lives--;
                    if (score > highScore) highScore = score;
                    if (lives <= 0) { state = STATE_MENU; gameStarted = false; }
                    resetBallsToPaddle();
                    break;
                }
            }
        }

        // move stuck balls with paddle
        for (auto &b: balls) if (b.stuck) { b.x = paddleX + paddleW*0.5f; b.y = paddleY - b.r - windowHeight*0.005f; }

        // level cleared
        if (bricks_alive == 0) {
            if (score > highScore) highScore = score;
            nextLevel();
        }
    }

    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// --- Input handlers ---
void passiveMouseMotion(int mx, int my) {
    float gx = (float)mx;
    paddleX = gx - paddleW * 0.5f;
    paddleX = clampf(paddleX, 0.0f, (float)windowWidth - paddleW);
}

void mouseClick(int button, int stateBtn, int x, int y) {
    if (stateBtn != GLUT_DOWN) return;

    if (state == STATE_MENU) {
        float boxW = windowWidth * 0.30f;
        float boxH = windowHeight * 0.08f;
        float cx = (windowWidth - boxW) * 0.5f;
        float startY = windowHeight * 0.28f;
        for (int i = 0; i < MENU_ITEMS; ++i) {
            float my = (float)y;
            float mx = (float)x;
            float top = startY + i * (boxH + windowHeight * 0.02f);
            float bottom = top + boxH;
            if (mx >= cx && mx <= cx + boxW && my >= top && my <= bottom) {
                if (i == 0) startNewGame();
                else if (i == 1 && resumeAvailable()) state = STATE_PLAYING;
                else if (i == 2) state = STATE_HIGHSCORE;
                else if (i == 3) exit(0);
            }
        }
    } else if (state == STATE_PLAYING) {
        // if any ball is stuck, release all stuck balls
        bool anyStuck = false;
        for (auto &b: balls) if (b.stuck) anyStuck = true;
        if (anyStuck) {
            for (auto &b: balls) { b.stuck = false; b.sy = -fabs(b.sy==0? -0.25f : b.sy); }
            return;
        }
        // otherwise, left click can fire lasers if enabled
        if (laserEnabled && button == GLUT_LEFT_BUTTON) {
            Laser L; L.x = paddleX + paddleW*0.5f; L.y = paddleY; L.h = 6.0f; lasers.push_back(L);
        }
    } else if (state == STATE_HIGHSCORE) {
        state = STATE_MENU;
    }
}

void keyboard(unsigned char key, int x, int y) {
    (void)x; (void)y;
    if (key == 27) {
        if (state == STATE_PLAYING) state = STATE_MENU;
        else if (state == STATE_MENU && gameStarted) state = STATE_PLAYING;
    } else if (key == ' ') {
        if (!gameStarted) startNewGame();
        else {
            // release stuck balls
            for (auto &b: balls) if (b.stuck) { b.stuck = false; b.sy = -fabs(b.sy); }
        }
    } else if (key == 'f' || key == 'F') {
        if (laserEnabled) { Laser L; L.x = paddleX + paddleW*0.5f; L.y = paddleY; L.h = 6.0f; lasers.push_back(L); }
    }
}

// --- Reshape: use window size as logical coords
void reshape(int w, int h) {
    windowWidth = (w > 100 ? w : 100);
    windowHeight = (h > 80 ? h : 80);

    glViewport(0, 0, windowWidth, windowHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, (double)windowWidth, (double)windowHeight, 0.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    recomputeLayout();
    if (bricks_alive == 0) loadLevelPattern(currentLevel);
}

// --- Init ---
void initGL() {
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    recomputeLayout();
    loadLevelPattern(currentLevel);
    resetBallsToPaddle();
}

// --- Main ---
int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(windowWidth, windowHeight);
    glutCreateWindow("DX Ball - Extended: Pickups Fall + Emoji");
    initGL();
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutPassiveMotionFunc(passiveMouseMotion);
    glutMouseFunc(mouseClick);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, update, 0);
    glutMainLoop();
    return 0;
}
