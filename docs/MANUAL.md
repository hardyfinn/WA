# LATTICE manual

LATTICE is a 3-track melodic sequencer for VCV Rack 2. Each track has 9 **lanes**: Gate, Note, Octave, Chord, Velocity, Length, Ratchet, Mod A and Mod B. Every lane has its own length, clock rate and direction, so the parts of a melody drift against each other. A 16-step rhythm against a 7-step note row and a 5-step octave row repeats only every 560 steps.

LATTICE-X is an optional expander that adds CV control and lets lanes listen to other signals.

## Install

1. Open the [latest build](https://github.com/hardyfinn/WA/releases/tag/latest) and download the `.vcvplugin` file for your computer (`win-x64`, `mac-arm64` for Apple Silicon, `mac-x64` for Intel Macs, `lin-x64`).
2. Drag the file onto the VCV Rack window, or put it in your Rack 2 `plugins` folder, then restart Rack.
3. Add **LATTICE** from the Wa brand in the module browser. Put **LATTICE-X** directly to its right if you want the expander.

## Quick start

1. Patch **PITCH 1** and **GATE 1** into a polyphonic voice (for example VCV VCO → ADSR → VCA). LATTICE runs from its own clock at 120 BPM; patch **CLOCK** to follow another clock instead.
2. Track 1 starts with a gate on every beat. Press lane **N** and turn the step knobs to change notes. Notes are scale degrees, so they are always in key.
3. Press **SHIFT** and **GEN** together. The whole track is filled with new material. Turn **CHAR** up first for busier, jumpier results and lanes of different lengths.
4. Press **KEEP** when you like what you hear. The track is saved to a free pattern slot.
5. Press **UNDO** if a GEN went too far.

## The panel

| Area | What it does |
|---|---|
| Display | Shows the selected track. On the LANES page each row is a lane: the bright frame is the playing step, the shaded part is the loop, white dots are pins, red frames are Tension steps. On other pages it labels what each step knob does. |
| Step knobs (16) | Endless knobs. What they edit depends on the page. Drag up/down or use the scroll wheel; hold Shift for fine steps, Ctrl/Cmd for coarse. |
| Step buttons (16) | Toggle gates on the Gate lane, pick a step, queue patterns, and so on, depending on the page. |
| T1–T3 | Select the track to edit. |
| G N O C V L R A B | Select the lane: Gate, Note, Octave, Chord, Velocity, Length, Ratchet, Mod A, Mod B. |
| PAGE / SHIFT / 17-32 | Next page (Shift+PAGE goes back). SHIFT is held for second functions. 17-32 shows the upper half of the 32 stored steps. |
| LEN, START, CLOCK, DIR | Settings of the selected lane: loop length (1–32), loop start, clock rate (×4 to ÷16, then the listening sources G1, G2, G3, EXT, ONS, THR) and direction (Fwd, Rev, Pendulum, Random, Drunk, Skip). Shift+CLOCK sets the event divider; Shift+DIR sets the skip interval. |
| RUN, RESET, BPM | Transport. BPM is the internal clock, used when nothing is patched to CLOCK. |
| CHAR, AMOUNT, SEED | GEN controls: character (calm to wild), how much of the lane is replaced, and the selected track's seed. |
| GEN, KEEP, UNDO, PIN | Generate, keep, undo, pin (see below). |
| ARC, DRIFT, DENSITY, SPREAD | Performance controls for the selected track (see below). |
| GESTURE, REC | Record a hand movement onto a lane. |
| X, Y, MORPH | Move through the morph map; MORPH turns it on. |
| HOME, FREEZE, FILL, MUTE 1–3 | Performance buttons. |

Inputs: CLOCK, RESET, RUN, TRANSP (1 V/oct, re-quantised to each track's scale), KEEP, GEN, FILL, a clock per track (CLK1–3) and an arc CV per track (ARC1–3).

Outputs per track: PITCH and GATE (polyphonic when a chord is set), VEL, ACC (accent trigger), MOD A, MOD B. Also EOP (end of pattern), EOS (end of song), ALL P and ALL G (all three tracks on one poly cable).

## How a note is made

The Gate lane decides when a note plays. At that moment LATTICE reads where every other lane currently is: Note, Octave and Chord give the pitch; Velocity, Length and Ratchet shape the gate. The other lanes keep moving on their own clocks, so the same gate step can get a different note every time round. Mod A and Mod B output continuously on their own clocks.

Length is a percentage of one Gate step. At 100 % or more the note ties into the next one (legato). Ratchet repeats the note inside the step, 1–8 times; Shift+turn on the Ratchet lane sets a velocity ramp (up or down).

## Pages

| Page | Step knobs | Step buttons |
|---|---|---|
| LANES | values of the selected lane | Gate lane: toggle steps. Other lanes: pick a step. Shift+step jumps to STEP. |
| STEP | 1 probability, 2 condition, 3 A, 4 B, 5 alternate value, 6 slide, 7 accent, 8 nudge (±49 %), 9 role, 10 pin | pick the step |
| PATTERN | 1 sync (Free, Gate, Bars), 2 bars, 3 pattern-change timing (Off, Beat, Bar, GateEnd), 4 swing, 5 glide time, 6 poly channels (Auto or 1–4), 7 pitch follows Note lane | queue a pattern; Shift+step locks a slot |
| ROLE | — | cycle each Gate step's role: Anchor, Pulse, Passing, Tension |
| RULES | 1–8 allowed intervals, 9 contour, 10 low, 11 high, 12 max repeats, 13–14 density range, 15 leap recovery, 16 end on | toggle must-hit steps |
| ANSWER (T2, T3) | 1 mode, 2 source track, 3 displace, 4 transpose, 5 fill | — |
| MORPH | 1–2 move the selected point | add or select a kept slot on the map; Shift+step removes it |
| MATRIX | four routing slots of 4 knobs: source, target lane, action, amount | — |
| SONG | 1 song, 2 row, 3–5 pattern per track, 6 repeats (0 = wait for NEXT), 7 play | 1 insert row, 2 delete row |
| SCALE | 1 root, 2 scale, 3 follow track 1, 4 track transpose, 9 user chord, 10–13 its degrees, 14 its size | toggle notes of a user scale |

**Conditions:** Always, A:B (plays on pass A of every B passes of that lane), First, Not first, Fill, Not fill, Pre and Not pre (the previous condition on this lane passed or failed). A failed step on a Gate lane is silent. On other lanes it uses its **alternate value** if it has one, otherwise the lane keeps its previous value.

**Scales:** 24 built in plus 4 user scales. Notes and chords are counted in scale degrees, so a "Triad" is always a triad of the current scale.

## Generate, keep, perform

- **GEN** fills the selected lane. **Shift+GEN** fills the whole track. AMOUNT sets how much is replaced; pinned steps are never touched. The same seed and the same number of presses give the same result.
- **KEEP** saves the track as it plays (including where each lane currently is) into the next free slot of 16. Shift+KEEP keeps all three tracks. When all slots are full, the oldest unlocked slot is overwritten.
- **UNDO** keeps 8 steps per track.
- **PIN (hold):** while held, every chance decision on the selected track is frozen as it happens. Hold it through one pass of a 60 % lane and the rhythm stops changing; it is now fixed the way it just played. Random and Drunk lanes record their path for one pass. Shift+PIN clears the selected lane; hold Shift+PIN for 1 second to clear the track.
- **DRIFT** slowly rewrites a few Gate, Note and Velocity steps at each bar. **HOME** returns to the kept version at the next bar.

## Performance

- **ARC** (knob plus ARC CV) decides which roles play. At 0 only Anchors play; Pulse opens around 0.25, Passing around 0.5, Tension around 0.75. Sweep it to grow a phrase from its skeleton to its full, tense form. Passing and Tension steps also get louder as ARC rises (right-click to change).
- **DENSITY** below the centre removes steps, Tension first and Anchors last. Above the centre it adds notes in the gaps.
- **SPREAD** pushes every lane's speed slightly apart so the lanes phase against each other continuously.
- **FREEZE** (hold) loops the last 1–8 steps of the selected track while the sequence keeps running silently underneath. Let go and it is back in time.
- **FILL** (hold or gate) switches Fill conditions and raises probabilities.
- **MUTE 1–3** mute on the next beat (right-click: off or bar).

## Morph

Every kept slot is placed on a square map (up to 8 points). With **MORPH** on, the **X** and **Y** knobs (and the expander's X/Y inputs) move through the map. Notes and rhythms change one step at a time as you get closer to another pattern; velocity, length and mod values blend smoothly. **Shift+MORPH** bakes the current mix into the track.

## Rules

On the RULES page, set the allowed intervals, a contour (Rise, Fall, Arch, Valley, Zigzag), the note range, how often a note may repeat, the rhythm density, steps that must play, leap recovery (after a big leap, step back the other way) and how the phrase ends. **Shift+GEN** on this page writes a Gate and Note lane that follow those rules. If they cannot all be met, the display says which rule it relaxed.

## Call and answer

Tracks 2 and 3 can answer an earlier track (ANSWER page). Their Gate, Note and Octave lanes are then worked out from the source and update when the source changes:

- **Echo:** the same phrase, displaced and transposed.
- **Mirror:** the melody upside down around its first note.
- **Retro:** the phrase backwards.
- **Resolve:** plays in the source's gaps and walks toward its chord tones, landing on the root or fifth.

Shift+KEEP on the ANSWER page bakes the answer into the track so you can edit it.

## Cross-modulation (MATRIX)

Each track has 4 routings. A source is any lane of any track, or an expander SRC input. Actions: **Offset** (shift where a lane reads), **Transpose** (Note lane, in scale degrees), **Direction** (flip when the source goes high), **Mute**, **Length** (±8 steps) and **Clock** (one speed notch up or down while high).

## Listening lanes and LATTICE-X

Turn a lane's CLOCK knob past ÷16 to make it move on events instead of the clock: **G1/G2/G3** (each note another track plays), **EXT** (expander EVENT input), **ONS** (onsets found in the expander AUDIO input; SENS sets how big a jump counts) or **THR** (expander SRC 1 crossing THRESH). Shift+CLOCK sets "every Nth event".

LATTICE-X inputs: SRC 1–8, EVENT, AUDIO with SENS and THRESH, RULE GEN, NEXT (song), PIN (gate), and per track X, Y, DENSITY (±5 V), MUTE (gate), FREEZE (gate), QUEUE (0–10 V picks slot 1–16) with QTRG to queue it. The small light on LATTICE shows when the expander is connected.

## Gesture to lane

Select a lane, press **REC** and move the **GESTURE** knob. In **Real time** mode (default) recording starts at the lane's next loop start and lasts exactly one loop; each step gets the average of your movement during it. In **Stretch** mode, press REC again to stop; the whole movement is squeezed onto the lane. Notes land inside the RULES page range, so the result stays in key.

## Right-click options

Clock input resolution (1, 2, 4 pulses per step, 24 or 48 PPQN), RUN input as a gate, KEEP input keeps all tracks, mute timing, freeze length, FILL amount, arc velocity boost, gesture mode and source, export/import a pattern as JSON, copy/paste a lane or track, clear lane.

## Try this

- **Polymeter:** set the Gate lane to 16 steps, Note to 7 and Velocity to 5. The melody takes 112 steps to come round.
- **Chords:** set a Chord step to Triad. Three voices sound, in key. Change the scale on the SCALE page and they follow.
- **Two notes from one step:** on the STEP page give a Note step 50 % probability and an alternate value. It flips between the two.
- **Sculpt chance:** fill a Gate lane at 60 % probability, listen, then hold PIN for one pass. The pattern stops changing.
- **Grow a phrase:** run a slow LFO into ARC 1. The phrase thins to its anchors and fills back in.
- **Melody that wanders across bars:** on track 3 make Mod A 13 steps at ÷2, then on track 1's MATRIX page route T3A → Note lane, Transpose.
- **Morph:** KEEP four different patterns, press MORPH and move X and Y slowly.
- **Rules:** contour Arch, intervals 2nd and 3rd only, end on Root, then Shift+GEN on the RULES page.
- **Answer:** on track 2 set ANSWER to Resolve from T1. Track 2 fills the gaps and resolves.
- **Listening:** set track 2's Note clock to G1. Its melody only moves when track 1 plays.
- **Gesture:** select the Note lane, press REC and move GESTURE for one loop.
