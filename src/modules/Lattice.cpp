// LATTICE: 3-track melodic lane sequencer (spec §10). Thin Rack wrapper around wa::wa::Engine.
#include "plugin.hpp"
#include "Engine.hpp"
#include "Generator.hpp"
#include "Expander.hpp"
#include <atomic>
#include <osdialog.h>

using namespace wa;

namespace {

const NVGcolor TRACK_COLORS[NT] = {nvgRGB(0xe8, 0xa3, 0x3c), nvgRGB(0x3c, 0xc8, 0xb4), nvgRGB(0x9b, 0x7b, 0xe8)};
const float TRACK_RGB[NT][3] = {{0.91f, 0.64f, 0.24f}, {0.24f, 0.78f, 0.71f}, {0.61f, 0.48f, 0.91f}};

// Panel geometry in mm (spec §10.1)
const float DISPLAY_X = 5.f, DISPLAY_Y = 5.f, DISPLAY_W = 233.84f, DISPLAY_H = 39.f;
inline float stepX(int i) { return 12.f + i * 14.4f; }
const float ENC_Y = 52.f, STEP_Y = 62.f, ROW3_Y = 74.f, ROW4_Y = 87.f, IN_Y = 101.f, OUT_Y = 117.f;

} // namespace

struct Lattice : Module {
	enum ParamIds {
		ENUMS(TRACK_PARAM, NT),
		ENUMS(LANE_PARAM, NL),
		ENUMS(STEP_PARAM, 16),
		PAGE_PARAM, SHIFT_PARAM, HI_PARAM,
		GEN_PARAM, KEEP_PARAM, UNDO_PARAM, PIN_PARAM,
		HOME_PARAM, FREEZE_PARAM, MORPH_PARAM, FILL_PARAM, REC_PARAM,
		ENUMS(MUTE_PARAM, NT),
		RUN_PARAM, RESET_PARAM,
		CHAR_PARAM, AMOUNT_PARAM, GESTURE_PARAM, BPM_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT, RESET_INPUT, RUN_INPUT, TRANSPOSE_INPUT, KEEP_INPUT, GEN_INPUT, FILL_INPUT,
		ENUMS(TCLK_INPUT, NT),
		ENUMS(ARC_INPUT, NT),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(PITCH_OUTPUT, NT),
		ENUMS(GATE_OUTPUT, NT),
		ENUMS(VEL_OUTPUT, NT),
		ENUMS(ACC_OUTPUT, NT),
		ENUMS(MODA_OUTPUT, NT),
		ENUMS(MODB_OUTPUT, NT),
		EOP_OUTPUT, EOS_OUTPUT, ALLP_OUTPUT, ALLG_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(TRACK_LIGHT, NT * 3),
		ENUMS(LANE_LIGHT, NL * 3),
		ENUMS(STEP_LIGHT, 16 * 3),
		ENUMS(MUTE_LIGHT, NT * 3),
		ENUMS(PAGE_LIGHT, 3), ENUMS(SHIFT_LIGHT, 3), ENUMS(HI_LIGHT, 3),
		ENUMS(GEN_LIGHT, 3), ENUMS(KEEP_LIGHT, 3), ENUMS(UNDO_LIGHT, 3), ENUMS(PIN_LIGHT, 3),
		ENUMS(HOME_LIGHT, 3), ENUMS(FREEZE_LIGHT, 3), ENUMS(MORPH_LIGHT, 3), ENUMS(FILL_LIGHT, 3), ENUMS(REC_LIGHT, 3),
		ENUMS(RUN_LIGHT, 3), ENUMS(RESET_LIGHT, 3),
		EXP_LIGHT,
		NUM_LIGHTS
	};

	wa::Engine* eng = nullptr;
	std::atomic<int> encDelta[NUM_ENC];
	dsp::BooleanTrigger btn[NUM_PARAMS];
	dsp::SchmittTrigger clockTrig, resetTrig, runTrig, keepTrig, genTrig;
	dsp::SchmittTrigger tclkTrig[NT];
	dsp::PulseGenerator accPulse[NT], eopPulse, eosPulse;
	dsp::ClockDivider lightDivider;
	bool running = true;
	int ppStep = 1;
	LatticeXMessage xmsg[2];
	EngineIn in;
	EngineOut out;
	float blink = 0.f;

	Lattice() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int t = 0; t < NT; t++) {
			configButton(TRACK_PARAM + t, string::f("Select track %d", t + 1));
			configButton(MUTE_PARAM + t, string::f("Mute track %d", t + 1));
		}
		for (int l = 0; l < NL; l++)
			configButton(LANE_PARAM + l, string::f("Select %s lane", LANE_NAMES[l]));
		for (int i = 0; i < 16; i++)
			configButton(STEP_PARAM + i, string::f("Step %d (meaning depends on the page)", i + 1));
		configButton(PAGE_PARAM, "Next page (Shift: previous)");
		configButton(SHIFT_PARAM, "Shift (hold)");
		configButton(HI_PARAM, "Show steps 17-32");
		configButton(GEN_PARAM, "GEN: generate the selected lane (Shift: whole track; Shift on RULES page: rule GEN)");
		configButton(KEEP_PARAM, "KEEP: save the selected track into a free pattern slot (Shift: all tracks; Shift on ANSWER page: bake)");
		configButton(UNDO_PARAM, "Undo the last generate or edit");
		configButton(PIN_PARAM, "PIN (hold): freeze chance outcomes as they happen (Shift: clear lane pins; Shift held 1 s: clear track)");
		configButton(HOME_PARAM, "HOME: return a drifted track to its kept version at the next bar");
		configButton(FREEZE_PARAM, "FREEZE (hold): loop the last few steps of the selected track");
		configButton(MORPH_PARAM, "MORPH on/off for the selected track (Shift: bake)");
		configButton(FILL_PARAM, "FILL (hold)");
		configButton(REC_PARAM, "Record a gesture onto the selected lane");
		configButton(RUN_PARAM, "Run / stop");
		configButton(RESET_PARAM, "Reset");
		configParam(CHAR_PARAM, 0.f, 1.f, 0.5f, "GEN character", "%", 0.f, 100.f);
		configParam(AMOUNT_PARAM, 0.f, 1.f, 1.f, "GEN amount (share of steps replaced)", "%", 0.f, 100.f);
		configParam(GESTURE_PARAM, 0.f, 1.f, 0.5f, "Gesture source", "%", 0.f, 100.f);
		configParam(BPM_PARAM, 30.f, 300.f, 120.f, "Internal tempo", " BPM");
		getParamQuantity(BPM_PARAM)->snapEnabled = true;

		configInput(CLOCK_INPUT, "Clock (resolution in the right-click menu)");
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run (trigger toggles; gate mode in the menu)");
		configInput(TRANSPOSE_INPUT, "Transpose, 1 V/oct, re-quantised to each track's scale");
		configInput(KEEP_INPUT, "KEEP trigger");
		configInput(GEN_INPUT, "GEN trigger");
		configInput(FILL_INPUT, "FILL gate");
		for (int t = 0; t < NT; t++) {
			configInput(TCLK_INPUT + t, string::f("Track %d clock (one pulse per base step)", t + 1));
			configInput(ARC_INPUT + t, string::f("Track %d arc CV (0-10 V)", t + 1));
			configOutput(PITCH_OUTPUT + t, string::f("Track %d pitch (1 V/oct, poly for chords)", t + 1));
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate (poly)", t + 1));
			configOutput(VEL_OUTPUT + t, string::f("Track %d velocity (0-10 V)", t + 1));
			configOutput(ACC_OUTPUT + t, string::f("Track %d accent trigger", t + 1));
			configOutput(MODA_OUTPUT + t, string::f("Track %d Mod A (±5 V)", t + 1));
			configOutput(MODB_OUTPUT + t, string::f("Track %d Mod B (±5 V)", t + 1));
		}
		configOutput(EOP_OUTPUT, "End of pattern (track 1 boundary)");
		configOutput(EOS_OUTPUT, "End of song");
		configOutput(ALLP_OUTPUT, "All tracks pitch (poly)");
		configOutput(ALLG_OUTPUT, "All tracks gate (poly)");

		for (int e = 0; e < NUM_ENC; e++)
			encDelta[e] = 0;
		eng = new wa::Engine();
		lightDivider.setDivision(64);
		rightExpander.producerMessage = &xmsg[0];
		rightExpander.consumerMessage = &xmsg[1];
	}

	~Lattice() {
		delete eng;
	}

	void onReset() override {
		eng->initialize();
		running = true;
	}

	void onRandomize() override {
		eng->randomizeAll(params[CHAR_PARAM].getValue());
	}

	bool pressed(int id) {
		return btn[id].process(params[id].getValue() > 0.f);
	}

	void process(const ProcessArgs& args) override {
		in = EngineIn();
		in.dt = args.sampleTime;

		// Run / reset
		if (pressed(RUN_PARAM))
			running = !running;
		if (inputs[RUN_INPUT].isConnected()) {
			if (eng->opt.runIsGate)
				running = inputs[RUN_INPUT].getVoltage() >= 1.f;
			else if (runTrig.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
				running = !running;
		}
		in.running = running;
		bool resetBtn = pressed(RESET_PARAM);
		in.reset = resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) || resetBtn;

		in.clockConn = inputs[CLOCK_INPUT].isConnected();
		in.clockPulse = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		in.ppStep = ppStep;
		for (int t = 0; t < NT; t++) {
			in.tclkConn[t] = inputs[TCLK_INPUT + t].isConnected();
			in.tclkPulse[t] = tclkTrig[t].process(inputs[TCLK_INPUT + t].getVoltage(), 0.1f, 1.f);
			in.arcCV[t] = inputs[ARC_INPUT + t].getVoltage();
		}
		in.bpm = params[BPM_PARAM].getValue();
		in.transposeV = inputs[TRANSPOSE_INPUT].getVoltage();
		in.keepTrig = keepTrig.process(inputs[KEEP_INPUT].getVoltage(), 0.1f, 1.f);
		in.genTrig = genTrig.process(inputs[GEN_INPUT].getVoltage(), 0.1f, 1.f);
		in.fill = inputs[FILL_INPUT].getVoltage() >= 1.f;
		in.gesture = params[GESTURE_PARAM].getValue();
		in.character = params[CHAR_PARAM].getValue();
		in.amount = params[AMOUNT_PARAM].getValue();

		// Buttons
		in.shift = params[SHIFT_PARAM].getValue() > 0.f;
		in.pinHeld = params[PIN_PARAM].getValue() > 0.f;
		in.freezeHeld = params[FREEZE_PARAM].getValue() > 0.f;
		in.fillHeld = params[FILL_PARAM].getValue() > 0.f;
		for (int t = 0; t < NT; t++) {
			in.pTrack[t] = pressed(TRACK_PARAM + t);
			in.pMute[t] = pressed(MUTE_PARAM + t);
		}
		for (int l = 0; l < NL; l++)
			in.pLane[l] = pressed(LANE_PARAM + l);
		for (int i = 0; i < 16; i++)
			in.pStep[i] = pressed(STEP_PARAM + i);
		in.pPage = pressed(PAGE_PARAM);
		in.pHi = pressed(HI_PARAM);
		in.pGen = pressed(GEN_PARAM);
		in.pKeep = pressed(KEEP_PARAM);
		in.pUndo = pressed(UNDO_PARAM);
		in.pHome = pressed(HOME_PARAM);
		in.pMorph = pressed(MORPH_PARAM);
		in.pRec = pressed(REC_PARAM);
		in.pPin = pressed(PIN_PARAM);
		for (int e = 0; e < NUM_ENC; e++) {
			if (encDelta[e].load(std::memory_order_relaxed) != 0)
				in.enc[e] = encDelta[e].exchange(0);
		}

		// Expander
		in.expander = rightExpander.module && rightExpander.module->model == modelLatticeX;
		if (in.expander) {
			const LatticeXMessage* m = (const LatticeXMessage*) rightExpander.consumerMessage;
			in.expander = m->connected;
			for (int i = 0; i < NSRC; i++)
				in.src[i] = m->src[i];
			in.eventTrig = m->event;
			in.audio = m->audio;
			in.sensDb = m->sensDb;
			in.thresh = m->thresh;
			for (int t = 0; t < NT; t++) {
				in.xCV[t] = m->x[t];
				in.yCV[t] = m->y[t];
				in.densCV[t] = m->dens[t];
				in.muteGate[t] = m->mute[t];
				in.freezeGate[t] = m->freeze[t];
				in.queueV[t] = m->queueV[t];
				in.queueTrig[t] = m->queueTrig[t];
			}
			in.ruleGenTrig = m->ruleGen;
			in.nextTrig = m->next;
			in.pinGate = m->pin;
		}

		eng->process(in, out);

		// Outputs
		int allCh = 0;
		for (int t = 0; t < NT; t++) {
			int ch = clamp(out.chans[t], 1, 4);
			outputs[PITCH_OUTPUT + t].setChannels(ch);
			outputs[GATE_OUTPUT + t].setChannels(ch);
			outputs[VEL_OUTPUT + t].setChannels(ch);
			for (int c = 0; c < ch; c++) {
				outputs[PITCH_OUTPUT + t].setVoltage(out.pitch[t][c], c);
				outputs[GATE_OUTPUT + t].setVoltage(out.gate[t][c], c);
				outputs[VEL_OUTPUT + t].setVoltage(out.vel[t], c);
				if (allCh < 16) {
					outputs[ALLP_OUTPUT].setVoltage(out.pitch[t][c], allCh);
					outputs[ALLG_OUTPUT].setVoltage(out.gate[t][c], allCh);
					allCh++;
				}
			}
			if (out.acc[t])
				accPulse[t].trigger(1e-3f);
			outputs[ACC_OUTPUT + t].setVoltage(accPulse[t].process(args.sampleTime) ? 10.f : 0.f);
			outputs[MODA_OUTPUT + t].setVoltage(out.mod[t][0]);
			outputs[MODB_OUTPUT + t].setVoltage(out.mod[t][1]);
		}
		outputs[ALLP_OUTPUT].setChannels(std::max(1, allCh));
		outputs[ALLG_OUTPUT].setChannels(std::max(1, allCh));
		if (out.eop)
			eopPulse.trigger(1e-3f);
		if (out.eos)
			eosPulse.trigger(1e-3f);
		outputs[EOP_OUTPUT].setVoltage(eopPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[EOS_OUTPUT].setVoltage(eosPulse.process(args.sampleTime) ? 10.f : 0.f);

		if (lightDivider.process())
			updateLights(args.sampleTime * lightDivider.getDivision());
	}

	void setRGB(int id, float r, float g, float b) {
		lights[id + 0].setBrightness(r);
		lights[id + 1].setBrightness(g);
		lights[id + 2].setBrightness(b);
	}
	void setTrackRGB(int id, int t, float k) {
		setRGB(id, TRACK_RGB[t][0] * k, TRACK_RGB[t][1] * k, TRACK_RGB[t][2] * k);
	}

	void updateLights(float dt) {
		blink += dt;
		if (blink > 1.f)
			blink -= 1.f;
		bool blinkOn = blink < 0.5f;
		wa::Engine& E = *eng;
		int t = E.selTrack;
		for (int u = 0; u < NT; u++) {
			setTrackRGB(TRACK_LIGHT + u * 3, u, u == t ? 1.f : (E.tr[u].gateEvent || E.tr[u].noteOn ? 0.25f : 0.08f));
			bool m = E.ts[u].mute;
			bool pend = E.tr[u].pendingMute >= 0;
			setRGB(MUTE_LIGHT + u * 3, m ? 0.9f : 0.f, 0.f, 0.f);
			if (pend && blinkOn)
				setRGB(MUTE_LIGHT + u * 3, 0.5f, 0.2f, 0.f);
		}
		for (int l = 0; l < NL; l++)
			setTrackRGB(LANE_LIGHT + l * 3, t, l == E.selLane ? 1.f : 0.06f);

		const Pattern& P = E.play[t];
		const TrackSettings& S = E.ts[t];
		for (int i = 0; i < 16; i++) {
			int raw = i + (E.hi ? 16 : 0);
			float r = 0, g = 0, b = 0;
			auto trk = [&](float k) {
				r = TRACK_RGB[t][0] * k;
				g = TRACK_RGB[t][1] * k;
				b = TRACK_RGB[t][2] * k;
			};
			switch (E.page) {
				case P_LANES:
				case P_STEP: {
					int l = E.selLane;
					const LaneSettings& ls = P.lanes[l];
					bool inLoop = ((raw - ls.start + NS) % NS) < ls.length;
					if (l == L_GATE)
						trk(P.steps[l][raw].value > 0.5f ? 0.8f : (inLoop ? 0.05f : 0.f));
					else
						trk(inLoop ? 0.12f : 0.f);
					if (E.page == P_STEP && raw == E.selStep)
						trk(1.f);
					if (raw == E.tr[t].lanes[l].curIdx && running) {
						r = g = b = 0.9f;
					}
					break;
				}
				case P_PATTERN:
					if (S.slot == i)
						trk(1.f);
					else if (E.tr[t].queued == i)
						trk(blinkOn ? 0.8f : 0.1f);
					else if (S.used[i])
						trk(0.2f);
					if (S.locked[i])
						r = std::max(r, 0.5f);
					break;
				case P_ROLE: {
					const Step& s = P.steps[L_GATE][raw];
					float k = 0.1f + 0.3f * (int) s.role;
					if (s.role == Role::Tension && !blinkOn)
						k = 0.3f;
					trk(s.value > 0.5f ? k : k * 0.3f);
					break;
				}
				case P_RULES:
					trk((E.edit[t].rules.mustHit & (1u << raw)) ? 0.9f : (P.steps[L_GATE][raw].value > 0.5f ? 0.12f : 0.f));
					break;
				case P_MORPH: {
					bool has = false, sel = false;
					for (int j = 0; j < NPOINT; j++)
						if (S.points[j].active && S.points[j].slot == i) {
							has = true;
							sel = j == E.selPoint;
						}
					trk(sel ? 1.f : (has ? 0.4f : (S.used[i] ? 0.06f : 0.f)));
					break;
				}
				case P_SCALE: {
					if (i < 12) {
						ScaleDef sc = getScale(P.scale, E.scales);
						bool in = false;
						for (int k = 0; k < sc.n; k++)
							if (sc.semis[k] == i)
								in = true;
						trk(in ? 0.6f : 0.f);
					}
					break;
				}
				case P_SONG:
					if (i < 2)
						trk(0.3f);
					break;
				default: break;
			}
			setRGB(STEP_LIGHT + i * 3, r, g, b);
		}
		auto white = [&](int id, float k) { setRGB(id, k, k, k); };
		white(PAGE_LIGHT, 0.3f);
		white(SHIFT_LIGHT, params[SHIFT_PARAM].getValue() > 0.f ? 1.f : 0.05f);
		white(HI_LIGHT, E.hi ? 0.8f : 0.05f);
		white(GEN_LIGHT, 0.15f);
		white(KEEP_LIGHT, 0.15f);
		white(UNDO_LIGHT, E.undoCount[t] > 0 ? 0.3f : 0.03f);
		white(PIN_LIGHT, params[PIN_PARAM].getValue() > 0.f ? 1.f : 0.05f);
		white(HOME_LIGHT, E.tr[t].pendingHome ? (blinkOn ? 1.f : 0.1f) : 0.05f);
		setTrackRGB(FREEZE_LIGHT, t, E.tr[t].frozen ? 1.f : 0.05f);
		setTrackRGB(MORPH_LIGHT, t, S.morphOn ? 1.f : 0.05f);
		white(FILL_LIGHT, (params[FILL_PARAM].getValue() > 0.f || in.fill) ? 1.f : 0.05f);
		setRGB(REC_LIGHT, E.gestRec ? 1.f : (E.gestArmed && blinkOn ? 0.6f : 0.05f), 0.f, 0.f);
		setRGB(RUN_LIGHT, 0.f, running ? 0.9f : 0.05f, 0.f);
		white(RESET_LIGHT, 0.1f);
		lights[EXP_LIGHT].setBrightness(in.expander ? 1.f : 0.f);
	}

	// ------------------------------------------------------------------ JSON (spec §13)

	static json_t* stepToJson(const Step& s) {
		json_t* a = json_array();
		json_array_append_new(a, json_real(s.value));
		json_array_append_new(a, json_real(s.alt));
		json_array_append_new(a, json_integer(s.hasAlt));
		json_array_append_new(a, json_integer(s.prob));
		json_array_append_new(a, json_integer((int) s.cond));
		json_array_append_new(a, json_integer(s.condA));
		json_array_append_new(a, json_integer(s.condB));
		json_array_append_new(a, json_integer(s.slide));
		json_array_append_new(a, json_integer(s.accent));
		json_array_append_new(a, json_integer(s.nudge));
		json_array_append_new(a, json_integer((int) s.role));
		json_array_append_new(a, json_integer((int) s.pin));
		return a;
	}
	static int ji(json_t* a, size_t i, int def = 0) {
		json_t* v = json_array_get(a, i);
		return v ? (int) json_integer_value(v) : def;
	}
	static float jf(json_t* a, size_t i, float def = 0.f) {
		json_t* v = json_array_get(a, i);
		return v ? (float) json_number_value(v) : def;
	}
	static void stepFromJson(json_t* a, Step& s) {
		if (!json_is_array(a))
			return;
		s.value = jf(a, 0);
		s.alt = jf(a, 1);
		s.hasAlt = ji(a, 2);
		s.prob = (uint8_t) clamp(ji(a, 3, 100), 0, 100);
		s.cond = (Cond) clamp(ji(a, 4), 0, (int) Cond::COUNT - 1);
		s.condA = (uint8_t) clamp(ji(a, 5, 1), 1, 8);
		s.condB = (uint8_t) clamp(ji(a, 6, 1), 1, 8);
		s.slide = ji(a, 7);
		s.accent = ji(a, 8);
		s.nudge = (int8_t) clamp(ji(a, 9), -49, 49);
		s.role = (Role) clamp(ji(a, 10, 1), 0, (int) Role::COUNT - 1);
		s.pin = (Pin) clamp(ji(a, 11), 0, 2);
	}

	static json_t* patternToJson(const Pattern& p) {
		json_t* o = json_object();
		json_t* lanes = json_array();
		for (int l = 0; l < NL; l++) {
			const LaneSettings& ls = p.lanes[l];
			json_t* lo = json_object();
			json_object_set_new(lo, "start", json_integer(ls.start));
			json_object_set_new(lo, "length", json_integer(ls.length));
			json_object_set_new(lo, "ratio", json_integer(ls.ratio));
			json_object_set_new(lo, "dir", json_integer((int) ls.dir));
			json_object_set_new(lo, "skip", json_integer(ls.skip));
			json_object_set_new(lo, "clock", json_integer((int) ls.clock));
			json_object_set_new(lo, "extDiv", json_integer(ls.extDiv));
			json_object_set_new(lo, "glide", json_boolean(ls.glide));
			json_t* st = json_array();
			for (int s = 0; s < NS; s++)
				json_array_append_new(st, stepToJson(p.steps[l][s]));
			json_object_set_new(lo, "steps", st);
			json_t* path = json_array();
			for (int s = 0; s < p.pathLen[l]; s++)
				json_array_append_new(path, json_integer(p.path[l][s]));
			json_object_set_new(lo, "path", path);
			json_array_append_new(lanes, lo);
		}
		json_object_set_new(o, "lanes", lanes);
		json_object_set_new(o, "root", json_integer(p.root));
		json_object_set_new(o, "scale", json_integer(p.scale));
		json_object_set_new(o, "followT1", json_boolean(p.followT1));
		json_object_set_new(o, "transpose", json_integer(p.transpose));
		json_object_set_new(o, "swing", json_integer(p.swing));
		json_object_set_new(o, "seed", json_integer(p.seed));
		const RulesConfig& r = p.rules;
		json_t* ro = json_object();
		json_object_set_new(ro, "intervals", json_integer(r.intervals));
		json_object_set_new(ro, "contour", json_integer((int) r.contour));
		json_object_set_new(ro, "lo", json_integer(r.lo));
		json_object_set_new(ro, "hi", json_integer(r.hi));
		json_object_set_new(ro, "maxRepeat", json_integer(r.maxRepeat));
		json_object_set_new(ro, "densMin", json_integer(r.densMin));
		json_object_set_new(ro, "densMax", json_integer(r.densMax));
		json_object_set_new(ro, "mustHit", json_integer(r.mustHit));
		json_object_set_new(ro, "leap", json_boolean(r.leapRecovery));
		json_object_set_new(ro, "endOn", json_integer((int) r.endOn));
		json_object_set_new(o, "rules", ro);
		const AnswerConfig& a = p.answer;
		json_t* ao = json_object();
		json_object_set_new(ao, "mode", json_integer((int) a.mode));
		json_object_set_new(ao, "source", json_integer(a.source));
		json_object_set_new(ao, "displace", json_integer(a.displace));
		json_object_set_new(ao, "transpose", json_integer(a.transpose));
		json_object_set_new(ao, "fill", json_integer(a.fill));
		json_object_set_new(o, "answer", ao);
		json_t* mx = json_array();
		for (int i = 0; i < NMATRIX; i++) {
			const MatrixSlot& m = p.matrix[i];
			json_t* ma = json_array();
			json_array_append_new(ma, json_integer(m.source));
			json_array_append_new(ma, json_integer(m.target));
			json_array_append_new(ma, json_integer((int) m.action));
			json_array_append_new(ma, json_real(m.amount));
			json_array_append_new(mx, ma);
		}
		json_object_set_new(o, "matrix", mx);
		return o;
	}

	static int gi(json_t* o, const char* k, int def) {
		json_t* v = json_object_get(o, k);
		return v ? (int) json_integer_value(v) : def;
	}
	static bool gb(json_t* o, const char* k, bool def) {
		json_t* v = json_object_get(o, k);
		return v ? json_is_true(v) : def;
	}
	static float gf(json_t* o, const char* k, float def) {
		json_t* v = json_object_get(o, k);
		return v ? (float) json_number_value(v) : def;
	}

	static void patternFromJson(json_t* o, Pattern& p) {
		p.clear();
		if (!json_is_object(o))
			return;
		json_t* lanes = json_object_get(o, "lanes");
		for (int l = 0; l < NL && json_is_array(lanes); l++) {
			json_t* lo = json_array_get(lanes, l);
			if (!lo)
				break;
			LaneSettings& ls = p.lanes[l];
			ls.start = (uint8_t) clamp(gi(lo, "start", 0), 0, NS - 1);
			ls.length = (uint8_t) clamp(gi(lo, "length", 16), 1, NS);
			ls.ratio = (uint8_t) clamp(gi(lo, "ratio", 4), 0, NRATIOS - 1);
			ls.dir = (Dir) clamp(gi(lo, "dir", 0), 0, (int) Dir::COUNT - 1);
			ls.skip = (uint8_t) clamp(gi(lo, "skip", 3), 2, 7);
			ls.clock = (ClockSrc) clamp(gi(lo, "clock", 0), 0, (int) ClockSrc::COUNT - 1);
			ls.extDiv = (uint8_t) clamp(gi(lo, "extDiv", 1), 1, 16);
			ls.glide = gb(lo, "glide", false);
			json_t* st = json_object_get(lo, "steps");
			for (int s = 0; s < NS && json_is_array(st); s++)
				stepFromJson(json_array_get(st, s), p.steps[l][s]);
			json_t* path = json_object_get(lo, "path");
			if (json_is_array(path)) {
				int n = std::min<int>(json_array_size(path), NS);
				p.pathLen[l] = (uint8_t) n;
				for (int s = 0; s < n; s++)
					p.path[l][s] = (uint8_t) clamp(ji(path, s), 0, NS - 1);
			}
		}
		p.root = (uint8_t) clamp(gi(o, "root", 0), 0, 11);
		p.scale = (uint8_t) clamp(gi(o, "scale", 0), 0, NUM_SCALES - 1);
		p.followT1 = gb(o, "followT1", false);
		p.transpose = (int8_t) clamp(gi(o, "transpose", 0), -14, 14);
		p.swing = (uint8_t) clamp(gi(o, "swing", 50), 50, 75);
		p.seed = (uint16_t) clamp(gi(o, "seed", 1), 0, 999);
		json_t* ro = json_object_get(o, "rules");
		if (ro) {
			RulesConfig& r = p.rules;
			r.intervals = (uint8_t) gi(ro, "intervals", 6);
			r.contour = (Contour) clamp(gi(ro, "contour", 0), 0, (int) Contour::COUNT - 1);
			r.lo = (uint8_t) clamp(gi(ro, "lo", 0), 0, 15);
			r.hi = (uint8_t) clamp(gi(ro, "hi", 10), 0, 15);
			r.maxRepeat = (uint8_t) clamp(gi(ro, "maxRepeat", 2), 1, 4);
			r.densMin = (uint8_t) clamp(gi(ro, "densMin", 25), 0, 100);
			r.densMax = (uint8_t) clamp(gi(ro, "densMax", 60), 0, 100);
			r.mustHit = (uint32_t) gi(ro, "mustHit", 0);
			r.leapRecovery = gb(ro, "leap", true);
			r.endOn = (EndOn) clamp(gi(ro, "endOn", 0), 0, (int) EndOn::COUNT - 1);
		}
		json_t* ao = json_object_get(o, "answer");
		if (ao) {
			AnswerConfig& a = p.answer;
			a.mode = (AnswerMode) clamp(gi(ao, "mode", 0), 0, (int) AnswerMode::COUNT - 1);
			a.source = (uint8_t) clamp(gi(ao, "source", 0), 0, NT - 1);
			a.displace = (uint8_t) clamp(gi(ao, "displace", 0), 0, 15);
			a.transpose = (int8_t) clamp(gi(ao, "transpose", 0), -7, 7);
			a.fill = (uint8_t) clamp(gi(ao, "fill", 100), 0, 100);
		}
		json_t* mx = json_object_get(o, "matrix");
		for (int i = 0; i < NMATRIX && json_is_array(mx); i++) {
			json_t* ma = json_array_get(mx, i);
			if (!ma)
				break;
			MatrixSlot& m = p.matrix[i];
			m.source = (uint8_t) clamp(ji(ma, 0), 0, MATRIX_SRC_COUNT - 1);
			m.target = (uint8_t) clamp(ji(ma, 1, 1), 0, NL - 1);
			m.action = (MatrixAction) clamp(ji(ma, 2, 1), 0, (int) MatrixAction::COUNT - 1);
			m.amount = clamp(jf(ma, 3, 0.5f), -1.f, 1.f);
		}
	}

	json_t* dataToJson() override {
		wa::Engine& E = *eng;
		json_t* root = json_object();
		json_object_set_new(root, "schemaVersion", json_integer(1));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "ppStep", json_integer(ppStep));
		json_t* tracks = json_array();
		for (int t = 0; t < NT; t++) {
			const TrackSettings& S = E.ts[t];
			json_t* to = json_object();
			// save the live edit into its slot first
			json_object_set_new(to, "edit", patternToJson(E.edit[t]));
			json_object_set_new(to, "home", patternToJson(E.home[t]));
			json_t* slots = json_array();
			for (int s = 0; s < NSLOT; s++) {
				if (S.used[s]) {
					json_t* so = patternToJson(E.slots[t][s]);
					json_object_set_new(so, "slotIndex", json_integer(s));
					json_object_set_new(so, "locked", json_boolean(S.locked[s]));
					json_object_set_new(so, "stamp", json_integer(E.keepStamp[t][s]));
					json_array_append_new(slots, so);
				}
			}
			json_object_set_new(to, "slots", slots);
			json_object_set_new(to, "slot", json_integer(S.slot));
			json_object_set_new(to, "sync", json_integer((int) S.sync));
			json_object_set_new(to, "syncBars", json_integer(S.syncBars));
			json_object_set_new(to, "quant", json_integer((int) S.quant));
			json_object_set_new(to, "glide", json_real(S.glide));
			json_object_set_new(to, "poly", json_integer(S.poly));
			json_object_set_new(to, "pitchFollows", json_boolean(S.pitchFollows));
			json_object_set_new(to, "mute", json_boolean(S.mute));
			json_object_set_new(to, "morphOn", json_boolean(S.morphOn));
			json_object_set_new(to, "arc", json_real(S.arc));
			json_object_set_new(to, "drift", json_real(S.drift));
			json_object_set_new(to, "density", json_real(S.density));
			json_object_set_new(to, "spread", json_real(S.spread));
			json_object_set_new(to, "mx", json_real(S.mx));
			json_object_set_new(to, "my", json_real(S.my));
			json_t* pts = json_array();
			for (int j = 0; j < NPOINT; j++) {
				json_t* pa = json_array();
				json_array_append_new(pa, json_integer(S.points[j].active ? S.points[j].slot : -1));
				json_array_append_new(pa, json_real(S.points[j].x));
				json_array_append_new(pa, json_real(S.points[j].y));
				json_array_append_new(pts, pa);
			}
			json_object_set_new(to, "points", pts);
			json_array_append_new(tracks, to);
		}
		json_object_set_new(root, "tracks", tracks);
		json_t* songs = json_array();
		for (int i = 0; i < NSONG; i++) {
			json_t* sa = json_array();
			for (int r = 0; r < E.songs[i].count; r++) {
				const SongRow& R = E.songs[i].rows[r];
				json_t* ra = json_array();
				for (int t = 0; t < NT; t++)
					json_array_append_new(ra, json_integer(R.pattern[t]));
				json_array_append_new(ra, json_integer(R.repeats));
				json_array_append_new(sa, ra);
			}
			json_array_append_new(songs, sa);
		}
		json_object_set_new(root, "songs", songs);
		json_object_set_new(root, "songIdx", json_integer(E.songIdx));
		json_object_set_new(root, "songOn", json_boolean(E.songOn));
		json_t* us = json_array();
		for (int i = 0; i < NUM_USER_SCALES; i++)
			json_array_append_new(us, json_integer(E.scales.userMask[i]));
		json_object_set_new(root, "userScales", us);
		json_t* uc = json_array();
		for (int i = 0; i < NUM_USER_CHORDS; i++) {
			json_t* ca = json_array();
			for (int j = 0; j < MAX_CHORD; j++)
				json_array_append_new(ca, json_integer(E.scales.userChord[i][j]));
			json_array_append_new(ca, json_integer(E.scales.userChordSize[i]));
			json_array_append_new(uc, ca);
		}
		json_object_set_new(root, "userChords", uc);
		json_t* op = json_object();
		json_object_set_new(op, "fillAmount", json_real(E.opt.fillAmount));
		json_object_set_new(op, "arcVel", json_real(E.opt.arcVel));
		json_object_set_new(op, "accentAmount", json_real(E.opt.accentAmount));
		json_object_set_new(op, "keepAllFromInput", json_boolean(E.opt.keepAllFromInput));
		json_object_set_new(op, "freezeLen", json_integer(E.opt.freezeLen));
		json_object_set_new(op, "muteQuant", json_integer(E.opt.muteQuant));
		json_object_set_new(op, "gestureMode", json_integer(E.opt.gestureMode));
		json_object_set_new(op, "gestureFromExp", json_boolean(E.opt.gestureFromExp));
		json_object_set_new(op, "runIsGate", json_boolean(E.opt.runIsGate));
		json_object_set_new(root, "options", op);
		json_object_set_new(root, "selTrack", json_integer(E.selTrack));
		return root;
	}

	void dataFromJson(json_t* root) override {
		wa::Engine& E = *eng;
		E.initialize();
		running = gb(root, "running", true);
		ppStep = clamp(gi(root, "ppStep", 1), 1, 12);
		json_t* tracks = json_object_get(root, "tracks");
		for (int t = 0; t < NT && json_is_array(tracks); t++) {
			json_t* to = json_array_get(tracks, t);
			if (!to)
				break;
			TrackSettings& S = E.ts[t];
			S = TrackSettings();
			json_t* slots = json_object_get(to, "slots");
			size_t i;
			json_t* so;
			json_array_foreach(slots, i, so) {
				int s = clamp(gi(so, "slotIndex", (int) i), 0, NSLOT - 1);
				patternFromJson(so, E.slots[t][s]);
				S.used[s] = true;
				S.locked[s] = gb(so, "locked", false);
				E.keepStamp[t][s] = (uint32_t) gi(so, "stamp", 0);
				E.stampCounter = std::max(E.stampCounter, E.keepStamp[t][s] + 1);
			}
			patternFromJson(json_object_get(to, "edit"), E.edit[t]);
			json_t* ho = json_object_get(to, "home");
			if (ho)
				patternFromJson(ho, E.home[t]);
			else
				E.home[t] = E.edit[t];
			S.slot = (int8_t) clamp(gi(to, "slot", 0), -1, NSLOT - 1);
			S.sync = (SyncMode) clamp(gi(to, "sync", 0), 0, (int) SyncMode::COUNT - 1);
			S.syncBars = (uint8_t) clamp(gi(to, "syncBars", 4), 1, 16);
			S.quant = (ChangeQuant) clamp(gi(to, "quant", 2), 0, (int) ChangeQuant::COUNT - 1);
			S.glide = clamp(gf(to, "glide", 0.1f), 0.f, 2.f);
			S.poly = (uint8_t) clamp(gi(to, "poly", 0), 0, 4);
			S.pitchFollows = gb(to, "pitchFollows", false);
			S.mute = gb(to, "mute", false);
			S.morphOn = gb(to, "morphOn", false);
			S.arc = clamp(gf(to, "arc", 1.f), 0.f, 1.f);
			S.drift = clamp(gf(to, "drift", 0.f), 0.f, 1.f);
			S.density = clamp(gf(to, "density", 0.f), -1.f, 1.f);
			S.spread = clamp(gf(to, "spread", 0.f), 0.f, 1.f);
			S.mx = clamp(gf(to, "mx", 0.5f), 0.f, 1.f);
			S.my = clamp(gf(to, "my", 0.5f), 0.f, 1.f);
			json_t* pts = json_object_get(to, "points");
			for (int j = 0; j < NPOINT && json_is_array(pts); j++) {
				json_t* pa = json_array_get(pts, j);
				if (!pa)
					break;
				int slot = ji(pa, 0, -1);
				S.points[j].active = slot >= 0 && slot < NSLOT;
				S.points[j].slot = (int8_t) (S.points[j].active ? slot : -1);
				S.points[j].x = clamp(jf(pa, 1), 0.f, 1.f);
				S.points[j].y = clamp(jf(pa, 2), 0.f, 1.f);
			}
			if (S.slot >= 0 && !S.used[S.slot])
				S.used[S.slot] = true;
			E.dirty[t] = true;
		}
		json_t* songs = json_object_get(root, "songs");
		for (int s = 0; s < NSONG && json_is_array(songs); s++) {
			json_t* sa = json_array_get(songs, s);
			if (!sa)
				break;
			Song& S = E.songs[s];
			S.count = (uint8_t) std::min<size_t>(json_array_size(sa), NROW);
			for (int r = 0; r < S.count; r++) {
				json_t* ra = json_array_get(sa, r);
				for (int t = 0; t < NT; t++)
					S.rows[r].pattern[t] = (int8_t) clamp(ji(ra, t, -1), -1, NSLOT - 1);
				S.rows[r].repeats = (uint8_t) clamp(ji(ra, NT, 1), 0, 64);
			}
		}
		E.songIdx = clamp(gi(root, "songIdx", 0), 0, NSONG - 1);
		E.songOn = gb(root, "songOn", false);
		json_t* us = json_object_get(root, "userScales");
		for (int i = 0; i < NUM_USER_SCALES && json_is_array(us); i++)
			E.scales.userMask[i] = (uint16_t) (ji(us, i, E.scales.userMask[i]) & 0xFFF);
		json_t* uc = json_object_get(root, "userChords");
		for (int i = 0; i < NUM_USER_CHORDS && json_is_array(uc); i++) {
			json_t* ca = json_array_get(uc, i);
			if (!ca)
				break;
			for (int j = 0; j < MAX_CHORD; j++)
				E.scales.userChord[i][j] = (int8_t) clamp(ji(ca, j), -7, 14);
			E.scales.userChordSize[i] = (uint8_t) clamp(ji(ca, MAX_CHORD, 3), 1, MAX_CHORD);
		}
		json_t* op = json_object_get(root, "options");
		if (op) {
			E.opt.fillAmount = gf(op, "fillAmount", 25.f);
			E.opt.arcVel = gf(op, "arcVel", 1.f);
			E.opt.accentAmount = gf(op, "accentAmount", 2.f);
			E.opt.keepAllFromInput = gb(op, "keepAllFromInput", false);
			E.opt.freezeLen = clamp(gi(op, "freezeLen", 8), 1, 8);
			E.opt.muteQuant = clamp(gi(op, "muteQuant", 1), 0, 2);
			E.opt.gestureMode = clamp(gi(op, "gestureMode", 0), 0, 1);
			E.opt.gestureFromExp = gb(op, "gestureFromExp", false);
			E.opt.runIsGate = gb(op, "runIsGate", false);
		}
		E.selTrack = clamp(gi(root, "selTrack", 0), 0, NT - 1);
		E.resetAll();
		for (int t = 0; t < NT; t++)
			E.rebuildPlay(t);
	}
};

// ===================================================================== UI helpers

namespace {

std::string srcName(int code) {
	if (code <= 0)
		return "-";
	if (code <= NT * NL)
		return string::f("T%d%s", (code - 1) / NL + 1, LANE_SHORT[(code - 1) % NL]);
	return string::f("SRC%d", code - NT * NL);
}

std::string laneValueText(int lane, float v) {
	switch (lane) {
		case L_GATE: return v > 0.5f ? "ON" : "-";
		case L_NOTE: return string::f("%d", (int) v);
		case L_OCT: return string::f("%+d", (int) v);
		case L_CHORD: return chordName((int) v);
		case L_VEL: return string::f("%.1f", v);
		case L_LEN: return string::f("%d%%", (int) v);
		case L_RAT: {
			const char* ramp[3] = {"", "/", "\\"};
			return string::f("x%d%s", ratchetCount(v), ramp[ratchetRamp(v)]);
		}
		default: return string::f("%+.2f", v);
	}
}

/** Label, value and ring position for each encoder on the current page. */
bool encInfo(const Snapshot& s, int e, std::string& label, std::string& value, float& frac) {
	const Pattern& P = s.editPat;
	const TrackSettings& S = s.ts[s.track];
	label = value = "";
	frac = -1.f;
	if (e >= E_LEN) {
		const LaneSettings& ls = P.lanes[s.lane];
		switch (e) {
			case E_LEN: frac = (ls.length - 1) / 31.f; return true;
			case E_START: frac = ls.start / 31.f; return true;
			case E_CLOCK: frac = ls.clock == ClockSrc::Track ? ls.ratio / 20.f : (14 + (int) ls.clock) / 20.f; return true;
			case E_DIR: frac = (int) ls.dir / 5.f; return true;
			case E_SEED: frac = P.seed / 999.f; return true;
			case E_ARC: frac = S.arc; return true;
			case E_DRIFT: frac = S.drift; return true;
			case E_DENS: frac = 0.5f + 0.5f * S.density; return true;
			case E_SPREAD: frac = S.spread; return true;
			case E_X: frac = S.mx; return true;
			case E_Y: frac = S.my; return true;
		}
		return false;
	}
	int k = e;
	int raw = k + (s.hi ? 16 : 0);
	switch (s.page) {
		case P_LANES: {
			const Step& st = P.steps[s.lane][raw];
			label = string::f("%d", raw + 1);
			value = laneValueText(s.lane, st.value);
			frac = normValue(s.lane, st.value);
			return true;
		}
		case P_STEP: {
			const Step& st = P.steps[s.lane][s.selStep];
			switch (k) {
				case 0: label = "PROB"; value = string::f("%d%%", st.prob); frac = st.prob / 100.f; return true;
				case 1: label = "COND"; value = COND_NAMES[(int) st.cond]; frac = (int) st.cond / 7.f; return true;
				case 2: label = "A"; value = string::f("%d", st.condA); frac = (st.condA - 1) / 7.f; return true;
				case 3: label = "B"; value = string::f("%d", st.condB); frac = (st.condB - 1) / 7.f; return true;
				case 4: label = "ALT"; value = st.hasAlt ? laneValueText(s.lane, st.alt) : "-"; frac = st.hasAlt ? normValue(s.lane, st.alt) : 0.f; return true;
				case 5: label = "SLIDE"; value = st.slide ? "ON" : "off"; frac = st.slide; return true;
				case 6: label = "ACCENT"; value = st.accent ? "ON" : "off"; frac = st.accent; return true;
				case 7: label = "NUDGE"; value = string::f("%+d%%", st.nudge); frac = (st.nudge + 49) / 98.f; return true;
				case 8: {
					const Step& g = P.steps[L_GATE][s.selStep];
					label = "ROLE";
					value = ROLE_NAMES[(int) g.role];
					frac = (int) g.role / 3.f;
					return true;
				}
				case 9: {
					const char* pins[3] = {"-", "FIRED", "NOT"};
					label = "PIN";
					value = pins[(int) st.pin];
					frac = (int) st.pin / 2.f;
					return true;
				}
			}
			return false;
		}
		case P_PATTERN:
			switch (k) {
				case 0: label = "SYNC"; value = SYNC_NAMES[(int) S.sync]; frac = (int) S.sync / 2.f; return true;
				case 1: label = "BARS"; value = string::f("%d", S.syncBars); frac = (S.syncBars - 1) / 15.f; return true;
				case 2: label = "CHANGE"; value = QUANT_NAMES[(int) S.quant]; frac = (int) S.quant / 3.f; return true;
				case 3: label = "SWING"; value = string::f("%d%%", P.swing); frac = (P.swing - 50) / 25.f; return true;
				case 4: label = "GLIDE"; value = string::f("%.2fs", S.glide); frac = S.glide / 2.f; return true;
				case 5: label = "POLY"; value = S.poly == 0 ? "AUTO" : string::f("%d", S.poly); frac = S.poly / 4.f; return true;
				case 6: label = "P.FOLLOW"; value = S.pitchFollows ? "ON" : "off"; frac = S.pitchFollows; return true;
			}
			return false;
		case P_ROLE: {
			const Step& g = P.steps[L_GATE][raw];
			label = string::f("%d", raw + 1);
			value = std::string(ROLE_NAMES[(int) g.role]).substr(0, 4);
			return true;
		}
		case P_RULES: {
			const RulesConfig& R = P.rules;
			const char* iv[8] = {"UNI", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "OCT"};
			if (k < 8) {
				label = iv[k];
				bool on = R.intervals & (1 << k);
				value = on ? "ON" : "off";
				frac = on;
				return true;
			}
			switch (k) {
				case 8: label = "CONTOUR"; value = CONTOUR_NAMES[(int) R.contour]; frac = (int) R.contour / 5.f; return true;
				case 9: label = "LOW"; value = string::f("%d", R.lo); frac = R.lo / 15.f; return true;
				case 10: label = "HIGH"; value = string::f("%d", R.hi); frac = R.hi / 15.f; return true;
				case 11: label = "REPEAT"; value = string::f("%d", R.maxRepeat); frac = (R.maxRepeat - 1) / 3.f; return true;
				case 12: label = "DENS MIN"; value = string::f("%d%%", R.densMin); frac = R.densMin / 100.f; return true;
				case 13: label = "DENS MAX"; value = string::f("%d%%", R.densMax); frac = R.densMax / 100.f; return true;
				case 14: label = "LEAP REC"; value = R.leapRecovery ? "ON" : "off"; frac = R.leapRecovery; return true;
				case 15: label = "END ON"; value = ENDON_NAMES[(int) R.endOn]; frac = (int) R.endOn / 2.f; return true;
			}
			return false;
		}
		case P_ANSWER: {
			if (s.track == 0) {
				if (k == 0) {
					label = "ANSWER";
					value = "T2/T3";
				}
				return k == 0;
			}
			const AnswerConfig& A = P.answer;
			switch (k) {
				case 0: label = "MODE"; value = ANSWER_NAMES[(int) A.mode]; frac = (int) A.mode / 4.f; return true;
				case 1: label = "SOURCE"; value = string::f("T%d", A.source + 1); frac = A.source / 1.f; return true;
				case 2: label = "DISPLACE"; value = string::f("%d", A.displace); frac = A.displace / 15.f; return true;
				case 3: label = "TRANSP"; value = string::f("%+d", A.transpose); frac = (A.transpose + 7) / 14.f; return true;
				case 4: label = "FILL"; value = string::f("%d%%", A.fill); frac = A.fill / 100.f; return true;
			}
			return false;
		}
		case P_MORPH: {
			if (k > 1 || s.selPoint < 0)
				return false;
			const MorphPoint& mp = S.points[s.selPoint];
			label = k == 0 ? "POINT X" : "POINT Y";
			float v = k == 0 ? mp.x : mp.y;
			value = string::f("%.2f", v);
			frac = v;
			return true;
		}
		case P_MATRIX: {
			const MatrixSlot& m = P.matrix[k / 4];
			int q = k / 4 + 1;
			switch (k % 4) {
				case 0: label = string::f("%d SRC", q); value = srcName(m.source); frac = m.source / (float) (MATRIX_SRC_COUNT - 1); return true;
				case 1: label = string::f("%d LANE", q); value = LANE_NAMES[m.target]; frac = m.target / 8.f; return true;
				case 2: label = string::f("%d ACT", q); value = MATRIX_ACTION_NAMES[(int) m.action]; frac = (int) m.action / 5.f; return true;
				case 3: label = string::f("%d AMT", q); value = string::f("%+.2f", m.amount); frac = 0.5f + 0.5f * m.amount; return true;
			}
			return false;
		}
		case P_SONG: {
			const Song& S2 = s.song;
			const SongRow* R = S2.count > 0 ? &S2.rows[clamp(s.songRow, 0, S2.count - 1)] : nullptr;
			switch (k) {
				case 0: label = "SONG"; value = string::f("%d", s.songIdx + 1); frac = s.songIdx / 15.f; return true;
				case 1: label = "ROW"; value = S2.count ? string::f("%d/%d", s.songRow + 1, S2.count) : "-"; frac = S2.count ? s.songRow / (float) std::max(1, S2.count - 1) : 0.f; return true;
				case 2:
				case 3:
				case 4: {
					label = string::f("T%d PAT", k - 1);
					int p = R ? R->pattern[k - 2] : -1;
					value = p < 0 ? "-" : string::f("%d", p + 1);
					frac = (p + 1) / 16.f;
					return true;
				}
				case 5: label = "REPEAT"; value = R ? (R->repeats == 0 ? "NEXT" : string::f("%d", R->repeats)) : "-"; frac = R ? R->repeats / 64.f : 0.f; return true;
				case 6: label = "PLAY"; value = s.songOn ? "ON" : "off"; frac = s.songOn; return true;
			}
			return false;
		}
		case P_SCALE: {
			switch (k) {
				case 0: label = "ROOT"; value = noteName(P.root); frac = P.root / 11.f; return true;
				case 1: label = "SCALE"; value = scaleName(P.scale); frac = P.scale / (float) (NUM_SCALES - 1); return true;
				case 2:
					label = "FOLLOW";
					value = s.track == 0 ? "-" : (P.followT1 ? "T1" : "off");
					frac = P.followT1;
					return true;
				case 3: label = "TRANSP"; value = string::f("%+d", P.transpose); frac = (P.transpose + 14) / 28.f; return true;
				case 8: label = "U.CHORD"; value = string::f("%d", s.selUserChord + 1); frac = s.selUserChord / 2.f; return true;
				case 9:
				case 10:
				case 11:
				case 12: {
					int j = k - 9;
					label = string::f("DEG %d", j + 1);
					bool used = j < s.scales.userChordSize[s.selUserChord];
					int v = s.scales.userChord[s.selUserChord][j];
					value = used ? string::f("%+d", v) : "(off)";
					frac = (v + 7) / 21.f;
					return true;
				}
				case 13: label = "SIZE"; value = string::f("%d", s.scales.userChordSize[s.selUserChord]); frac = (s.scales.userChordSize[s.selUserChord] - 1) / 3.f; return true;
			}
			return false;
		}
	}
	return false;
}

} // namespace

// ===================================================================== Widgets

/** Endless knob that sends detents to the module (spec §2.3). */
struct EncoderKnob : widget::OpaqueWidget {
	Lattice* module = nullptr;
	int index = 0;
	float acc = 0.f;
	float radiusMm = 3.6f;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			e.consume(this);
		}
	}
	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		acc = 0.f;
		APP->window->cursorLock();
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		APP->window->cursorUnlock();
	}
	void send(int d) {
		if (module && d != 0)
			module->encDelta[index].fetch_add(d);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		int mods = APP->window->getMods();
		float px = 8.f;
		if (mods & GLFW_MOD_SHIFT)
			px = 32.f;
		float mult = (mods & RACK_MOD_CTRL) ? 4.f : 1.f;
		acc += (-e.mouseDelta.y + e.mouseDelta.x) / APP->scene->rackScroll->getZoom();
		int d = (int) (acc / px);
		if (d != 0) {
			acc -= d * px;
			send((int) (d * mult));
		}
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		float y = e.scrollDelta.y;
		if (y != 0.f) {
			send(y > 0 ? 1 : -1);
			e.consume(this);
		}
	}
	void draw(const DrawArgs& args) override {
		float r = box.size.x / 2.f;
		Vec c = box.size.div(2);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, r * 0.78f);
		nvgFillColor(args.vg, nvgRGB(0x14, 0x14, 0x15));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x50, 0x50, 0x55));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		// ring of 16 segments
		float frac = -1.f;
		int track = 0;
		if (module) {
			const Snapshot& s = module->eng->snapshot();
			std::string l, v;
			encInfo(s, index, l, v, frac);
			track = s.track;
		}
		for (int i = 0; i < 16; i++) {
			float a = (float) (M_PI * (0.75 + 1.5 * i / 15.0));
			bool lit = frac >= 0.f && i <= (int) std::round(frac * 15.f);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x + std::cos(a) * r * 0.95f, c.y + std::sin(a) * r * 0.95f, r * 0.07f);
			nvgFillColor(args.vg, lit ? TRACK_COLORS[track] : nvgRGB(0x30, 0x30, 0x33));
			nvgFill(args.vg);
		}
		// pointer
		float a = (float) (M_PI * (0.75 + 1.5 * (frac >= 0.f ? frac : 0.5f)));
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, c.x + std::cos(a) * r * 0.25f, c.y + std::sin(a) * r * 0.25f);
		nvgLineTo(args.vg, c.x + std::cos(a) * r * 0.7f, c.y + std::sin(a) * r * 0.7f);
		nvgStrokeColor(args.vg, nvgRGB(0xd8, 0xd8, 0xd8));
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);
	}
};

/** Panel labels drawn with NanoVG (Rack does not render SVG text). */
struct PanelLabels : widget::Widget {
	struct Label {
		Vec pos;
		std::string text;
		NVGcolor color;
		float size;
	};
	std::vector<Label> labels;

	void add(float xMm, float yMm, const std::string& text, NVGcolor color = nvgRGB(0xd8, 0xd8, 0xd8), float size = 6.f) {
		labels.push_back({mm2px(Vec(xMm, yMm)), text, color, size});
	}
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (const Label& l : labels) {
			nvgFontSize(args.vg, l.size);
			nvgFillColor(args.vg, l.color);
			nvgText(args.vg, l.pos.x, l.pos.y, l.text.c_str(), nullptr);
		}
	}
};

struct LatticeDisplay : widget::TransparentWidget {
	Lattice* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		float W = box.size.x, H = box.size.y;
		if (!module) {
			nvgFontSize(vg, 18.f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, TRACK_COLORS[0]);
			nvgText(vg, W / 2, H / 2, "LATTICE", nullptr);
			return;
		}
		const Snapshot& s = module->eng->snapshot();
		NVGcolor tc = TRACK_COLORS[s.track];
		NVGcolor dim = nvgRGB(0x60, 0x60, 0x64);
		NVGcolor text = nvgRGB(0xd8, 0xd8, 0xd8);

		// header
		nvgFontSize(vg, 10.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		const TrackSettings& S = s.ts[s.track];
		std::string head = string::f("T%d  P%d%s  %s  %s", s.track + 1, S.slot + 1, s.queued[s.track] >= 0 ? string::f(">%d", s.queued[s.track] + 1).c_str() : "",
			PAGE_NAMES[s.page], LANE_NAMES[s.lane]);
		nvgFillColor(vg, tc);
		nvgText(vg, 4, 2, head.c_str(), nullptr);
		std::string right = string::f("%s%s%s%s %.0f BPM %s", S.morphOn ? "MORPH " : "", s.pat.answer.mode != AnswerMode::Off && s.track > 0 ? "ANS " : "",
			S.drift > 0.f ? "DRIFT " : "", s.expander ? "X " : "", s.bpm, s.running ? "" : "STOP");
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgFillColor(vg, dim);
		nvgText(vg, W - 4, 2, right.c_str(), nullptr);
		if (s.msg[0]) {
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
			nvgText(vg, W * 0.52f, 2, s.msg, nullptr);
		}
		float top = 14.f;

		if (s.page == P_LANES) {
			drawLanes(vg, s, top, W, H - top);
			return;
		}
		// knob-aligned grid of labels and values
		nvgFontSize(vg, 8.f);
		for (int k = 0; k < 16; k++) {
			std::string l, v;
			float f;
			if (!encInfo(s, k, l, v, f))
				continue;
			float x = mm2px(stepX(k) - DISPLAY_X);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, dim);
			nvgText(vg, x, top + 2, l.c_str(), nullptr);
			nvgFillColor(vg, text);
			nvgFontSize(vg, 9.f);
			nvgText(vg, x, top + 12, v.c_str(), nullptr);
			nvgFontSize(vg, 8.f);
		}
		float y2 = top + 28.f;
		switch (s.page) {
			case P_STEP: {
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(vg, tc);
				nvgFontSize(vg, 10.f);
				const Step& st = s.editPat.steps[s.lane][s.selStep];
				std::string d = string::f("Step %d of %s lane: value %s", s.selStep + 1, LANE_NAMES[s.lane], laneValueText(s.lane, st.value).c_str());
				nvgText(vg, 4, y2 + 8, d.c_str(), nullptr);
				nvgFillColor(vg, dim);
				nvgText(vg, 4, y2 + 24, "Step buttons pick the step. Shift+step on LANES jumps here.", nullptr);
				break;
			}
			case P_PATTERN: {
				nvgFontSize(vg, 8.f);
				for (int i = 0; i < NSLOT; i++) {
					float x = mm2px(stepX(i) - DISPLAY_X);
					bool used = S.used[i], cur = S.slot == i, q = s.queued[s.track] == i;
					nvgBeginPath(vg);
					nvgRect(vg, x - 12, y2 + 6, 24, 18);
					nvgFillColor(vg, cur ? tc : (used ? nvgRGB(0x33, 0x33, 0x36) : nvgRGB(0x16, 0x16, 0x18)));
					nvgFill(vg);
					if (q) {
						nvgStrokeColor(vg, tc);
						nvgStrokeWidth(vg, 1.5f);
						nvgStroke(vg);
					}
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, cur ? nvgRGB(0, 0, 0) : text);
					nvgText(vg, x, y2 + 15, string::f("%d%s", i + 1, S.locked[i] ? "L" : "").c_str(), nullptr);
				}
				break;
			}
			case P_ROLE:
			case P_RULES: {
				for (int i = 0; i < 16; i++) {
					int raw = i + (s.hi ? 16 : 0);
					const Step& g = s.editPat.steps[L_GATE][raw];
					float x = mm2px(stepX(i) - DISPLAY_X);
					float h = 6.f + 6.f * (int) g.role;
					nvgBeginPath(vg);
					nvgRect(vg, x - 10, y2 + 36 - h, 20, h);
					NVGcolor c = tc;
					c.a = g.value > 0.5f ? 1.f : 0.2f;
					nvgFillColor(vg, c);
					nvgFill(vg);
					if (s.page == P_RULES && (s.editPat.rules.mustHit & (1u << raw))) {
						nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
						nvgStrokeWidth(vg, 1.5f);
						nvgStroke(vg);
					}
				}
				break;
			}
			case P_MORPH: {
				float size = H - top - 4;
				float x0 = W - size - 8, y0 = top + 2;
				nvgBeginPath(vg);
				nvgRect(vg, x0, y0, size, size);
				nvgStrokeColor(vg, dim);
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
				for (int j = 0; j < NPOINT; j++) {
					const MorphPoint& mp = S.points[j];
					if (!mp.active)
						continue;
					float px = x0 + mp.x * size, py = y0 + (1.f - mp.y) * size;
					nvgBeginPath(vg);
					nvgCircle(vg, px, py, 3.f + 8.f * s.morphW[j]);
					NVGcolor c = tc;
					c.a = j == s.selPoint ? 1.f : 0.6f;
					nvgFillColor(vg, c);
					nvgFill(vg);
					nvgFontSize(vg, 8.f);
					nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, text);
					nvgText(vg, px + 6, py, string::f("%d", mp.slot + 1).c_str(), nullptr);
				}
				float cx = x0 + S.mx * size, cy = y0 + (1.f - S.my) * size;
				nvgBeginPath(vg);
				nvgMoveTo(vg, cx - 5, cy);
				nvgLineTo(vg, cx + 5, cy);
				nvgMoveTo(vg, cx, cy - 5);
				nvgLineTo(vg, cx, cy + 5);
				nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
				nvgStrokeWidth(vg, 1.5f);
				nvgStroke(vg);
				nvgFontSize(vg, 9.f);
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(vg, dim);
				nvgText(vg, 4, y2 + 10, S.morphOn ? "MORPH ON: X/Y knobs move through kept patterns" : "MORPH OFF: press MORPH to play the map", nullptr);
				nvgText(vg, 4, y2 + 22, "Step button = add/select a kept slot, Shift+step = remove", nullptr);
				break;
			}
			case P_SONG: {
				nvgFontSize(vg, 9.f);
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				const Song& S2 = s.song;
				int startRow = std::max(0, s.songRow - 2);
				for (int r = startRow; r < std::min<int>(S2.count, startRow + 5); r++) {
					const SongRow& R = S2.rows[r];
					std::string line = string::f("%s%2d  T1:%s T2:%s T3:%s  x%s", r == s.songRow ? ">" : " ", r + 1,
						R.pattern[0] < 0 ? "-" : string::f("%d", R.pattern[0] + 1).c_str(),
						R.pattern[1] < 0 ? "-" : string::f("%d", R.pattern[1] + 1).c_str(),
						R.pattern[2] < 0 ? "-" : string::f("%d", R.pattern[2] + 1).c_str(),
						R.repeats == 0 ? "NEXT" : string::f("%d", R.repeats).c_str());
					nvgFillColor(vg, r == s.songRow ? tc : text);
					nvgText(vg, mm2px(150.f), top + 2 + (r - startRow) * 11, line.c_str(), nullptr);
				}
				nvgFillColor(vg, dim);
				nvgText(vg, 4, y2 + 22, "Step 1 = insert row, step 2 = delete row", nullptr);
				break;
			}
			case P_SCALE: {
				ScaleDef sc = getScale(s.editPat.scale, s.scales);
				for (int i = 0; i < 12; i++) {
					bool in = false;
					for (int k = 0; k < sc.n; k++)
						if (sc.semis[k] == i)
							in = true;
					float x = mm2px(stepX(i) - DISPLAY_X);
					nvgBeginPath(vg);
					nvgRect(vg, x - 10, y2 + 8, 20, 14);
					nvgFillColor(vg, in ? tc : nvgRGB(0x16, 0x16, 0x18));
					nvgFill(vg);
					nvgFontSize(vg, 8.f);
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, in ? nvgRGB(0, 0, 0) : dim);
					nvgText(vg, x, y2 + 15, noteName(s.editPat.root + i), nullptr);
				}
				break;
			}
			case P_ANSWER:
				if (s.track == 0) {
					nvgFontSize(vg, 10.f);
					nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
					nvgFillColor(vg, dim);
					nvgText(vg, 4, y2 + 10, "Track 1 is the caller. Select track 2 or 3 to make it answer.", nullptr);
				}
				break;
			default: break;
		}
	}

	void drawLanes(NVGcontext* vg, const Snapshot& s, float top, float W, float h) {
		NVGcolor tc = TRACK_COLORS[s.track];
		NVGcolor dim = nvgRGB(0x60, 0x60, 0x64);
		float rowH = h / NL;
		float labelW = 18.f, infoW = 70.f;
		float cellsW = W - labelW - infoW - 6;
		float cw = cellsW / 16.f;
		int base = s.hi ? 16 : 0;
		const Pattern& P = s.pat;
		for (int l = 0; l < NL; l++) {
			float y = top + l * rowH;
			const LaneSettings& ls = P.lanes[l];
			bool sel = l == s.lane;
			bool ans = s.track > 0 && P.answer.mode != AnswerMode::Off && (l == L_GATE || l == L_NOTE || l == L_OCT);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, sel ? tc : dim);
			nvgText(vg, 4, y + rowH / 2, LANE_SHORT[l], nullptr);
			for (int i = 0; i < 16; i++) {
				int idx = base + i;
				const Step& st = P.steps[l][idx];
				float x = labelW + i * cw;
				bool inLoop = ((idx - ls.start + NS) % NS) < ls.length;
				nvgBeginPath(vg);
				nvgRect(vg, x + 1, y + 1, cw - 2, rowH - 2);
				nvgFillColor(vg, inLoop ? nvgRGB(0x24, 0x24, 0x27) : nvgRGB(0x12, 0x12, 0x13));
				nvgFill(vg);
				NVGcolor c = tc;
				c.a = inLoop ? 1.f : 0.35f;
				float v = normValue(l, st.value);
				if (l == L_GATE) {
					if (st.value > 0.5f) {
						nvgBeginPath(vg);
						nvgRect(vg, x + 3, y + 2, cw - 6, rowH - 4);
						nvgFillColor(vg, c);
						nvgFill(vg);
					}
					if (st.role == Role::Tension) {
						nvgBeginPath(vg);
						nvgRect(vg, x + 2, y + 1.5f, cw - 4, rowH - 3);
						nvgStrokeColor(vg, nvgRGB(0xff, 0x60, 0x60));
						nvgStrokeWidth(vg, 1.f);
						nvgStroke(vg);
					}
					if (st.prob < 100) {
						nvgFontSize(vg, 7.f);
						nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
						nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
						nvgText(vg, x + cw / 2, y + rowH / 2, string::f("%d", st.prob).c_str(), nullptr);
					}
				}
				else if (l == L_NOTE || l == L_OCT || l == L_CHORD || l == L_RAT) {
					nvgFontSize(vg, 7.5f);
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, c);
					std::string tx;
					if (l == L_NOTE) {
						ScaleDef sc = getScale(P.scale, s.scales);
						tx = noteName(P.root + degreeToSemis(sc, (int) st.value));
					}
					else
						tx = laneValueText(l, st.value);
					nvgText(vg, x + cw / 2, y + rowH / 2, tx.c_str(), nullptr);
				}
				else {
					float bh = (rowH - 4) * v;
					nvgBeginPath(vg);
					nvgRect(vg, x + 4, y + rowH - 2 - bh, cw - 8, std::max(1.f, bh));
					nvgFillColor(vg, c);
					nvgFill(vg);
				}
				if (st.pin != Pin::None) {
					nvgBeginPath(vg);
					nvgCircle(vg, x + cw - 4, y + 4, 1.6f);
					nvgFillColor(vg, st.pin == Pin::Fired ? nvgRGB(0xff, 0xff, 0xff) : nvgRGB(0xff, 0x50, 0x50));
					nvgFill(vg);
				}
				if (idx == s.curIdx[l] && s.running) {
					nvgBeginPath(vg);
					nvgRect(vg, x + 0.5f, y + 0.5f, cw - 1, rowH - 1);
					nvgStrokeColor(vg, nvgRGB(0xff, 0xff, 0xff));
					nvgStrokeWidth(vg, 1.f);
					nvgStroke(vg);
				}
			}
			// lane info
			std::string info;
			if (ls.clock == ClockSrc::Track)
				info = string::f("%2d %s %s", ls.length, RATIO_NAMES[ls.ratio], DIR_NAMES[(int) ls.dir]);
			else
				info = string::f("%2d %s/%d %s", ls.length, CLOCKSRC_NAMES[(int) ls.clock], ls.extDiv, DIR_NAMES[(int) ls.dir]);
			if (ans)
				info += " ANS";
			else if (P.pathLen[l] > 0)
				info += " PATH";
			nvgFontSize(vg, 7.5f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, sel ? tc : dim);
			nvgText(vg, labelW + cellsW + 4, y + rowH / 2, info.c_str(), nullptr);
		}
	}
};

// ===================================================================== Panel

struct LatticeWidget : ModuleWidget {
	template <typename T>
	void addButton(float x, float y, int param, int light) {
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedGreenBlueLight>>>(mm2px(Vec(x, y)), module, param, light));
	}

	LatticeWidget(Lattice* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lattice.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		PanelLabels* labels = new PanelLabels;
		labels->box.size = box.size;
		NVGcolor dim = nvgRGB(0x9a, 0x9a, 0x9e);

		LatticeDisplay* disp = new LatticeDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(DISPLAY_X, DISPLAY_Y));
		disp->box.size = mm2px(Vec(DISPLAY_W, DISPLAY_H));
		addChild(disp);

		auto addEnc = [&](float x, float y, int idx, float sizeMm) {
			EncoderKnob* k = new EncoderKnob;
			k->module = module;
			k->index = idx;
			k->box.size = mm2px(Vec(sizeMm, sizeMm));
			k->box.pos = mm2px(Vec(x, y)).minus(k->box.size.div(2));
			addChild(k);
		};

		// Step row
		for (int i = 0; i < 16; i++) {
			addEnc(stepX(i), ENC_Y, E_STEP0 + i, 8.f);
			addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(stepX(i), STEP_Y)), module, Lattice::STEP_PARAM + i, Lattice::STEP_LIGHT + i * 3));
		}

		// Row 3: tracks, lanes, page/shift/hi, lane settings, transport, GEN knobs
		const char* tnames[NT] = {"T1", "T2", "T3"};
		for (int t = 0; t < NT; t++) {
			float x = 10.f + t * 8.f;
			addButton<int>(x, ROW3_Y, Lattice::TRACK_PARAM + t, Lattice::TRACK_LIGHT + t * 3);
			labels->add(x, ROW3_Y - 5.f, tnames[t], TRACK_COLORS[t]);
		}
		for (int l = 0; l < NL; l++) {
			float x = 37.f + l * 8.f;
			addButton<int>(x, ROW3_Y, Lattice::LANE_PARAM + l, Lattice::LANE_LIGHT + l * 3);
			labels->add(x, ROW3_Y - 5.f, LANE_SHORT[l], dim);
		}
		addButton<int>(111.f, ROW3_Y, Lattice::PAGE_PARAM, Lattice::PAGE_LIGHT);
		labels->add(111.f, ROW3_Y - 5.f, "PAGE", dim);
		addButton<int>(119.f, ROW3_Y, Lattice::SHIFT_PARAM, Lattice::SHIFT_LIGHT);
		labels->add(119.f, ROW3_Y - 5.f, "SHIFT", dim);
		addButton<int>(127.f, ROW3_Y, Lattice::HI_PARAM, Lattice::HI_LIGHT);
		labels->add(127.f, ROW3_Y - 5.f, "17-32", dim);
		const char* lsn[4] = {"LEN", "START", "CLOCK", "DIR"};
		for (int i = 0; i < 4; i++) {
			addEnc(140.f + i * 11.f, ROW3_Y, E_LEN + i, 7.f);
			labels->add(140.f + i * 11.f, ROW3_Y - 5.5f, lsn[i], dim);
		}
		addButton<int>(186.f, ROW3_Y, Lattice::RUN_PARAM, Lattice::RUN_LIGHT);
		labels->add(186.f, ROW3_Y - 5.f, "RUN", dim);
		addButton<int>(194.f, ROW3_Y, Lattice::RESET_PARAM, Lattice::RESET_LIGHT);
		labels->add(194.f, ROW3_Y - 5.f, "RESET", dim);
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(206.f, ROW3_Y)), module, Lattice::BPM_PARAM));
		labels->add(206.f, ROW3_Y - 5.5f, "BPM", dim);
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(219.f, ROW3_Y)), module, Lattice::CHAR_PARAM));
		labels->add(219.f, ROW3_Y - 5.5f, "CHAR", dim);
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(231.f, ROW3_Y)), module, Lattice::AMOUNT_PARAM));
		labels->add(231.f, ROW3_Y - 5.5f, "AMOUNT", dim);

		// Row 4: generate and perform
		addEnc(10.f, ROW4_Y, E_SEED, 7.f);
		labels->add(10.f, ROW4_Y - 5.5f, "SEED", dim);
		const char* gnames[4] = {"GEN", "KEEP", "UNDO", "PIN"};
		const int gparams[4] = {Lattice::GEN_PARAM, Lattice::KEEP_PARAM, Lattice::UNDO_PARAM, Lattice::PIN_PARAM};
		const int glights[4] = {Lattice::GEN_LIGHT, Lattice::KEEP_LIGHT, Lattice::UNDO_LIGHT, Lattice::PIN_LIGHT};
		for (int i = 0; i < 4; i++) {
			addButton<int>(20.f + i * 8.f, ROW4_Y, gparams[i], glights[i]);
			labels->add(20.f + i * 8.f, ROW4_Y - 5.f, gnames[i], dim);
		}
		const char* pnames[4] = {"ARC", "DRIFT", "DENSITY", "SPREAD"};
		for (int i = 0; i < 4; i++) {
			addEnc(56.f + i * 11.f, ROW4_Y, E_ARC + i, 7.f);
			labels->add(56.f + i * 11.f, ROW4_Y - 5.5f, pnames[i], dim);
		}
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(102.f, ROW4_Y)), module, Lattice::GESTURE_PARAM));
		labels->add(102.f, ROW4_Y - 6.5f, "GESTURE", dim);
		addButton<int>(113.f, ROW4_Y, Lattice::REC_PARAM, Lattice::REC_LIGHT);
		labels->add(113.f, ROW4_Y - 5.f, "REC", dim);
		addEnc(124.f, ROW4_Y, E_X, 7.f);
		labels->add(124.f, ROW4_Y - 5.5f, "X", dim);
		addEnc(135.f, ROW4_Y, E_Y, 7.f);
		labels->add(135.f, ROW4_Y - 5.5f, "Y", dim);
		const char* fnames[4] = {"HOME", "FREEZE", "MORPH", "FILL"};
		const int fparams[4] = {Lattice::HOME_PARAM, Lattice::FREEZE_PARAM, Lattice::MORPH_PARAM, Lattice::FILL_PARAM};
		const int flights[4] = {Lattice::HOME_LIGHT, Lattice::FREEZE_LIGHT, Lattice::MORPH_LIGHT, Lattice::FILL_LIGHT};
		for (int i = 0; i < 4; i++) {
			addButton<int>(146.f + i * 8.f, ROW4_Y, fparams[i], flights[i]);
			labels->add(146.f + i * 8.f, ROW4_Y - 5.f, fnames[i], dim);
		}
		for (int t = 0; t < NT; t++) {
			addButton<int>(182.f + t * 8.f, ROW4_Y, Lattice::MUTE_PARAM + t, Lattice::MUTE_LIGHT + t * 3);
			labels->add(182.f + t * 8.f, ROW4_Y - 5.f, string::f("MUTE%d", t + 1), TRACK_COLORS[t]);
		}
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(236.f, ROW4_Y)), module, Lattice::EXP_LIGHT));
		labels->add(236.f, ROW4_Y - 4.f, "X", dim, 5.f);

		// Inputs
		const char* inNames[7] = {"CLOCK", "RESET", "RUN", "TRANSP", "KEEP", "GEN", "FILL"};
		for (int i = 0; i < 7; i++) {
			float x = 10.f + i * 11.f;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, IN_Y)), module, Lattice::CLOCK_INPUT + i));
			labels->add(x, IN_Y - 6.5f, inNames[i], dim);
		}
		for (int t = 0; t < NT; t++) {
			float x = 87.f + t * 11.f;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, IN_Y)), module, Lattice::TCLK_INPUT + t));
			labels->add(x, IN_Y - 6.5f, string::f("CLK%d", t + 1), TRACK_COLORS[t]);
			float x2 = 120.f + t * 11.f;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x2, IN_Y)), module, Lattice::ARC_INPUT + t));
			labels->add(x2, IN_Y - 6.5f, string::f("ARC%d", t + 1), TRACK_COLORS[t]);
		}
		const char* oNames[4] = {"EOP", "EOS", "ALL P", "ALL G"};
		for (int i = 0; i < 4; i++) {
			float x = 164.f + i * 11.f;
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, IN_Y)), module, Lattice::EOP_OUTPUT + i));
			labels->add(x, IN_Y - 6.5f, oNames[i], dim);
		}
		// Track outputs
		const char* outNames[6] = {"PITCH", "GATE", "VEL", "ACC", "MOD A", "MOD B"};
		for (int t = 0; t < NT; t++) {
			for (int c = 0; c < 6; c++) {
				float x = 10.f + t * 78.f + c * 11.5f;
				addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, OUT_Y)), module, Lattice::PITCH_OUTPUT + c * NT + t));
				labels->add(x, OUT_Y - 6.5f, outNames[c], TRACK_COLORS[t]);
			}
		}
		labels->add(121.92f, 125.5f, "LATTICE", nvgRGB(0xd8, 0xd8, 0xd8), 7.f);
		addChild(labels);
	}

	void appendContextMenu(Menu* menu) override {
		Lattice* m = dynamic_cast<Lattice*>(module);
		if (!m)
			return;
		wa::Engine* E = m->eng;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Clock input resolution", {"1 pulse per step", "2 per step", "4 per step", "24 PPQN", "48 PPQN"},
			[=]() {
				switch (m->ppStep) {
					case 2: return 1;
					case 4: return 2;
					case 6: return 3;
					case 12: return 4;
					default: return 0;
				}
			},
			[=](size_t i) {
				const int v[5] = {1, 2, 4, 6, 12};
				m->ppStep = v[i];
			}));
		menu->addChild(createBoolPtrMenuItem("RUN input is a gate", "", &E->opt.runIsGate));
		menu->addChild(createBoolPtrMenuItem("KEEP input keeps all tracks", "", &E->opt.keepAllFromInput));
		menu->addChild(createIndexSubmenuItem("Mute quantise", {"Off", "Beat", "Bar"},
			[=]() { return E->opt.muteQuant; }, [=](size_t i) { E->opt.muteQuant = (int) i; }));
		menu->addChild(createIndexSubmenuItem("Freeze length", {"1 step", "2 steps", "4 steps", "8 steps"},
			[=]() { return E->opt.freezeLen == 1 ? 0 : E->opt.freezeLen == 2 ? 1 : E->opt.freezeLen == 4 ? 2 : 3; },
			[=](size_t i) { const int v[4] = {1, 2, 4, 8}; E->opt.freezeLen = v[i]; }));
		menu->addChild(createIndexSubmenuItem("FILL adds to probability", {"+10%", "+25%", "+50%", "+100%"},
			[=]() { float f = E->opt.fillAmount; return f <= 10 ? 0 : f <= 25 ? 1 : f <= 50 ? 2 : 3; },
			[=](size_t i) { const float v[4] = {10, 25, 50, 100}; E->opt.fillAmount = v[i]; }));
		menu->addChild(createIndexSubmenuItem("Arc velocity boost", {"0 V", "1 V", "2 V"},
			[=]() { return (int) std::round(E->opt.arcVel); }, [=](size_t i) { E->opt.arcVel = (float) i; }));
		menu->addChild(createIndexSubmenuItem("Gesture fit", {"Real time (one lane pass)", "Stretch (press REC to stop)"},
			[=]() { return E->opt.gestureMode; }, [=](size_t i) { E->opt.gestureMode = (int) i; }));
		menu->addChild(createBoolPtrMenuItem("Gesture source: expander SRC 2", "", &E->opt.gestureFromExp));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Track %d", E->selTrack + 1)));
		menu->addChild(createMenuItem("Export current pattern...", "", [=]() { exportPattern(m); }));
		menu->addChild(createMenuItem("Import pattern...", "", [=]() { importPattern(m); }));
		menu->addChild(createMenuItem("Copy lane", "", [=]() { copyLane(m); }));
		menu->addChild(createMenuItem("Paste lane", "", [=]() { pasteLane(m); }));
		menu->addChild(createMenuItem("Copy track", "", [=]() { copyTrack(m); }));
		menu->addChild(createMenuItem("Paste track", "", [=]() { pasteTrack(m); }));
		menu->addChild(createMenuItem("Clear lane", "", [=]() {
			int t = E->selTrack, l = E->selLane;
			E->pushUndo(t);
			for (int s = 0; s < NS; s++) {
				E->edit[t].steps[l][s] = Step();
				E->edit[t].steps[l][s].value = Pattern::defaultValue(l);
			}
			E->edit[t].pathLen[l] = 0;
			E->markEdited(t);
		}));
	}

	// Clipboard helpers. These run on the UI thread and write straight into the edit pattern,
	// the same way Rack's own modules handle menu actions.
	static Pattern& clip() {
		static Pattern p;
		return p;
	}
	static void copyLane(Lattice* m) {
		wa::Engine* E = m->eng;
		clip() = E->edit[E->selTrack];
		json_t* j = Lattice::patternToJson(clip());
		char* s = json_dumps(j, 0);
		glfwSetClipboardString(APP->window->win, s);
		free(s);
		json_decref(j);
	}
	static void pasteLane(Lattice* m) {
		wa::Engine* E = m->eng;
		int t = E->selTrack, l = E->selLane;
		E->pushUndo(t);
		E->edit[t].lanes[l] = clip().lanes[l];
		for (int s = 0; s < NS; s++)
			E->edit[t].steps[l][s] = clip().steps[l][s];
		E->markEdited(t);
	}
	static void copyTrack(Lattice* m) {
		copyLane(m);
	}
	static void pasteTrack(Lattice* m) {
		wa::Engine* E = m->eng;
		int t = E->selTrack;
		E->pushUndo(t);
		E->edit[t] = clip();
		E->markEdited(t);
	}
	static void exportPattern(Lattice* m) {
		char* path = osdialog_file(OSDIALOG_SAVE, nullptr, "pattern.json", nullptr);
		if (!path)
			return;
		json_t* j = Lattice::patternToJson(m->eng->edit[m->eng->selTrack]);
		json_dump_file(j, path, JSON_INDENT(1));
		json_decref(j);
		free(path);
	}
	static void importPattern(Lattice* m) {
		char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, nullptr);
		if (!path)
			return;
		json_error_t err;
		json_t* j = json_load_file(path, 0, &err);
		free(path);
		if (!j)
			return;
		wa::Engine* E = m->eng;
		int t = E->selTrack;
		E->pushUndo(t);
		Lattice::patternFromJson(j, E->edit[t]);
		json_decref(j);
		E->markEdited(t);
	}
};

Model* modelLattice = createModel<Lattice, LatticeWidget>("Lattice");
