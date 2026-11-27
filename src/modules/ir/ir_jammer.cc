/**
 * Advanced IR Jammer Implementation
 *
 * This module provides multiple IR jamming techniques to disrupt IR remote control
 * signals across various frequencies and protocols. Features include:
 * - Multiple jamming algorithms (basic, enhanced, sweep, random, empty)
 * - Adjustable frequency (30-56kHz)
 * - Configurable timing parameters
 * - Real-time statistics
 *
 * Originally based on work by Bruce Lee/EA7KDO with significant enhancements
 * for effectiveness and configurability.
 */

#include "ir_jammer.h"
#include "TV-B-Gone.h" // for checkIrTxPin()
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "core/settings.h"
#include "core/utils.h"
#include "ir_utils.h"
#include <globals.h>
#include <interface.h>

// Common IR remote control frequencies in Hz
// Covers most consumer devices (30-56kHz range)
const uint16_t IR_FREQUENCIES[] = {30000, 33000, 36000, 38000, 40000, 42000, 56000};
const int NUM_FREQS = sizeof(IR_FREQUENCIES) / sizeof(IR_FREQUENCIES[0]);

// Human-readable mode names for display
const char *IR_MODE_NAMES[] = {"BASIC", "ENH. BASIC", "SWEEP", "RANDOM", "EMPTY"};

/**
 * Initialize the jammer state with safe default values
 * Sets up timing parameters, patterns, and resets statistics
 */
void initJammerState(JammerState &state) {
    // Initialize timing values with safe defaults
    state.markTiming = 12;
    state.spaceTiming = 12;
    state.minTiming = 8;
    state.maxTiming = 70;
    state.dutyCycle = 50;
    state.jamDensity = 5;
    state.sweepSpeed = 1;
    state.sweepDirection = 1;
    state.current_freq_idx = 3; // Start with 38kHz (most common)

    // Initialize basic pattern
    for (int i = 0; i < 20; i += 2) {
        state.basicPattern[i] = state.markTiming;
        state.basicPattern[i + 1] = state.spaceTiming;
    }

    // Initialize random pattern
    randomSeed(millis());
    for (int i = 0; i < 30; i++) { state.randomPattern[i] = random(10, 1000); }

    // Reset stats
    state.jamCount = 0;
    state.startTime = millis();

    // Update settings based on mode
    updateMaxSettings(state);
}

/**
 * Adjust mode-specific parameters based on the currently selected setting
 *
 * @param state Current jammer state
 * @param settingIndex Which parameter is being adjusted (3+ are mode-specific)
 * @param adjustment Amount and direction of change (+1 or -1)
 */
void adjustModeSpecificSetting(JammerState &state, uint8_t settingIndex, int adjustment) {
    switch (state.currentMode) {
        case BASIC:
            if (settingIndex == 3) {
                state.markTiming = constrain(state.markTiming + adjustment, 5, 100);
                state.spaceTiming = state.markTiming; // Keep in sync for basic mode
            }
            break;

        case ENHANCED_BASIC:
            switch (settingIndex) {
                case 3: state.markTiming = constrain(state.markTiming + adjustment, 5, 100); break;
                case 4: state.spaceTiming = constrain(state.spaceTiming + adjustment, 1, 100); break;
                case 5: state.jamDensity = constrain(state.jamDensity + adjustment, 1, 20); break;
            }
            break;

        case SWEEP:
            switch (settingIndex) {
                case 3:
                    state.minTiming = constrain(state.minTiming + adjustment, 1, state.maxTiming - 5);
                    break;
                case 4:
                    state.maxTiming = constrain(state.maxTiming + adjustment, state.minTiming + 5, 150);
                    break;
                case 5: state.sweepSpeed = constrain(state.sweepSpeed + adjustment, 1, 10); break;
                case 6: state.jamDensity = constrain(state.jamDensity + adjustment, 1, 20); break;
            }
            break;

        case RANDOM:
        case EMPTY:
            if (settingIndex == 3) { state.jamDensity = constrain(state.jamDensity + adjustment, 1, 20); }
            break;
    }
}

/**
 * Render the mode-specific settings UI elements based on the current jamming mode
 * Each mode has different configurable parameters that need to be displayed
 *
 * @param state Current jammer configuration
 * @param curY Current Y position for drawing (will be updated as content is added)
 * @param ySpacing Vertical spacing between UI elements
 */
void renderModeSettings(JammerState &state, int &curY, int ySpacing) {
    uint16_t surf = uiSurfaceColor();
    uint16_t selCol = bruceConfig.priColor;
    uint16_t normCol = uiTextBrightColor();
    uint16_t mutedCol = uiTextMutedColor();
    bool compact = (tftHeight < 160);
    int leftX = compact ? 14 : 20;

    auto drawParam = [&](uint8_t idx, const char *label, const String &val) {
        curY += ySpacing;
        bool active = (state.settingIndex == idx);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(FP);
        tft.setTextColor(active ? selCol : mutedCol, surf);
        tft.drawString(active ? ("> " + String(label)) : ("  " + String(label)), leftX, curY, 1);
        tft.setTextColor(active ? bruceConfig.secColor : normCol, surf);
        tft.drawString(val + "   ", leftX + 54, curY, 1);
    };

    switch (state.currentMode) {
        case BASIC:
            drawParam(3, "TIME:", String(state.markTiming) + "us");
            break;
        case ENHANCED_BASIC:
            drawParam(3, "MARK:", String(state.markTiming) + "us");
            drawParam(4, "SPCE:", String(state.spaceTiming) + "us");
            drawParam(5, "PWR:", String(state.jamDensity));
            break;
        case SWEEP:
            drawParam(3, "MIN:", String(state.minTiming) + "us");
            drawParam(4, "MAX:", String(state.maxTiming) + "us");
            drawParam(5, "SPD:", String(state.sweepSpeed));
            drawParam(6, "PWR:", String(state.jamDensity));
            break;
        case RANDOM:
        case EMPTY:
            drawParam(3, "PWR:", String(state.jamDensity));
            break;
    }
}

void displayStats(JammerState &state, int x, int y) {
    uint16_t surf = uiSurfaceColor();
    uint32_t runtime = (millis() - state.startTime) / 1000;
    if (state.jamming_active) { state.runtime = runtime; }
    float jps = state.runtime > 0 ? (float)state.jamCount / state.runtime : 0;

    int boxW = max(68, tftWidth - x - 14);
    int rowH = (tftHeight < 160) ? 12 : 16;
    tft.fillRect(x, y, boxW, rowH * 3 + 4, surf);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu", (unsigned long)(state.runtime / 60), (unsigned long)(state.runtime % 60));
    char jbuf[16];
    snprintf(jbuf, sizeof(jbuf), "%.1f", jps);

    uiDrawKeyValueRow(x, y, boxW, "Jams", String(state.jamCount), bruceConfig.priColor, surf);
    uiDrawKeyValueRow(x, y + rowH, boxW, "Time", String(tbuf), uiTextBrightColor(), surf);
    uiDrawKeyValueRow(x, y + rowH * 2, boxW, "J/s", String(jbuf), bruceConfig.secColor, surf);
}

/**
 * Process user input for navigating and changing jammer settings
 * Handles Selection button for cycling through settings
 *
 * @param state Current jammer state to be updated based on input
 */
void handleJammerInput(JammerState &state) {
    // Handle Select button press to cycle through settings
    if (check(SelPress)) {
        if (!state.selPressHandled) {
            // Cycle to next setting (wraps around using modulo)
            state.settingIndex = (state.settingIndex + 1) % state.maxSettings;
            state.redraw = true;
            state.selPressHandled = true;
            delay(150); // Debounce to prevent multiple triggers
        }
    } else {
        // Reset the handled flag when button is released
        state.selPressHandled = false;
    }

    // Handle Next/Prev buttons to change the selected setting's value
    handleSettingChange(state, check(NextPress), check(PrevPress));
}

/**
 * Process changes to setting values based on Next/Prev button presses
 *
 * @param state Current jammer state to be updated
 * @param nextPressed Whether the Next button was pressed
 * @param prevPressed Whether the Previous button was pressed
 */
void handleSettingChange(JammerState &state, bool nextPressed, bool prevPressed) {
    // Exit if neither button is pressed
    if (!nextPressed && !prevPressed) return;

    // Determine adjustment direction based on which button was pressed
    int adjustment = nextPressed ? 1 : -1;

    // Handle changes based on which setting is currently selected
    switch (state.settingIndex) {
        case 0: // Status (active/paused)
            state.jamming_active = !state.jamming_active;
            if (state.jamming_active) {
                // Reset statistics when starting a new jamming session
                state.startTime = millis();
                state.jamCount = 0;
            }
            break;

        case 1: // Frequency selection
            // Cycle through available frequencies with wrap-around
            state.current_freq_idx = (state.current_freq_idx + adjustment + NUM_FREQS) % NUM_FREQS;
            break;

        case 2: // Jamming mode selection
            // Cycle through available modes with wrap-around
            state.currentMode = (JamMode)((state.currentMode + adjustment + 5) % 5);
            // Update available settings based on new mode
            updateMaxSettings(state);
            updatePatterns(state);
            break;

        // Mode-specific settings (handled by the helper function)
        default: adjustModeSpecificSetting(state, state.settingIndex, adjustment); break;
    }

    // Mark UI for redrawing and update pattern arrays
    state.redraw = true;
    updatePatterns(state);
    delay(100); // Prevent too rapid changes when button is held
}

/**
 * Update pattern arrays based on current timing settings
 * These patterns are used by the IR library for signal generation
 *
 * @param state Current jammer state containing timing parameters
 */
void updatePatterns(JammerState &state) {
    // Update the basic pattern with current mark/space timings
    // Even indices are mark durations, odd indices are space durations
    for (int i = 0; i < 20; i += 2) {
        state.basicPattern[i] = state.markTiming;
        state.basicPattern[i + 1] = state.spaceTiming;
    }

    // Note: Random pattern is updated separately in the random jamming function
    // because it needs to change with each transmission
}

/**
 * Update the maximum number of settings available based on the current mode
 * Different modes have different numbers of configurable parameters
 *
 * @param state Current jammer state to update
 */
void updateMaxSettings(JammerState &state) {
    switch (state.currentMode) {
        case BASIC:
            // Basic mode: status toggle, frequency, mode selection, timing
            state.maxSettings = 4;
            break;
        case ENHANCED_BASIC:
            // Enhanced Basic: adds separate mark/space timing and power control
            state.maxSettings = 6;
            break;
        case SWEEP:
            // Sweep mode: adds min/max bounds, sweep speed, and power control
            state.maxSettings = 7;
            break;
        case RANDOM:
        case EMPTY:
            // Random and Empty modes: only basic controls plus power
            state.maxSettings = 4;
            break;
    }
}

/**
 * Initialize the IR transmitter hardware
 *
 * @param irsend Reference to the IR transmitter object
 */
void setupJammer(IRsend &irsend) {
    // Validate IR transmitter pin configuration
    checkIrTxPin();

    // Initialize IR transmission library
    irsend.begin();

    // Configure IR LED pin as output
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);

    // Draw UI border on the display
    drawMainBorder();
}

/**
 * Render the jammer user interface based on current state
 * Updates display with current settings, mode, and performance statistics
 *
 * @param state Current jammer configuration and state
 */
void renderJammerUI(JammerState &state) {
    uint32_t currentMillis = millis();
    if (!state.redraw && currentMillis - state.lastUIUpdate < 300) return;

    bool blinkState = (currentMillis % 600 < 300);
    state.lastUIUpdate = currentMillis;

    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 12;
    int cardY = BORDER_PAD_Y + 22;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 16 : 20);
    int leftX = compact ? 14 : 20;
    int rightColumnX = tftWidth / 2 + 8;
    int ySpacing = compact ? 10 : 15;
    uint16_t surf = uiSurfaceColor();
    uint16_t selCol = bruceConfig.priColor;
    uint16_t normCol = uiTextBrightColor();
    uint16_t mutedCol = uiTextMutedColor();

    if (state.redraw) {
        drawMainBorderWithTitle("IR Jammer");
        uiDrawCard(cardX, cardY, cardW, cardH, true);
        printCenterFootnote("SEL: Next  PREV/NEXT: Edit  ESC: Exit");
    }

    int curY = cardY + (compact ? 7 : 11);

    // Status row with badge + activity pulse dot
    tft.fillRect(cardX + 6, curY - 1, cardW - 12, 16, surf);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor((state.settingIndex == 0) ? selCol : mutedCol, surf);
    tft.drawString((state.settingIndex == 0) ? "> STAT" : "  STAT", leftX, curY + 3, 1);

    uint16_t stBg = state.jamming_active ? TFT_RED : uiSurfaceHiColor();
    int16_t stW = uiDrawBadge(leftX + 46, curY, state.jamming_active ? "ACTIVE" : "PAUSED", stBg, 14);
    if (state.jamming_active) {
        tft.fillCircle(leftX + 54 + stW, curY + 7, 3, blinkState ? bruceConfig.secColor : surf);
    }

    // Frequency row
    curY += (compact ? 17 : 21);
    tft.setTextColor((state.settingIndex == 1) ? selCol : mutedCol, surf);
    tft.drawString((state.settingIndex == 1) ? "> FREQ:" : "  FREQ:", leftX, curY, 1);
    tft.setTextColor((state.settingIndex == 1) ? bruceConfig.secColor : normCol, surf);
    tft.drawString(String(getFrequency(state.current_freq_idx) / 1000) + " kHz   ", leftX + 54, curY, 1);

    // Mode row
    curY += ySpacing;
    tft.setTextColor((state.settingIndex == 2) ? selCol : mutedCol, surf);
    tft.drawString((state.settingIndex == 2) ? "> MODE:" : "  MODE:", leftX, curY, 1);
    tft.setTextColor((state.settingIndex == 2) ? bruceConfig.secColor : normCol, surf);
    tft.drawString(String(getModeName(state.currentMode)) + "    ", leftX + 54, curY, 1);

    // Mode-specific settings & vertical divider for right-side stats
    renderModeSettings(state, curY, ySpacing);
    tft.drawFastVLine(rightColumnX - 6, cardY + 26, cardH - 34, uiBorderDimColor());
    displayStats(state, rightColumnX, cardY + (compact ? 28 : 38));

    state.redraw = false;
}

/**
 * Update jamming statistics after transmitting signals
 * Increments jam count and triggers UI update when needed
 *
 * @param state Current jammer state to update
 */
void updateStats(JammerState &state) {
    // Increment jam count for each signal transmitted
    state.jamCount++;

    // Only update UI every 10 jams to reduce display flicker
    if (state.jamCount % 10 == 0) { state.redraw = true; }
}

/**
 * Execute jamming operations based on current mode and settings
 * Controls the frequency of signals based on jam density
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performJamming(JammerState &state, IRsend &irsend) {
    // Skip if jamming is paused
    if (!state.jamming_active) return;

    uint32_t currentMillis = millis();

    // Control signal frequency based on density setting
    // Higher density = more frequent signals
    if (currentMillis - state.lastJamTime >= (10 / state.jamDensity)) {
        state.lastJamTime = currentMillis;

        // Select appropriate jamming implementation based on mode
        switch (state.currentMode) {
            case BASIC: performBasicJamming(state, irsend); break;
            case ENHANCED_BASIC: performEnhancedBasicJamming(state, irsend); break;
            case SWEEP: performSweepJamming(state, irsend); break;
            case RANDOM: performRandomJamming(state, irsend); break;
            case EMPTY: performEmptyJamming(state, irsend); break;
        }

        // Update statistics after sending jam signals
        updateStats(state);
    }
}

/**
 * Implement basic jamming mode with fixed, equal mark/space timing
 * Uses both direct LED control and IR library for maximum effectiveness
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performBasicJamming(JammerState &state, IRsend &irsend) {
    uint32_t currentMillis = millis();

    // Throttle transmission rate to prevent hardware overload
    if (currentMillis - state.last_update > 20) {
        // Method 1: Direct LED control for precise timing
        // Generates simple square wave with equal mark/space duration
        for (int i = 0; i < 50 * state.jamDensity; i++) {
            digitalWrite(bruceConfigPins.irTx, HIGH);
            delayMicroseconds(state.markTiming);
            digitalWrite(bruceConfigPins.irTx, LOW);
            delayMicroseconds(state.markTiming);
        }

        // Method 2: Use IR library for compatibility with different protocols
        irsend.sendRaw(state.basicPattern, 20, getFrequency(state.current_freq_idx));
        state.last_update = currentMillis;
    }
}

/**
 * Implement enhanced basic jamming with separate mark/space timing control
 * This mode provides more precise control over IR signal characteristics
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performEnhancedBasicJamming(JammerState &state, IRsend &irsend) {
    uint32_t currentMillis = millis();

    // Throttle transmission rate
    if (currentMillis - state.last_update > 20) {
        // Method 1: Direct LED control with separate mark/space timing
        // Provides more flexibility to target specific IR protocols
        for (int i = 0; i < 25 * state.jamDensity; i++) {
            digitalWrite(bruceConfigPins.irTx, HIGH);
            delayMicroseconds(state.markTiming);
            digitalWrite(bruceConfigPins.irTx, LOW);
            delayMicroseconds(state.spaceTiming);
        }

        // Method 2: Use IR library with custom pattern
        irsend.sendRaw(state.basicPattern, 20, getFrequency(state.current_freq_idx));
        state.last_update = currentMillis;
    }
}

/**
 * Implement sweep jamming mode that varies timing continuously
 * Effective against IR protocols with dynamic timing adaptation
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performSweepJamming(JammerState &state, IRsend &irsend) {
    uint32_t currentMillis = millis();

    // Faster update rate for smooth sweep
    if (currentMillis - state.last_update > 30) {
        // Update timing values based on current direction and speed
        state.markTiming += state.sweepDirection * state.sweepSpeed;

        // Change direction if we hit the min/max bounds
        if (state.markTiming > state.maxTiming || state.markTiming < state.minTiming) {
            state.sweepDirection *= -1; // Reverse direction
            // Constrain to prevent exceeding bounds
            state.markTiming = constrain(state.markTiming, state.minTiming, state.maxTiming);
        }

        // For sweep mode, keep mark and space timings equal
        state.spaceTiming = state.markTiming;

        // Update pattern array with new timings
        updatePatterns(state);

        // Direct LED control for precise timing
        for (int i = 0; i < 20 * state.jamDensity; i++) {
            digitalWrite(bruceConfigPins.irTx, HIGH);
            delayMicroseconds(state.markTiming);
            digitalWrite(bruceConfigPins.irTx, LOW);
            delayMicroseconds(state.markTiming);
        }

        // Also use IR library for protocol compatibility
        irsend.sendRaw(state.basicPattern, 20, getFrequency(state.current_freq_idx));
        state.last_update = currentMillis;
    }
}

/**
 * Implement random jamming mode with unpredictable patterns
 * Most effective against smart/learning remotes and adaptive systems
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performRandomJamming(JammerState &state, IRsend &irsend) {
    uint32_t currentMillis = millis();

    // Slower update rate to allow for more random pattern generation
    if (currentMillis - state.last_update > 100) {
        // Generate new random pattern for each transmission
        for (int i = 0; i < 30; i++) {
            state.randomPattern[i] = random(5, 1000); // Random durations between 5-1000µs
        }

        // Send multiple random patterns based on jam density
        for (int i = 0; i < state.jamDensity / 2 + 1; i++) {
            // Randomly change carrier frequency to disrupt more protocols
            if (random(10) < 3) { state.current_freq_idx = random(NUM_FREQS); }

            // Send the random pattern at the selected frequency
            irsend.sendRaw(state.randomPattern, 30, getFrequency(state.current_freq_idx));
        }

        state.last_update = currentMillis;
    }
}

/**
 * Implement empty packet jamming mode with minimal valid IR signals
 * Effective at confusing IR receivers while using minimal power
 *
 * @param state Current jammer configuration
 * @param irsend IR transmitter interface
 */
void performEmptyJamming(JammerState &state, IRsend &irsend) {
    uint32_t currentMillis = millis();

    // Medium update rate optimized for empty packet transmission
    if (currentMillis - state.last_update > 50) {
        // Send multiple empty packets at the current frequency
        for (int i = 0; i < state.jamDensity; i++) {
            irsend.sendRaw(state.emptyPattern, 4, getFrequency(state.current_freq_idx));
        }

        // Occasionally change frequency (40% chance)
        if (random(5) < 2) { state.current_freq_idx = (state.current_freq_idx + 1) % NUM_FREQS; }

        state.last_update = currentMillis;
    }
}

/**
 * Clean up resources and show exit message when jammer is stopped
 *
 * @param irsend IR transmitter interface to reset
 */
void cleanupJammer(IRsend &irsend) {

#ifdef USE_BOOST /// ENABLE 5V OUTPUT
    hal_pmic_disable_otg();
#endif
    // Ensure IR LED is turned off
    digitalWrite(bruceConfigPins.irTx, LOW);

    // Display exit message
    displayRedStripe("IR Jamming Stopped");

    // Short delay for user to see the message
    delay(1000);
}

/**
 * Main entry point for IR jammer functionality
 * Initializes hardware, runs the main loop, and handles cleanup
 */
void startIrJammer() {
#ifdef USE_BOOST /// ENABLE 5V OUTPUT
    hal_pmic_enable_otg();
#endif
    // Initialize IR transmitter with configured pin
    IRsend irsend(bruceConfigPins.irTx);

    // Initialize jammer state structure
    JammerState state;

    // Set up hardware and state
    setupJammer(irsend);
    initJammerState(state);

    // Main jammer loop - runs until ESC is pressed
    while (!check(EscPress)) {
        renderJammerUI(state);         // Update display
        performJamming(state, irsend); // Execute jamming if active
        handleJammerInput(state);      // Process user input

        // Small delay to prevent system overload
        delay(5);
    }

    // Clean up when exiting
    cleanupJammer(irsend);
}

/**
 * Get IR carrier frequency in Hz for the given index
 * Uses program memory for efficient storage of frequency values
 *
 * @param index Index into the IR_FREQUENCIES array
 * @return Frequency value in Hz
 */
uint16_t getFrequency(uint8_t index) { return pgm_read_word(&IR_FREQUENCIES[index]); }

/**
 * Get human-readable name for the current jamming mode
 *
 * @param index Index from JamMode enum
 * @return String containing the mode name
 */
const char *getModeName(uint8_t index) { return IR_MODE_NAMES[index]; }
