# Build instructions: LATTICE, a melodic lane sequencer for VCV Rack

**Audience:** an Opus model implementing this plugin from scratch in this repository (`hardyfinn/WA`).
**Owner:** Haer. Haer is fluent in VCV Rack but does **not** read C++. Haer checks each milestone by building and listening in Rack, using the checklists in this file. Write everything a C++ reader would usually be trusted to infer.

**What it is:** one large module, **LATTICE** (48 HP), and one expander, **LATTICE-X** (14 HP). LATTICE is a 3-track melodic sequencer. Every parameter of a step (gate, note, octave, chord, velocity, length, ratchet, two mod CVs) lives on its own **lane** with its own length, clock rate and direction, so the parameters drift against each other. On top of that sit seven newer ideas: pinned dice, roles and an arc, morph space, rule-based generation, call and answer, listening lanes, and gesture-to-lane.

**Design history:** the design draft (Claude Doc) is at https://claude.ai/artifact/VbeKQeyo98TGPJ6YUqodtm. Where it disagrees with **this** file, **this file wins**. Haer's decisions of 2026-11-14 that shaped this file:

| Question | Haer's answer |
|---|---|
| Which new ideas | 1 pinned dice, 3 roles and arc, 4 morph space, 5 rules, 6 call and answer, 7 listening lanes, 8 gesture to lane. **Not** 2 (zoom steps). |
| Content | Melodic content only. No drum mode. |
| Size | 3 tracks, 9 lanes per track. |
| Chords | Polyphonic output per track. |
| Form | One large module plus an expander. |

---

## 0. Ground rules for the implementer

1. **Build in milestones (§14).** At the end of each one: the plugin compiles with no warnings at `-Wall -Wextra`, all unit tests pass, and you hand Haer a short note: what to install, what to patch, and what they should hear, taken from that milestone's "Haer's checklist".
2. **Keep the engine separate from Rack.** All sequencing logic lives in `src/engine/` as plain C++17 with no Rack includes. The Rack `Module` and `ModuleWidget` classes in `src/modules/` are thin wrappers that feed the engine clock edges, CV values and edit commands, and read its outputs. This is what makes unit testing possible.
3. **No allocation, locks, file I/O or logging in `process()`.** Allocate every buffer in the constructor. Heavy work (rule-based generation, call-and-answer, gesture fitting) runs on a worker thread (§4.3).
4. **All randomness is seeded and repeatable** (§3.3). The same patch with the same seeds, clock and edits gives identical output.
5. **Every parameter has a tooltip, a unit and a sensible default.** Every port has a description (`configInput(id, "Name")`, `configOutput`). Right-click menus explain what an option does.
6. **Don't invent features.** If the spec is ambiguous, pick the simplest reading, write it in `docs/DECISIONS.md`, and tell Haer at the milestone hand-off.
7. **CPU budget:** LATTICE with all three tracks running, morph on and the expander attached stays under 2% of one core at 48 kHz on a mid-range 2020 laptop (Rack: View → Show CPU meter).
8. Don't copy code from other plugins unless their licence is compatible with GPL-3.0-or-later. If you do, credit it in `docs/CREDITS.md`.

---

## 1. Setup

### 1.1 Toolchain
- Target **VCV Rack 2.x** with the newest 2.x Rack SDK from https://vcvrack.com/downloads/. Read https://vcvrack.com/manual/PluginDevelopmentTutorial and https://vcvrack.com/manual/Building first.
- `export RACK_DIR=/path/to/Rack-SDK`, then `make`, `make dist`, `make install`.
- Unit tests use **doctest** (single header, MIT) in `tests/`. Add a `make test` target that builds `tests/*.cpp` together with `src/engine/*.cpp` as a normal host executable (no Rack) and runs it.

### 1.2 Repository layout
The repository was empty when this file was written. Create:

```
WA/
  plugin.json
  Makefile                  # standard SDK Makefile + `test` target
  README.md                 # one paragraph + install steps for Haer
  res/
    Lattice.svg  LatticeX.svg
  src/
    plugin.hpp / plugin.cpp
    engine/                 # pure C++17: everything in §3–§12
      Types.hpp             # Step, Lane, Track, Pattern, Song structs
      Scale.hpp/.cpp        # scales, chords, quantiser
      Rng.hpp               # xoshiro128** + splitmix64 + hash
      Transport.hpp/.cpp    # clock following, swing
      LaneRunner.hpp/.cpp   # lane position, direction, conditions, pins
      TrackEngine.hpp/.cpp  # combines lanes into notes and CVs
      Generator.hpp/.cpp    # character GEN, drift
      Rules.hpp/.cpp        # constraint solver
      Answer.hpp/.cpp       # call and answer
      Morph.hpp/.cpp        # morph space
      Gesture.hpp/.cpp      # gesture recording and fitting
      Matrix.hpp/.cpp       # cross-modulation
      Commands.hpp          # UI → engine command queue
      Serialize.hpp/.cpp    # JSON via jansson-compatible interface (see §13)
    modules/
      Lattice.cpp  LatticeX.cpp
    widgets/
      LatticeDisplay.hpp/.cpp   # the main screen
      EncoderKnob.hpp/.cpp      # endless knobs (§2.3)
  tests/
    test_main.cpp  test_*.cpp
  docs/
    design/BUILD-INSTRUCTIONS.md   # this file
    MANUAL.md  DECISIONS.md  CREDITS.md  PATCHES.md
```

### 1.3 plugin.json
- `slug`: `"Wa"`, `name`: `"Wa"`, `version`: `"2.0.0"`, `license`: `"GPL-3.0-or-later"`, `author`: `"Haer"`.
- Modules: `Lattice` ("LATTICE", tags `"Sequencer"`, `"Polyphonic"`, `"Random"`, `"Quantizer"`) and `LatticeX` ("LATTICE-X", tags `"Expander"`, `"Sequencer"`).
- "LATTICE" is a working name. Ask Haer at the first hand-off whether to keep it; renaming only touches display names, not slugs, if done before release.

### 1.4 Panels
- Hand-made SVGs with a hidden **`components` layer** of colour-coded placeholders (red `#ff0000` params, green `#00ff00` inputs, blue `#0000ff` outputs, magenta `#ff00ff` lights, yellow `#ffff00` custom widgets). Generate starting code with `$RACK_DIR/helper.py createmodule Lattice res/Lattice.svg src/modules/Lattice.cpp`.
- Convert all text to paths (Rack does not render SVG text), e.g. `inkscape --export-text-to-path`. If Inkscape is unavailable, draw labels with NanoVG in a widget.
- Style: panel `#1d1d1f`, text `#d8d8d8`. One accent colour per track used everywhere that track appears (buttons, display, jack rings): **track 1 amber `#e8a33c`, track 2 teal `#3cc8b4`, track 3 violet `#9b7be8`**. Lane colours on the display are shades of the track colour.
- Use the standard component library (`RoundBlackKnob`, `RoundSmallBlackKnob`, `Trimpot`, `PJ301MPort`, `VCVButton`, `VCVLightButton`, `CKSS`, small lights) except for the endless step knobs (§2.3).

---

## 2. Concepts and conventions

### 2.1 Signals

| Signal | Range | Rule |
|---|---|---|
| Gate / trigger | 0 / 10 V | Output triggers last 1 ms (`dsp::PulseGenerator`). Inputs use a Schmitt trigger: low 0.1 V, high 1 V. |
| Pitch | 1 V/oct, 0 V = C4 | |
| Velocity | 0 to 10 V | |
| Mod CV | −5 to +5 V | |
| Arc, morph X/Y, density CV | 0 to 10 V → 0 to 1 | Added to the knob value, then clamped. |
| Transpose | 1 V/oct | Rounded to semitones, then re-quantised to the track scale (§5). |

### 2.2 Vocabulary
- **Track:** one of 3 melodic voices. Each has its own outputs.
- **Lane:** one parameter row of a track. There are 9 lanes, always in this order: `Gate, Note, Octave, Chord, Velocity, Length, Ratchet, ModA, ModB`.
- **Step:** one cell of a lane. Every lane stores **32** steps. Only the steps in its **loop** play.
- **Base step:** one tick of the track clock (by default a 16th note). Lane clock ratios are relative to it.
- **Pattern:** the full contents of one track (9 lanes, their settings, step extras, roles, rules, answer settings, matrix slots, scale). Each track has **16 pattern slots**.
- **Kept pattern:** a pattern slot written by KEEP. Morph space works on kept patterns.
- **Bar:** 16 base steps.

### 2.3 Endless step knobs
The 16 step knobs edit whichever lane, step or page is selected, so they must not hold an absolute position. Implement `EncoderKnob`: a `Knob` subclass (draw it like `RoundSmallBlackKnob` with a ring of 16 small segment lights drawn in NanoVG) whose drag produces **deltas**. On drag, convert mouse movement to detents (one detent per 8 px; Shift held = fine, one detent per 32 px; Ctrl held = coarse ×4) and push an `Edit` command (§4.2) with the delta. The ring shows the current value of whatever the knob edits, read from the display snapshot. These knobs are not Rack params, so they are not MIDI-mappable; note this in the manual. The 4 lane-setting knobs (§10.4) are also `EncoderKnob`s.

---

## 3. Data model (`src/engine/Types.hpp`)

### 3.1 Step
```cpp
enum class Cond : uint8_t { Always, AofB, First, NotFirst, Fill, NotFill, Pre, NotPre };
enum class Role : uint8_t { Anchor, Pulse, Passing, Tension };
enum class Pin  : uint8_t { None, Fired, NotFired };

struct Step {
  float   value   = 0;     // meaning depends on lane (§3.2)
  float   alt     = 0;     // alternate value used when the step's condition or probability fails
  bool    hasAlt  = false;
  uint8_t prob    = 100;   // 0..100 %
  Cond    cond    = Cond::Always;
  uint8_t condA = 1, condB = 1;   // for AofB: plays on pass A of every B (1 ≤ A ≤ B ≤ 8)
  bool    slide   = false; // Note, ModA, ModB only
  bool    accent  = false; // Gate only
  int8_t  nudge   = 0;     // Gate only: −49..+49 % of a gate-lane step
  Role    role    = Role::Pulse; // Gate only
  Pin     pin     = Pin::None;
};
```

### 3.2 Lane value meanings

| Lane | `value` range | Default | Notes |
|---|---|---|---|
| Gate | 0 or 1 | 0 | 1 = step on |
| Note | integer 0..15 | 0 | scale degree above the track root |
| Octave | integer −3..+3 | 0 | |
| Chord | integer 0..8 | 0 | index into chord shapes (§5.3); 0 = single note |
| Velocity | 0..10 V | 7.5 | |
| Length | 1..400 % | 50 | of one gate-lane step; ≥ 100 % ties into the next gate (legato) |
| Ratchet | integer 1..8, plus ramp | 1 | ramp stored in `alt` is not used here; store ramp as `value = count + 10*ramp` where ramp 0 flat, 1 up, 2 down. Document the packing in code. |
| ModA, ModB | −5..+5 V | 0 | `slide` = glide to this value |

Each lane's value is also needed **normalised to 0..1** for the matrix (§9) and morph (§8): Gate 0/1; Note /15; Octave (v+3)/6; Chord /8; Velocity /10; Length (v−1)/399; Ratchet (count−1)/7; Mod (v+5)/10.

### 3.3 Lane settings
```cpp
enum class Dir : uint8_t { Forward, Reverse, Pendulum, Random, Drunk, Skip };
enum class ClockSrc : uint8_t { Track, GateOfTrack1, GateOfTrack2, GateOfTrack3, ExtTrig, Onset, Threshold }; // §11

struct LaneSettings {
  uint8_t  start   = 0;    // 0..31
  uint8_t  length  = 16;   // 1..32; the loop wraps inside the 32 stored steps: index = (start + k) % 32
  uint8_t  ratio   = 4;    // index into RATIOS (below); 4 = ×1
  Dir      dir     = Dir::Forward;
  uint8_t  skip    = 3;    // for Dir::Skip: interval 2..7
  ClockSrc clock   = ClockSrc::Track;
  uint8_t  extDiv  = 1;    // for event sources: advance every N events, 1..16
  bool     glide   = false;// ModA/ModB: glide on every step, not only slide steps
};
```

`RATIOS` (lane steps per base step): `×4, ×3, ×2, ×3/2, ×1, ×2/3, ÷2, ÷3, ÷4, ÷5, ÷6, ÷7, ÷8, ÷12, ÷16`, i.e. `{4, 3, 2, 1.5, 1, 2/3., 1/2., 1/3., 1/4., 1/5., 1/6., 1/7., 1/8., 1/12., 1/16.}`. Display them with those labels.

Default lane lengths for a new pattern: all 16, all ×1, all Forward. (Haer will make them unequal; GEN can too, §7.1.)

### 3.4 Track pattern
```cpp
struct Pattern {
  Step         steps[9][32];
  LaneSettings lanes[9];
  // track-wide
  uint8_t root = 0;              // 0..11 semitones above C
  uint8_t scale = 0;             // 0..27 (24 presets + 4 user)
  bool    followT1 = false;      // tracks 2 and 3 only (§5.4)
  uint8_t swing = 50;            // 50..75 %
  uint64_t seed = 1;             // GEN / drift seed, 0..999 shown
  RulesConfig  rules;            // §7.3
  AnswerConfig answer;           // §7.4 (tracks 2 and 3)
  MatrixSlot   matrix[4];        // §9
  uint32_t version = 0;          // incremented on every change; drives Answer recompute
};
```

### 3.5 Track state (not saved in the pattern)
`TrackSettings` (saved in the patch, not per pattern): current pattern slot, queued slot, mute, glide time (0..2 s, knob), poly channel mode (Auto or 1..4), sync mode (§6.5), pattern-change quantise (§10.6), morph on/off and morph map (§8), keep-slot locks, drift amount, density, spread, arc knob value.

`TrackRuntime` (never saved): transport position, lane runners, pass counters, current values, output state, freeze buffer, pins-log, undo ring.

### 3.6 Song
`Song { Row rows[64]; uint8_t count; }` where `Row { int8_t pattern[3]; /* −1 = keep current */ uint8_t repeats; /* 1..64, 0 = until NEXT trigger */ }`. 16 songs. A song row ends when **track 1's** pattern-change boundary (§10.6) has passed `repeats` times.

---

## 4. Threads, commands and the worker

### 4.1 Ownership
The **audio thread owns all pattern data.** The UI never writes patterns directly.

### 4.2 UI → engine commands
`Commands.hpp` defines a fixed-size single-producer single-consumer ring buffer (1024 entries) of `Command` structs: `{ type, track, lane, step, field, int delta, float value, void* payload }`. Types include `Edit` (encoder delta on a field), `SetField`, `ToggleGate`, `SelectPattern`, `Queue`, `Keep`, `Undo`, `Gen`, `Pin*`, `CopyLane`, `PasteLane`, `ApplyPattern` (payload = pointer to a worker-produced `Pattern` from the pool, §4.3), etc. `process()` drains the queue at the start of each call (max 64 per call).

### 4.3 Worker thread
One `std::thread` per LATTICE instance, started in the constructor and joined in the destructor. It handles: rule-based GEN (§7.3), call-and-answer (§7.4), gesture fitting (§12), and JSON pattern export. Jobs arrive through a single-slot atomic mailbox per job type; results are written into a **pool of 8 preallocated `Pattern` buffers** and handed to the audio thread as an `ApplyPattern` command (the worker may push to its own second SPSC queue that `process()` also drains). The audio thread copies the pattern in at the next lane-step boundary of the target track and returns the buffer to the pool.

### 4.4 Engine → UI snapshot
After each `process()` block of 64 samples, the engine writes a `DisplaySnapshot` (selected track's lanes, positions, current values, pins, roles, page state, messages) into one half of a double buffer and flips an atomic index. `LatticeDisplay` reads the other half. No locks.

---

## 5. Pitch: scales, chords, follow (`Scale.hpp`)

### 5.1 Scales
24 presets, as semitone sets: Major, Natural minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic minor, Melodic minor, Major pentatonic, Minor pentatonic, Blues (0 3 5 6 7 10), Whole tone, Chromatic, Hirajoshi (0 2 3 7 8), In-sen (0 1 5 7 10), Pelog approx. (0 1 3 7 8), Hungarian minor, Phrygian dominant, Double harmonic, Diminished half-whole, Diminished whole-half, Lydian dominant, Altered. Plus **4 user scales**, edited on the SCALE page as 12 on/off toggles (at least one note must stay on).

### 5.2 Degree to pitch
With a scale of `n` notes `S[0..n-1]`:
`semis(d) = 12*floor(d/n) + S[d mod n]` (use floor division so negative degrees work).
Final pitch of the root note of a step:
`v = (root + semis(degree) + 12*octave + transposeSemis) / 12` volts, where `degree = noteLane + matrixTranspose + followOffset`, then **re-quantise** `v` to the nearest note of the scale (ties go down) only if `transposeSemis ≠ 0`. `transposeSemis = round(transposeInput * 12)`.

### 5.3 Chords
Chord shapes are stacks of **scale degrees** added to the step's degree, so chords stay in key:

| # | Name | Degrees |
|---|---|---|
| 0 | Single | 0 |
| 1 | Fifth | 0 4 |
| 2 | Triad | 0 2 4 |
| 3 | Seventh | 0 2 4 6 |
| 4 | Sus | 0 3 4 |
| 5 | Cluster | 0 1 2 |
| 6–8 | User 1–3 | up to 4 degrees, −7..+14, edited on the SCALE page |

(This is 3 user shapes, not 4 as the draft said, to keep the Chord lane at 9 values. Note it in DECISIONS.md.)

Channel `c` of the track's Pitch output gets degree `degree + shape[c]`. **Poly channel count:** right-click option per track, `Auto` (default: the largest chord size used anywhere in the current pattern's Chord loop) or fixed 1–4. Channels beyond the current chord's size: gate low, pitch held at their last value.

### 5.4 Follow track 1
For tracks 2 and 3 with `followT1` on: `followOffset = track 1's current degree` (its Note lane value plus its matrix transpose, without octave) **minus track 1's Note value at its loop start**. So the follower moves diatonically with track 1's melody but sits still when track 1 is at its first note. Follow uses the follower's own scale and root.

---

## 6. Time: transport and lanes (`Transport`, `LaneRunner`)

### 6.1 Main clock
- **CLOCK input patched:** right-click "Clock input resolution": 1 pulse per base step (default), 2, 4, or 24 PPQN / 48 PPQN (= 6 / 12 pulses per base step).
- **No clock cable:** internal clock from the **BPM knob** (30–300, default 120; base step = 16th).
- **RUN** button + RUN gate input (toggle on trigger; right-click option "Run input is a gate").
- **RESET** button + input: at the next clock pulse (or immediately when stopped), set every transport position to 0, all lanes to loop start, all pass counters to 0, clear freeze. Pending queued patterns change at the same moment.

### 6.2 Continuous transport
Each track keeps a transport position `T` in base steps (double). Between clock pulses, `T` advances each sample by `1 / (periodSamples)` where `periodSamples` is the average of the last two pulse intervals converted to base steps. At each pulse, set `T = pulseCount / pulsesPerStep` (snap). Never let `T` run past `lastSnap + 1/pulsesPerStep`; if it gets there before the next pulse, hold. This lets ×2 to ×4 lanes and negative nudges work between pulses.

**Track clock inputs** (one per track): when patched, that track's `T` follows its own clock instead, using the same rules.

### 6.3 Swing
Per track, 50–75 %. Apply to `T` before the lanes see it, as a piecewise-linear warp over each pair of base steps: with `s = swing/100`, the first base step of each pair stretches to `2s` of the pair and the second shrinks to `2(1−s)`. All lanes on the track see the warped time.

### 6.4 Lane position and direction
For a lane with ratio `r`, its counter is `k = floor(Tw * r * spreadFactor) + matrixOffset` (spread §10.3, offset §9) for **clock-driven** lanes. For **event-driven** lanes (§11), `k` increments by 1 on every `extDiv`-th event instead.

When `k` changes, the lane moves to its next **position** `i` in `0..length-1`, then plays stored step `(start + i) % 32`:
- **Forward:** `i = k mod L`. **Reverse:** `i = L−1 − (k mod L)`.
- **Pendulum:** period `2L−2` (L ≥ 2; L = 1 → 0). `j = k mod (2L−2)`, `i = j < L ? j : 2L−2−j`.
- **Skip:** `i = (k * skip) mod L`.
- **Random:** `i = rng.uniformInt(L)`; never the same as the previous `i` when L > 1.
- **Drunk:** `i = clamp-wrap(prev ± 1)` with equal chance, wrapping at the ends.

A **pass** is counted each time `i` returns to 0 (Forward, Skip, Pendulum), to L−1 (Reverse), or every L moves (Random, Drunk). Pass counters drive AofB conditions and the pinned-path feature.

**Look-ahead:** every lane decides its next position and evaluates that step's probability and condition **one step early** (when it arrives at step k, it already resolves step k+1). This is needed for negative nudge (§6.6) and makes pins well defined. If a matrix action changes the lane between decision and arrival, re-decide.

A lane jumped by more than one step in one sample (e.g. ×4 at audio-rate clock) processes each skipped step's decision in order, max 8 per sample.

### 6.5 Sync mode (per track)
- **Free** (default): lanes never re-align. True polymeter.
- **Gate:** whenever the Gate lane completes a pass, all other lanes restart at position 0 (counters re-based).
- **Bars N:** every N bars (1–16), all lanes restart.

### 6.6 Nudge (Gate lane only)
A Gate step at lane counter k with nudge `n` % fires when `lanePos ≥ k + n/100`, where `lanePos = Tw * r` (continuous). Negative nudge fires before the step boundary, using the look-ahead decision.

---

## 7. Making notes (`TrackEngine`) and generating material

### 7.0 How lanes combine
When the **Gate lane fires** a step (after probability, condition, role/arc, density, mute — in that order, §7.0.1):
1. Read the **current value** of every other lane (the value at its own current position, after its own probability/condition/alt/pin handling). Lanes do **not** advance because of the gate; they advance on their own clocks.
2. Compute pitch per channel (§5), velocity (+ accent amount if accent), gate length = `Length% × (gate-lane step duration)`, ratchet count and ramp.
3. **Ratchets:** split the gate-lane step into `count` equal parts; each sub-gate has length `min(Length%, 95%)` of its part. Ramp up: velocity rises linearly from 40 % to 100 % of the step velocity; ramp down: the reverse.
4. **Ties:** if Length ≥ 100 % and the next gate fires before this one ends, keep the gate high and change pitch (legato). If the new Note step has `slide`, glide pitch over the track glide time.
5. **Pitch output** is sample-and-hold at gate time (default). Right-click option per track "Pitch follows Note lane" updates pitch whenever the Note lane moves, even without a gate.
6. **ModA / ModB** output continuously: they change when their own lane moves, gliding over the track glide time if the step's `slide` is on or the lane's `glide` setting is on.

#### 7.0.1 Step evaluation order (any lane)
1. If the step has `pin = Fired` → treat as passed; `pin = NotFired` → treat as failed. Skip 2–3.
2. Condition: `Always` passes; `AofB`: `(pass % B) == A−1`; `First`: pass == 0 since reset; `NotFirst`; `Fill`: FILL held; `NotFill`; `Pre`/`NotPre`: the previous condition evaluated on this lane passed/failed.
3. Probability: `rng.uniform() * 100 < prob` (FILL held adds the FILL amount, §10.2).
4. Gate lane only, if passed: role/arc (§7.2), then density (§10.3), then mute.
5. **Result:** passed → use `value`. Failed → Gate lane: silent; other lanes: use `alt` if `hasAlt`, otherwise **hold the previous value**.
6. Log the decision for pinning (§7.1.3).

### 7.1 Character GEN, KEEP, undo, pinned dice, drift (`Generator`)

#### 7.1.1 GEN
Controls: **CHARACTER** (0–1), **AMOUNT** (0–100 %), **SEED** (0–999, snapped), **GEN** button and GEN trigger input. GEN acts on the **selected lane**; **Shift+GEN** acts on all 9 lanes of the selected track. AMOUNT is the share of steps in the loop that are replaced (100 % = all; steps chosen by the seeded RNG; pinned steps are never changed). GEN uses `Rng::fork(seed, track, lane, genCount)` so each press gives new material, and pressing UNDO then GEN with the same seed and `genCount` reproduces it.

Character meanings (0 → 1):
- **Gate:** sparse → dense. Density `= 0.15 + 0.7c`. Steps on strong positions (0, 4, 8, 12 of 16) get weight 1.5, off-beats 1.0. Roles are assigned too: strong positions Anchor, other on-beats Pulse, the rest Passing; 10 % of on-steps become Tension, chosen among Passing ones.
- **Note:** stepwise → leaping. Random walk from degree 0..7; step size drawn from {±1} with weight `1−c` and {±2..±5} with weight `c`; clamp to 0..15.
- **Octave:** mostly 0; chance of ±1 is `0.4c`, ±2 is `0.1c`.
- **Chord:** chance of non-zero chord `0.5c`, shape uniform among 1–5.
- **Velocity:** flat → jagged. `7.5 V ± 2.5c` uniform, plus accents on Anchor steps.
- **Length:** short → long. Uniform in `[10, 10+190c]` %.
- **Ratchet:** chance of ratchet `0.3c`, count 2–4 (up to 8 when c > 0.8), ramp random.
- **ModA/ModB:** smooth → jagged. Smooth = a random sine-like curve over the loop (sum of 2 harmonics) with every step `slide`; jagged = independent uniform values, no slide.
- **Shift+GEN only:** with CHARACTER > 0.5, also randomise lane lengths: Gate keeps its length; other lanes get lengths drawn from {3, 5, 6, 7, 9, 11, 12, 13} so loops drift.

#### 7.1.2 KEEP and pattern slots
**KEEP** button and input (main panel) copy the selected track's **current state** into a pattern slot: the edit pattern, or the morph-rendered pattern if morph is on (§8), with answer lanes baked in (§7.4). It also saves every lane's current position as that lane's new `start`, so the slot plays from where it was kept. Slot choice: the lowest empty slot; if all 16 are full, overwrite the oldest **unlocked** slot (Shift+step on the PATTERN page toggles a lock). The display shows "KEPT → 7". **Shift+KEEP** keeps all 3 tracks at once.

#### 7.1.3 Pinned dice (idea 1)
Every chance decision is logged per step: whether its probability/condition passed, and for Random/Drunk lanes, which position was chosen.
- **PIN** (hold): while held, every decision that happens **on the selected track** is pinned: its step gets `pin = Fired` or `NotFired` to match what just happened. Release to stop pinning. LEDs: pinned steps show a dot on the display.
- **Shift+PIN:** clear all pins on the selected lane. **Shift+PIN held for 1 s:** clear all pins on the track.
- **Pinned path:** if a Random or Drunk lane completes a full pass while PIN is held, the visited order of positions is stored (32 bytes per lane) and the lane replays that order from then on. The display shows "PATH" on that lane. Clearing pins on the lane clears the path.
- On the STEP page (§10.5), each step's pin can be set by hand.
- Pins are part of the pattern (saved, kept, undone).

#### 7.1.4 Undo
8-level undo per track (a ring of preallocated `Pattern` copies). A snapshot is pushed before: GEN, Shift+GEN, rules GEN, answer bake, morph bake, gesture fit, clear, paste, drift HOME. UNDO restores the latest. Encoder edits are grouped: a snapshot is pushed when the first encoder edit happens after any change of track, lane or page.

#### 7.1.5 Drift and HOME
**DRIFT** knob per selected track (0–1). At every bar, with probability `drift`, choose one random unpinned step in the Gate, Note or Velocity loop and re-roll it with the character GEN rules. Changes are made to the playing pattern only; the **home** copy (the pattern as last edited or recalled) is kept. Cap: at most `50% × drift` of steps may differ from home; past the cap, a drift move restores one changed step instead. **HOME** button: return to the home copy at the next bar.

### 7.2 Roles and arc (idea 3)
Each Gate step has a `role`: Anchor, Pulse, Passing or Tension (set by GEN, on the STEP page, or with the ROLE page where step buttons cycle the role). Each track has an **ARC** value `a` (knob 0–1 + ARC CV input on the main panel).

A passed Gate step also needs its role to be "open" at the current arc: thresholds `Anchor 0, Pulse 0.25, Passing 0.5, Tension 0.75`. With `t` the role's threshold: if `a ≥ t + 0.1`, it plays; if `a ≤ t − 0.1`, it is silent; in between it plays with probability `(a − (t − 0.1)) / 0.2` (seeded). Arc also adds velocity: `+ arcVel × a` volts to Tension and Passing steps, where `arcVel` (0–2 V) is a right-click setting, default 1 V.

Effect: one CV sweep from 0 to 10 V turns an anchors-only line into the full, tense phrase.

### 7.3 Rules instead of rolls (idea 5)
The **RULES page** (§10.5) holds the track's `RulesConfig`:

| Field | Values | Default |
|---|---|---|
| Intervals allowed | 8 toggles: unison, 2nd, 3rd, 4th, 5th, 6th, 7th, octave (in scale degrees) | 2nd, 3rd on |
| Contour | Free, Rise, Fall, Arch, Valley, Zigzag | Free |
| Range | low/high degree 0..15 | 0..10 |
| Max repeats | 1..4 same note in a row | 2 |
| Density | min/max % of loop steps gated | 25..60 |
| Must-hit steps | step toggles (gate forced on) | none |
| Leap recovery | on/off: after a leap larger than a 3rd, the next move goes the other way by step | on |
| End on | Any, Root (degree 0 mod n), Chord tone (0, 2, 4 mod n) | Any |

**RULE GEN** (Shift+GEN while the RULES page is shown, or the RULES-GEN input on the expander) sends a job to the worker:
1. **Gates:** choose a count between the density bounds, place must-hit steps, then fill the rest with seeded weighted choice (strong positions weighted 1.5). Assign roles as in §7.1.1.
2. **Notes**, only for gated steps, in loop order: depth-first search with backtracking. At each step, candidate degrees are those reachable by an allowed interval from the previous note, inside the range, not breaking max repeats or leap recovery. Order candidates by distance to the **contour target** for that step (Rise: linear from low to high; Fall: reverse; Arch: up then down, peak at 60 %; Valley: reverse of Arch; Zigzag: alternate above/below the middle; Free: no ordering) plus seeded jitter of ±1 degree. The last gated note must satisfy End on.
3. Cap the search at 5000 nodes. If it fails, relax rules in this order and retry: contour → leap recovery → max repeats → end on → intervals (add 2nd and 3rd). Show "RELAXED: contour" (etc.) on the display for 3 s.
4. Result replaces the Gate and Note lanes (unpinned steps only) via `ApplyPattern`, with an undo snapshot.

### 7.4 Call and answer (idea 6)
Tracks 2 and 3 have an **ANSWER page** with `AnswerConfig`: mode (Off, Echo, Mirror, Retro, Resolve), source track (a lower-numbered track: track 2 → 1; track 3 → 1 or 2), displace (0–15 steps), transpose (−7..+7 degrees), fill (0–100 %).

When mode ≠ Off, the answer track's **Gate, Note and Octave lanes are derived** from the source's lanes (length, ratio, start and direction copied too); its other 6 lanes stay its own. The display badges derived lanes "ANS" and the step knobs cannot edit them.
- **Echo:** source gates and notes rotated by `displace` steps, notes + `transpose`.
- **Mirror:** source rhythm rotated; notes inverted around the source's first gated note: `d' = 2·d0 − d`, then + transpose, clamped 0..15.
- **Retro:** source loop reversed, then rotated and transposed.
- **Resolve:** gates are the **complement** of the source's gates in the loop (steps where the source is silent), thinned to `fill` % (seeded). Notes: for each answer gate, move stepwise (±1 degree) from the previous answer note toward the nearest chord tone (degrees 0, 2, 4 mod n) of the source's most recent gated note; the last answer gate in the loop lands on degree 0 or 4 (whichever is closer).

Recompute on the worker whenever the source pattern's `version` changes; apply at the source's next pass start. **KEEP** or the BAKE action (Shift+KEEP on the ANSWER page) writes the derived lanes into the answer track for real and sets mode to Off.

---

## 8. Morph space (idea 4) (`Morph`)

Each track has a **morph map**: up to 8 **points**, each linking a kept pattern slot to a position (x, y) in the unit square. Default placement when a slot is first kept: points fill corners then edge midpoints in the order (0,0) (1,0) (1,1) (0,1) (0.5,0) (1,0.5) (0.5,1) (0,0.5). Points can be moved on the MORPH page (select a point with its step button, move it with knobs 1 and 2) and removed (Shift+step).

**MORPH** button per selected track toggles morph on. While on, the track plays a **rendered pattern** computed from the X/Y position (knobs X, Y on the main panel for the selected track; X/Y CV per track on the expander):
1. Weights by inverse distance squared: `w_j = 1 / (dist² + 1e-4)`, normalised to sum 1. Points with `w_j < 0.02` are dropped and the rest renormalised.
2. **Continuous lanes** (Velocity, Length, ModA, ModB) per stored step: weighted average of the points' values.
3. **Discrete lanes** (Gate, Note, Octave, Chord, Ratchet) and step extras (role, prob, cond, slide, accent, nudge): **weighted choice** using a stable per-step random number `u = hash(trackSeed, lane, step) ∈ [0,1)`: walk the points in fixed order, summing weights, and take the first point whose running sum exceeds `u`. Because `u` is fixed per step, moving X/Y flips steps one at a time instead of flickering.
4. **Lane settings** (start, length, ratio, direction) come from the point with the largest weight.
5. Scale, root, rules, answer and matrix: from the largest-weight point.

Evaluate lazily: recompute weights only when X/Y moves by more than 0.002; evaluate each lane step's morphed value when the lane arrives at it (cost ≈ 8 points × 1 step). Lane settings are re-read only at that lane's pass start, so loops don't jump mid-pass.

**Shift+MORPH** = BAKE: write the rendered pattern into the edit pattern (with undo) and switch morph off.

---

## 9. Cross-modulation matrix (`Matrix`)

Each track has **4 slots** on the MATRIX page. A slot has: source, target lane (on this track), action, amount (−1..+1). Sources: any lane of any track (its normalised current value, §3.2) or expander SRC inputs 1–8 (0–10 V → 0..1). Actions:

| Action | Effect on the target lane |
|---|---|
| Offset | adds `round(src·amount·L)` to the lane counter |
| Transpose | Note lane only: adds `round(src·amount·14)` degrees |
| Direction | when `src` crosses 0.5 upward, flip Forward↔Reverse (other directions: reverse their step order) |
| Mute | while `src > 0.5` (amount > 0) or `< 0.5` (amount < 0): Gate lane silent; other lanes hold |
| Length | effective length `L + round(src·amount·8)`, clamped 1..32 |
| Clock | effective ratio index ± 1 notch while `src > 0.5`, direction set by amount's sign |

Evaluate a slot whenever its target lane is about to decide its next step (and continuously for Mute). Sources read the value at that moment. A track may modulate itself. Cycles are fine because sources are read, not recomputed.

---

## 10. Panel and controls: LATTICE (48 HP)

Positions are in mm from the panel's top-left (128.5 mm tall; 48 HP = 243.8 mm wide). They are a guide; adjust for clearance and note changes.

### 10.1 Zones, top to bottom
1. **Display** (yellow widget) at x 8–236, y 10–52: the main screen (§10.7).
2. **Step row** at y 62: 16 `EncoderKnob`s, spaced 13.5 mm from x 18. **Step buttons** (`VCVLightButton`, track-coloured) at y 74 below each knob.
3. **Select row** at y 86: TRACK 1–3 buttons; 9 LANE buttons (labels G N O C V L R A B); PAGE button; SHIFT button; STEP 17–32 button (`HI`).
4. **Lane settings** at y 98: 4 `EncoderKnob`s: LENGTH, START, CLOCK, DIRECTION (direction cycles; when Skip, Shift+turn sets skip).
5. **Generate column** (right side, y 62–98 is taken; put this at y 98 on the right): CHARACTER, AMOUNT, SEED knobs; GEN, KEEP, UNDO, PIN buttons.
6. **Performance row** at y 108: ARC, DRIFT, DENSITY, SPREAD, GESTURE (large knob), X, Y knobs for the selected track; HOME, FREEZE, MORPH, FILL buttons; MUTE 1–3 buttons; BPM knob; RUN and RESET buttons.
7. **Jacks** at y 120 (two rows if needed): inputs on the left, outputs grouped by track on the right with track-coloured rings.

### 10.2 Inputs (main module)
CLOCK, RESET, RUN, TRANSPOSE (1 V/oct, all tracks), KEEP (trigger, selected track; right-click option "KEEP input keeps all tracks"), GEN (trigger), FILL (gate), CLOCK 1–3 (per-track clocks), ARC 1–3 (0–10 V).

FILL: while high, Fill/NotFill conditions switch and every probability gets `+ fillAmount` (right-click, default +25 %).

### 10.3 Performance controls (selected track unless stated)
- **MUTE 1–3:** quantised to the next beat (right-click: Off, Beat, Bar).
- **FREEZE:** each track records its output events (gate on/off times with pitch per channel, velocity, accent, mod values) for the last **8 base steps** into a preallocated ring buffer, always. While FREEZE is held, the outputs replay that buffer looped; the lanes keep running silently underneath. On release, outputs return to live at once. Right-click: freeze length 1, 2, 4 or 8 base steps.
- **DENSITY** (−1..+1, centre 0; expander has CV per track): for an on-step that passed, compute `drop = role·0.25 + h·0.25` with `h = hash(seed, step)`, role Anchor 0 … Tension 3. With density < 0, the step is silent if `drop ≥ 1 + density`, so Tension steps go first and Anchors last. With density > 0, an **off** step plays (role Passing, Note lane's current value) if `hash2(seed, step) < density`.
- **SPREAD** (0–1): every lane except Gate gets `spreadFactor = 1 + spread·δ_i` with `δ_i = (−1)^i / (i+2)` for lane index i = 1..8. Non-integer factors make the lanes phase continuously against each other.
- **ARC**, **DRIFT**, **HOME**: §7.2, §7.1.5.
- **X, Y, MORPH**: §8.
- **GESTURE** knob and REC (Shift+GESTURE-area button, see §12).
- **Pattern queue:** on the PATTERN page, step buttons 1–16 queue that pattern for the selected track; Shift+step locks/unlocks the slot.

### 10.4 Lane settings knobs
Edit the selected lane of the selected track. Turning CLOCK past the last ratio cycles into the event sources of §11 (shown as `G1`, `G2`, `G3`, `EXT`, `ONS`, `THR`); with an event source, Shift+CLOCK sets `extDiv`.

### 10.5 Pages
PAGE cycles: **LANES** (default) → **STEP** → **PATTERN** → **ROLE** → **RULES** → **ANSWER** (tracks 2, 3) → **MORPH** → **MATRIX** → **SONG** → **SCALE**. Shift+PAGE goes back. On every page the display draws a label above each of the 16 step knobs saying what it edits; unlabelled knobs do nothing.

- **LANES:** knobs edit the selected lane's step values; step buttons toggle Gate steps on the Gate lane and select a step elsewhere. HI shows steps 17–32.
- **STEP:** for the selected step (last pressed step button): knob 1 prob, 2 condition, 3 A, 4 B, 5 alt value (turning it sets `hasAlt`; Shift+press step button clears it), 6 slide, 7 accent, 8 nudge, 9 role, 10 pin.
- **PATTERN:** step buttons queue patterns; LEDs: bright = current, blinking = queued, dim = kept, off = empty. Knob 1 sync mode, 2 bars N, 3 pattern-change quantise (Off, Beat, Bar, Gate-lane end), 4 swing, 5 glide time, 6 poly channels.
- **ROLE:** step buttons cycle the Gate step's role; LED colour shows the role (dim, normal, bright, blinking for Tension).
- **RULES:** knobs 1–8 toggle intervals, 9 contour, 10 range low, 11 range high, 12 max repeats, 13 density min, 14 density max, 15 leap recovery, 16 end on. Step buttons toggle must-hit steps. Shift+GEN runs RULE GEN.
- **ANSWER:** knob 1 mode, 2 source, 3 displace, 4 transpose, 5 fill. Shift+KEEP bakes.
- **MORPH:** step buttons 1–16 select or add a point for that kept slot; knobs 1, 2 move it; Shift+step removes. The display draws the square, the points and the current X/Y.
- **MATRIX:** knobs 1–4 for slot 1 (source, target lane, action, amount), 5–8 slot 2, 9–12 slot 3, 13–16 slot 4.
- **SONG:** knob 1 song number, 2 row, 3–5 pattern for tracks 1–3 (`—` = keep), 6 repeats, 7 song on/off; step button 1 inserts a row, 2 deletes it. NEXT trigger on the expander advances a row set to "until".
- **SCALE:** knob 1 root, 2 scale, 3 follow T1, 4 transpose (degrees, whole track); for user scales, step buttons 1–12 toggle notes; knobs 9–16 edit user chord shapes.

### 10.6 Pattern change
A queued pattern starts at the selected track's next boundary (PATTERN page knob 3): Off = at the next base step, Beat = 4 base steps, Bar = 16 (default), Gate-lane end = when the Gate lane completes a pass. At the change, all lanes restart at position 0 of the new pattern.

### 10.7 Display
NanoVG, drawn from the snapshot. LANES page: 9 rows (one per lane), 16 cells each (or 32 when HI is held, half width), showing each step's value as bar height (or note name for Note), loop window shaded, play head per row, pins as dots, Tension steps outlined, "ANS"/"PATH" badges, and lane ratio/direction at the row end. Top line: track, pattern slot, page, BPM, messages ("KEPT → 7", "RELAXED: contour"). Other pages draw their fields as a 16-column label grid matching the knobs.

### 10.8 Outputs (main module)
Per track (×3): **PITCH** (poly), **GATE** (poly, matches PITCH), **VEL** (poly, same value on each channel), **ACC** (trigger), **MOD A**, **MOD B**. Plus **EOP** (trigger when track 1's pattern-change boundary passes), **EOS** (end of song), and **ALL PITCH / ALL GATE** (poly: channels of tracks 1, 2, 3 concatenated, max 12). 22 outputs.

---

## 11. Listening lanes (idea 7) and the expander LATTICE-X (14 HP)

### 11.1 Event clock sources
A lane's clock can be (§3.3): `GateOfTrack1/2/3` (each gate that track fires, after all filtering), `ExtTrig` (expander EVENT input), `Onset` (expander AUDIO input), or `Threshold` (expander SRC 1 input crossing the THRESH knob upward, with 0.1 V hysteresis). The lane advances one position every `extDiv` events. Gate-lane nudge is ignored for event-driven Gate lanes.

**Onset detector** (in `src/engine/`, tested): full-wave rectify, envelope follower (attack 1 ms, release 50 ms), trigger when the envelope rises by more than the SENS knob (−40..0 dB, default −20) over its value 10 ms earlier, with a 50 ms refractory time.

### 11.2 LATTICE-X panel and protocol
Sits **to the right** of LATTICE. Uses Rack's expander messaging (`rightExpander.producerMessage` / `consumerMessage` on LATTICE's side, double buffered as in the Rack manual). LATTICE-X sends its input voltages and knob values every sample; LATTICE sends back nothing but a "connected" light state. If LATTICE-X is absent, the features that need it show "needs X" on the display.

Inputs:
- **SRC 1–8** (matrix sources, threshold and gesture source; ±10 V accepted)
- **EVENT** (trigger), **AUDIO** (onset detection) with **SENS** knob, **THRESH** knob
- Per track ×3: **X**, **Y** (morph), **DENSITY**, **MUTE** (gate), **FREEZE** (gate), **QUEUE** (0–10 V picks slot 1–16) + **QUEUE TRIG**
- **RULE GEN** trigger, **NEXT** (song row) trigger, **PIN** gate (acts as the PIN button)

That is 8 + 2 + 18 + 3 = 31 jacks; arrange in a grid of 4 columns. Widen the panel if needed and note it.

---

## 12. Gesture to lane (idea 8) (`Gesture`)

The **GESTURE** knob (0–1) on the main panel, or expander SRC 2 if patched (right-click choice), is the gesture source. **REC** (the small button next to the GESTURE knob) arms recording for the selected lane.

Two fit modes (right-click, per module):
- **Real time** (default): recording starts at the lane's next pass start and lasts exactly one pass. The source is sampled at 1 kHz; each step gets the **mean** of the samples in its time span.
- **Stretch:** press REC to start, press again to stop (max 60 s, buffer 60 000 floats preallocated). The recording is split into `L` equal segments; each step gets the segment mean.

Mapping the 0–1 means to the lane: Gate → on if mean > 0.5; Note → `round(mean·(hi−lo)) + lo` using the RULES range; Octave/Chord/Ratchet → rounded over their range; Velocity/Length/Mod → linear over their range. Fitting runs on the worker; the result arrives as `ApplyPattern` with an undo snapshot. While recording, the display shows a live trace.

---

## 13. Saving

- `dataToJson()` / `dataFromJson()` save: all 3 tracks' 16 pattern slots (only non-empty ones), current/queued slots, TrackSettings, morph maps, locks, pins and paths, home copies, the 16 songs, user scales and chords, and all right-click options. Do **not** save undo rings, freeze buffers or gesture recordings.
- Keep the engine's serialiser behind a tiny interface so tests can run without jansson: `Serialize.hpp` builds a plain tree (`std::map`/`std::vector` variant) that the Rack wrapper converts to `json_t`. Write a `schemaVersion: 1` field.
- Right-click **Export pattern…** / **Import pattern…** for the selected track's current slot, as `.json` via `osdialog` (Rack's bundled dialog library), on the UI thread.
- Right-click **Copy lane / Paste lane / Copy track / Paste track** use an internal clipboard (also put the JSON on the system clipboard with `glfwSetClipboardString`).
- **Initialize** (Rack's reset) clears everything to defaults; **Randomize** runs Shift+GEN on all tracks with their current seeds.

---

## 14. Milestones

Each milestone ends with: clean build, `make test` passing, `docs/MANUAL.md` updated for what was added, and a hand-off note to Haer with the checklist below.

### M1: Core lanes (§2–§6, §7.0 without probability, §10 LANES page, §10.8, §13 basic)
Tracks, lanes, transport, ratios, directions, scales, chords and poly, outputs, EncoderKnob, display LANES page, save/load.
Unit tests: ratio timing at 120 BPM (a ×3 lane moves 3 times per base step); pendulum and skip orders; degree→pitch for Major and Pelog including negative degrees; chord channel pitches; swing timing; reset.
**Haer's checklist:** patch PITCH 1/GATE 1 to a poly voice; set Gate 16 steps, Note 7 steps, Velocity 5 steps → the melody takes 112 steps to repeat; set a Chord step to Triad → three voices sound, all in key; change scale on the SCALE page → notes stay in key; save, reopen → identical.

### M2: Step extras, patterns and songs (§7.0.1, §6.5–6.6, §10.5 STEP/PATTERN/SONG, §10.6)
Probability, conditions, alt values, slide, accent, nudge, ratchets, ties, sync modes, pattern slots, queueing, songs, FILL, MUTE.
Unit tests: AofB on pass counts; Pre/NotPre; alt vs hold; negative nudge fires before the boundary; ratchet sub-gate timings and ramps; legato tie keeps gate high; queued change lands on the bar.
**Haer's checklist:** a Note step at 50 % with an alt value flips between two notes; 2:4 condition plays every other pass; ratchet ×4 ramp up audible; hold FILL → Fill steps appear; build a 3-row song and hear it advance.

### M3: GEN, KEEP, undo, pinned dice, drift (§7.1)
Unit tests: same seed + genCount → identical lanes; AMOUNT 25 % changes 4 of 16 steps; pinned steps survive GEN; pinned path replays order; drift cap respected; HOME restores.
**Haer's checklist:** Shift+GEN at CHARACTER 0.8 → lanes of different lengths drift; hold PIN for one pass of a 60 % probability Gate lane → the rhythm stops changing; KEEP three times → slots 1–3 lit; UNDO after GEN brings the old lane back; DRIFT at 0.5 wanders, HOME returns.

### M4: Roles, arc, matrix, performance (§7.2, §9, §10.3)
Unit tests: arc thresholds and soft edges; density drop order (Tension first); spread factors; each matrix action; freeze replays the last N steps exactly.
**Haer's checklist:** sweep ARC with a slow LFO → the phrase thins to anchors and fills back; route track 3 ModA (13 steps, ÷2) → Transpose on track 1 Note → melody rises and falls across bars; hold FREEZE → a stutter loop, release → back in time.

### M5: Morph space (§8)
Unit tests: weights sum to 1; at a point's exact position the render equals that pattern; discrete steps change one at a time as X moves (monotone set growth for a 2-point line); bake equals render.
**Haer's checklist:** keep 4 patterns, turn MORPH on, move X/Y slowly with an LFO → the melody changes a note at a time between the four; BAKE, then UNDO.

### M6: Rules GEN and call and answer (§7.3, §7.4, worker §4.3)
Unit tests: rules output satisfies every rule (property test over 200 seeds); relax order when rules are impossible; Echo/Mirror/Retro/Resolve outputs for a fixed source; recompute triggers on source change; worker never blocks `process()` (time `process()` while a 5000-node search runs).
**Haer's checklist:** set Contour Arch, intervals 2nd/3rd, End on Root → Shift+GEN gives a phrase that rises, falls and lands home; track 2 ANSWER Resolve from track 1 → track 2 fills the gaps and resolves; change track 1 → the answer follows at the next pass.

### M7: Expander, listening lanes, gesture (§11, §12)
Unit tests: onset detector on a synthetic click train (all clicks found, no doubles within 50 ms); threshold hysteresis; event-driven lane advances by extDiv; gesture real-time and stretch fitting on known ramps.
**Haer's checklist:** add LATTICE-X; set track 2's Note lane clock to `G1` → track 2's melody moves only when track 1 plays; feed a drum loop into AUDIO and set a Mod lane to `ONS` → the CV steps with the drums; REC a knob gesture onto a Note lane in Real time → the melody follows the hand movement, in key.

---

## 15. Things not to build
- No drum mode or trigger-per-track drum layout (Haer: melodic only).
- No zoom/nested steps (idea 2 was not chosen).
- No MIDI output, no audio generation.
- No link to the HaerSystemMk1 plugin's KeepProtocol; LATTICE stands alone. (If Haer later wants it, add it as a separate milestone.)
