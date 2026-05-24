#include "TranspositionBrain.h"
#include <string>
#include <iostream>

namespace engine {

TranspositionBrain::TranspositionBrain() {}
TranspositionBrain::~TranspositionBrain() {}

int TranspositionBrain::calculateTransposition(int sourceNote, const Chord& liveChord, const CasmRule& rule) {
    if (liveChord.rootNote == -1) {
        return sourceNote; // No chord being played, output the raw note
    }
    
    // NEVER transpose the drum tracks! (Yamaha standardizes as Rhy1 / Rhy2)
    if (rule.trackName.find("Rhy") != std::string::npos || 
        rule.trackName.find("rhy") != std::string::npos || 
        rule.trackName.find("dr") != std::string::npos || 
        rule.trackName.find("drum") != std::string::npos) {
        return sourceNote;
    }

    // Yamaha Styles are traditionally recorded in CMaj7 (Root C = 0).
    int sourceOctave = sourceNote / 12;
    int sourcePitchClass = sourceNote % 12;
    
    int mappedPitchClass = sourcePitchClass;
    std::string type = liveChord.type;
    
    // Apply Chord Type scale degree mapping (source is in C Major/Maj7)
    if (type == "m" || type == "m7" || type == "m9" || type == "m6" || type == "m(add9)" || type == "mM7" || type == "m7b5") {
        // Minor chords
        if (sourcePitchClass == 4) { // Major 3rd -> Minor 3rd
            mappedPitchClass = 3;
        } else if (sourcePitchClass == 11) { // Major 7th -> Minor 7th / Major 7th
            if (type == "mM7") mappedPitchClass = 11;
            else mappedPitchClass = 10;
        }
    }
    else if (type == "dim" || type == "dim7") {
        // Diminished
        if (sourcePitchClass == 4) mappedPitchClass = 3; // Minor 3rd
        else if (sourcePitchClass == 7) mappedPitchClass = 6; // Diminished 5th
        else if (sourcePitchClass == 11) {
            if (type == "dim7") mappedPitchClass = 9; // Diminished 7th
            else mappedPitchClass = 10; // Half-diminished
        }
    }
    else if (type == "aug") {
        // Augmented
        if (sourcePitchClass == 7) mappedPitchClass = 8; // Augmented 5th
    }
    else if (type == "sus4" || type == "7sus4") {
        // Suspended 4th
        if (sourcePitchClass == 4) mappedPitchClass = 5; // Replace 3rd with 4th
        else if (sourcePitchClass == 11 && type == "7sus4") mappedPitchClass = 10;
    }
    else {
        // Major / Dominant 7th etc.
        if (sourcePitchClass == 11) {
            // If it's a dominant 7th style chord, map major 7th to flat 7th
            if (type == "7" || type == "9" || type == "11" || type == "13" || type == "7b5") {
                mappedPitchClass = 10;
            }
        }
    }

    // Now shift the mapped pitch class to the target root pitch
    int transposedNote = (sourceOctave * 12) + mappedPitchClass + liveChord.rootNote;

    // --- 2. Fingered on Bass Exception ---
    // If this track is the Bass, and the user played an inverted bass note (e.g. CMaj/E)
    // we override the root interval and transpose strictly to the Bass note.
    if (rule.trackName.find("bass") != std::string::npos && liveChord.bassNote != liveChord.rootNote) {
        // Shift base of transposition to the bass note instead of root
        int bassInterval = liveChord.bassNote - 0;
        transposedNote = sourceNote + bassInterval;
        
        // Shift octaves down to keep it in the bass register
        while (transposedNote > 45) { // MIDI Note 45 is roughly the top of standard bass
            transposedNote -= 12;
        }
    }

    // --- 3. Apply CASM High Key ---
    if (rule.highKey != 0xFF) {
        transposedNote = applyHighKey(transposedNote, rule.highKey);
    }

    // --- 4. Apply CASM Note Limits ---
    if (rule.noteLimitHigh != 0xFF && rule.noteLimitLow != 0xFF) {
        transposedNote = applyNoteLimits(transposedNote, rule.noteLimitLow, rule.noteLimitHigh);
    }

    // Safety check: Ensure we don't return an invalid MIDI note (must be 0-127)
    if (transposedNote < 0) transposedNote = 0;
    if (transposedNote > 127) transposedNote = 127;

    return transposedNote;
}

int TranspositionBrain::applyHighKey(int note, int highKey) {
    int pitchClass = note % 12;
    // If the calculated pitch class is above the High Key ceiling, wrap it down an octave
    if (pitchClass > highKey) {
        return note - 12;
    }
    return note;
}

int TranspositionBrain::applyNoteLimits(int note, int limitLow, int limitHigh) {
    // Fold the note up octaves until it is inside the lower bound
    while (note < limitLow) {
        note += 12;
    }
    // Fold the note down octaves until it is inside the upper bound
    while (note > limitHigh) {
        note -= 12;
    }
    return note;
}

} // namespace engine
