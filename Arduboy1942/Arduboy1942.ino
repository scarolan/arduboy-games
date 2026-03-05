// ============================================================
// 1942 - Arduboy Portrait Mode
// A vertical scrolling shooter inspired by Capcom's 1942
// Hold Arduboy rotated CCW (D-pad at bottom, right-handed)
// D-pad: move, A: shoot, B: loop dodge
// ============================================================

#include <Arduboy2.h>

Arduboy2 arduboy;
BeepPin1 beep;

// --- Portrait dimensions ---
const uint8_t P_W = 64;
const uint8_t P_H = 128;
const uint8_t HUD_H = 10;
const uint8_t PLAY_TOP = HUD_H + 1;

// --- Object pool sizes ---
const uint8_t MAX_PB = 20;
const uint8_t MAX_EB = 16;
const uint8_t MAX_EN = 10;
const uint8_t MAX_EX = 5;
const uint8_t MAX_PU = 2;
const uint8_t NUM_STARS = 20;

// --- Player constants ---
const uint8_t SHIP_W = 9;
const uint8_t SHIP_H = 9;
const uint8_t SHIP_SPEED = 2;
const uint8_t PB_SPEED = 4;
const uint8_t SHOOT_CD = 6;

// --- Enemy types ---
const uint8_t ET_FIGHTER = 0;
const uint8_t ET_BOMBER  = 1;
const uint8_t ET_ACE     = 2;
const uint8_t ET_HEAVY   = 3;

// Enemy props: w, h, hp, baseSpeed, shootInterval (0=none)
const uint8_t PROGMEM eProp[][5] = {
  {5, 5, 1, 1, 100},
  {7, 7, 2, 1, 90},
  {5, 6, 2, 1, 60},
  {9, 8, 4, 1, 50},
};
const uint8_t PROGMEM ePoints[] = {10, 30, 50, 100};

// --- Movement patterns ---
const uint8_t PAT_STRAIGHT = 0;
const uint8_t PAT_DIAG_L   = 1;
const uint8_t PAT_DIAG_R   = 2;
const uint8_t PAT_ZIGZAG   = 3;
const uint8_t PAT_SWOOP_L  = 4; // swoop down, curve left, fly back up
const uint8_t PAT_SWOOP_R  = 5; // swoop down, curve right, fly back up
const uint8_t PAT_DIVE     = 6; // fast dive then pull back up
const uint8_t PAT_RISE     = 7; // enter from bottom, fly up and out

// --- Power-up types ---
const uint8_t PU_WEAPON = 0;
const uint8_t PU_LOOP   = 1;
const uint8_t PU_SIDE   = 2;

// --- Game states ---
enum GameState : uint8_t {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_DYING,
  STATE_GAMEOVER
};

// ============================================================
// Data structures
// ============================================================

struct PBullet { int16_t x, y; int8_t dx, dy; bool active; };
struct EBullet { int16_t x, y; int8_t dx, dy; bool active; };

struct Enemy {
  int16_t x, y;
  uint8_t type;
  uint8_t hp;
  uint8_t pattern;
  int8_t patDir;
  uint8_t phase;     // for swoop: 0=descend, 1=curve, 2=ascend
  uint8_t timer;
  bool active;
  bool flashHit;
  bool isLeader;
};

struct Explosion { int16_t x, y; uint8_t frame; bool active; };
struct PowerUp { int16_t x, y; uint8_t type; bool active; };

const uint8_t BOSS_W = 21;
const uint8_t BOSS_H = 15;

// ============================================================
// Game variables
// ============================================================

GameState gameState;

// Player
int16_t plX, plY;
uint8_t lives;
uint8_t power;         // 0-4 weapon levels
uint8_t shootCD;
uint8_t loopsLeft;
uint8_t loopTimer;
uint8_t invTimer;
bool sideL, sideR;     // individual side fighter tracking
uint16_t killCount;

// Score
uint16_t score;
uint16_t highScore;

// Wave system
uint8_t waveNum;
uint8_t waveSpawnLeft;
uint8_t waveSpawnTimer;
uint8_t waveType;
uint8_t wavePat;
int16_t waveSpX;
int8_t waveSpXStep;
uint16_t waveCD;

// Boss
int16_t bossX, bossY;
int8_t bossDX;
uint8_t bossHP, bossMaxHP;
uint8_t bossShootTimer;
uint8_t bossPattern, bossPatTimer;
bool bossActive, bossEntering, bossFlash;
uint8_t bossWarning;
uint8_t bossesBeaten;

// Side fighter fire toggle
bool sideFireToggle;

// Death
uint8_t deathTimer;

// Object pools
PBullet pb[MAX_PB];
EBullet eb[MAX_EB];
Enemy en[MAX_EN];
Explosion ex[MAX_EX];
PowerUp pu[MAX_PU];

// Stars
uint8_t stX[NUM_STARS];
uint8_t stY[NUM_STARS];

// ============================================================
// Portrait coordinate helpers
// ============================================================

void pPixel(int16_t px, int16_t py, uint8_t c) {
  arduboy.drawPixel(127 - py, px, c);
}
void pFillR(int16_t px, int16_t py, uint8_t pw, uint8_t ph, uint8_t c) {
  arduboy.fillRect(127 - py - ph + 1, px, ph, pw, c);
}
void pDrawR(int16_t px, int16_t py, uint8_t pw, uint8_t ph, uint8_t c) {
  arduboy.drawRect(127 - py - ph + 1, px, ph, pw, c);
}
void pHLine(int16_t px, int16_t py, uint8_t len, uint8_t c) {
  arduboy.drawFastVLine(127 - py, px, len, c);
}
void pVLine(int16_t px, int16_t py, uint8_t len, uint8_t c) {
  arduboy.drawFastHLine(127 - py - len + 1, px, len, c);
}

// ============================================================
// Portrait text
// ============================================================

void pChar(int16_t px, int16_t py, char c, uint8_t sz = 1) {
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&Arduboy2::font5x7[(uint8_t)c * 5 + col]);
    for (uint8_t row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        if (sz == 1) pPixel(px + col, py + row, WHITE);
        else pFillR(px + col * sz, py + row * sz, sz, sz, WHITE);
      }
    }
  }
}

void pText(int16_t px, int16_t py, const __FlashStringHelper *s, uint8_t sz = 1) {
  const char *p = (const char *)s;
  char c;
  while ((c = pgm_read_byte(p++)) != 0) {
    pChar(px, py, c, sz);
    px += 6 * sz;
  }
}

void pNum(int16_t px, int16_t py, uint16_t n, uint8_t sz = 1) {
  char buf[6]; uint8_t len = 0;
  if (n == 0) { buf[len++] = '0'; }
  else {
    char tmp[6]; uint8_t t = 0;
    while (n > 0) { tmp[t++] = '0' + (n % 10); n /= 10; }
    while (t > 0) buf[len++] = tmp[--t];
  }
  for (uint8_t i = 0; i < len; i++) pChar(px + i * 6 * sz, py, buf[i], sz);
}

// ============================================================
// Setup & main loop
// ============================================================

void setup() {
  arduboy.begin();
  arduboy.setFrameRate(60);
  arduboy.initRandomSeed();
  beep.begin();
  highScore = 0;
  initStars();
  gameState = STATE_TITLE;
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();
  beep.timer();
  arduboy.clear();
  drawStars();

  switch (gameState) {
    case STATE_TITLE:    doTitle();    break;
    case STATE_PLAYING:  doPlaying();  break;
    case STATE_DYING:    doDying();    break;
    case STATE_GAMEOVER: doGameOver(); break;
  }
  arduboy.display();
}

// ============================================================
// Stars
// ============================================================

void initStars() {
  for (uint8_t i = 0; i < NUM_STARS; i++) {
    stX[i] = random(0, P_W);
    stY[i] = random(0, P_H);
  }
}

void drawStars() {
  for (uint8_t i = 0; i < NUM_STARS; i++) {
    pPixel(stX[i], stY[i], WHITE);
    if (gameState == STATE_PLAYING && arduboy.everyXFrames(3)) {
      stY[i]++;
      if (stY[i] >= P_H) { stY[i] = 0; stX[i] = random(0, P_W); }
    }
  }
}

// ============================================================
// Title screen
// ============================================================

void doTitle() {
  pText(7, 10, F("1942"), 2);
  pHLine(8, 32, 48, WHITE);
  drawShip(28, 42, WHITE);
  pText(7, 60, F("A:shoot"));
  pText(7, 72, F("B:loop"));
  pText(4, 84, F("<D-pad>"));

  if ((arduboy.frameCount / 30) % 2 == 0)
    pText(4, 102, F("Press A"));

  if (highScore > 0) {
    pText(4, 116, F("Hi:"));
    pNum(22, 116, highScore);
  }

  if (arduboy.justPressed(A_BUTTON)) {
    initGame();
    gameState = STATE_PLAYING;
  }
}

// ============================================================
// Carrier takeoff
// ============================================================

void initGame() {
  plX = (P_W - SHIP_W) / 2;
  plY = P_H - 20;
  lives = 3;
  power = 0;
  shootCD = 0;
  loopsLeft = 3;
  loopTimer = 0;
  invTimer = 0;
  sideL = false; sideR = false;
  killCount = 0;
  score = 0;

  waveNum = 0;
  waveSpawnLeft = 0;
  waveCD = 90;
  bossActive = false;
  bossEntering = false;
  bossWarning = 0;
  bossesBeaten = 0;

  for (uint8_t i = 0; i < MAX_PB; i++) pb[i].active = false;
  for (uint8_t i = 0; i < MAX_EB; i++) eb[i].active = false;
  for (uint8_t i = 0; i < MAX_EN; i++) en[i].active = false;
  for (uint8_t i = 0; i < MAX_EX; i++) ex[i].active = false;
  for (uint8_t i = 0; i < MAX_PU; i++) pu[i].active = false;

  sideFireToggle = false;
  invTimer = 60;
}

// ============================================================
// Gameplay
// ============================================================

void doPlaying() {
  updatePlayer();
  updatePBullets();
  updateEnemies();
  updateEBullets();
  updateExplosions();
  updatePowerUps();
  updateBoss();
  updateWaves();
  checkCollisions();

  drawEnemies();
  if (bossActive) drawBoss();
  drawPBullets();
  drawEBullets();
  drawPowerUps();
  drawExplosions();

  // Player (blink if invincible/looping)
  bool show = true;
  if (loopTimer > 0) show = (arduboy.frameCount / 2) % 2 == 0;
  else if (invTimer > 0) show = (arduboy.frameCount / 3) % 2 == 0;
  if (show) drawShip(plX, plY, WHITE);

  // Side fighters
  if (sideL) drawSideFighter(plX - 5, plY + 3);
  if (sideR) drawSideFighter(plX + SHIP_W + 1, plY + 3);

  if (bossWarning > 0 && (arduboy.frameCount / 6) % 2 == 0)
    pText(5, 55, F("WARNING!!"));

  if (bossActive && !bossEntering) drawBossHP();

  drawHUD();
}

// ============================================================
// Player
// ============================================================

void updatePlayer() {
  if (arduboy.pressed(UP_BUTTON))    plX -= SHIP_SPEED;
  if (arduboy.pressed(DOWN_BUTTON))  plX += SHIP_SPEED;
  if (arduboy.pressed(RIGHT_BUTTON)) plY -= SHIP_SPEED;
  if (arduboy.pressed(LEFT_BUTTON))  plY += SHIP_SPEED;

  if (plX < 0) plX = 0;
  if (plX > P_W - SHIP_W) plX = P_W - SHIP_W;
  if (plY < PLAY_TOP) plY = PLAY_TOP;
  if (plY > P_H - SHIP_H - 2) plY = P_H - SHIP_H - 2;

  if (invTimer > 0) invTimer--;
  if (loopTimer > 0) { loopTimer--; return; }

  if (shootCD > 0) shootCD--;
  if (arduboy.pressed(A_BUTTON) && shootCD == 0) {
    firePlayerBullets();
    shootCD = SHOOT_CD;
    beep.tone(900, 2);
  }

  if (arduboy.justPressed(B_BUTTON) && loopsLeft > 0 && loopTimer == 0) {
    loopTimer = 40;
    loopsLeft--;
    beep.tone(500, 12);
  }
}

void firePlayerBullets() {
  int16_t cx = plX + SHIP_W / 2;
  int16_t ty = plY - 2;

  switch (power) {
    case 0: // single
      spawnPB(cx, ty, 0, -PB_SPEED);
      break;
    case 1: // double
      spawnPB(cx - 2, ty, 0, -PB_SPEED);
      spawnPB(cx + 2, ty, 0, -PB_SPEED);
      break;
    case 2: // triple
      spawnPB(cx, ty - 2, 0, -PB_SPEED);
      spawnPB(cx - 3, ty, 0, -PB_SPEED);
      spawnPB(cx + 3, ty, 0, -PB_SPEED);
      break;
    case 3: // wide spread
      spawnPB(cx, ty - 2, 0, -PB_SPEED);
      spawnPB(cx - 2, ty, -1, -PB_SPEED);
      spawnPB(cx + 2, ty, 1, -PB_SPEED);
      spawnPB(cx - 4, ty + 1, -2, -3);
      spawnPB(cx + 4, ty + 1, 2, -3);
      break;
    default: // level 4: heavy barrage + rear defense
      spawnPB(cx, ty - 2, 0, -PB_SPEED);
      spawnPB(cx - 3, ty, -1, -PB_SPEED);
      spawnPB(cx + 3, ty, 1, -PB_SPEED);
      spawnPB(cx - 1, plY + SHIP_H, 0, 3); // rear shot
      spawnPB(cx + 1, plY + SHIP_H, 0, 3); // rear shot
      break;
  }

  // Side fighter bullets (every other volley)
  sideFireToggle = !sideFireToggle;
  if (sideFireToggle) {
    if (sideL) spawnPB(plX - 3, plY + 2, 0, -PB_SPEED);
    if (sideR) spawnPB(plX + SHIP_W + 3, plY + 2, 0, -PB_SPEED);
  }
}

void spawnPB(int16_t x, int16_t y, int8_t dx, int8_t dy) {
  for (uint8_t i = 0; i < MAX_PB; i++) {
    if (!pb[i].active) {
      pb[i] = {x, y, dx, dy, true};
      return;
    }
  }
}

// ============================================================
// Player bullets
// ============================================================

void updatePBullets() {
  for (uint8_t i = 0; i < MAX_PB; i++) {
    if (!pb[i].active) continue;
    pb[i].x += pb[i].dx;
    pb[i].y += pb[i].dy;
    if (pb[i].y < PLAY_TOP - 4 || pb[i].y > P_H + 4 ||
        pb[i].x < -4 || pb[i].x > P_W + 4)
      pb[i].active = false;
  }
}

void drawPBullets() {
  for (uint8_t i = 0; i < MAX_PB; i++) {
    if (!pb[i].active) continue;
    pVLine(pb[i].x, pb[i].y, 3, WHITE);
  }
}

// ============================================================
// Enemies
// ============================================================

uint8_t eW(uint8_t t) { return pgm_read_byte(&eProp[t][0]); }
uint8_t eH(uint8_t t) { return pgm_read_byte(&eProp[t][1]); }
uint8_t eHP(uint8_t t) { return pgm_read_byte(&eProp[t][2]); }
uint8_t eSpd(uint8_t t) { return pgm_read_byte(&eProp[t][3]); }
uint8_t eShoot(uint8_t t) { return pgm_read_byte(&eProp[t][4]); }

void spawnEnemy(uint8_t type, int16_t x, int16_t y, uint8_t pat, bool leader) {
  for (uint8_t i = 0; i < MAX_EN; i++) {
    if (!en[i].active) {
      en[i].x = x; en[i].y = y;
      en[i].type = type;
      en[i].hp = eHP(type);
      en[i].pattern = pat;
      en[i].patDir = (pat == PAT_SWOOP_L || pat == PAT_DIAG_L) ? -1 :
                     (pat == PAT_RISE && x > P_W/2) ? -1 : 1;
      en[i].phase = 0;
      en[i].timer = 0;
      en[i].active = true;
      en[i].flashHit = false;
      en[i].isLeader = leader;
      return;
    }
  }
}

void updateEnemies() {
  for (uint8_t i = 0; i < MAX_EN; i++) {
    if (!en[i].active) continue;
    Enemy &e = en[i];
    uint8_t spd = eSpd(e.type);

    switch (e.pattern) {
      case PAT_STRAIGHT:
        e.y += spd;
        break;

      case PAT_DIAG_L:
        e.y += spd;
        e.x -= 1;
        break;

      case PAT_DIAG_R:
        e.y += spd;
        e.x += 1;
        break;

      case PAT_ZIGZAG:
        e.y += spd;
        e.x += e.patDir;
        if (e.timer % 20 == 0) e.patDir = -e.patDir;
        break;

      case PAT_SWOOP_L:
      case PAT_SWOOP_R: {
        // Classic 1942: fly down, curve, fly back up
        int8_t drift = (e.pattern == PAT_SWOOP_L) ? -1 : 1;
        switch (e.phase) {
          case 0: // Descend
            e.y += spd;
            if (e.timer > 10) e.x += drift; // start drifting
            if (e.timer >= 45) { e.phase = 1; e.timer = 0; }
            break;
          case 1: // Bottom of curve - mostly horizontal
            e.x += drift * 2;
            if (e.timer < 10) e.y += spd; // still sinking slightly
            // Shooting happens at bottom of swoop
            if (e.timer >= 25) { e.phase = 2; e.timer = 0; }
            break;
          case 2: // Ascend back out
            e.y -= spd;
            e.x += drift;
            if (e.y < -10) e.active = false;
            break;
        }
        break;
      }

      case PAT_DIVE: {
        // Fast dive then sharp pullback
        switch (e.phase) {
          case 0: // Fast dive
            e.y += spd * 2;
            e.x += e.patDir;
            if (e.timer >= 30) { e.phase = 1; e.timer = 0; }
            break;
          case 1: // Sharp curve
            e.y += 0;
            e.x += e.patDir * 2;
            if (e.timer >= 15) { e.phase = 2; e.timer = 0; }
            break;
          case 2: // Fly back up fast
            e.y -= spd * 2;
            e.x += e.patDir;
            if (e.y < -10) e.active = false;
            break;
        }
        break;
      }

      case PAT_RISE: {
        // Enter from below, fly up, drift sideways, exit top
        switch (e.phase) {
          case 0: // Rise up
            e.y -= spd;
            e.x += e.patDir;
            if (e.timer >= 40) { e.phase = 1; e.timer = 0; }
            break;
          case 1: // Curve across screen
            e.y -= spd;
            e.x += e.patDir * 2;
            if (e.timer >= 25) { e.phase = 2; e.timer = 0; }
            break;
          case 2: // Exit upward
            e.y -= spd * 2;
            if (e.y < -10) e.active = false;
            break;
        }
        break;
      }
    }

    e.timer++;

    // Off screen (for non-swoop patterns)
    if (e.y > P_H + 10 || e.x < -15 || e.x > P_W + 15) {
      e.active = false;
      continue;
    }

    // Shooting - enemies shoot during attack phases
    uint8_t si = eShoot(e.type);
    if (si > 0 && e.y > PLAY_TOP && e.y < P_H - 10) {
      bool inAttackPhase;
      if (e.pattern == PAT_RISE) {
        inAttackPhase = (e.phase == 0 || e.phase == 1); // rising: shoot while climbing
      } else if (e.pattern >= PAT_SWOOP_L) {
        inAttackPhase = (e.phase == 1); // swoop/dive: shoot at curve
      } else {
        inAttackPhase = true; // straight/diag/zigzag: always
      }
      if (inAttackPhase && e.timer % si == 0) {
        enemyFire(e);
      }
    }

    e.flashHit = false;
  }
}

void enemyFire(Enemy &e) {
  int16_t cx = e.x + eW(e.type) / 2;
  int16_t by = e.y + eH(e.type);
  bool rising = (e.pattern == PAT_RISE);

  switch (e.type) {
    case ET_FIGHTER:
      if (rising) {
        int8_t dx = 0;
        if (plX + SHIP_W/2 < cx - 4) dx = -1;
        else if (plX + SHIP_W/2 > cx + 4) dx = 1;
        spawnEB(cx, by, dx, 2);
      } else {
        spawnEB(cx, by, 0, 3);
      }
      break;
    case ET_BOMBER:
      spawnEB(cx - 1, by, -1, 2);
      spawnEB(cx + 1, by, 1, 2);
      break;
    case ET_ACE: {
      int8_t dx = 0;
      if (plX + SHIP_W/2 < cx - 4) dx = -1;
      else if (plX + SHIP_W/2 > cx + 4) dx = 1;
      spawnEB(cx, by, dx, 2);
      break;
    }
    case ET_HEAVY:
      spawnEB(cx, by, -1, 1);
      spawnEB(cx, by, 0, 2);
      spawnEB(cx, by, 1, 1);
      beep.tone(200, 3);
      break;
  }
}

void drawEnemies() {
  for (uint8_t i = 0; i < MAX_EN; i++) {
    if (!en[i].active) continue;
    uint8_t c = en[i].flashHit ? BLACK : WHITE;
    switch (en[i].type) {
      case ET_FIGHTER: drawFighter(en[i].x, en[i].y, c); break;
      case ET_BOMBER:  drawBomber(en[i].x, en[i].y, c);  break;
      case ET_ACE:     drawAce(en[i].x, en[i].y, c);     break;
      case ET_HEAVY:   drawHeavy(en[i].x, en[i].y, c);   break;
    }
  }
}

// ============================================================
// Enemy bullets
// ============================================================

void spawnEB(int16_t x, int16_t y, int8_t dx, int8_t dy) {
  for (uint8_t i = 0; i < MAX_EB; i++) {
    if (!eb[i].active) {
      eb[i] = {x, y, dx, dy, true};
      return;
    }
  }
}

void updateEBullets() {
  for (uint8_t i = 0; i < MAX_EB; i++) {
    if (!eb[i].active) continue;
    eb[i].x += eb[i].dx;
    eb[i].y += eb[i].dy;
    if (eb[i].y > P_H + 4 || eb[i].y < PLAY_TOP - 4 ||
        eb[i].x < -4 || eb[i].x > P_W + 4)
      eb[i].active = false;
  }
}

void drawEBullets() {
  for (uint8_t i = 0; i < MAX_EB; i++) {
    if (!eb[i].active) continue;
    pFillR(eb[i].x, eb[i].y, 2, 2, WHITE);
  }
}

// ============================================================
// Explosions
// ============================================================

void spawnExplosion(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < MAX_EX; i++) {
    if (!ex[i].active) {
      ex[i] = {x, y, 0, true};
      return;
    }
  }
}

void updateExplosions() {
  for (uint8_t i = 0; i < MAX_EX; i++) {
    if (!ex[i].active) continue;
    ex[i].frame++;
    if (ex[i].frame > 12) ex[i].active = false;
  }
}

void drawExplosions() {
  for (uint8_t i = 0; i < MAX_EX; i++) {
    if (!ex[i].active) continue;
    int16_t x = ex[i].x, y = ex[i].y;
    uint8_t f = ex[i].frame;
    if (f < 4) {
      pPixel(x, y, WHITE);
      pPixel(x-1, y, WHITE); pPixel(x+1, y, WHITE);
      pPixel(x, y-1, WHITE); pPixel(x, y+1, WHITE);
    } else if (f < 8) {
      uint8_t r = f - 2;
      pPixel(x-r, y, WHITE); pPixel(x+r, y, WHITE);
      pPixel(x, y-r, WHITE); pPixel(x, y+r, WHITE);
      pPixel(x-r+1, y-r+1, WHITE); pPixel(x+r-1, y-r+1, WHITE);
      pPixel(x-r+1, y+r-1, WHITE); pPixel(x+r-1, y+r-1, WHITE);
    } else {
      pPixel(x-(f-6), y-(f-7), WHITE); pPixel(x+(f-6), y+(f-7), WHITE);
      pPixel(x+(f-7), y-(f-6), WHITE); pPixel(x-(f-7), y+(f-6), WHITE);
    }
  }
}

// ============================================================
// Power-ups
// ============================================================

void spawnPowerUp(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < MAX_PU; i++) {
    if (!pu[i].active) {
      pu[i].x = x; pu[i].y = y;
      uint8_t r = random(0, 10);
      pu[i].type = (r < 5) ? PU_WEAPON : (r < 8) ? PU_LOOP : PU_SIDE;
      pu[i].active = true;
      return;
    }
  }
}

void updatePowerUps() {
  for (uint8_t i = 0; i < MAX_PU; i++) {
    if (!pu[i].active) continue;
    pu[i].y += 1;
    if (pu[i].y > P_H) pu[i].active = false;
  }
}

void drawPowerUps() {
  for (uint8_t i = 0; i < MAX_PU; i++) {
    if (!pu[i].active) continue;
    if ((arduboy.frameCount / 4) % 2 == 0)
      pDrawR(pu[i].x - 1, pu[i].y - 1, 7, 9, WHITE);
    char c = (pu[i].type == PU_WEAPON) ? 'P' : (pu[i].type == PU_LOOP) ? 'L' : 'S';
    pChar(pu[i].x, pu[i].y, c);
  }
}

void collectPowerUp(uint8_t type) {
  beep.tone(1200, 8);
  switch (type) {
    case PU_WEAPON:
      if (power < 4) power++;
      score += 50;
      break;
    case PU_LOOP:
      loopsLeft += 2;
      if (loopsLeft > 9) loopsLeft = 9;
      score += 50;
      break;
    case PU_SIDE:
      sideL = true; sideR = true;
      score += 100;
      break;
  }
}

// ============================================================
// Wave system
// ============================================================

void updateWaves() {
  if (bossActive || bossWarning > 0) return;

  if (waveSpawnLeft > 0) {
    waveSpawnTimer++;
    uint8_t interval = 14 - min((uint8_t)6, (uint8_t)(waveNum / 3));
    if (waveSpawnTimer >= interval) {
      waveSpawnTimer = 0;
      bool leader = (waveSpawnLeft == 1);
      int16_t spY = (wavePat == PAT_RISE) ? (int16_t)(P_H + eH(waveType)) : -(int16_t)eH(waveType);
      spawnEnemy(waveType, waveSpX, spY, wavePat, leader);
      waveSpX += waveSpXStep;
      if (waveSpX < 2) waveSpX = 2;
      if (waveSpX > P_W - 10) waveSpX = P_W - 10;
      waveSpawnLeft--;
    }
    return;
  }

  if (waveCD > 0) { waveCD--; return; }
  startNextWave();
}

void startNextWave() {
  waveNum++;
  waveSpawnTimer = 0;

  if (waveNum % 12 == 0) {
    bossWarning = 120;
    beep.tone(300, 20);
    return;
  }

  // More variety, bigger waves, enemies from behind
  uint8_t phase = waveNum % 12;
  uint8_t extra = waveNum / 12; // more enemies each cycle
  switch (phase) {
    case 1: // Swoop from left
      setupWave(ET_FIGHTER, 6 + extra, PAT_SWOOP_R, 3, 5);
      break;
    case 2: // Swoop from right
      setupWave(ET_FIGHTER, 6 + extra, PAT_SWOOP_L, P_W - 8, -5);
      break;
    case 3: // Fighters rise from below-left
      setupWaveBottom(ET_FIGHTER, 4 + extra, PAT_RISE, 5, 6);
      break;
    case 4: // Bombers fly through + fighters swoop
      setupWave(ET_BOMBER, 3 + extra/2, PAT_STRAIGHT, 8, 16);
      break;
    case 5: // Dive bombers
      setupWave(ET_FIGHTER, 5 + extra, PAT_DIVE, P_W / 2 - 5, 0);
      break;
    case 6: // Aces swoop from right
      setupWave(ET_ACE, 4 + extra, PAT_SWOOP_L, P_W - 6, -5);
      break;
    case 7: // Fighters rise from below-right
      setupWaveBottom(ET_FIGHTER, 4 + extra, PAT_RISE, P_W - 10, -6);
      break;
    case 8: // Zigzag fighters
      setupWave(ET_FIGHTER, 6 + extra, PAT_ZIGZAG, P_W / 2, 0);
      break;
    case 9: // Aces from below
      setupWaveBottom(ET_ACE, 3 + extra, PAT_RISE, P_W / 2, 4);
      break;
    case 10: // Heavy + escorts
      setupWave(ET_HEAVY, 1 + extra/2, PAT_STRAIGHT, (P_W - 9) / 2, 0);
      break;
    case 11: // Big mixed swoop
      setupWave(ET_ACE, 5 + extra, PAT_SWOOP_R, 2, 5);
      break;
  }

  // Tighter cooldowns - action stays intense
  uint8_t scaled = waveNum * 4;
  waveCD = (scaled >= 70) ? 20 : (70 - scaled);
  if (waveCD < 20) waveCD = 20;
}

void setupWaveBottom(uint8_t type, uint8_t count, uint8_t pat, int16_t sx, int8_t sxStep) {
  waveType = type;
  waveSpawnLeft = count;
  wavePat = pat;
  waveSpX = sx;
  waveSpXStep = sxStep;
}

void setupWave(uint8_t type, uint8_t count, uint8_t pat, int16_t sx, int8_t sxStep) {
  waveType = type;
  waveSpawnLeft = count;
  wavePat = pat;
  waveSpX = sx;
  waveSpXStep = sxStep;
}

// ============================================================
// Boss
// ============================================================

void startBoss() {
  bossX = (P_W - BOSS_W) / 2;
  bossY = -(int16_t)BOSS_H;
  bossDX = 1;
  bossMaxHP = 20 + bossesBeaten * 10;
  bossHP = bossMaxHP;
  bossShootTimer = 0;
  bossPattern = 0;
  bossPatTimer = 0;
  bossActive = true;
  bossEntering = true;
  bossFlash = false;
}

void updateBoss() {
  if (bossWarning > 0) {
    bossWarning--;
    if (bossWarning == 0) startBoss();
    return;
  }
  if (!bossActive) return;

  if (bossEntering) {
    if (arduboy.everyXFrames(2)) bossY += 1;
    if (bossY >= PLAY_TOP + 8) {
      bossEntering = false;
      bossY = PLAY_TOP + 8;
    }
    return;
  }

  // Slow drift
  if (arduboy.everyXFrames(3)) bossX += bossDX;
  if (bossX <= 1) { bossX = 1; bossDX = 1; }
  if (bossX >= P_W - BOSS_W - 1) { bossX = P_W - BOSS_W - 1; bossDX = -1; }

  // Vertical roaming in upper 1/3
  if (arduboy.everyXFrames(8)) {
    int16_t targetY = PLAY_TOP + 8 + (bossPattern * 10);
    if (bossY < targetY) bossY++;
    else if (bossY > targetY) bossY--;
  }

  bossPatTimer++;
  if (bossPatTimer >= 180) {
    bossPatTimer = 0;
    bossPattern = (bossPattern + 1) % 3;
  }

  bossShootTimer++;
  int16_t bcx = bossX + BOSS_W / 2;
  int16_t bby = bossY + BOSS_H;

  switch (bossPattern) {
    case 0:
      if (bossShootTimer >= 40) {
        bossShootTimer = 0;
        int8_t dx = 0;
        if (plX + SHIP_W/2 < bcx - 6) dx = -1;
        else if (plX + SHIP_W/2 > bcx + 6) dx = 1;
        spawnEB(bossX + 2, bby - 2, dx, 1);
        spawnEB(bossX + BOSS_W - 2, bby - 2, dx, 1);
        beep.tone(250, 2);
      }
      break;
    case 1:
      if (bossShootTimer >= 50) {
        bossShootTimer = 0;
        spawnEB(bcx, bby, -2, 1);
        spawnEB(bcx, bby, -1, 2);
        spawnEB(bcx, bby, 0, 2);
        spawnEB(bcx, bby, 1, 2);
        spawnEB(bcx, bby, 2, 1);
        beep.tone(180, 3);
      }
      break;
    case 2:
      if (bossShootTimer >= 20) {
        bossShootTimer = 0;
        if ((bossPatTimer / 20) % 2 == 0) {
          spawnEB(bossX + 3, bby, -1, 2);
          spawnEB(bcx, bby, 0, 1);
        } else {
          spawnEB(bossX + BOSS_W - 3, bby, 1, 2);
          spawnEB(bcx, bby, 0, 1);
        }
        beep.tone(300, 1);
      }
      break;
  }

  // Spawn minion swoops from top
  if (arduboy.everyXFrames(100)) {
    spawnEnemy(ET_FIGHTER, bossX - 4, bossY + 6, PAT_SWOOP_L, false);
    spawnEnemy(ET_FIGHTER, bossX + BOSS_W + 1, bossY + 6, PAT_SWOOP_R, false);
  }
  // Spawn fighters from below to attack from behind
  if (arduboy.everyXFrames(150)) {
    int16_t rx = random(5, P_W - 10);
    int8_t dir = (rx < P_W/2) ? 1 : -1;
    spawnEnemy(ET_FIGHTER, rx, P_H + 5, PAT_RISE, false);
  }

  bossFlash = false;
}

void bossHit() {
  bossHP--;
  bossFlash = true;
  beep.tone(350, 2);

  if (bossHP == 0) {
    bossActive = false;
    score += 500 + bossesBeaten * 200;
    bossesBeaten++;
    spawnExplosion(bossX + BOSS_W/2, bossY + BOSS_H/2);
    spawnExplosion(bossX + 3, bossY + 4);
    spawnExplosion(bossX + BOSS_W - 3, bossY + 4);
    spawnExplosion(bossX + BOSS_W/2, bossY + 2);
    spawnExplosion(bossX + BOSS_W/2, bossY + BOSS_H - 2);
    spawnPowerUp(bossX + BOSS_W/2 - 2, bossY + BOSS_H/2);
    if (loopsLeft < 9) loopsLeft++;
    beep.tone(100, 20);
    waveCD = 120;
    for (uint8_t i = 0; i < MAX_EB; i++) eb[i].active = false;
  }
}

void drawBoss() {
  int16_t x = bossX, y = bossY;
  uint8_t c = bossFlash ? BLACK : WHITE;

  pPixel(x+10, y, c);
  pFillR(x+9, y+1, 3, 1, c);
  pFillR(x+8, y+2, 5, 1, c);
  pPixel(x+8, y+3, c); pPixel(x+9, y+3, c);
  pPixel(x+11, y+3, c); pPixel(x+12, y+3, c);
  pFillR(x+7, y+4, 3, 1, c); pFillR(x+11, y+4, 3, 1, c);
  pFillR(x+6, y+5, 3, 1, c); pPixel(x+10, y+5, c); pFillR(x+12, y+5, 3, 1, c);
  pPixel(x+3, y+6, c);
  pFillR(x+6, y+6, 3, 1, c); pPixel(x+10, y+6, c); pFillR(x+12, y+6, 3, 1, c);
  pPixel(x+17, y+6, c);
  pFillR(x+2, y+7, 3, 1, c);
  pFillR(x+6, y+7, 3, 1, c); pPixel(x+10, y+7, c); pFillR(x+12, y+7, 3, 1, c);
  pFillR(x+16, y+7, 3, 1, c);
  pFillR(x, y+8, 21, 1, c);
  pFillR(x+1, y+9, 4, 1, c);
  pFillR(x+7, y+9, 7, 1, c);
  pFillR(x+16, y+9, 4, 1, c);
  pFillR(x+7, y+10, 7, 1, c);
  pFillR(x+8, y+11, 5, 1, c);
  pPixel(x+8, y+12, c); pPixel(x+9, y+12, c);
  pPixel(x+11, y+12, c); pPixel(x+12, y+12, c);
  pPixel(x+9, y+13, c); pPixel(x+11, y+13, c);
  pPixel(x+9, y+14, c); pPixel(x+11, y+14, c);
}

void drawBossHP() {
  uint8_t barW = 50;
  uint8_t barX = (P_W - barW) / 2;
  uint8_t barY = P_H - 5;
  pDrawR(barX - 1, barY - 1, barW + 2, 5, WHITE);
  uint8_t fillW = (uint16_t)barW * bossHP / bossMaxHP;
  if (fillW > 0) pFillR(barX, barY, fillW, 3, WHITE);
}

// ============================================================
// Collision detection
// ============================================================

bool boxHit(int16_t ax, int16_t ay, uint8_t aw, uint8_t ah,
            int16_t bx, int16_t by, uint8_t bw, uint8_t bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void checkCollisions() {
  // Player bullets vs boss
  if (bossActive && !bossEntering) {
    for (uint8_t i = 0; i < MAX_PB; i++) {
      if (!pb[i].active) continue;
      if (boxHit(pb[i].x, pb[i].y, 1, 3, bossX, bossY, BOSS_W, BOSS_H)) {
        pb[i].active = false;
        bossHit();
        if (!bossActive) break;
      }
    }
  }

  // Player bullets vs enemies
  for (uint8_t i = 0; i < MAX_PB; i++) {
    if (!pb[i].active) continue;
    for (uint8_t j = 0; j < MAX_EN; j++) {
      if (!en[j].active) continue;
      if (boxHit(pb[i].x, pb[i].y, 1, 3, en[j].x, en[j].y, eW(en[j].type), eH(en[j].type))) {
        pb[i].active = false;
        en[j].hp--;
        if (en[j].hp == 0) destroyEnemy(j);
        else { en[j].flashHit = true; beep.tone(400, 2); }
        break;
      }
    }
  }

  if (invTimer > 0 || loopTimer > 0) return;

  // Player hitbox (3x3 center)
  int16_t hx = plX + 3, hy = plY + 3;

  // Enemy bullets vs player and side fighters
  for (uint8_t i = 0; i < MAX_EB; i++) {
    if (!eb[i].active) continue;

    // Check side fighters first (they can absorb hits)
    if (sideL && boxHit(plX - 5, plY + 3, 3, 4, eb[i].x, eb[i].y, 2, 2)) {
      eb[i].active = false;
      sideL = false;
      spawnExplosion(plX - 4, plY + 5);
      beep.tone(150, 6);
      continue;
    }
    if (sideR && boxHit(plX + SHIP_W + 1, plY + 3, 3, 4, eb[i].x, eb[i].y, 2, 2)) {
      eb[i].active = false;
      sideR = false;
      spawnExplosion(plX + SHIP_W + 2, plY + 5);
      beep.tone(150, 6);
      continue;
    }

    if (boxHit(hx, hy, 3, 3, eb[i].x, eb[i].y, 2, 2)) {
      eb[i].active = false;
      playerDeath();
      return;
    }
  }

  // Enemies vs player
  for (uint8_t i = 0; i < MAX_EN; i++) {
    if (!en[i].active) continue;
    if (boxHit(hx, hy, 3, 3, en[i].x, en[i].y, eW(en[i].type), eH(en[i].type))) {
      destroyEnemy(i);
      playerDeath();
      return;
    }
  }

  // Boss body vs player
  if (bossActive && !bossEntering) {
    if (boxHit(hx, hy, 3, 3, bossX, bossY, BOSS_W, BOSS_H)) {
      playerDeath();
      return;
    }
  }

  // Player vs power-ups
  for (uint8_t i = 0; i < MAX_PU; i++) {
    if (!pu[i].active) continue;
    if (boxHit(plX, plY, SHIP_W, SHIP_H, pu[i].x - 1, pu[i].y - 1, 7, 9)) {
      collectPowerUp(pu[i].type);
      pu[i].active = false;
    }
  }
}

void destroyEnemy(uint8_t idx) {
  Enemy &e = en[idx];
  spawnExplosion(e.x + eW(e.type)/2, e.y + eH(e.type)/2);
  score += pgm_read_byte(&ePoints[e.type]);
  killCount++;
  beep.tone(200, 4);
  if (e.isLeader) spawnPowerUp(e.x + eW(e.type)/2 - 2, e.y + eH(e.type)/2);
  e.active = false;
}

void playerDeath() {
  spawnExplosion(plX + SHIP_W/2, plY + SHIP_H/2);
  beep.tone(80, 30);
  lives--;
  sideL = false; sideR = false;

  if (lives == 0) {
    if (score > highScore) highScore = score;
    deathTimer = 60;
    gameState = STATE_DYING;
  } else {
    plX = (P_W - SHIP_W) / 2;
    plY = P_H - SHIP_H - 8;
    invTimer = 90;
    if (power > 0) power--;
  }
}

// ============================================================
// Death / Game over
// ============================================================

void doDying() {
  updateExplosions();
  updateEBullets();
  updateEnemies();
  drawEnemies();
  drawEBullets();
  drawExplosions();
  drawHUD();
  deathTimer--;
  if (deathTimer == 0) gameState = STATE_GAMEOVER;
}

void doGameOver() {
  drawEnemies();

  pFillR(0, 30, P_W, 58, BLACK);
  pDrawR(0, 30, P_W, 58, WHITE);

  pText(5, 34, F("GAME OVER"));
  pHLine(5, 44, 54, WHITE);
  pText(5, 48, F("Score"));
  pNum(5, 58, score);
  pText(5, 70, F("Best"));
  pNum(5, 78, highScore);

  if ((arduboy.frameCount / 30) % 2 == 0)
    pText(5, 96, F("Press A"));
  pText(5, 110, F("B=title"));

  if (arduboy.justPressed(A_BUTTON)) { initGame(); gameState = STATE_PLAYING; }
  if (arduboy.justPressed(B_BUTTON)) gameState = STATE_TITLE;
}

// ============================================================
// HUD
// ============================================================

void drawHUD() {
  pNum(1, 1, score);

  for (uint8_t i = 0; i < lives && i < 5; i++) {
    uint8_t lx = P_W - 6 - i * 6;
    pFillR(lx + 1, 1, 3, 2, WHITE);
    pPixel(lx + 2, 0, WHITE);
    pFillR(lx, 3, 5, 1, WHITE);
  }

  for (uint8_t i = 0; i < loopsLeft && i < 6; i++) {
    pPixel(P_W - 2 - i * 3, 6, WHITE);
    pPixel(P_W - 1 - i * 3, 6, WHITE);
  }

  for (uint8_t i = 0; i <= power && i < 5; i++) {
    pPixel(1 + i * 3, 8, WHITE);
    pPixel(2 + i * 3, 8, WHITE);
  }

  pHLine(0, HUD_H, P_W, WHITE);
}

// ============================================================
// Ship drawing
// ============================================================

void drawShip(int16_t x, int16_t y, uint8_t c) {
  pPixel(x+4, y, c);
  pFillR(x+3, y+1, 3, 1, c);
  pFillR(x+2, y+2, 5, 1, c);
  pPixel(x+1, y+3, c); pPixel(x+2, y+3, c);
  pPixel(x+4, y+3, c); pPixel(x+6, y+3, c); pPixel(x+7, y+3, c);
  pFillR(x, y+4, 9, 1, c);
  pFillR(x, y+5, 3, 1, c); pFillR(x+3, y+5, 3, 1, c); pFillR(x+6, y+5, 3, 1, c);
  pPixel(x+1, y+6, c); pPixel(x+4, y+6, c); pPixel(x+7, y+6, c);
  if ((arduboy.frameCount / 3) % 2 == 0) {
    pPixel(x+2, y+7, c); pPixel(x+6, y+7, c);
  } else {
    pPixel(x+1, y+8, c); pPixel(x+4, y+7, c); pPixel(x+7, y+8, c);
  }
}

void drawSideFighter(int16_t x, int16_t y) {
  pPixel(x+1, y, WHITE);
  pFillR(x, y+1, 3, 1, WHITE);
  pFillR(x, y+2, 3, 1, WHITE);
  pPixel(x+1, y+3, WHITE);
  if ((arduboy.frameCount / 3) % 2 == 0) pPixel(x+1, y+4, WHITE);
}

// ============================================================
// Enemy drawing
// ============================================================

void drawFighter(int16_t x, int16_t y, uint8_t c) {
  pFillR(x+1, y, 3, 1, c);
  pFillR(x, y+1, 5, 1, c);
  pFillR(x+1, y+2, 3, 1, c);
  pPixel(x, y+3, c); pPixel(x+4, y+3, c);
  pPixel(x, y+4, c); pPixel(x+4, y+4, c);
}

void drawBomber(int16_t x, int16_t y, uint8_t c) {
  pFillR(x+2, y, 3, 1, c);
  pFillR(x+1, y+1, 5, 1, c);
  pFillR(x, y+2, 7, 1, c);
  pFillR(x, y+3, 7, 1, c);
  pFillR(x+1, y+4, 5, 1, c);
  pPixel(x+1, y+5, c); pPixel(x+3, y+5, c); pPixel(x+5, y+5, c);
  pPixel(x+2, y+6, c); pPixel(x+4, y+6, c);
}

void drawAce(int16_t x, int16_t y, uint8_t c) {
  pPixel(x+2, y, c);
  pFillR(x+1, y+1, 3, 1, c);
  pFillR(x, y+2, 5, 1, c);
  pFillR(x, y+3, 5, 1, c);
  pPixel(x, y+4, c); pPixel(x+2, y+4, c); pPixel(x+4, y+4, c);
  pPixel(x+1, y+5, c); pPixel(x+3, y+5, c);
}

void drawHeavy(int16_t x, int16_t y, uint8_t c) {
  pFillR(x+3, y, 3, 1, c);
  pFillR(x+2, y+1, 5, 1, c);
  pFillR(x+1, y+2, 7, 1, c);
  pFillR(x, y+3, 9, 1, c);
  pFillR(x, y+4, 9, 1, c);
  pFillR(x+1, y+5, 7, 1, c);
  pPixel(x+1, y+6, c); pPixel(x+4, y+6, c); pPixel(x+7, y+6, c);
  pPixel(x+2, y+7, c); pPixel(x+4, y+7, c); pPixel(x+6, y+7, c);
}
