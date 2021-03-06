/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <platform.h>

#include <build_config.h>

#ifdef LED_STRIP

#include <common/color.h>

#include "drivers/light_ws2811strip.h"
#include "drivers/system.h"
#include "drivers/serial.h"

#include <common/maths.h>
#include <common/printf.h>
#include <common/typeconversion.h>

#include "sensors/battery.h"

#include "config/runtime_config.h"
#include "config/config.h"
#include "rx/rx.h"
#include "io/rc_controls.h"
#include "flight/failsafe.h"

#include "io/ledstrip.h"

static bool ledStripInitialised = false;
static failsafe_t* failsafe;

#if MAX_LED_STRIP_LENGTH > WS2811_LED_STRIP_LENGTH
#error "Led strip length must match driver"
#endif

hsvColor_t *colors;

//#define USE_LED_ANIMATION

//                          H    S    V
#define LED_BLACK        {  0,   0,   0}
#define LED_WHITE        {  0, 255, 255}
#define LED_RED          {  0,   0, 255}
#define LED_ORANGE       { 30,   0, 255}
#define LED_YELLOW       { 60,   0, 255}
#define LED_LIME_GREEN   { 90,   0, 255}
#define LED_GREEN        {120,   0, 255}
#define LED_MINT_GREEN   {150,   0, 255}
#define LED_CYAN         {180,   0, 255}
#define LED_LIGHT_BLUE   {210,   0, 255}
#define LED_BLUE         {240,   0, 255}
#define LED_DARK_VIOLET  {270,   0, 255}
#define LED_MAGENTA      {300,   0, 255}
#define LED_DEEP_PINK    {330,   0, 255}

const hsvColor_t hsv_black       = LED_BLACK;
const hsvColor_t hsv_white       = LED_WHITE;
const hsvColor_t hsv_red         = LED_RED;
const hsvColor_t hsv_orange      = LED_ORANGE;
const hsvColor_t hsv_yellow      = LED_YELLOW;
const hsvColor_t hsv_limeGreen   = LED_LIME_GREEN;
const hsvColor_t hsv_green       = LED_GREEN;
const hsvColor_t hsv_mintGreen   = LED_MINT_GREEN;
const hsvColor_t hsv_cyan        = LED_CYAN;
const hsvColor_t hsv_lightBlue   = LED_LIGHT_BLUE;
const hsvColor_t hsv_blue        = LED_BLUE;
const hsvColor_t hsv_darkViolet  = LED_DARK_VIOLET;
const hsvColor_t hsv_magenta     = LED_MAGENTA;
const hsvColor_t hsv_deepPink    = LED_DEEP_PINK;

#define LED_DIRECTION_COUNT 6

const hsvColor_t * const defaultColors[] = {
        &hsv_black,
        &hsv_white,
        &hsv_red,
        &hsv_orange,
        &hsv_yellow,
        &hsv_limeGreen,
        &hsv_green,
        &hsv_mintGreen,
        &hsv_cyan,
        &hsv_lightBlue,
        &hsv_blue,
        &hsv_darkViolet,
        &hsv_magenta,
        &hsv_deepPink
};

typedef enum {
    COLOR_BLACK = 0,
    COLOR_WHITE,
    COLOR_RED,
    COLOR_ORANGE,
    COLOR_YELLOW,
    COLOR_LIME_GREEN,
    COLOR_GREEN,
    COLOR_MINT_GREEN,
    COLOR_CYAN,
    COLOR_LIGHT_BLUE,
    COLOR_BLUE,
    COLOR_DARK_VIOLET,
    COLOR_MAGENTA,
    COLOR_DEEP_PINK,
} colorIds;

typedef enum {
    DIRECTION_NORTH = 0,
    DIRECTION_EAST,
    DIRECTION_SOUTH,
    DIRECTION_WEST,
    DIRECTION_UP,
    DIRECTION_DOWN
} directionId_e;

typedef struct modeColorIndexes_s {
    uint8_t north;
    uint8_t east;
    uint8_t south;
    uint8_t west;
    uint8_t up;
    uint8_t down;
} modeColorIndexes_t;


// Note, the color index used for the mode colors below refer to the default colors.
// if the colors are reconfigured the index is still valid but the displayed color might
// be different.
// See colors[] and defaultColors[] and applyDefaultColors[]

static const modeColorIndexes_t orientationModeColors = {
        COLOR_WHITE,
        COLOR_DARK_VIOLET,
        COLOR_RED,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};

static const modeColorIndexes_t headfreeModeColors = {
        COLOR_LIME_GREEN,
        COLOR_DARK_VIOLET,
        COLOR_ORANGE,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};

static const modeColorIndexes_t horizonModeColors = {
        COLOR_BLUE,
        COLOR_DARK_VIOLET,
        COLOR_YELLOW,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};

static const modeColorIndexes_t angleModeColors = {
        COLOR_CYAN,
        COLOR_DARK_VIOLET,
        COLOR_YELLOW,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};

static const modeColorIndexes_t magModeColors = {
        COLOR_MINT_GREEN,
        COLOR_DARK_VIOLET,
        COLOR_ORANGE,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};

static const modeColorIndexes_t baroModeColors = {
        COLOR_LIGHT_BLUE,
        COLOR_DARK_VIOLET,
        COLOR_RED,
        COLOR_DEEP_PINK,
        COLOR_BLUE,
        COLOR_ORANGE
};


uint8_t ledGridWidth;
uint8_t ledGridHeight;
uint8_t ledCount;

ledConfig_t *ledConfigs;

const ledConfig_t defaultLedStripConfig[] = {
    { CALCULATE_LED_XY( 2,  2), LED_DIRECTION_SOUTH | LED_DIRECTION_EAST | LED_FUNCTION_INDICATOR | LED_FUNCTION_ARM_STATE },
    { CALCULATE_LED_XY( 2,  1), LED_DIRECTION_EAST | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 2,  0), LED_DIRECTION_NORTH | LED_DIRECTION_EAST | LED_FUNCTION_INDICATOR | LED_FUNCTION_ARM_STATE },
    { CALCULATE_LED_XY( 1,  0), LED_DIRECTION_NORTH | LED_FUNCTION_FLIGHT_MODE },
    { CALCULATE_LED_XY( 0,  0), LED_DIRECTION_NORTH | LED_DIRECTION_WEST | LED_FUNCTION_INDICATOR | LED_FUNCTION_ARM_STATE },
    { CALCULATE_LED_XY( 0,  1), LED_DIRECTION_WEST | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 0,  2), LED_DIRECTION_SOUTH | LED_DIRECTION_WEST | LED_FUNCTION_INDICATOR | LED_FUNCTION_ARM_STATE },
    { CALCULATE_LED_XY( 1,  2), LED_DIRECTION_SOUTH | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 1,  1), LED_DIRECTION_UP | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 1,  1), LED_DIRECTION_UP | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 1,  1), LED_DIRECTION_DOWN | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
    { CALCULATE_LED_XY( 1,  1), LED_DIRECTION_DOWN | LED_FUNCTION_FLIGHT_MODE | LED_FUNCTION_WARNING },
};


/*
 * 6 coords @nn,nn
 * 4 direction @##
 * 6 modes @####
 * = 16 bytes per led
 * 16 * 32 leds = 512 bytes storage needed worst case.
 * = not efficient to store led configs as strings in flash.
 * = becomes a problem to send all the data via cli due to serial/cli buffers
 */

typedef enum {
    X_COORDINATE,
    Y_COORDINATE,
    DIRECTIONS,
    FUNCTIONS
} parseState_e;

#define PARSE_STATE_COUNT 4

static const char chunkSeparators[PARSE_STATE_COUNT] = {',', ':', ':', '\0' };

static const char directionCodes[] = { 'N', 'E', 'S', 'W', 'U', 'D' };
#define DIRECTION_COUNT (sizeof(directionCodes) / sizeof(directionCodes[0]))
static const uint8_t directionMappings[DIRECTION_COUNT] = {
    LED_DIRECTION_NORTH,
    LED_DIRECTION_EAST,
    LED_DIRECTION_SOUTH,
    LED_DIRECTION_WEST,
    LED_DIRECTION_UP,
    LED_DIRECTION_DOWN
};

static const char functionCodes[] = { 'I', 'W', 'F', 'A', 'T' };
#define FUNCTION_COUNT (sizeof(functionCodes) / sizeof(functionCodes[0]))
static const uint16_t functionMappings[FUNCTION_COUNT] = {
    LED_FUNCTION_INDICATOR,
    LED_FUNCTION_WARNING,
    LED_FUNCTION_FLIGHT_MODE,
    LED_FUNCTION_ARM_STATE,
    LED_FUNCTION_THROTTLE
};

// grid offsets
uint8_t highestYValueForNorth;
uint8_t lowestYValueForSouth;
uint8_t highestXValueForWest;
uint8_t lowestXValueForEast;

void determineLedStripDimensions(void)
{
    ledGridWidth = 0;
    ledGridHeight = 0;

    uint8_t ledIndex;
    const ledConfig_t *ledConfig;

    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {
        ledConfig = &ledConfigs[ledIndex];

        if (GET_LED_X(ledConfig) >= ledGridWidth) {
            ledGridWidth = GET_LED_X(ledConfig) + 1;
        }
        if (GET_LED_Y(ledConfig) >= ledGridHeight) {
            ledGridHeight = GET_LED_Y(ledConfig) + 1;
        }
    }
}

void determineOrientationLimits(void)
{
    bool isOddHeight = (ledGridHeight & 1);
    bool isOddWidth = (ledGridWidth & 1);
    uint8_t heightModifier = isOddHeight ? 1 : 0;
    uint8_t widthModifier = isOddWidth ? 1 : 0;

    highestYValueForNorth = (ledGridHeight / 2) - 1;
    lowestYValueForSouth = (ledGridHeight / 2) + heightModifier;
    highestXValueForWest = (ledGridWidth / 2) - 1;
    lowestXValueForEast = (ledGridWidth / 2) + widthModifier;
}

void updateLedCount(void)
{
    uint8_t ledIndex;
    ledCount = 0;
    for (ledIndex = 0; ledIndex < MAX_LED_STRIP_LENGTH; ledIndex++) {
        if (ledConfigs[ledIndex].flags == 0 && ledConfigs[ledIndex].xy == 0) {
            break;
        }
        ledCount++;
    }
}

static void reevalulateLedConfig(void)
{
    updateLedCount();
    determineLedStripDimensions();
    determineOrientationLimits();
}

#define CHUNK_BUFFER_SIZE 10

#define NEXT_PARSE_STATE(parseState) ((parseState + 1) % PARSE_STATE_COUNT)


bool parseLedStripConfig(uint8_t ledIndex, const char *config)
{
    char chunk[CHUNK_BUFFER_SIZE];
    uint8_t chunkIndex;
    uint8_t val;

    uint8_t parseState = X_COORDINATE;
    bool ok = true;

    if (ledIndex >= MAX_LED_STRIP_LENGTH) {
        return !ok;
    }

    ledConfig_t *ledConfig = &ledConfigs[ledIndex];
    memset(ledConfig, 0, sizeof(ledConfig_t));

    while (ok) {

        char chunkSeparator = chunkSeparators[parseState];

        memset(&chunk, 0, sizeof(chunk));
        chunkIndex = 0;

        while (*config && chunkIndex < CHUNK_BUFFER_SIZE && *config != chunkSeparator) {
            chunk[chunkIndex++] = *config++;
        }

        if (*config++ != chunkSeparator) {
            ok = false;
            break;
        }

        switch((parseState_e)parseState) {
            case X_COORDINATE:
                val = atoi(chunk);
                ledConfig->xy |= CALCULATE_LED_X(val);
                break;
            case Y_COORDINATE:
                val = atoi(chunk);
                ledConfig->xy |= CALCULATE_LED_Y(val);
                break;
            case DIRECTIONS:
                for (chunkIndex = 0; chunk[chunkIndex] && chunkIndex < CHUNK_BUFFER_SIZE; chunkIndex++) {
                    for (uint8_t mappingIndex = 0; mappingIndex < DIRECTION_COUNT; mappingIndex++) {
                        if (directionCodes[mappingIndex] == chunk[chunkIndex]) {
                            ledConfig->flags |= directionMappings[mappingIndex];
                            break;
                        }
                    }
                }
                break;
            case FUNCTIONS:
                for (chunkIndex = 0; chunk[chunkIndex] && chunkIndex < CHUNK_BUFFER_SIZE; chunkIndex++) {
                    for (uint8_t mappingIndex = 0; mappingIndex < FUNCTION_COUNT; mappingIndex++) {
                        if (functionCodes[mappingIndex] == chunk[chunkIndex]) {
                            ledConfig->flags |= functionMappings[mappingIndex];
                            break;
                        }
                    }
                }
                break;
        }

        parseState++;
        if (parseState >= PARSE_STATE_COUNT) {
            break;
        }
    }

    if (!ok) {
        memset(ledConfig, 0, sizeof(ledConfig_t));
    }

    reevalulateLedConfig();

    return ok;
}

void generateLedConfig(uint8_t ledIndex, char *ledConfigBuffer, size_t bufferSize)
{
    char functions[FUNCTION_COUNT];
    char directions[DIRECTION_COUNT];
    uint8_t index;
    uint8_t mappingIndex;
    ledConfig_t *ledConfig = &ledConfigs[ledIndex];

    memset(ledConfigBuffer, 0, bufferSize);
    memset(&functions, 0, sizeof(functions));
    memset(&directions, 0, sizeof(directions));

    for (mappingIndex = 0, index = 0; mappingIndex < FUNCTION_COUNT; mappingIndex++) {
        if (ledConfig->flags & functionMappings[mappingIndex]) {
            functions[index++] = functionCodes[mappingIndex];
        }
    }

    for (mappingIndex = 0, index = 0; mappingIndex < DIRECTION_COUNT; mappingIndex++) {
        if (ledConfig->flags & directionMappings[mappingIndex]) {
            directions[index++] = directionCodes[mappingIndex];
        }
    }

    sprintf(ledConfigBuffer, "%u,%u:%s:%s", GET_LED_X(ledConfig), GET_LED_Y(ledConfig), directions, functions);
}

// timers
uint32_t nextAnimationUpdateAt = 0;
uint32_t nextIndicatorFlashAt = 0;
uint32_t nextWarningFlashAt = 0;

#define LED_STRIP_20HZ ((1000 * 1000) / 20)
#define LED_STRIP_10HZ ((1000 * 1000) / 10)
#define LED_STRIP_5HZ ((1000 * 1000) / 5)

void applyDirectionalModeColor(const uint8_t ledIndex, const ledConfig_t *ledConfig, const modeColorIndexes_t *modeColors)
{
    // apply up/down colors regardless of quadrant.
    if ((ledConfig->flags & LED_DIRECTION_UP)) {
        setLedHsv(ledIndex, &colors[modeColors->up]);
    }

    if ((ledConfig->flags & LED_DIRECTION_DOWN)) {
        setLedHsv(ledIndex, &colors[modeColors->down]);
    }

    // override with n/e/s/w colors to each n/s e/w half - bail at first match.
    if ((ledConfig->flags & LED_DIRECTION_WEST) && GET_LED_X(ledConfig) <= highestXValueForWest) {
        setLedHsv(ledIndex, &colors[modeColors->west]);
    }

    if ((ledConfig->flags & LED_DIRECTION_EAST) && GET_LED_X(ledConfig) >= lowestXValueForEast) {
        setLedHsv(ledIndex, &colors[modeColors->east]);
    }

    if ((ledConfig->flags & LED_DIRECTION_NORTH) && GET_LED_Y(ledConfig) <= highestYValueForNorth) {
        setLedHsv(ledIndex, &colors[modeColors->north]);
    }

    if ((ledConfig->flags & LED_DIRECTION_SOUTH) && GET_LED_Y(ledConfig) >= lowestYValueForSouth) {
        setLedHsv(ledIndex, &colors[modeColors->south]);
    }

}

typedef enum {
    QUADRANT_NORTH_EAST = 1,
    QUADRANT_SOUTH_EAST,
    QUADRANT_SOUTH_WEST,
    QUADRANT_NORTH_WEST
} quadrant_e;

void applyQuadrantColor(const uint8_t ledIndex, const ledConfig_t *ledConfig, const quadrant_e quadrant, const hsvColor_t *color)
{
    switch (quadrant) {
        case QUADRANT_NORTH_EAST:
            if (GET_LED_Y(ledConfig) <= highestYValueForNorth && GET_LED_X(ledConfig) >= lowestXValueForEast) {
                setLedHsv(ledIndex, color);
            }
            return;

        case QUADRANT_SOUTH_EAST:
            if (GET_LED_Y(ledConfig) >= lowestYValueForSouth && GET_LED_X(ledConfig) >= lowestXValueForEast) {
                setLedHsv(ledIndex, color);
            }
            return;

        case QUADRANT_SOUTH_WEST:
            if (GET_LED_Y(ledConfig) >= lowestYValueForSouth && GET_LED_X(ledConfig) <= highestXValueForWest) {
                setLedHsv(ledIndex, color);
            }
            return;

        case QUADRANT_NORTH_WEST:
            if (GET_LED_Y(ledConfig) <= highestYValueForNorth && GET_LED_X(ledConfig) <= highestXValueForWest) {
                setLedHsv(ledIndex, color);
            }
            return;
    }
}

void applyLedModeLayer(void)
{
    const ledConfig_t *ledConfig;

    uint8_t ledIndex;
    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {

        ledConfig = &ledConfigs[ledIndex];

        setLedHsv(ledIndex, &hsv_black);

        if (!(ledConfig->flags & LED_FUNCTION_FLIGHT_MODE)) {
            if (ledConfig->flags & LED_FUNCTION_ARM_STATE) {
                if (!ARMING_FLAG(ARMED)) {
                    setLedHsv(ledIndex, &hsv_green);
                } else {
                    setLedHsv(ledIndex, &hsv_blue);
                }
            }
            continue;
        }

        applyDirectionalModeColor(ledIndex, ledConfig, &orientationModeColors);

        if (FLIGHT_MODE(HEADFREE_MODE)) {
            applyDirectionalModeColor(ledIndex, ledConfig, &headfreeModeColors);
#ifdef MAG
        } else if (FLIGHT_MODE(MAG_MODE)) {
            applyDirectionalModeColor(ledIndex, ledConfig, &magModeColors);
#endif
#ifdef BARO
        } else if (FLIGHT_MODE(BARO_MODE)) {
            applyDirectionalModeColor(ledIndex, ledConfig, &baroModeColors);
#endif
        } else if (FLIGHT_MODE(HORIZON_MODE)) {
            applyDirectionalModeColor(ledIndex, ledConfig, &horizonModeColors);
        } else if (FLIGHT_MODE(ANGLE_MODE)) {
            applyDirectionalModeColor(ledIndex, ledConfig, &angleModeColors);
        }
    }
}

typedef enum {
    WARNING_FLAG_NONE = 0,
    WARNING_FLAG_LOW_BATTERY = (1 << 0),
    WARNING_FLAG_FAILSAFE = (1 << 1),
    WARNING_FLAG_ARMING_DISABLED = (1 << 2)
} warningFlags_e;

void applyLedWarningLayer(uint8_t warningState, uint8_t warningFlags)
{
    const ledConfig_t *ledConfig;
    static uint8_t warningFlashCounter = 0;

    if (warningState) {
        warningFlashCounter++;
        warningFlashCounter = warningFlashCounter % 4;
    }

    uint8_t ledIndex;
    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {

        ledConfig = &ledConfigs[ledIndex];

        if (!(ledConfig->flags & LED_FUNCTION_WARNING)) {
            continue;
        }

        if (warningState == 0) {
            if (warningFlashCounter == 0 && (warningFlags & WARNING_FLAG_ARMING_DISABLED)) {
                setLedHsv(ledIndex, &hsv_yellow);
            }
            if (warningFlashCounter == 1 && (warningFlags & WARNING_FLAG_LOW_BATTERY)) {
                setLedHsv(ledIndex, &hsv_red);
            }
            if (warningFlashCounter > 1 && (warningFlags & WARNING_FLAG_FAILSAFE)) {
                setLedHsv(ledIndex, &hsv_lightBlue);
            }
        } else {
            if (warningFlashCounter == 0 && (warningFlags & WARNING_FLAG_ARMING_DISABLED)) {
                setLedHsv(ledIndex, &hsv_black);
            }
            if (warningFlashCounter == 1 && (warningFlags & WARNING_FLAG_LOW_BATTERY)) {
                setLedHsv(ledIndex, &hsv_black);
            }
            if (warningFlashCounter > 1 && (warningFlags & WARNING_FLAG_FAILSAFE)) {
                setLedHsv(ledIndex, &hsv_limeGreen);
            }
        }
    }
}

void applyLedIndicatorLayer(uint8_t indicatorFlashState)
{
    const ledConfig_t *ledConfig;
    static const hsvColor_t *flashColor;


    if (indicatorFlashState == 0) {
        flashColor = &hsv_orange;
    } else {
        flashColor = &hsv_black;
    }


    uint8_t ledIndex;
    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {

        ledConfig = &ledConfigs[ledIndex];

        if (!(ledConfig->flags & LED_FUNCTION_INDICATOR)) {
            continue;
        }

        if (rcCommand[ROLL] > 50) {
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_NORTH_EAST, flashColor);
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_SOUTH_EAST, flashColor);
        }

        if (rcCommand[ROLL] < -50) {
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_NORTH_WEST, flashColor);
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_SOUTH_WEST, flashColor);
        }

        if (rcCommand[PITCH] > 50) {
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_NORTH_EAST, flashColor);
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_NORTH_WEST, flashColor);
        }

        if (rcCommand[PITCH] < -50) {
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_SOUTH_EAST, flashColor);
            applyQuadrantColor(ledIndex, ledConfig, QUADRANT_SOUTH_WEST, flashColor);
        }
    }
}

void applyLedThrottleLayer()
{
    const ledConfig_t *ledConfig;
    hsvColor_t color;

    uint8_t ledIndex;
    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {
        ledConfig = &ledConfigs[ledIndex];
        if(!(ledConfig->flags & LED_FUNCTION_THROTTLE)) {
            continue;
        }

        getLedHsv(ledIndex, &color);

        int scaled = scaleRange(rcData[THROTTLE], PWM_RANGE_MIN, PWM_RANGE_MAX, -60, +60);
        scaled += HSV_HUE_MAX;
        color.h = scaled % HSV_HUE_MAX;
        setLedHsv(ledIndex, &color);
    }
}

static uint8_t frameCounter = 0;

static uint8_t previousRow;
static uint8_t currentRow;
static uint8_t nextRow;

static void updateLedAnimationState(void)
{
    uint8_t animationFrames = ledGridHeight;

    previousRow = (frameCounter + animationFrames - 1) % animationFrames;
    currentRow = frameCounter;
    nextRow = (frameCounter + 1) % animationFrames;

    frameCounter = (frameCounter + 1) % animationFrames;
}

#ifdef USE_LED_ANIMATION
static void applyLedAnimationLayer(void)
{
    const ledConfig_t *ledConfig;

    if (ARMING_FLAG(ARMED)) {
        return;
    }

    uint8_t ledIndex;
    for (ledIndex = 0; ledIndex < ledCount; ledIndex++) {

        ledConfig = &ledConfigs[ledIndex];

        if (GET_LED_Y(ledConfig) == previousRow) {
            setLedHsv(ledIndex, &white);
            setLedBrightness(ledIndex, 50);

        } else if (GET_LED_Y(ledConfig) == currentRow) {
            setLedHsv(ledIndex, &white);
        } else if (GET_LED_Y(ledConfig) == nextRow) {
            setLedBrightness(ledIndex, 50);
        }
    }
}
#endif

void updateLedStrip(void)
{
    if (!(ledStripInitialised && isWS2811LedStripReady())) {
        return;
    }

    uint32_t now = micros();

    bool animationUpdateNow = (int32_t)(now - nextAnimationUpdateAt) >= 0L;
    bool indicatorFlashNow = (int32_t)(now - nextIndicatorFlashAt) >= 0L;
    bool warningFlashNow = (int32_t)(now - nextWarningFlashAt) >= 0L;

    if (!(warningFlashNow || indicatorFlashNow || animationUpdateNow)) {
        return;
    }

    static uint8_t indicatorFlashState = 0;
    static uint8_t warningState = 0;
    static uint8_t warningFlags;

    // LAYER 1
    applyLedModeLayer();
    applyLedThrottleLayer();

    // LAYER 2

    if (warningFlashNow) {
        nextWarningFlashAt = now + LED_STRIP_10HZ;

        if (warningState == 0) {
            warningState = 1;

            warningFlags = WARNING_FLAG_NONE;
            if (feature(FEATURE_VBAT) && shouldSoundBatteryAlarm()) {
                warningFlags |= WARNING_FLAG_LOW_BATTERY;
            }
            if (failsafe->vTable->hasTimerElapsed()) {
                warningFlags |= WARNING_FLAG_FAILSAFE;
            }
            if (!ARMING_FLAG(ARMED) && !ARMING_FLAG(OK_TO_ARM)) {
                warningFlags |= WARNING_FLAG_ARMING_DISABLED;
            }

        } else {
            warningState = 0;
        }
    }

    if (warningFlags) {
        applyLedWarningLayer(warningState, warningFlags);
    }

    // LAYER 3

    if (indicatorFlashNow) {

        uint8_t rollScale = abs(rcCommand[ROLL]) / 50;
        uint8_t pitchScale = abs(rcCommand[PITCH]) / 50;
        uint8_t scale = max(rollScale, pitchScale);
        nextIndicatorFlashAt = now + (LED_STRIP_5HZ / max(1, scale));

        if (indicatorFlashState == 0) {
            indicatorFlashState = 1;
        } else {
            indicatorFlashState = 0;
        }
    }

    applyLedIndicatorLayer(indicatorFlashState);

    if (animationUpdateNow) {
        nextAnimationUpdateAt = now + LED_STRIP_20HZ;
        updateLedAnimationState();
    }

#ifdef USE_LED_ANIMATION
    applyLedAnimationLayer();
#endif
    ws2811UpdateStrip();
}

bool parseColor(uint8_t index, char *colorConfig)
{
    char *remainingCharacters = colorConfig;

    hsvColor_t *color = &colors[index];

    bool ok = true;

    uint8_t componentIndex;
    for (componentIndex = 0; ok && componentIndex < HSV_COLOR_COMPONENT_COUNT; componentIndex++) {
        uint16_t val = atoi(remainingCharacters);
        switch (componentIndex) {
            case HSV_HUE:
                if (val > HSV_HUE_MAX) {
                    ok = false;
                    continue;
                }
                colors[index].h = val;
                break;
            case HSV_SATURATION:
                if (val > HSV_SATURATION_MAX) {
                    ok = false;
                    continue;
                }
                colors[index].s = (uint8_t)val;
                break;
            case HSV_VALUE:
                if (val > HSV_VALUE_MAX) {
                    ok = false;
                    continue;
                }
                colors[index].v = (uint8_t)val;
                break;
        }
        remainingCharacters = strstr(remainingCharacters, ",");
        if (remainingCharacters) {
            remainingCharacters++;
        } else {
            if (componentIndex < 2) {
                ok = false;
            }
        }
    }

    if (!ok) {
        memset(color, 0, sizeof(hsvColor_t));
    }

    return ok;
}

void applyDefaultColors(hsvColor_t *colors, uint8_t colorCount)
{
    memset(colors, 0, colorCount * sizeof(colors));
    for (uint8_t colorIndex = 0; colorIndex < colorCount && colorIndex < (sizeof(defaultColors) / sizeof(defaultColors[0])); colorIndex++) {
        *colors++ = *defaultColors[colorIndex];
    }
}

void applyDefaultLedStripConfig(ledConfig_t *ledConfigs)
{
    memset(ledConfigs, 0, MAX_LED_STRIP_LENGTH * sizeof(ledConfig_t));
    memcpy(ledConfigs, &defaultLedStripConfig, sizeof(defaultLedStripConfig));

    reevalulateLedConfig();
}

void ledStripInit(ledConfig_t *ledConfigsToUse, hsvColor_t *colorsToUse, failsafe_t* failsafeToUse)
{
    ledConfigs = ledConfigsToUse;
    colors = colorsToUse;
    failsafe = failsafeToUse;
    ledStripInitialised = false;
}

void ledStripEnable(void)
{
    reevalulateLedConfig();
    ledStripInitialised = true;

    ws2811LedStripInit();
}
#endif
