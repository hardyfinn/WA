# Wa

A VCV Rack 2 plugin by Haer.

- **LATTICE**: a 3-track melodic sequencer where every parameter (gate, note, octave, chord, velocity, length, ratchet and two mod CVs) runs on its own lane with its own length, rate and direction. It adds pinned dice, roles and an arc, morphing between kept patterns, rule-based generation, call and answer between tracks, lanes that listen to other signals, and gesture recording.
- **LATTICE-X**: its expander.

## Install

Download the `.vcvplugin` for your computer from the [latest build](https://github.com/hardyfinn/WA/releases/tag/latest) and drag it onto the Rack window. Then read the [manual](docs/MANUAL.md).

## Build from source

```
export RACK_DIR=/path/to/Rack-SDK
make            # build
make test       # unit tests (no Rack needed)
make dist       # .vcvplugin in dist/
```

Design: [docs/design/BUILD-INSTRUCTIONS.md](docs/design/BUILD-INSTRUCTIONS.md). Implementation choices: [docs/DECISIONS.md](docs/DECISIONS.md).
