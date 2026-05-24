#include "TranspositionBrain.h"
#include <string>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>

namespace engine {

namespace {

// Helper to check if a live chord type is minor
bool checkLiveMinor(const std::string& type) {
    return (type == "m" || type == "m7" || type == "m9" || type == "m6" || 
            type == "m(add9)" || type == "mM7" || type == "m7b5");
}

// Retrieves the precise allowed chord tone intervals for "Chord" NTT snapping
std::vector<int> getChordTones(const std::string& chordType) {
    if (checkLiveMinor(chordType)) {
        if (chordType == "m6") return {0, 3, 7, 9};
        if (chordType == "m(add9)") return {0, 2, 3, 7};
        if (chordType == "mM7") return {0, 3, 7, 11};
        if (chordType == "m7b5") return {0, 3, 6, 10};
        return {0, 3, 7, 10}; // m, m7, m9
    }
    if (chordType == "dim" || chordType == "dim7") {
        return (chordType == "dim") ? std::vector<int>{0, 3, 6} : std::vector<int>{0, 3, 6, 9};
    }
    if (chordType == "aug") return {0, 4, 8};
    if (chordType == "sus4" || chordType == "7sus4") {
        return (chordType == "sus4") ? std::vector<int>{0, 5, 7} : std::vector<int>{0, 5, 7, 10};
    }
    if (chordType == "sus2") return {0, 2, 7};
    if (chordType == "7" || chordType == "9" || chordType == "11" || chordType == "13" || chordType == "7b5") {
        return (chordType == "7b5") ? std::vector<int>{0, 4, 6, 10} : std::vector<int>{0, 4, 7, 10};
    }
    if (chordType == "M7" || chordType == "Maj7" || chordType == "M9") return {0, 4, 7, 11};
    if (chordType == "6") return {0, 4, 7, 9};
    if (chordType == "add9") return {0, 2, 4, 7};
    if (chordType == "5") return {0, 7};
    
    return {0, 4, 7}; // Default Major
}

// Fixed directional chord snapping to avoid octave jumps
int snapToNearestChordTone(int mappedInterval, const std::string& chordType) {
    std::vector<int> allowedTones = getChordTones(chordType);
    
    // If it's already an allowed scale degree, return unmodified
    if (std::find(allowedTones.begin(), allowedTones.end(), mappedInterval) != allowedTones.end()) {
        return mappedInterval;
    }

    int bestInterval = mappedInterval;
    int minDistance = 999;

    for (int tone : allowedTones) {
        int dist = std::abs(mappedInterval - tone);
        if (dist < minDistance) {
            minDistance = dist;
            bestInterval = tone;
        }
    }
    return bestInterval;
}
} // namespace

TranspositionBrain::TranspositionBrain() {}
TranspositionBrain::~TranspositionBrain() {}

int TranspositionBrain::calculateTransposition(int sourceNote, const Chord& liveChord, const CasmRule& rule) {
    if (liveChord.rootNote == -1) {
        return sourceNote; 
    }

    // 1. Structural Muting & Bypass Rules
    if (rule.playNote == 0) {
        return -1; // -1 Signals the engine to swallow/mute this event entirely
    }
    
    // Protect Rhythm Parts (Standard Yamaha targets channels 9 & 10 for drums/percussion)
    if (rule.destChannel == 9 || rule.destChannel == 8) {
        return sourceNote;
    }

    int sourcePitchClass = sourceNote % 12;
    
    // 2. Derive Style Scale Degree Interval relative to recorded file metadata
    int styleInterval = (sourcePitchClass - rule.sourceRoot + 12) % 12;
    int mappedInterval = styleInterval;
    std::string type = liveChord.type;

    bool isSourceMinor = (rule.sourceChordType >= 0x08 && rule.sourceChordType <= 0x0B);
    bool isLiveMinor = checkLiveMinor(type);

    // 3. Process NTT (Note Transposition Table Rules)
    if (rule.playChord != 0 && rule.ntt != 0) { // 0 = Bypass Table
        
        // Handle standard scalar corrections (Melody=1, Chord=2, Bass=3, Guitar=4)
        if (rule.ntt == 1 || rule.ntt == 2 || rule.ntt == 3 || rule.ntt == 4) {
            
            // Map Thirds and Sixths (Major <-> Minor Conversions)
            if (isSourceMinor && !isLiveMinor) {
                if (styleInterval == 3) mappedInterval = 4; // Flat 3rd -> Natural 3rd
                if (styleInterval == 8) mappedInterval = 9; // Flat 6th -> Natural 6th
            }
            else if (!isSourceMinor && isLiveMinor) {
                if (styleInterval == 4) mappedInterval = 3; // Natural 3rd -> Flat 3rd
                if (styleInterval == 9) mappedInterval = 8; // Natural 6th -> Flat 6th
            }

            // Map Sevenths / Dominants
            bool isLiveDominant = (type == "7" || type == "9" || type == "11" || type == "13" || type == "7b5");
            bool isLiveFlat7 = isLiveDominant || (isLiveMinor && type != "mM7");
            
            if (isLiveFlat7 && styleInterval == 11) {
                mappedInterval = 10;
            }
            else if (!isLiveFlat7 && styleInterval == 10) {
                mappedInterval = 11;
            }
            
            // Structural mappings for exotic modifications
            if (type == "dim" || type == "dim7") {
                if (styleInterval == 4 || styleInterval == 3) mappedInterval = 3;
                else if (styleInterval == 7 || styleInterval == 8) mappedInterval = 6;
                else if (styleInterval == 11 || styleInterval == 10) {
                    mappedInterval = (type == "dim7") ? 9 : 10;
                }
            }
            else if (type == "aug") {
                if (styleInterval == 7 || styleInterval == 8) mappedInterval = 8;
            }
            else if (type == "sus4" || type == "7sus4") {
                if (styleInterval == 4 || styleInterval == 3) mappedInterval = 5;
            }

            // Rule 2 Specific: Chord Padding requires strict structural tone restriction
            if (rule.ntt == 2) {
                mappedInterval = snapToNearestChordTone(mappedInterval, type);
            }
        }
    }

    // 4. Calculate Pitch Shift Vector (Preserving exact relative intervals)
    int pitchOffset = mappedInterval - styleInterval;
    int transposedNote = sourceNote + pitchOffset + (liveChord.rootNote - rule.sourceRoot);

    // 5. Process NTR (Note Transposition Rule Metrics)
    if (rule.ntr == 1) { // 01H = Root Fixed Voice Leading (Keep inversions cluster optimized)
        int delta = transposedNote - sourceNote;
        while (delta > 6) {
            transposedNote -= 12;
            delta = transposedNote - sourceNote;
        }
        while (delta < -6) {
            transposedNote += 12;
            delta = transposedNote - sourceNote;
        }
    }
    // Note: rule.ntr == 0 (Root Transposition) shifts parallelly, which our pitchOffset calculation already handles.

    // 6. Bass Specific Modifications (Fingered on Bass / Slash Chords)
    // Match either by NTT type 3 OR by explicit dest channel 10 (MIDI Ch 11 = user bass channel)
    bool isBassChannel = (rule.destChannel == 10 || rule.ntt == 3);
    if (isBassChannel) {
        if (liveChord.bassNote != -1 && liveChord.bassNote != liveChord.rootNote) {
            // Apply strict shift mapping relative to the customized slash inversion note
            int currentPitchClass = transposedNote % 12;
            int bassShiftDelta = liveChord.bassNote - currentPitchClass;
            
            // Normalize semitone shift vector
            if (bassShiftDelta > 6)  bassShiftDelta -= 12;
            if (bassShiftDelta < -6) bassShiftDelta += 12;
            transposedNote += bassShiftDelta;
        }
        
        // Force output note safety bounds directly into standard Bass register (E1-G3)
        while (transposedNote > 55) transposedNote -= 12;
        while (transposedNote < 28) transposedNote += 12;
    }

    // 7. Guitar Mode Exception / Fret Alterations (NTT = 4 is Guitar SFF2)
    if (rule.ntt == 4) {
        int intervalFromRoot = (transposedNote - liveChord.rootNote + 24) % 12;
        // Shift lower muddy intervals up an octave to replicate native open-string chord fingerboard geography
        if ((intervalFromRoot == 3 || intervalFromRoot == 4 || intervalFromRoot == 10) && transposedNote < 57) {
            transposedNote += 12;
        }
    }

    // 8. Apply Physical Master Limits
    if (rule.highKey != 0xFF) {
        transposedNote = applyHighKey(transposedNote, liveChord.rootNote, rule.highKey);
    }
    if (rule.noteLimitHigh != 0xFF && rule.noteLimitLow != 0xFF) {
        transposedNote = applyNoteLimits(transposedNote, rule.noteLimitLow, rule.noteLimitHigh);
    }

    // Ultimate Safety Boundary
    if (transposedNote < 0) return 0;
    if (transposedNote > 127) return 127;

    return transposedNote;
}

int TranspositionBrain::applyHighKey(int note, int rootNote, int highKey) {
    // If the target root pitch passes the specified high key threshold, fold the voicing downward
    if (rootNote > highKey) {
        return note - 12;
    }
    return note;
}

int TranspositionBrain::applyNoteLimits(int note, int limitLow, int limitHigh) {
    while (note < limitLow)  note += 12;
    while (note > limitHigh) note -= 12;
    return note;
}

} // namespace engine