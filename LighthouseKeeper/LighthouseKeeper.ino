// ============================================================
// Lighthouse Keeper - Arduboy
// Guide ships safely past rocky shores with your lighthouse beam.
// The Arduboy's LED IS your lighthouse light.
// A: toggle light, B: foghorn, D-pad: aim beam
// ============================================================

#include <Arduboy2.h>

Arduboy2 arduboy;
BeepPin1 beep;

// --- Screen ---
const uint8_t W = 128;
const uint8_t H = 64;

// --- Game states ---
enum State : uint8_t {
  ST_TITLE,
  ST_PLAYING,
  ST_GAMEOVER
};

// --- Lighthouse ---
const int16_t LH_X = 18;   // lighthouse tip x
const int16_t LH_Y = 12;   // lighthouse tip y
const uint8_t BEAM_LEN = 60;
const uint8_t OIL_MAX = 200;

// --- Rocks ---
const uint8_t MAX_ROCKS = 6;
struct Rock {
  int16_t x, y;
  uint8_t w, h;
};

// --- Ships ---
const uint8_t MAX_SHIPS = 4;
struct Ship {
  int16_t x, y;
  int8_t speed;      // negative = moving left
  int8_t targetY;    // y they're trying to reach
  bool active;
  bool lit;          // currently illuminated by beam
  bool warned;       // foghorn warned
  uint8_t type;      // 0=small, 1=medium, 2=supply
  uint8_t timer;
};

// --- Waves (ocean) ---
const uint8_t NUM_WAVE_PTS = 16;

// --- Game vars ---
State state;
uint16_t score;
uint16_t highScore;
uint8_t lives;        // ships lost
uint8_t maxLives;

// Lighthouse
uint8_t oil;
bool lightOn;
int8_t beamAngle;     // 0-7, representing direction (right-ish quadrant)
uint8_t beamPulse;    // visual pulse timer

// Weather
uint8_t weather;      // 0=clear, 1=fog, 2=storm
uint16_t weatherTimer;
uint8_t fogDensity;

// Spawning
uint16_t spawnTimer;
uint8_t spawnRate;
uint8_t difficulty;
uint16_t frameCount_;

// Objects
Rock rocks[MAX_ROCKS];
Ship ships[MAX_SHIPS];

// Beam angle lookup: dx*16, dy*16 (fixed point) for 8 directions
// Angles fan from upper-right to lower-right
const int8_t PROGMEM beamDX[] = { 16,  15,  14,  11,   8,   5,   2,  -2};
const int8_t PROGMEM beamDY[] = {  0,   4,   8,  11,  14,  15,  16,  16};

// ============================================================
// Setup
// ============================================================

void setup() {
  arduboy.begin();
  arduboy.setFrameRate(30);
  arduboy.initRandomSeed();
  beep.begin();
  highScore = 0;
  state = ST_TITLE;
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();
  beep.timer();
  arduboy.clear();

  switch (state) {
    case ST_TITLE:   doTitle();   break;
    case ST_PLAYING: doPlaying(); break;
    case ST_GAMEOVER: doGameOver(); break;
  }

  arduboy.display();
}

// ============================================================
// Title
// ============================================================

void doTitle() {
  // Turn off LED on title
  arduboy.setRGBled(0, 0, 0);

  arduboy.setCursor(16, 4);
  arduboy.setTextSize(2);
  arduboy.print(F("LIGHT"));
  arduboy.setCursor(16, 22);
  arduboy.print(F("HOUSE"));
  arduboy.setTextSize(1);

  arduboy.setCursor(22, 42);
  arduboy.print(F("A:aim  B:horn"));

  if ((arduboy.frameCount / 15) % 2 == 0) {
    arduboy.setCursor(34, 54);
    arduboy.print(F("Press A"));
  }

  if (highScore > 0) {
    arduboy.setCursor(80, 4);
    arduboy.setTextSize(1);
    arduboy.print(F("Hi:"));
    arduboy.print(highScore);
  }

  if (arduboy.justPressed(A_BUTTON)) {
    initGame();
  }
}

// ============================================================
// Init
// ============================================================

void initGame() {
  score = 0;
  lives = 0;
  maxLives = 3;
  oil = OIL_MAX;
  lightOn = true;
  beamAngle = 4;  // aim middle-right
  beamPulse = 0;
  weather = 0;
  weatherTimer = 300;  // ~10 seconds at 30fps
  fogDensity = 0;
  spawnTimer = 0;
  spawnRate = 90;  // 3 seconds
  difficulty = 0;
  frameCount_ = 0;

  for (uint8_t i = 0; i < MAX_SHIPS; i++) ships[i].active = false;

  // Place rocks in the ocean area
  initRocks();

  state = ST_PLAYING;
}

void initRocks() {
  // Scatter rocks in the ocean (right portion of screen)
  rocks[0] = {45, 42, 6, 5};
  rocks[1] = {70, 30, 5, 4};
  rocks[2] = {55, 52, 7, 5};
  rocks[3] = {90, 45, 5, 6};
  rocks[4] = {80, 55, 6, 4};
  rocks[5] = {105, 35, 4, 5};
}

// ============================================================
// Gameplay
// ============================================================

void doPlaying() {
  frameCount_++;
  updateLighthouse();
  updateShips();
  updateWeather();
  updateSpawning();
  checkCollisions();

  // Draw everything
  drawOcean();
  drawRocks();
  drawShips();
  drawLighthouse();
  if (lightOn && oil > 0) drawBeam();
  drawWeather();
  drawHUD();

  // Update LED
  updateLED();

  // Difficulty ramp
  if (frameCount_ % 300 == 0) {
    difficulty++;
    if (spawnRate > 40) spawnRate -= 5;
  }
}

// ============================================================
// Lighthouse controls
// ============================================================

void updateLighthouse() {
  // Toggle light
  if (arduboy.justPressed(A_BUTTON)) {
    lightOn = !lightOn;
    if (lightOn) beep.tone(600, 3);
    else beep.tone(200, 3);
  }

  // Aim beam
  if (arduboy.justPressed(UP_BUTTON)) {
    if (beamAngle > 0) beamAngle--;
    beep.tone(500, 1);
  }
  if (arduboy.justPressed(DOWN_BUTTON)) {
    if (beamAngle < 7) beamAngle++;
    beep.tone(500, 1);
  }

  // Foghorn
  if (arduboy.justPressed(B_BUTTON)) {
    beep.tone(80, 20);
    // Warn nearby ships
    for (uint8_t i = 0; i < MAX_SHIPS; i++) {
      if (ships[i].active && ships[i].x < 100) {
        ships[i].warned = true;
      }
    }
  }

  // Oil consumption
  if (lightOn && oil > 0) {
    beamPulse++;
    if (beamPulse % 3 == 0) oil--;
    if (oil == 0) {
      lightOn = false;
      beep.tone(100, 10);
    }
  }
}

// ============================================================
// Ships
// ============================================================

void updateShips() {
  for (uint8_t i = 0; i < MAX_SHIPS; i++) {
    if (!ships[i].active) continue;
    Ship &s = ships[i];

    s.timer++;

    // Check if illuminated by beam
    s.lit = false;
    if (lightOn && oil > 0) {
      s.lit = isInBeam(s.x, s.y);
    }

    // Ship AI: if lit or warned, try to avoid rocks
    if (s.lit || s.warned) {
      // Find nearest rock ahead and steer away
      int8_t steer = 0;
      for (uint8_t r = 0; r < MAX_ROCKS; r++) {
        int16_t rdx = rocks[r].x - s.x;
        int16_t rdy = rocks[r].y - s.y;
        if (rdx > -10 && rdx < 20 && abs(rdy) < 12) {
          // Rock is nearby ahead
          steer = (rdy > 0) ? -1 : 1;
          break;
        }
      }
      // Move vertically to avoid
      if (s.timer % 3 == 0) {
        s.y += steer;
        // Also gently move toward target lane
        if (steer == 0 && s.y != s.targetY) {
          s.y += (s.targetY > s.y) ? 1 : -1;
        }
      }
    }

    // Move horizontally
    if (s.timer % 2 == 0) {
      s.x += s.speed;
    }

    // Ship passed safely (made it to the left past lighthouse)
    if (s.x < -8) {
      s.active = false;
      score += (s.type == 2) ? 20 : 10;
      beep.tone(800, 3);
      // Supply ships refuel
      if (s.type == 2) {
        oil += 50;
        if (oil > OIL_MAX) oil = OIL_MAX;
        beep.tone(1000, 6);
      }
    }

    // Ship went off right (shouldn't happen, but cleanup)
    if (s.x > W + 10) s.active = false;
  }
}

bool isInBeam(int16_t sx, int16_t sy) {
  // Check if point is within the beam cone
  int8_t dx = pgm_read_byte(&beamDX[beamAngle]);
  int8_t dy = pgm_read_byte(&beamDY[beamAngle]);

  // Vector from lighthouse to ship
  int16_t vx = sx - LH_X;
  int16_t vy = sy - LH_Y;

  // Distance check
  int16_t dist = vx * vx + vy * vy;
  if (dist > (int16_t)BEAM_LEN * BEAM_LEN) return false;
  if (dist < 16) return false;

  // Dot product for angle check (beam has a cone width)
  int32_t dot = (int32_t)vx * dx + (int32_t)vy * dy;
  int32_t vmag = vx * vx + vy * vy;
  int32_t dmag = dx * dx + dy * dy;

  // Check if within ~25 degree cone: dot^2 > 0.8 * vmag * dmag
  // Using integer math: dot^2 * 10 > 8 * vmag * dmag
  return (dot > 0) && (dot * dot * 10 > 8 * vmag * dmag);
}

void spawnShip() {
  for (uint8_t i = 0; i < MAX_SHIPS; i++) {
    if (!ships[i].active) {
      Ship &s = ships[i];
      s.x = W + 4;
      s.y = 20 + random(0, 35);
      s.targetY = s.y;
      s.speed = -1;
      s.active = true;
      s.lit = false;
      s.warned = false;
      s.timer = 0;

      // Ship type
      uint8_t r = random(0, 10);
      if (r < 2) {
        s.type = 2;  // supply ship (20%)
      } else if (r < 5) {
        s.type = 1;  // medium
      } else {
        s.type = 0;  // small
      }

      return;
    }
  }
}

// ============================================================
// Weather
// ============================================================

void updateWeather() {
  if (weatherTimer > 0) {
    weatherTimer--;
    return;
  }

  // Change weather
  uint8_t r = random(0, 10);
  if (weather == 0) {
    // From clear
    weather = (r < 4 + difficulty/2) ? 1 : 0;
    if (r == 9) weather = 2;
    fogDensity = (weather == 1) ? 3 : (weather == 2) ? 5 : 0;
  } else {
    // Back to clear (usually)
    weather = (r < 3) ? weather : 0;
    fogDensity = (weather == 0) ? 0 : fogDensity;
  }

  weatherTimer = 200 + random(0, 200);
  if (weather > 0) beep.tone(150, 8);
}

// ============================================================
// Spawning
// ============================================================

void updateSpawning() {
  spawnTimer++;
  if (spawnTimer >= spawnRate) {
    spawnTimer = 0;
    spawnShip();
  }
}

// ============================================================
// Collisions
// ============================================================

void checkCollisions() {
  for (uint8_t i = 0; i < MAX_SHIPS; i++) {
    if (!ships[i].active) continue;
    Ship &s = ships[i];

    for (uint8_t r = 0; r < MAX_ROCKS; r++) {
      // Ship hitbox: roughly 8x4 for small, 10x5 for medium, 12x5 for supply
      uint8_t sw = 6 + s.type * 2;
      uint8_t sh = 3 + s.type;
      if (s.x < rocks[r].x + rocks[r].w &&
          s.x + sw > rocks[r].x &&
          s.y < rocks[r].y + rocks[r].h &&
          s.y + sh > rocks[r].y) {
        // Ship hit a rock!
        s.active = false;
        lives++;
        beep.tone(60, 25);
        // Flash LED red
        arduboy.setRGBled(255, 0, 0);

        if (lives >= maxLives) {
          if (score > highScore) highScore = score;
          state = ST_GAMEOVER;
          return;
        }
      }
    }
  }
}

// ============================================================
// Drawing
// ============================================================

void drawOcean() {
  // Wavy water line at y=18 (shore)
  for (uint8_t x = 30; x < W; x += 2) {
    uint8_t wy = 18 + ((x + frameCount_/2) % 8 < 4 ? 0 : 1);
    arduboy.drawPixel(x, wy, WHITE);
  }

  // Subtle wave texture in ocean
  for (uint8_t x = 32; x < W; x += 8) {
    for (uint8_t y = 22; y < H; y += 10) {
      uint8_t wx = x + ((frameCount_ / 4) % 4);
      if (wx < W) arduboy.drawPixel(wx, y, WHITE);
    }
  }
}

void drawRocks() {
  for (uint8_t i = 0; i < MAX_ROCKS; i++) {
    Rock &r = rocks[i];
    // Jagged rock shape
    arduboy.fillRect(r.x + 1, r.y, r.w - 2, r.h, WHITE);
    arduboy.fillRect(r.x, r.y + 1, r.w, r.h - 2, WHITE);
  }
}

void drawShips() {
  for (uint8_t i = 0; i < MAX_SHIPS; i++) {
    if (!ships[i].active) continue;
    Ship &s = ships[i];

    // Ship blinks if lit
    if (s.lit && (frameCount_ / 2) % 3 == 0) continue;

    switch (s.type) {
      case 0: // small boat
        arduboy.drawFastHLine(s.x, s.y + 1, 6, WHITE);
        arduboy.drawFastHLine(s.x + 1, s.y, 4, WHITE);
        arduboy.drawPixel(s.x + 2, s.y - 1, WHITE); // mast
        break;
      case 1: // medium ship
        arduboy.fillRect(s.x, s.y + 1, 8, 3, WHITE);
        arduboy.drawFastHLine(s.x + 1, s.y, 6, WHITE);
        arduboy.drawPixel(s.x + 3, s.y - 1, WHITE);
        arduboy.drawPixel(s.x + 3, s.y - 2, WHITE);
        break;
      case 2: // supply ship (distinctive)
        arduboy.fillRect(s.x, s.y + 1, 10, 3, WHITE);
        arduboy.drawFastHLine(s.x + 1, s.y, 8, WHITE);
        arduboy.drawPixel(s.x + 3, s.y - 1, WHITE);
        arduboy.drawPixel(s.x + 6, s.y - 1, WHITE);
        // Flag/cross marker
        arduboy.drawPixel(s.x + 3, s.y - 2, WHITE);
        arduboy.drawFastHLine(s.x + 2, s.y - 2, 3, WHITE);
        break;
    }
  }
}

void drawLighthouse() {
  // Rocky cliff / ground (left side)
  arduboy.fillRect(0, 20, 30, 44, WHITE);
  // Cliff edge (jagged)
  arduboy.drawPixel(30, 22, WHITE);
  arduboy.drawPixel(31, 24, WHITE);
  arduboy.drawPixel(30, 26, WHITE);
  arduboy.drawPixel(32, 28, WHITE);
  arduboy.drawFastVLine(30, 30, 34, WHITE);

  // Lighthouse tower
  arduboy.fillRect(14, 5, 9, 16, WHITE);
  // Tower stripes (black bands)
  arduboy.drawFastHLine(14, 8, 9, BLACK);
  arduboy.drawFastHLine(14, 12, 9, BLACK);
  arduboy.drawFastHLine(14, 16, 9, BLACK);

  // Lantern room
  arduboy.fillRect(13, 2, 11, 4, WHITE);
  arduboy.drawRect(13, 2, 11, 4, WHITE);

  // Dome
  arduboy.drawFastHLine(15, 1, 7, WHITE);
  arduboy.drawFastHLine(16, 0, 5, WHITE);

  // Light glow when on
  if (lightOn && oil > 0) {
    // Bright center
    arduboy.fillRect(14, 3, 9, 2, BLACK);
    // Pulsing light dots
    if ((frameCount_ / 2) % 2 == 0) {
      arduboy.drawPixel(16, 3, WHITE);
      arduboy.drawPixel(18, 3, WHITE);
      arduboy.drawPixel(20, 3, WHITE);
    } else {
      arduboy.drawPixel(15, 3, WHITE);
      arduboy.drawPixel(17, 3, WHITE);
      arduboy.drawPixel(19, 3, WHITE);
      arduboy.drawPixel(21, 3, WHITE);
    }
  }

  // Door
  arduboy.fillRect(16, 17, 5, 3, BLACK);

  // Beam direction indicator (small arrow on tower)
  uint8_t indY = 6 + beamAngle;
  arduboy.drawPixel(24, indY, BLACK);
  arduboy.drawPixel(25, indY, BLACK);
}

void drawBeam() {
  int8_t dx = pgm_read_byte(&beamDX[beamAngle]);
  int8_t dy = pgm_read_byte(&beamDY[beamAngle]);

  // Draw beam as dotted/dashed line from lighthouse
  // Two edge lines for cone effect
  for (uint8_t t = 4; t < BEAM_LEN; t += 2) {
    int16_t bx = LH_X + (int16_t)dx * t / 16;
    int16_t by = LH_Y + (int16_t)dy * t / 16;

    if (bx < 0 || bx >= W || by < 0 || by >= H) break;

    // Center line
    arduboy.drawPixel(bx, by, WHITE);

    // Cone edges (spread increases with distance)
    int8_t spread = t / 12;
    if (spread > 0) {
      // Perpendicular offset
      int16_t px = -(int16_t)dy * spread / 16;
      int16_t py = (int16_t)dx * spread / 16;
      int16_t ex1 = bx + px, ey1 = by + py;
      int16_t ex2 = bx - px, ey2 = by - py;
      if (ex1 >= 0 && ex1 < W && ey1 >= 0 && ey1 < H)
        arduboy.drawPixel(ex1, ey1, WHITE);
      if (ex2 >= 0 && ex2 < W && ey2 >= 0 && ey2 < H)
        arduboy.drawPixel(ex2, ey2, WHITE);
    }

    // Dashed effect - skip some
    if (t % 4 < 2) t++;
  }
}

void drawWeather() {
  if (weather == 0) return;

  // Fog: random dots obscuring the view
  if (weather >= 1) {
    for (uint8_t i = 0; i < fogDensity * 8; i++) {
      uint8_t fx = 30 + random(0, W - 30);
      uint8_t fy = random(0, H);
      arduboy.drawPixel(fx, fy, BLACK);
    }
  }

  // Storm: rain lines
  if (weather == 2) {
    for (uint8_t i = 0; i < 12; i++) {
      uint8_t rx = random(0, W);
      uint8_t ry = random(0, H);
      arduboy.drawLine(rx, ry, rx - 1, ry + 3, WHITE);
    }
  }
}

void drawHUD() {
  // Score top right
  arduboy.setCursor(90, 0);
  arduboy.print(F("S:"));
  arduboy.print(score);

  // Oil bar bottom left
  uint8_t barX = 1;
  uint8_t barY = H - 5;
  uint8_t barW = 26;
  // Black background for bar area
  arduboy.fillRect(barX - 1, barY - 1, barW + 2, 6, BLACK);
  arduboy.drawRect(barX - 1, barY - 1, barW + 2, 6, WHITE);
  uint8_t fillW = (uint16_t)barW * oil / OIL_MAX;
  if (fillW > 0) arduboy.fillRect(barX, barY, fillW, 4, WHITE);

  // Oil icon
  // (small flame/drop near bar)

  // Lives lost (X marks)
  for (uint8_t i = 0; i < lives; i++) {
    uint8_t lx = 34 + i * 8;
    arduboy.setCursor(lx, H - 6);
    arduboy.print(F("X"));
  }

  // Weather indicator
  if (weather == 1) {
    arduboy.setCursor(64, 0);
    arduboy.print(F("FOG"));
  } else if (weather == 2) {
    arduboy.setCursor(56, 0);
    arduboy.print(F("STORM"));
  }

  // Light status
  if (!lightOn || oil == 0) {
    if ((frameCount_ / 4) % 2 == 0) {
      arduboy.setCursor(0, 0);
      arduboy.print(F("DARK"));
    }
  }
}

// ============================================================
// LED control
// ============================================================

void updateLED() {
  if (!lightOn || oil == 0) {
    arduboy.setRGBled(0, 0, 0);
    return;
  }

  // Warm yellow glow, intensity based on oil level
  uint8_t brightness = 40 + (uint16_t)80 * oil / OIL_MAX;

  // Pulse effect
  uint8_t pulse = (frameCount_ % 8 < 4) ? 0 : 15;
  brightness += pulse;

  // Dim in bad weather
  if (weather == 1) brightness = brightness * 2 / 3;
  if (weather == 2) brightness = brightness / 2;

  // Check if any ship is near rocks and unlit — flash red warning
  bool danger = false;
  for (uint8_t i = 0; i < MAX_SHIPS; i++) {
    if (!ships[i].active || ships[i].lit || ships[i].warned) continue;
    for (uint8_t r = 0; r < MAX_ROCKS; r++) {
      int16_t rdx = rocks[r].x - ships[i].x;
      int16_t rdy = rocks[r].y - ships[i].y;
      if (rdx > -5 && rdx < 15 && abs(rdy) < 10) {
        danger = true;
        break;
      }
    }
    if (danger) break;
  }

  if (danger && (frameCount_ / 3) % 2 == 0) {
    arduboy.setRGBled(brightness, 0, 0);  // red flash
  } else {
    arduboy.setRGBled(brightness, brightness * 3 / 4, 0);  // warm yellow
  }
}

// ============================================================
// Game over
// ============================================================

void doGameOver() {
  arduboy.setRGBled(0, 0, 0);

  arduboy.setCursor(22, 8);
  arduboy.setTextSize(2);
  arduboy.print(F("GAME"));
  arduboy.setCursor(22, 26);
  arduboy.print(F("OVER"));
  arduboy.setTextSize(1);

  arduboy.setCursor(20, 46);
  arduboy.print(F("Score: "));
  arduboy.print(score);

  if (score >= highScore && score > 0) {
    arduboy.setCursor(20, 54);
    arduboy.print(F("NEW BEST!"));
  }

  if ((arduboy.frameCount / 15) % 2 == 0) {
    arduboy.setCursor(34, 56);
    arduboy.print(F("Press A"));
  }

  if (arduboy.justPressed(A_BUTTON)) {
    initGame();
  }
  if (arduboy.justPressed(B_BUTTON)) {
    state = ST_TITLE;
  }
}
