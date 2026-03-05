// SpaceDodge - Portrait Mode
// Dodge falling asteroids and survive as long as you can!
// Hold Arduboy rotated CCW (D-pad at bottom, right-handed)
// Controls: D-pad LEFT/RIGHT to move ship, A to start/restart

#include <Arduboy2.h>

Arduboy2 arduboy;

// Portrait dimensions (device rotated 90 CCW)
// Physical 128x64 becomes logical 64 wide x 128 tall
const uint8_t P_WIDTH = 64;
const uint8_t P_HEIGHT = 128;

// Player settings
const uint8_t PLAYER_W = 9;
const uint8_t PLAYER_H = 9;
const uint8_t PLAYER_Y = P_HEIGHT - PLAYER_H - 4;
const uint8_t PLAYER_SPEED = 2;

// Asteroid settings
const uint8_t MAX_ASTEROIDS = 12;
const uint8_t ASTEROID_MIN_W = 5;
const uint8_t ASTEROID_MAX_W = 9;

// Star background
const uint8_t NUM_STARS = 25;

// Game states
enum GameState : uint8_t {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_GAMEOVER
};

// Asteroid structure
struct Asteroid {
  int16_t x;
  int16_t y;
  uint8_t size;
  uint8_t speed;
  bool active;
};

// Game variables
GameState gameState;
int16_t playerX;
Asteroid asteroids[MAX_ASTEROIDS];
uint16_t score;
uint16_t highScore;
uint8_t spawnTimer;
uint8_t spawnRate;
uint8_t difficulty;
uint8_t framesSurvived;

// Star positions
uint8_t starX[NUM_STARS];
uint8_t starY[NUM_STARS];

// ============================================================
// Portrait coordinate system
// ============================================================
// Device rotated 90 CCW: left edge is bottom, D-pad under right thumb
// Portrait (px, py) -> Screen (127 - py, px)
// Portrait rect (px, py, pw, ph) -> Screen rect (127 - py - ph + 1, px, ph, pw)

void pDrawPixel(int16_t px, int16_t py, uint8_t color) {
  arduboy.drawPixel(127 - py, px, color);
}

void pFillRect(int16_t px, int16_t py, uint8_t pw, uint8_t ph, uint8_t color) {
  arduboy.fillRect(127 - py - ph + 1, px, ph, pw, color);
}

void pDrawRect(int16_t px, int16_t py, uint8_t pw, uint8_t ph, uint8_t color) {
  arduboy.drawRect(127 - py - ph + 1, px, ph, pw, color);
}

void pDrawVLine(int16_t px, int16_t py, uint8_t len, uint8_t color) {
  arduboy.drawFastHLine(127 - py - len + 1, px, len, color);
}

void pDrawHLine(int16_t px, int16_t py, uint8_t len, uint8_t color) {
  arduboy.drawFastVLine(127 - py, px, len, color);
}

// ============================================================
// Portrait text rendering using Arduboy2::font5x7
// ============================================================
// Each char in font5x7 is 5 bytes (columns), each byte has 7 bits (rows)
// font5x7 is indexed by raw ASCII value: font5x7[c * 5 + col]

void pDrawChar(int16_t px, int16_t py, char c, uint8_t size = 1) {
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&Arduboy2::font5x7[(uint8_t)c * 5 + col]);
    for (uint8_t row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        if (size == 1) {
          pDrawPixel(px + col, py + row, WHITE);
        } else {
          pFillRect(px + col * size, py + row * size, size, size, WHITE);
        }
      }
    }
  }
}

void pPrint(int16_t px, int16_t py, const __FlashStringHelper *str, uint8_t size = 1) {
  const char *p = (const char *)str;
  char c;
  int16_t x = px;
  uint8_t charW = 6 * size;
  while ((c = pgm_read_byte(p++)) != 0) {
    pDrawChar(x, py, c, size);
    x += charW;
  }
}

// Print an integer at portrait position
void pPrintNum(int16_t px, int16_t py, uint16_t num, uint8_t size = 1) {
  char buf[6];
  uint8_t i = 0;
  if (num == 0) {
    buf[i++] = '0';
  } else {
    // Build digits in reverse
    char tmp[6];
    uint8_t t = 0;
    while (num > 0) {
      tmp[t++] = '0' + (num % 10);
      num /= 10;
    }
    while (t > 0) {
      buf[i++] = tmp[--t];
    }
  }
  for (uint8_t j = 0; j < i; j++) {
    pDrawChar(px + j * 6 * size, py, buf[j], size);
  }
}

// Get pixel width of a number for right-alignment
uint8_t numWidth(uint16_t num, uint8_t size = 1) {
  if (num == 0) return 6 * size;
  uint8_t digits = 0;
  uint16_t n = num;
  while (n > 0) { digits++; n /= 10; }
  return digits * 6 * size;
}

// ============================================================
// Game setup
// ============================================================

void setup() {
  arduboy.begin();
  arduboy.setFrameRate(60);
  arduboy.initRandomSeed();

  highScore = 0;
  initStars();
  gameState = STATE_TITLE;
}

void loop() {
  if (!arduboy.nextFrame()) return;

  arduboy.pollButtons();
  arduboy.clear();

  drawStars();

  switch (gameState) {
    case STATE_TITLE:   updateTitle();    break;
    case STATE_PLAYING: updateGame();     break;
    case STATE_GAMEOVER: updateGameOver(); break;
  }

  arduboy.display();
}

// ============================================================
// Star background
// ============================================================

void initStars() {
  for (uint8_t i = 0; i < NUM_STARS; i++) {
    starX[i] = random(0, P_WIDTH);
    starY[i] = random(0, P_HEIGHT);
  }
}

void drawStars() {
  for (uint8_t i = 0; i < NUM_STARS; i++) {
    pDrawPixel(starX[i], starY[i], WHITE);
    if (gameState == STATE_PLAYING && arduboy.everyXFrames(3)) {
      starY[i]++;
      if (starY[i] >= P_HEIGHT) {
        starY[i] = 0;
        starX[i] = random(0, P_WIDTH);
      }
    }
  }
}

// ============================================================
// Title screen
// ============================================================

void updateTitle() {
  // "SPACE" centered at size 2 (5 chars * 12px = 60px, offset 2)
  pPrint(2, 15, F("SPACE"), 2);
  // "DODGE" centered
  pPrint(2, 33, F("DODGE"), 2);

  // Divider line
  pDrawHLine(8, 52, 48, WHITE);

  // Ship preview
  drawShipAt(28, 62);

  // Blinking prompt
  if ((arduboy.frameCount / 30) % 2 == 0) {
    pPrint(4, 85, F("Press A"));
  }

  if (highScore > 0) {
    pPrint(4, 100, F("Best:"));
    pPrintNum(34, 100, highScore);
  }

  // Controls hint
  pPrint(3, 115, F("<D-pad>"));

  if (arduboy.justPressed(A_BUTTON)) {
    startGame();
  }
}

// ============================================================
// Game logic
// ============================================================

void startGame() {
  playerX = (P_WIDTH - PLAYER_W) / 2;
  score = 0;
  spawnTimer = 0;
  spawnRate = 25;
  difficulty = 0;
  framesSurvived = 0;

  for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
    asteroids[i].active = false;
  }

  gameState = STATE_PLAYING;
}

void updateGame() {
  // Button mapping for CCW rotation (right-handed):
  // Physical UP = portrait LEFT, Physical DOWN = portrait RIGHT
  if (arduboy.pressed(UP_BUTTON)) {
    playerX -= PLAYER_SPEED;
  }
  if (arduboy.pressed(DOWN_BUTTON)) {
    playerX += PLAYER_SPEED;
  }

  // Clamp player
  if (playerX < 0) playerX = 0;
  if (playerX > P_WIDTH - PLAYER_W) playerX = P_WIDTH - PLAYER_W;

  // Spawn asteroids
  spawnTimer++;
  if (spawnTimer >= spawnRate) {
    spawnTimer = 0;
    spawnAsteroid();
  }

  // Update asteroids
  for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
    if (!asteroids[i].active) continue;

    asteroids[i].y += asteroids[i].speed;

    // Off screen - score!
    if (asteroids[i].y > P_HEIGHT) {
      asteroids[i].active = false;
      score++;
    }

    // Collision (AABB with 1px forgiveness)
    if (asteroids[i].active &&
        collides(playerX, PLAYER_Y, PLAYER_W, PLAYER_H,
                 asteroids[i].x, asteroids[i].y,
                 asteroids[i].size, asteroids[i].size)) {
      gameOver();
      return;
    }
  }

  // Score from survival time (1 point per second)
  framesSurvived++;
  if (framesSurvived >= 60) {
    framesSurvived = 0;
    score++;
  }

  // Increase difficulty every 3 seconds
  if (arduboy.everyXFrames(180)) {
    if (spawnRate > 8) spawnRate -= 2;
    difficulty++;
  }

  // Draw
  drawShipAt(playerX, PLAYER_Y);
  drawAsteroids();
  drawHUD();
}

void spawnAsteroid() {
  for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
    if (!asteroids[i].active) {
      uint8_t sz = ASTEROID_MIN_W + random(0, ASTEROID_MAX_W - ASTEROID_MIN_W + 1);
      asteroids[i].x = random(0, P_WIDTH - sz);
      asteroids[i].y = -(int16_t)sz;
      asteroids[i].size = sz;
      asteroids[i].speed = 1 + random(0, 2) + (difficulty / 4);
      if (asteroids[i].speed > 4) asteroids[i].speed = 4;
      asteroids[i].active = true;
      return;
    }
  }
}

bool collides(int16_t ax, int16_t ay, uint8_t aw, uint8_t ah,
              int16_t bx, int16_t by, uint8_t bw, uint8_t bh) {
  return (ax + 1 < bx + bw - 1) &&
         (ax + aw - 1 > bx + 1) &&
         (ay + 1 < by + bh - 1) &&
         (ay + ah - 1 > by + 1);
}

// ============================================================
// Drawing
// ============================================================

void drawShipAt(int16_t x, int16_t y) {
  // Ship body - pointed triangle nose + rectangular body
  // Nose (top center)
  pDrawPixel(x + 4, y, WHITE);
  pDrawHLine(x + 3, y + 1, 3, WHITE);
  pDrawHLine(x + 2, y + 2, 5, WHITE);
  // Body
  pFillRect(x + 1, y + 3, 7, 3, WHITE);
  // Wings
  pFillRect(x, y + 5, 9, 2, WHITE);
  // Exhaust
  pDrawPixel(x + 2, y + 7, WHITE);
  pDrawPixel(x + 4, y + 7, WHITE);
  pDrawPixel(x + 6, y + 7, WHITE);
  // Thruster glow (animated)
  if ((arduboy.frameCount / 4) % 2 == 0) {
    pDrawPixel(x + 3, y + 8, WHITE);
    pDrawPixel(x + 5, y + 8, WHITE);
  } else {
    pDrawPixel(x + 2, y + 8, WHITE);
    pDrawPixel(x + 4, y + 8, WHITE);
    pDrawPixel(x + 6, y + 8, WHITE);
  }
}

void drawAsteroids() {
  for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
    if (!asteroids[i].active) continue;
    int16_t x = asteroids[i].x;
    int16_t y = asteroids[i].y;
    uint8_t s = asteroids[i].size;

    // Draw asteroid as a rough filled shape with jagged edges
    if (s <= 6) {
      // Small asteroid
      pFillRect(x + 1, y, s - 2, s, WHITE);
      pFillRect(x, y + 1, s, s - 2, WHITE);
    } else {
      // Larger asteroid - more detailed
      pFillRect(x + 2, y, s - 4, s, WHITE);
      pFillRect(x + 1, y + 1, s - 2, s - 2, WHITE);
      pFillRect(x, y + 2, s, s - 4, WHITE);
      // Inner detail (crater)
      pDrawPixel(x + s/2, y + s/2, BLACK);
    }
  }
}

void drawHUD() {
  // Score in top-left of portrait view
  pPrint(1, 1, F("S:"));
  pPrintNum(13, 1, score);

  // Thin line under HUD
  pDrawHLine(0, 9, P_WIDTH, WHITE);
}

// ============================================================
// Game over
// ============================================================

void gameOver() {
  if (score > highScore) {
    highScore = score;
  }
  gameState = STATE_GAMEOVER;
}

void updateGameOver() {
  // Draw frozen game state
  drawShipAt(playerX, PLAYER_Y);
  drawAsteroids();

  // Overlay box (centered in portrait)
  uint8_t boxW = 56;
  uint8_t boxH = 52;
  uint8_t boxX = (P_WIDTH - boxW) / 2;
  uint8_t boxY = 30;
  pFillRect(boxX, boxY, boxW, boxH, BLACK);
  pDrawRect(boxX, boxY, boxW, boxH, WHITE);

  // "GAME OVER" text
  pPrint(boxX + 4, boxY + 4, F("GAME"));
  pPrint(boxX + 4, boxY + 14, F("OVER!"));

  // Score
  pPrint(boxX + 4, boxY + 26, F("Sc:"));
  pPrintNum(boxX + 22, boxY + 26, score);

  // High score
  pPrint(boxX + 4, boxY + 36, F("Hi:"));
  pPrintNum(boxX + 22, boxY + 36, highScore);

  // Blinking restart prompt
  if ((arduboy.frameCount / 30) % 2 == 0) {
    pPrint(6, 90, F("Press A"));
  }

  // B for title
  pPrint(6, 105, F("B=title"));

  if (arduboy.justPressed(A_BUTTON)) {
    startGame();
  }
  if (arduboy.justPressed(B_BUTTON)) {
    gameState = STATE_TITLE;
  }
}
