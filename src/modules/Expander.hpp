#pragma once
// Message from LATTICE-X (right) to LATTICE (left), sent every sample (spec §11.2).
#include "Types.hpp"

struct LatticeXMessage {
	bool connected = false;
	float src[wa::NSRC] = {};
	bool event = false;
	float audio = 0.f;
	float sensDb = -20.f;
	float thresh = 1.f;
	float x[wa::NT] = {}, y[wa::NT] = {}, dens[wa::NT] = {};
	bool mute[wa::NT] = {}, freeze[wa::NT] = {};
	float queueV[wa::NT] = {};
	bool queueTrig[wa::NT] = {};
	bool ruleGen = false, next = false, pin = false;
};
