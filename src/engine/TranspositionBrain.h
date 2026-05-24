#pragma once
#include "ChordRecognizer.h"
#include "../reader/SFF2Parser.h"

namespace engine {

class TranspositionBrain {
public:
    TranspositionBrain();
    ~TranspositionBrain();

    // Calculates the final MIDI note to output based on the live chord and CASM rules
    int calculateTransposition(int sourceNote, const Chord& liveChord, const CasmRule& rule);

private:
    int applyHighKey(int note, int rootNote, int highKey);
    int applyNoteLimits(int note, int limitLow, int limitHigh);
};

} // namespace engine
