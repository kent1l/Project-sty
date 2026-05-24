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

    // Respect playNote flag (if 0, skip transposition entirely)
    if (rule.playNote == 0) {
        return sourceNote;
    }
    
    // NEVER transpose the drum tracks! (Yamaha standardizes as Rhy1 / Rhy2)
    if (rule.destChannel == 9 || rule.destChannel == 8 || // Protect Channels 10 & 9
        rule.trackName.find("Rhy") != std::string::npos || 
        rule.trackName.find("rhy") != std::string::npos || 
        rule.trackName.find("dr") != std::string::npos || 
        rule.trackName.find("drum") != std::string::npos) {
        return sourceNote;
    }

    // Yamaha Styles are traditionally recorded in CMaj7 (Root C = 0), but SFF2 can define custom Source Root/Chord.
    int sourceOctave = sourceNote / 12;
    int sourcePitchClass = sourceNote % 12;
    
    // Calculate the style interval relative to the style's recorded source root
    int styleInterval = (sourcePitchClass - rule.sourceRoot + 12) % 12;
    int mappedInterval = styleInterval;
    std::string type = liveChord.type;

    bool isSourceMinor = (rule.sourceChordType >= 0x08 && rule.sourceChordType <= 0x0B);
    bool isLiveMinor = (type == "m" || type == "m7" || type == "m9" || type == "m6" || 
                        type == "m(add9)" || type == "mM7" || type == "m7b5");

    // Apply Chord Type scale degree mapping if playChord explicitly allows it
    if (rule.playChord != 0) {
        // 1. Map Thirds
        if (isSourceMinor && !isLiveMinor) {
            // Style was recorded in Minor (has minor third = 3), live is Major (needs major third = 4)
            if (styleInterval == 3) {
                mappedInterval = 4;
            }
        }
        else if (!isSourceMinor && isLiveMinor) {
            // Style was recorded in Major (has major third = 4), live is Minor (needs minor third = 3)
            if (styleInterval == 4) {
                mappedInterval = 3;
            }
        }

        // 2. Map Sevenths / Dominants
        bool isLive7th = (type == "7" || type == "9" || type == "11" || type == "13" || type == "7b5" || type == "m7" || type == "m7b5");
        if (rule.sourceChordType == 0x02 && isLive7th) { // Source is Maj7 (has 11), live is flat 7th (needs 10)
            if (styleInterval == 11) {
                mappedInterval = 10;
            }
        }
        else if ((rule.sourceChordType == 0x13 || rule.sourceChordType == 0x0A) && type == "Maj7") { // Source is 7th/min7 (has 10), live is Maj7 (needs 11)
            if (styleInterval == 10) {
                mappedInterval = 11;
            }
        }
        
        // 3. Fallback standard mappings for sus4 / dim / aug chord qualities if not covered above
        if (type == "dim" || type == "dim7") {
            if (styleInterval == 4 || styleInterval == 3) mappedInterval = 3;
            else if (styleInterval == 7) mappedInterval = 6;
            else if (styleInterval == 11) {
                if (type == "dim7") mappedInterval = 9;
                else mappedInterval = 10;
            }
        }
        else if (type == "aug") {
            if (styleInterval == 7) mappedInterval = 8;
        }
        else if (type == "sus4" || type == "7sus4") {
            if (styleInterval == 4 || styleInterval == 3) mappedInterval = 5;
            else if (styleInterval == 11 && type == "7sus4") mappedInterval = 10;
        }
    }

    int mappedPitchClass = mappedInterval;

    // Now shift the mapped pitch class to the target root pitch
    int transposedNote = (sourceOctave * 12) + mappedPitchClass + liveChord.rootNote;

    // --- 2. Fingered on Bass Exception ---
    // If this track is the Bass, and the user played an inverted bass note (e.g. CMaj/E)
    // we override the root interval and transpose strictly to the Bass note.
    if (rule.trackName.find("bass") != std::string::npos && liveChord.bassNote != liveChord.rootNote) {
        if (sourcePitchClass == rule.sourceRoot) {
            // Remap the base root note of the bassline to match the inverted bass note
            transposedNote = (sourceOctave * 12) + liveChord.bassNote;
        } else {
            // The rest of the rhythmic bass pattern remains mathematically relative to the original root chord
            transposedNote = (sourceOctave * 12) + mappedPitchClass + liveChord.rootNote;
        }

        // Shift octaves down to keep it in the bass register
        while (transposedNote > 45) { 
            transposedNote -= 12;
        }
    }

    // --- 3. Root Fixed Voice Leading ---
    // If Root Fixed NTR is active, keep notes from jumping wildly
    if (rule.ntr == 1) { // 01H = Root Fixed
        int diff = transposedNote - sourceNote;
        while (diff > 6) {
            transposedNote -= 12;
            diff = transposedNote - sourceNote;
        }
        while (diff < -6) {
            transposedNote += 12;
            diff = transposedNote - sourceNote;
        }
    }

    // --- 4. Guitar Mode Exception ---
    // Guitar voicings avoid tight 3rd clusters in the lower registers (below note 57)
    if (rule.trackName.find("Gtr") != std::string::npos || 
        rule.trackName.find("gtr") != std::string::npos ||
        rule.trackName.find("Guitar") != std::string::npos ||
        rule.trackName.find("guitar") != std::string::npos ||
        rule.ntt == 4) {
        
        int intervalFromRoot = (transposedNote - liveChord.rootNote + 24) % 12;
        if ((intervalFromRoot == 3 || intervalFromRoot == 4) && transposedNote < 57) {
            // Shift muddy thirds in the lower register up an octave to simulate open guitar voicing
            transposedNote += 12;
        }
    }

    // --- 5. Apply CASM High Key ---
    if (rule.highKey != 0xFF) {
        transposedNote = applyHighKey(transposedNote, liveChord.rootNote, rule.highKey);
    }

    // --- 6. Apply CASM Note Limits ---
    if (rule.noteLimitHigh != 0xFF && rule.noteLimitLow != 0xFF) {
        transposedNote = applyNoteLimits(transposedNote, rule.noteLimitLow, rule.noteLimitHigh);
    }

    // Safety check: Ensure we don't return an invalid MIDI note (must be 0-127)
    if (transposedNote < 0) transposedNote = 0;
    if (transposedNote > 127) transposedNote = 127;

    return transposedNote;
}

int TranspositionBrain::applyHighKey(int note, int rootNote, int highKey) {
    // Drop the ENTIRE chord an octave to preserve the voicing shape if the ROOT is too high
    if (rootNote > highKey) {
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
