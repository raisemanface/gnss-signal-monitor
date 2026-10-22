English | [简体中文](README.md)

# GNSS Signal Monitor Bench

A 2.4-inch touchscreen that tells you, in real time, whether the satellite signals overhead are **actually trustworthy right now**.

This is not another "it can get a fix" GPS module demo — it computes the error bound, watches for slow drift, identifies multipath, and writes every integrity-state change into a power-fail-safe event log.

> The hardware is off-the-shelf modules. The software is where all the value of this bench lives.

---

## 1. The problem it solves

An ordinary GNSS module outputs two things: a position, and "I have a fix."

In the real world, **"having a fix" and "being able to trust that fix" are two different things**. A cold-start first fix, half the sky blocked by buildings, or reflections off window glass can all produce a solution with `fix=1` that is hundreds of meters off — and whatever consumes that data has no idea it is being fed garbage.

This bench turns "trustworthiness" into a single word on screen that you understand at a glance, plus a set of traceable numbers.

| What you see | Meaning |
|---|---|
| `INTEG-OK` (green) | Solution is within the error bound — usable |
| `DRIFT` (amber) | Solution hasn't crossed the line, but the offset is growing slowly and monotonically — stale baseline suspected |
| `HPL-HIGH` (purple) | The error bound itself is too large (few satellites / weak signals) — the "accuracy" at this moment is meaningless |
| `POS-BAD` (red) | Offset has crossed the threshold right now — an established fact, not a trend |
| `SIG-BAD` / `LOST` | Signal or fix has been lost |

---

## 2. The two boards

```
        sky
         │  NMEA (UART)
         ▼
  ┌──────────────┐      UART crossover       ┌──────────────┐
  │  ESP32-S3    │◄────── protocol P2 ──────►│  ESP32-C3    │
  │  bench panel │    heartbeat / ack /      │ handheld     │
  │  2.4" touch  │    event forwarding       │ command pad  │
  │  GNSS + RH&T │                           │ menu + kbd   │
  └──────────────┘                           └──────────────┘
```

**S3 = bench panel.** Connects to the GNSS module and the temperature/humidity sensor; runs all the algorithms and the UI. It is the end that "sits on the ground and watches" — mounted on a 3-meter pole, out of arm's reach.

**C3 = handheld command pad.** One UART cable over, and from the ground you can set the baseline, check status, browse events, and configure WiFi. Its screen is the same size as the S3's, but it only does menus and input — typing on a keyboard held in your hand beats reaching up a pole.

The two boards exchange heartbeats (`ping`/`pong`). If the link dies, the C3 shows it — you will never find yourself tapping a dead menu.

---

## 3. Hardware

| Part | Model | Notes |
|---|---|---|
| Main controller | ESP32-S3 (16 MB flash / 8 MB OPI PSRAM) | `CDCOnBoot=cdc`, custom partition table |
| Display | 2.4″ ILI9341 240×320 SPI + resistive touch | Same model on both boards |
| GNSS | ATGM336H-5N | UART 9600, NMEA 0183 |
| Temp/RH | SHT30-D | I²C; supports multipath interpretation |
| Command pad | ESP32-C3 dev board (DevKit class) | On-board USB-UART bridge (flashing port) + native USB (console port) |

Wiring — including the fixed color sequence of ribbon/DuPont cables and the rule that signal–ground–signal runs must be taken from adjacent positions on the cable — is in [`docs/接线表.md`](docs/接线表.md) (Chinese; pin tables are language-independent).

> ⚠️ Breadboard + DuPont jumpers are a known-unreliable layer. In this project, many "the chip is dead" verdicts eventually traced back to bad contact. Buzz everything out with a multimeter before you solder.

---

## 4. The three algorithms

All run on the S3 at 1 Hz in real time, with the intermediate quantities visible on screen — it never just hands you a verdict.

### 4.1 Horizontal protection level (HPL)

```
HPL = 5.33 × HDOP × σ_UERE
σ_UERE is back-derived from C/N0: σ = 0.6 m @ 45 dB-Hz, degrading with signal strength
```

The `5.33` constant combines the horizontal/vertical error ratio with the integrity-risk multiplier. HPL answers **"how big is the error circle I can guarantee"** — if the offset hasn't exceeded it, no anomaly should be raised.

> When picking σ, take the strongest N satellites by C/N0. **Do not use the GSA PRN slots** —
> multiple `$GNGSA` sentences, sentence names always `GN`, PRN numbers reused across constellations.
> Following GSA produces σ ≈ 8.7 m and HPL permanently around 40 m with false alarms
> (measured in this project).

### 4.2 Slow drift (CUSUM)

Sudden jumps in the offset are easy to catch; **slow drift is hard**: a 0.05 m/s ramp walks 30 m in 10 minutes while every single epoch looks normal.

Use a cumulative sum: `S += (dev − k)`; raise an alarm when `S` crosses its threshold.
- μ = slow baseline (τ ≈ 5.5 min), not the current value
- The statistic has an 8 m absolute gate, preventing "the baseline drifting along"
- **For the first 90 s, only μ is built; the statistic is locked at 0** — cold-start data is not trustworthy,
  and using it as the baseline makes the device "drift from the moment it powers on"

> Measured conclusion (contrary to the original design intent): under ramps of 0.02–0.4 m/s,
> **the 8 m absolute gate trips first and CUSUM confirms afterwards**. So the CUSUM here is a
> second gate, not an early warning. `tools/cusum_replay.py` reproduces this table offline.

### 4.3 Multipath identification

A C/N0-vs-elevation template is built per satellite; a reading below the template by more than a threshold counts as multipath. The template coefficients are updated online with recursive least squares (RLS), and a forgetting factor lets the template follow seasons and growing canopies.

---

## 5. UI

Main screen (starfield particle background + live quantities in the four corners + data bar):

```
┌──────────────────────────────┐
│ sats used/visible   HDOP      │
│                              │
│      ·  ·   ·                │
│   ·   ·  ·    ·   ← starfield│
│  ·   ·   ·  ·                │
│                              │
│ lat/lon           temp/RH    │
├──────────────────────────────┤
│ INTEG-OK   OFF 12.4m  HPL 15m│
│            CN0 31   SAT 16/26│
├──────────────────────────────┤
│  CHARTS  │  LOG  │  TUNE  │  │
└──────────────────────────────┘
```

- **Hold the data bar 900 ms** → the bottom swaps to the last 8 raw NMEA sentences
- **Hold CHARTS 1.5 s** → parameter page (8 tunable thresholds, saved across power cycles)
- **Tap BASE** → set the baseline with a 10 s sample average (two admission gates, below)
- **Top-right of main screen** → WiFi card, shows the IP
- Six pages: main / charts / log / events / parameters / parameter editing

The charts page has four live cards: per-satellite bars, error trend, integrity, detectors — every one of them moves, proving the math is really running.

---

## 6. One pitfall worth its own section

**The baseline set by BASE is written to flash permanently, and every boot thereafter uses it as the ruler.**

An old version's admission check was only `fixValid` — but "having a fix" is not "having an accurate fix". A cold-start first fix, or multipath next to a wall, can produce a solution with `fix=true` that is 200 m off; one tap and the bad ruler is welded in, presenting as "the device has permanently drifted 200 m and a reboot won't clear it".

The current two gates:

```
P1 quality thresholds: fixValid + sats ≥ 6 + HDOP ≤ 2.0 + fix fresher than 3 s
P2 sample average: then sample 10 s at 1 Hz; ≥ 8 samples and range ≤ 25 m before the mean is written
```

**For any reference value that gets written to flash, its admission check is that value's accuracy ceiling.**
Validating "is it there" but not "is it accurate" permanently bakes in random error — and the symptom will masquerade as some other component failing.

The UI shows both `base set` and `base new`: the former is the old baseline, the latter the one just taken. When `off` reads 266 m, you must be able to tell "the ruler is stale" from "the device is truly broken" — the two call for opposite actions.

---

## 7. Build & flash

### 7.1 Dependencies

- Arduino ESP32 core **3.3.8**
- **TFT_eSPI 2.5.43**

TFT_eSPI's pin configuration does not live in this repo and only takes effect inside the library directory — copy the two headers from `hardware/` into the library folder:

```
<Arduino>/libraries/TFT_eSPI/User_Setup_GNSS_Bench.h   ← for the S3
<Arduino>/libraries/TFT_eSPI/User_Setup_C3_CmdPad.h    ← for the C3
```

Then point the `#include` in `TFT_eSPI/User_Setup_Select.h` at the matching one.
**This layer is the classic source of "it compiled fine and the screen doesn't light a single pixel"** —
with a missing font, there is no error and no crash; nothing is drawn, that's all.

### 7.2 Command line

```bash
# S3
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=custom" \
  --build-path "$TEMP/gnss-build" --output-dir "$TEMP/gnss-out" firmware/GNSS_Bench

# C3
arduino-cli compile --fqbn "esp32:esp32:esp32c3" \
  --build-path "$TEMP/c3-build" --output-dir "$TEMP/c3-out" firmware/C3_CmdPad
```

> **Never point `--build-path` at a cloud-synced folder (OneDrive/Dropbox/Baidu Netdisk etc.).**
> The linker stage will throw pages of `undefined reference` — even `millis` — because the sync
> daemon rewrites cache files mid-build. (Measured, not hypothetical.)

### 7.3 Flash without compiling

`firmware/prebuilt/` contains the current binaries for both boards. For the S3, flashing only the app (0x10000) keeps calibration, the WiFi list, the event log, the baseline and all parameters:

```bash
esptool.py --chip esp32s3 --port COM4 write_flash 0x10000 firmware/prebuilt/S3/GNSS_Bench.ino.bin
```

The C3 needs a full-image flash (mandatory when switching boards, otherwise stale NVS data will clash).
The full address table is in [`docs/台架搭建.md`](docs/台架搭建.md) (Chinese).

---

## 8. Serial console

USB to the S3, 115200 baud. Type `?` to print all commands (17).

Over the UART link, what the C3 can remotely trigger is limited to a **single-key whitelist of 8** —
a deliberate security boundary: anything reachable from the bottom of the pole must be something whose effect you can see coming.

---

## 9. Known limitations (an honest list)

- **Mobile use unverified**: all thresholds were tuned on a fixed bench; no dynamic scenarios were tested
- **Multipath criterion not compared against ground truth**: only "the threshold fires" was verified; no labeled dataset, no accuracy figure
- **CUSUM is not an early warning** (see 4.2) — do not expect it to beat the plain threshold
- **Not a spoofing detector**: single-frequency, single-constellation hardware cannot distinguish "signal is forged" from "multipath is severe"
- The UI is custom-laid-out for 240×320; switching displays means re-laying-out everything

---

## 10. Repository layout

```
firmware/
  GNSS_Bench/      S3 bench panel (main controller)
  C3_CmdPad/       C3 handheld command pad
  prebuilt/        flash-without-compiling binaries (S3 / C3)
hardware/          TFT_eSPI pin configurations (one per board)
docs/
  接线表.md         pin tables + ribbon-cable color-sequence constraints
  台架搭建.md       build from scratch / compile / flash / troubleshooting
  设计规则.md       UI, redraw, interaction and algorithm design constraints, with the "why"
tools/
  cusum_replay.py    offline CUSUM replay (reproduces the section 4.2 conclusion)
  check_proto_sync.py two-board protocol-version consistency check
```

---

## License

Code: [Apache License 2.0](LICENSE)

Hardware wiring and documentation: CC BY 4.0

The `MANFACE STUDIO` mark in the boot animation is this project's own signature, not a third-party trademark.

---

## Acknowledgements

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — rendering and fonts
- NMEA field definitions from the ATGM336H datasheet (Unicore)

> The NMEA parser, SHT30 driver, starfield background and integrity algorithms are all written in this repo.
> Apart from TFT_eSPI and the ESP32 core, there are no other third-party library dependencies.
