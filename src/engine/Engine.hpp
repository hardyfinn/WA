#pragma once
// LATTICE sequencing engine: pure C++17, no Rack (spec §0.2).
// The Rack module feeds it one EngineIn per sample and reads EngineOut.
// Everything runs on the audio thread; the UI only sends button presses and encoder deltas
// and reads the Snapshot (spec §4, see docs/DECISIONS.md for why there is no worker thread).
#include "Types.hpp"
#include "Scale.hpp"
#include "Rng.hpp"
#include <atomic>
#include <cstdint>

namespace wa {

enum Page { P_LANES, P_STEP, P_PATTERN, P_ROLE, P_RULES, P_ANSWER, P_MORPH, P_MATRIX, P_SONG, P_SCALE, NUM_PAGES };
extern const char* PAGE_NAMES[NUM_PAGES];

enum Enc {
	E_STEP0 = 0,
	E_LEN = 16, E_START, E_CLOCK, E_DIR,
	E_SEED, E_ARC, E_DRIFT, E_DENS, E_SPREAD, E_X, E_Y,
	NUM_ENC
};

struct EngineIn {
	float dt = 1.f / 48000.f;
	bool running = true;
	// clocks
	bool clockConn = false, clockPulse = false;
	int ppStep = 1;                       // main clock pulses per base step
	bool tclkConn[NT] = {}, tclkPulse[NT] = {};
	bool reset = false;
	float bpm = 120.f;
	float transposeV = 0.f;
	// CV
	float arcCV[NT] = {}, xCV[NT] = {}, yCV[NT] = {}, densCV[NT] = {};
	bool muteGate[NT] = {}, freezeGate[NT] = {};
	float queueV[NT] = {};
	bool queueTrig[NT] = {};
	bool keepTrig = false, genTrig = false, fill = false, ruleGenTrig = false, nextTrig = false, pinGate = false;
	float src[NSRC] = {};
	bool eventTrig = false;
	float audio = 0.f;
	float sensDb = -20.f;
	float thresh = 1.f;
	float gesture = 0.f;                  // panel GESTURE knob, 0..1
	float character = 0.5f, amount = 1.f;
	bool expander = false;
	// panel buttons: p* = pressed this sample, *Held = held
	bool shift = false, pinHeld = false, freezeHeld = false, fillHeld = false;
	bool pTrack[NT] = {}, pLane[NL] = {}, pStep[16] = {};
	bool pPage = false, pHi = false, pGen = false, pKeep = false, pUndo = false, pHome = false, pMorph = false, pRec = false, pPin = false;
	bool pMute[NT] = {};
	int enc[NUM_ENC] = {};
};

struct EngineOut {
	float pitch[NT][4] = {};
	float gate[NT][4] = {};
	int chans[NT] = {1, 1, 1};
	float vel[NT] = {};
	bool acc[NT] = {};
	float mod[NT][2] = {};
	bool eop = false, eos = false;
};

struct Options {
	float fillAmount = 25.f;   // % added to probabilities while FILL
	float arcVel = 1.f;        // volts added to Passing/Tension at full arc
	float accentAmount = 2.f;  // volts added by an accent
	bool keepAllFromInput = false;
	int freezeLen = 8;         // base steps: 1, 2, 4, 8
	int muteQuant = 1;         // 0 off, 1 beat, 2 bar
	int gestureMode = 0;       // 0 real time, 1 stretch
	bool gestureFromExp = false;
	bool runIsGate = false;
};

struct Snapshot {
	int track = 0, lane = 0, page = 0, selStep = 0, selPoint = -1, selUserChord = 0;
	bool hi = false, shift = false, running = false, expander = false;
	float bpm = 120.f;
	Pattern pat;                // play pattern of the selected track
	Pattern editPat;            // edit pattern of the selected track
	int curIdx[NL] = {};
	float laneVal[NL] = {};
	TrackSettings ts[NT];
	int queued[NT] = {-1, -1, -1};
	bool frozen[NT] = {};
	bool gateHigh[NT] = {};
	float morphW[NPOINT] = {};
	Song song;
	int songIdx = 0, songRow = 0;
	bool songOn = false;
	ScaleSet scales;
	char msg[40] = {};
	bool gestArmed = false, gestRec = false;
	float gestTrace[64] = {};
	int gestHead = 0;
	int undoCount = 0;
};

struct LaneRun {
	double pos = 0.0;
	int64_t k = -1;
	int64_t moves = -1;
	int curI = 0, curIdx = 0;
	bool curPassed = true;
	bool haveNext = false;
	int nextI = 0, nextIdx = 0;
	bool nextPassed = true;
	float val = 0.f;
	bool slide = false;
	bool prevCond = true;
	bool reversed = false;
	int prevI = 0;
	int events = 0;
	bool pathRec = false;
	int pathCount = 0;
	uint8_t pathBuf[NS] = {};
	float dirPrev[NMATRIX] = {};
};

struct Fire {
	double t;
	int idx;
	bool passed;
};

struct FreezeFrame {
	float pitch[4];
	uint8_t gateMask;
	uint8_t chans;
	uint8_t acc;
	float vel;
	float mod[2];
};

constexpr int FREEZE_CAP = 16000; // 16 s at 1 kHz

struct TrackRun {
	// transport
	double T = 0.0, Tw = 0.0, dTw = 0.0;
	double rate = 8.0;            // base steps per second
	bool started = false;
	int64_t pulses = -1;
	double sincePulse = 0.0, lastSnap = 0.0;
	double iv[2] = {0.125, 0.125};
	int64_t lastBase = -1, lastBeat = -1, lastBar = -1, lastSync = -1;
	LaneRun lanes[NL];
	Fire fires[8];
	int nFires = 0;
	// note
	bool noteOn = false;
	double noteT = 0.0, stepDur = 0.125;
	int ratN = 1, ratRamp = 0, chordN = 1;
	float lenFrac = 0.5f, vel = 0.f;
	float gapTimer = 0.f;
	float pitchTgt[4] = {}, pitchCur[4] = {}, glideRate[4] = {};
	float modTgt[2] = {}, modCur[2] = {}, modRate[2] = {};
	bool accPending = false;
	double lastGateTime = 0.0, timeSec = 0.0;
	Rng rng;
	int queued = -1;
	bool pendingHome = false;
	bool gateEndHit = false;
	int pendingMute = -1;        // -1 none, 0/1 target state
	bool gateEvent = false, gateEventPrev = false;
	int chanAuto = 1;
	// freeze
	FreezeFrame* frames = nullptr;
	int fWrite = 0, fLoopStart = 0, fLoopLen = 0, fPlay = 0;
	double fAcc = 0.0;
	bool frozen = false;
	bool accRec = false;
};

class Engine {
public:
	Engine();
	~Engine();

	void process(const EngineIn& in, EngineOut& out);

	/** Reset to defaults (Rack Initialize). */
	void initialize();
	/** Randomize: Shift+GEN on every track. */
	void randomizeAll(float character);

	// Pattern data (saved)
	Pattern slots[NT][NSLOT];
	Pattern edit[NT];
	TrackSettings ts[NT];
	Song songs[NSONG];
	ScaleSet scales;
	Options opt;
	uint32_t keepStamp[NT][NSLOT] = {};
	uint32_t stampCounter = 1;
	bool songOn = false;
	int songIdx = 0;

	// Runtime (not saved)
	Pattern play[NT];
	Pattern home[NT];
	Pattern undoBuf[NT][NUNDO];
	int undoHead[NT] = {}, undoCount[NT] = {};
	TrackRun tr[NT];
	bool dirty[NT] = {true, true, true};
	uint32_t playVersion[NT] = {};
	uint32_t answerSeen[NT] = {};
	float lastMx[NT] = {-1, -1, -1}, lastMy[NT] = {-1, -1, -1};
	float morphW[NT][NPOINT] = {};
	int songRow = 0, songRepeat = 0;
	bool songNextPending = false;
	uint32_t genCount[NT] = {};

	// UI state
	int selTrack = 0, selLane = 0, page = P_LANES, selStep = 0, selPoint = -1, selUserChord = 0;
	bool hi = false;
	int editKey = -1;
	char msg[40] = {};
	float msgTime = 0.f;

	// Gesture
	bool gestArmed = false, gestRec = false;
	int gestTrack = 0, gestLane = 0;
	double gestSum[NS] = {};
	int gestCount[NS] = {};
	float* gestBuf = nullptr;
	int gestLen = 0;
	double gestAcc = 0.0;
	float gestTrace[64] = {};
	int gestHead = 0;
	double gestTraceAcc = 0.0;

	// Listening
	float onsetEnv = 0.f;
	float onsetHist[10] = {};
	int onsetHistIdx = 0;
	double onsetHistAcc = 0.0;
	float onsetRefractory = 0.f;
	bool threshHigh = false;
	float pinHoldTime = 0.f;
	bool lastRunning = false;

	// Snapshot for the UI
	Snapshot snaps[2];
	std::atomic<int> snapIdx{0};
	double snapAcc = 0.0;
	const Snapshot& snapshot() const { return snaps[snapIdx.load(std::memory_order_acquire)]; }

	// Helpers exposed for tests and the module
	void markEdited(int t);
	void pushUndo(int t);
	void undo(int t);
	int keepTrack(int t);
	void loadSlot(int t, int slot);
	void resetTrack(int t);
	void resetAll();
	void rebuildPlay(int t);
	void setMsg(const char* m);
	ScaleDef scaleOf(int t) const { return getScale(play[t].scale, scales); }

private:
	void handleUI(const EngineIn& in);
	void applyEncoder(int e, int d, bool shift);
	void editStepValue(int t, int lane, int idx, int d, bool shift);
	void handleStepButton(int i, bool shift);
	void advanceTransport(int t, const EngineIn& in);
	void runLanes(int t, const EngineIn& in);
	void advanceLane(int t, int l, const EngineIn& in);
	void decide(int t, int l, int64_t m, const EngineIn& in);
	int position(int t, int l, int64_t mm, int L);
	int effLen(int t, int l, const EngineIn& in);
	int matrixOffset(int t, int l, int L, const EngineIn& in);
	int matrixClockShift(int t, int l, const EngineIn& in);
	bool matrixMute(int t, int l, const EngineIn& in);
	int matrixTranspose(int t, const EngineIn& in);
	float srcValue(int code, const EngineIn& in);
	float laneRatio(int t, int l, const EngineIn& in);
	void fireGate(int t, int idx, bool passed, const EngineIn& in);
	void triggerNote(int t, const Step& g, Role role, const EngineIn& in);
	void computePitches(int t, bool glide, const EngineIn& in);
	bool gateHigh(const TrackRun& r) const;
	void passStart(int t, int l, bool completed, const EngineIn& in);
	void changePattern(int t);
	void boundary(int t, int kind, const EngineIn& in, EngineOut& out);
	void doDrift(int t, float character);
	void writeSnapshot(const EngineIn& in);
	void songApplyRow(bool immediate);
	void finishGesture();
	int followOffset(int t, const EngineIn& in);
	bool answerLane(int t, int l) const;
	float arcValue(int t, const EngineIn& in) const;
	float densityValue(int t, const EngineIn& in) const;
	void toggleMute(int t);

	bool fillActive = false;
	bool pinActive = false;
	int64_t sampleCount = 0;
	bool gateEvtNow[NT] = {};
	bool extEvent = false, onsetEvent = false, threshEvent = false;
};

} // namespace wa
