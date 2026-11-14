# Implementation decisions

Where the build did not follow `docs/design/BUILD-INSTRUCTIONS.md` exactly, or the spec left room, the choice is listed here.

## Structure
- **No worker thread (§4.3).** Every generation job (GEN, rule GEN, call and answer, morph render, gesture fit) is bounded and runs on the audio thread when it is triggered. The rule search is capped at 5000 nodes. A benchmark with all three tracks generated, morph on with the map moving, drift and an answer track running uses about 1.6 % of one core at 48 kHz (budget: 2 %). This removed a whole class of threading problems.
- **Fewer files than §1.2.** `src/engine/Engine.cpp` holds transport, lanes, notes, matrix and UI handling; `src/engine/Generator.cpp` holds GEN, rules, answer, morph and gesture fitting. The display and encoder widgets live in `src/modules/Lattice.cpp`. JSON is written with jansson directly in the module, so the unit tests cover the engine but not saving.
- **UI → engine.** Buttons are Rack params read in `process()`. Encoder detents go through one atomic counter per encoder. The engine writes a display snapshot 60 times a second into a double buffer.

## Behaviour
- **Anchor steps always play** whatever the ARC value. The spec's soft edge at threshold 0 would have dropped anchors half the time at ARC 0.
- **Edits write through.** The track's edit pattern is saved back to its current slot when you switch pattern; KEEP saves a separate copy into a new slot.
- **Answers update immediately** when the source changes, not at the source's next pass start.
- **Morph lane settings apply immediately** rather than at each lane's pass start.
- **Pass counts** are "moves since reset ÷ lane length" for every direction, including Pendulum.
- **Rule GEN** copies the Gate lane's timing (start, length, ratio, direction, clock) onto the Note lane, so notes line up with the gates it wrote. Ungated positions hold the previous note.
- **Echo/Mirror/Retro** answers set the answer's Note lane length to the source's Gate lane length.
- **Pinning** only records steps whose probability is below 100 % (conditions are not random).
- **3 user chord shapes**, not 4, so the Chord lane has 9 values.
- **Reset** is immediate with the internal clock; with an external clock it takes effect at the next clock pulse.
- **Freeze** records the outputs at 1 kHz, so gate edges in a frozen loop are accurate to 1 ms.
- **Expander MUTE** is a gate: the track is muted while it is high.
- **Song rows** count track 1's pattern-change boundaries.
- **Encoders are not MIDI-mappable.** The 16 step knobs, the 4 lane-setting knobs and the per-track SEED, ARC, DRIFT, DENSITY, SPREAD, X and Y knobs are endless encoders because they edit whichever track is selected. CHAR, AMOUNT, GESTURE and BPM are normal Rack knobs.
- **Ratchet ramp** is set with Shift+turn on the Ratchet lane.
- **The gesture trace** is kept in the snapshot but not drawn on the display yet.
- **Panels** are plain SVG backgrounds; all text is drawn with NanoVG.

## Testing
- 26 unit tests (`make test`) cover timing, ratios, directions, conditions, alternate values, nudge, ratchets, chords, GEN repeatability, pins, arc, density, rules, answers, morph, gesture fitting, KEEP, undo, freeze, event-driven lanes and the onset detector.
- The plugin was compiled here on Linux against the Rack 2.6.4 headers with no warnings. Rack itself could not be run in the build environment, so the panel and display have not been checked on screen. GitHub Actions builds the plugin for Windows, macOS (Intel and Apple Silicon) and Linux on every push to `main`.
