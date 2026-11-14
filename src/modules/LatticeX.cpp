// LATTICE-X expander (spec §11.2). Sits to the right of LATTICE and sends its inputs every sample.
#include "plugin.hpp"
#include "Expander.hpp"

using namespace wa;

struct LatticeX : Module {
	enum ParamIds { SENS_PARAM, THRESH_PARAM, NUM_PARAMS };
	enum InputIds {
		ENUMS(SRC_INPUT, NSRC),
		EVENT_INPUT, AUDIO_INPUT, RULEGEN_INPUT, NEXT_INPUT, PIN_INPUT,
		ENUMS(X_INPUT, NT),
		ENUMS(Y_INPUT, NT),
		ENUMS(DENS_INPUT, NT),
		ENUMS(MUTE_INPUT, NT),
		ENUMS(FREEZE_INPUT, NT),
		ENUMS(QUEUE_INPUT, NT),
		ENUMS(QTRIG_INPUT, NT),
		NUM_INPUTS
	};
	enum OutputIds { NUM_OUTPUTS };
	enum LightIds { CONNECTED_LIGHT, NUM_LIGHTS };

	dsp::SchmittTrigger eventTrig, ruleTrig, nextTrig, qTrig[NT];

	LatticeX() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SENS_PARAM, -40.f, 0.f, -20.f, "Onset sensitivity (rise needed)", " dB");
		configParam(THRESH_PARAM, -10.f, 10.f, 1.f, "SRC 1 threshold", " V");
		for (int i = 0; i < NSRC; i++)
			configInput(SRC_INPUT + i, string::f("SRC %d (matrix source%s)", i + 1, i == 0 ? ", threshold clock" : (i == 1 ? ", gesture source option" : "")));
		configInput(EVENT_INPUT, "EVENT trigger (lane clock source EXT)");
		configInput(AUDIO_INPUT, "Audio for onset detection (lane clock source ONS)");
		configInput(RULEGEN_INPUT, "RULE GEN trigger (selected track)");
		configInput(NEXT_INPUT, "Song NEXT trigger");
		configInput(PIN_INPUT, "PIN gate (acts like holding PIN)");
		for (int t = 0; t < NT; t++) {
			configInput(X_INPUT + t, string::f("Track %d morph X (0-10 V)", t + 1));
			configInput(Y_INPUT + t, string::f("Track %d morph Y (0-10 V)", t + 1));
			configInput(DENS_INPUT + t, string::f("Track %d density (±5 V)", t + 1));
			configInput(MUTE_INPUT + t, string::f("Track %d mute gate", t + 1));
			configInput(FREEZE_INPUT + t, string::f("Track %d freeze gate", t + 1));
			configInput(QUEUE_INPUT + t, string::f("Track %d pattern select (0-10 V = slot 1-16)", t + 1));
			configInput(QTRIG_INPUT + t, string::f("Track %d queue trigger", t + 1));
		}
	}

	void process(const ProcessArgs& args) override {
		(void) args;
		bool connected = leftExpander.module && leftExpander.module->model == modelLattice;
		lights[CONNECTED_LIGHT].setBrightness(connected ? 1.f : 0.f);
		if (!connected)
			return;
		LatticeXMessage* m = (LatticeXMessage*) leftExpander.module->rightExpander.producerMessage;
		if (!m)
			return;
		m->connected = true;
		for (int i = 0; i < NSRC; i++)
			m->src[i] = inputs[SRC_INPUT + i].getVoltage();
		m->event = eventTrig.process(inputs[EVENT_INPUT].getVoltage(), 0.1f, 1.f);
		m->audio = inputs[AUDIO_INPUT].getVoltage();
		m->sensDb = params[SENS_PARAM].getValue();
		m->thresh = params[THRESH_PARAM].getValue();
		for (int t = 0; t < NT; t++) {
			m->x[t] = inputs[X_INPUT + t].getVoltage();
			m->y[t] = inputs[Y_INPUT + t].getVoltage();
			m->dens[t] = inputs[DENS_INPUT + t].getVoltage();
			m->mute[t] = inputs[MUTE_INPUT + t].getVoltage() >= 1.f;
			m->freeze[t] = inputs[FREEZE_INPUT + t].getVoltage() >= 1.f;
			m->queueV[t] = inputs[QUEUE_INPUT + t].getVoltage();
			m->queueTrig[t] = qTrig[t].process(inputs[QTRIG_INPUT + t].getVoltage(), 0.1f, 1.f);
		}
		m->ruleGen = ruleTrig.process(inputs[RULEGEN_INPUT].getVoltage(), 0.1f, 1.f);
		m->next = nextTrig.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f);
		m->pin = inputs[PIN_INPUT].getVoltage() >= 1.f;
		leftExpander.module->rightExpander.requestMessageFlip();
	}
};

namespace {

struct XLabels : widget::Widget {
	struct L {
		Vec p;
		std::string t;
		NVGcolor c;
	};
	std::vector<L> ls;
	void add(float x, float y, const std::string& t, NVGcolor c) {
		ls.push_back({mm2px(Vec(x, y)), t, c});
	}
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 6.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (const L& l : ls) {
			nvgFillColor(args.vg, l.c);
			nvgText(args.vg, l.p.x, l.p.y, l.t.c_str(), nullptr);
		}
	}
};

} // namespace

struct LatticeXWidget : ModuleWidget {
	LatticeXWidget(LatticeX* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/LatticeX.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		XLabels* lab = new XLabels;
		lab->box.size = box.size;
		NVGcolor dim = nvgRGB(0x9a, 0x9a, 0x9e);
		const NVGcolor tc[NT] = {nvgRGB(0xe8, 0xa3, 0x3c), nvgRGB(0x3c, 0xc8, 0xb4), nvgRGB(0x9b, 0x7b, 0xe8)};
		const float cols[6] = {8.f, 19.f, 30.f, 41.f, 52.f, 63.f};
		const float rows[7] = {20.f, 35.f, 50.f, 66.f, 82.f, 98.f, 114.f};

		auto in = [&](int c, int r, int id, const std::string& name, NVGcolor col) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cols[c], rows[r])), module, id));
			lab->add(cols[c], rows[r] - 6.f, name, col);
		};
		for (int i = 0; i < 6; i++)
			in(i, 0, LatticeX::SRC_INPUT + i, string::f("SRC%d", i + 1), dim);
		in(0, 1, LatticeX::SRC_INPUT + 6, "SRC7", dim);
		in(1, 1, LatticeX::SRC_INPUT + 7, "SRC8", dim);
		in(2, 1, LatticeX::EVENT_INPUT, "EVENT", dim);
		in(3, 1, LatticeX::AUDIO_INPUT, "AUDIO", dim);
		addParam(createParamCentered<Trimpot>(mm2px(Vec(cols[4], rows[1])), module, LatticeX::SENS_PARAM));
		lab->add(cols[4], rows[1] - 6.f, "SENS", dim);
		addParam(createParamCentered<Trimpot>(mm2px(Vec(cols[5], rows[1])), module, LatticeX::THRESH_PARAM));
		lab->add(cols[5], rows[1] - 6.f, "THRESH", dim);
		in(0, 2, LatticeX::RULEGEN_INPUT, "RULEGEN", dim);
		in(1, 2, LatticeX::NEXT_INPUT, "NEXT", dim);
		in(2, 2, LatticeX::PIN_INPUT, "PIN", dim);
		for (int t = 0; t < NT; t++)
			in(3 + t, 2, LatticeX::QTRIG_INPUT + t, string::f("QTRG%d", t + 1), tc[t]);
		const char* names[6] = {"X", "Y", "DENS", "MUTE", "FRZ", "QUEUE"};
		const int ids[6] = {LatticeX::X_INPUT, LatticeX::Y_INPUT, LatticeX::DENS_INPUT, LatticeX::MUTE_INPUT, LatticeX::FREEZE_INPUT, LatticeX::QUEUE_INPUT};
		for (int t = 0; t < NT; t++)
			for (int c = 0; c < 6; c++)
				in(c, 3 + t, ids[c] + t, string::f("%s%d", names[c], t + 1), tc[t]);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(35.56f, 8.f)), module, LatticeX::CONNECTED_LIGHT));
		lab->add(35.56f, 124.5f, "LATTICE-X", nvgRGB(0xd8, 0xd8, 0xd8));
		addChild(lab);
	}
};

Model* modelLatticeX = createModel<LatticeX, LatticeXWidget>("LatticeX");
