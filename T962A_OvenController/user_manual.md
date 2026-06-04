# User Manual — OvenController v1.11.0

## Overview

The OvenController converts a T962A reflow oven into a programmable, closed-loop reflow soldering station with precise temperature control, live monitoring, and safety features.

## Hardware Overview

### Front Panel Controls
| Button | Function |
|--------|----------|
| F1 (UP) | Navigate up / increase value |
| F2 (DOWN) | Navigate down / decrease value |
| F3 (LEFT) | Navigate left / previous field |
| F4 (RIGHT) | Navigate right / next field |
| S (SELECT/START) | Confirm selection / start reflow |
| E-STOP | Emergency stop (immediate SSR cut) |

### Display
- 128x64 GLCD screen (KS0108) with multi-page menu system
- Splash screen on boot shows firmware version + detected line frequency (50Hz or 60Hz)
- Live plot during reflow (8-minute rolling window, 280°C range)

## Main Menu

On boot, the main menu displays:
1. **Run Reflow** — select and start a reflow profile
2. **Create Recipe** — create a new reflow profile
3. **Edit Recipe** — modify an existing recipe
4. **Bake Mode** — simple constant-temperature bake/drying
5. **Settings** — units, system config
6. **Calibration** — TC offset, profile calibration test, and PID/calibration data viewer

Use **F1 (UP)** and **F2 (DOWN)** to scroll. Press **S (SELECT)** to enter the highlighted item.

## Creating a Recipe

Select **Create Recipe** to create a new recipe with default values, then edit fields:

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Preheat Temp | 25–280°C | 150°C | Target temperature for ramp phase |
| Soak Temp | 25–280°C | 180°C | Target temperature for soak phase |
| Peak Temp | 25–280°C | 220°C | Maximum reflow temperature |
| Ramp Time | 0–600 sec | 90 sec | Time to reach soak temperature |
| Soak Time | 0–300 sec | 60 sec | Dwell time at soak temperature |
| Reflow Time | 0–300 sec | 45 sec | Time above liquidus (peak) |
| Hold Time | 0–120 sec | 10 sec | Additional hold at peak |
| Fine-Tune | ON/OFF | ON | Per-profile adaptive gain fine-tuning |

**Editing controls:**
- **F3 (LEFT) / F4 (RIGHT)**: Switch between fields
- **F1 (UP) / F2 (DOWN)**: Increase / decrease value (temperatures in 5°C steps, times in 5s steps; **toggles Fine-Tune ON/OFF**)
- **S (SELECT)**: Save recipe and return to main menu
- **F3 (LEFT)** on any field: Cancel and return to main menu

**About Fine-Tuning**: When enabled (ON), the system evaluates PID performance after each zone completes (overshoot, steady error, oscillation) and adjusts gains conservatively (±5%) per zone, per run. Gains converge over 3–5 runs. Disable (OFF) once the profile tracks well to lock gains. If a Calibration Test runs, it overrides all per-profile gains with fresh Ziegler-Nichols-computed values.

## Running a Reflow Cycle

1. From main menu, select **Run Reflow**
2. Choose a recipe from the recipe list using **F1 (UP) / F2 (DOWN)**
3. Press **S (SELECT)** to confirm and start
4. The running display shows:
   - Stage name (PREHEAT → SOAK → REFLOW → COOLDOWN → DONE) with elapsed time
   - Setpoint and actual temperatures
   - Heater power bar with percentage
   - Fan/cooling power bar with percentage
   - TC1 and TC2 individual temperatures
   - Mini temperature bar at the bottom (actual vs setpoint)
5. During a run, press **F3 (LEFT) / F4 (RIGHT)** to toggle between the running display and a full-screen live temperature graph
6. Cycle completes automatically; buzzer sounds three times, returns to main menu

## Bake Mode

For drying solder paste, preheating PCBs, or general baking:
- Temperature range: 25–280°C
- Duration: 1 minute up to system max (default 300 min)
- Constant temperature hold with PID control
- **F1 (UP) / F2 (DOWN)**: Adjust value
- **F4 (RIGHT)**: Switch between temperature and duration fields
- **S (SELECT)**: Start bake cycle
- **F3 (LEFT)**: Return to main menu

## Settings

| Setting | Range | Description |
|---------|-------|-------------|
| Units | °C / °F | Toggle with **F4 (RIGHT)** or **S (SELECT)** |
| Max Bake | 5–999 min | Maximum allowed bake duration. Adjust with **F1 (UP) / F2 (DOWN)** |

Press **F3 (LEFT)** to save and return to main menu.

## Calibration Submenu

Select **Calibration** from the main menu to open the calibration submenu with three options:

1. **TC Offset Info** — displays instructions for calibrating thermocouple offsets.
2. **Profile Cal Test** — runs a two-phase (heat then cool) calibration test at full power to measure per-zone heating/cooling rates and system dead times. Results are applied to all recipes with a 1.2× safety margin.
3. **View Cal Info** — read-only viewer for all stored calibration data and PID gains.

### View Cal Info Screen

The **View Cal Info** screen has three pages cycled by pressing **S (SELECT)**:

- **PID Gains page** (default): shows recipe name, recipe index, and per-zone heater PID gains (Kp/Ki/Kd) and cooling PID gains per zone. Use **F1 (UP) / F2 (DOWN)** to scroll through all recipes.
- **Cal Data page**: shows the heating and cooling rate measurements per zone from the last profile calibration test, and the measured heat and cool dead times (in milliseconds).
- **Recipe Temps page**: shows the recipe's temperature setpoints (preheat, soak, peak) and times (ramp, soak, reflow, hold), all in the user's selected temperature unit.

**Temperature unit conversion**: All temperature values throughout the UI (running display, plot, recipe edit, bake setup, calibration info) automatically convert to the unit selected in **Settings** (Settings → Units → °C or °F). Internal calculations always use °C; the display shows the converted value with a `C` or `F` suffix.

Press **F3 (LEFT)** at any time to return to the calibration submenu.

## Safety Features

### Automatic Shutdown Triggers
The system immediately enters E-STOP state and disables both SSR outputs when:
- **Over-temperature**: Any sensor exceeds 280°C
- **Sensor fault**: Average temperature reads below 5°C (indicates TC disconnect)
- **Spatial delta**: Temperature difference between TC1 and TC2 exceeds 45°C
- **Hardware E-STOP**: Dedicated E-STOP button pressed

### E-STOP Recovery
1. Resolve the fault condition (over-temp, sensor fault, etc.)
2. The display shows "EMERGENCY STOP / Over-temp or Fault / Press SELECT to acknowledge"
3. Press **S (SELECT)** to acknowledge, reset PID, and return to main menu
4. If the fault persists, the system will re-enter E-STOP on the next control cycle

## Display Screens

### Calibration Test Running Display
```
=HEAT Z3/5 100%PWR 185C=
R:+1.2C/s M:+1.8C/s
HEAT  01:23     [S]Stop
┌────────────────────────┐
│        ∙∙ ∙∙           │
│     ∙∙∙  ∙  ∙∙         │
│   ∙∙          ∙∙       │
│  ∙              ∙      │
│ ∙                ∙     │
└────────────────────────┘
0C                   280C
```
The calibration test screen shows a real-time temperature-vs-time graph alongside the current phase (HEAT/COOL), zone, power level, current temperature (in user-selected °C or °F), current rate, max rate, and elapsed time. The graph Y-axis spans 0 to 280°C (or 536°F in Fahrenheit mode). Temperature and rate values respect the unit setting from **Settings** → **Units**.

### Startup Splash
```
Reflow Oven
 Controller
──────────────
Ver 1.11.0
50 Hz
```
Shown for ~2s on boot. Displays firmware version and auto-detected line frequency. If measurement fails, shows "Measuring..." until timeout.

### Live Run Display (default view)
```
PREHEAT  02:30
Set:150C Act:142C
PWR [████░░░░░] 45%
FAN [██░░░░░░░] 20%
TC1:142C TC2:140C
[▄▄▄▄▄▄▄▄▄▄▄▄▄▄|  ]
```
Shows stage + elapsed time, setpoint vs actual, heater power bar with %, cooling fan bar with %, individual TC readings, and a mini temperature bar at the bottom (filled = actual, vertical line = setpoint). All temperature values respect the unit selected in **Settings** and display with a `C` or `F` suffix.

### Live Plot View
```
Plot SP:150C (or 302F in Fahrenheit mode)
┌────────────────────────────┐
│     ∙ ∙∙ ∙∙ ∙             │
│    ∙∙ ∙ ∙ ∙ ∙∙ ∙∙ ∙∙     │
│   ∙∙ ∙   ∙   ∙   ∙  ∙∙   │
│  ∙∙           ∙     ∙ ∙   │
│ ∙∙                  ∙∙    │
│ ∙                    ∙∙   │
│∙                      ∙   │
└────────────────────────────┘
```
Full-screen temperature graph showing actual vs setpoint over time. Toggle with **F3 (LEFT) / F4 (RIGHT)** during a run. The setpoint line appears as a dashed row of pixels. X-axis scrolls with new data (8-minute window, 480 samples at ~2s intervals).

### Calibration Info View (PID Gains page)
```
= Cal PID Info =  S:Nxt
R1/3:LeadFree
Z0 H:1.2/.50/.67 C:.9/.11/.22
Z1 H:1.3/.50/.70 C:.9/.12/.23
Z2 H:1.4/.55/.75 C:1.0/.12/.24
Z3 H:1.5/.60/.80 C:1.0/.13/.25
Z4 H:1.6/.65/.85 C:1.0/.13/.26
^v:Rec <:Back
```
Recipes are scrolled with **F1 (UP) / F2 (DOWN)**. Press **S** to cycle to next page.

### Calibration Info View (Cal Data page)
```
== Cal Data ==      S:Nxt
Heat(C/s):
1.2 1.5 1.8
2.1 2.4
Cool(C/s):
0.8 1.1 1.3
1.5 1.7
DT H:1234ms C:5678ms
```
Shows heating/cooling rates per zone (in °C/s) and dead times (in ms) from the last profile calibration test.

### Calibration Info View (Recipe Temps page)
```
= Recipe Temps =   S:Nxt
R1/3:LeadFree
Pre:150C Soak:180C
Peak:220C
Ramp:90s Soak:60s
Reflow:45s Hold:10s
^v:Rec <:Back
```
Shows recipe temperature setpoints (in user-selected °C or °F) and timing parameters. Press **S** to cycle to PID Gains page.

### Plot Axes
- X-axis: up to 480 samples (rolling ~8 minutes)
- Y-axis: 0–280°C
- Setpoint line: dashed row of pixels
- Actual temperature: connected line segments

## Maintenance

### Calibration
Thermocouple offsets (TC1 and TC2) are stored in NVS and applied automatically at startup. To calibrate:
1. Place a calibrated temperature probe next to the oven TC
2. Navigate to **Calibration → TC Offset Info** for instructions
3. Offsets are adjusted by modifying `g_settings.tc1Offset` / `g_settings.tc2Offset` in the source or via the calibration info screen

### Sensor Check
If the display shows "SENSOR FAULT":
- Verify K-type thermocouple connections to the ADS1015
- Check for open circuits or damaged TC wires
- Ensure ADS1015 power (3.3V) is stable

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| No display | LCD wiring issue | Check D0–D7, RS, E, CS1, CS2 connections |
| Temp reads 0°C | TC disconnected | Check thermocouple wiring |
| Temp reads erratic | Noise on I2C lines | Shorter wires, add pull-ups |
| Oven not heating | SSR wiring or ZC signal | Check SSR gate and zero-cross wiring |
| Cycle stops early | E-STOP triggered | Check for over-temp or sensor fault |
| Buzzer constant | E-STOP alarm active | Press S to acknowledge |
| Recipe won't save | NVS full or corrupted | Reflash firmware to erase NVS |
| Wrong line frequency | Mains missing at startup | Check ZC input signal; fallback is 50Hz |

## Technical Specifications

- **Control period**: ~2.13s (60Hz) / ~2.56s (50Hz) — auto-detected at startup
- **UI refresh**: ~30ms
- **Power resolution**: 256 steps (Bresenham over 256 half-cycles)
- **Temperature resolution**: ~2°C (12-bit ADC with K-type TC)
- **ADC range**: ±4.096V (ADS1015)
- **Fan PWM**: 25kHz, 8-bit resolution
- **Line frequency**: Auto-detected 50Hz or 60Hz via ZC interrupt timing
- **Recipe storage**: 10 recipes in NVS
- **PID zones**: 5 stages per recipe (PREHEAT/SOAK/REFLOW/COOLDOWN/IDLE)
- **Feedforward**: Ramp-rate based, capped at 80% of max output
- **Fine-tuning**: Per-profile, per-zone ±5% conservative adjustment, sequence-numbered precedence
- **Temperature filtering**: EMA (alpha=0.15) on both thermocouples + filtered derivative in PID

## Firmware Version

Current version: **v1.11.0**

Version is defined in `main/Config.h` and appended to the firmware binary automatically by the `rename_firmware.py` CMake POST_BUILD step (runs after every `idf.py build`). Version and line frequency are shown on the GLCD splash screen, and also logged via serial monitor (`ESP_LOGI` output).
