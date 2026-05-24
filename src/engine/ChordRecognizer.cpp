#include "ChordRecognizer.h"
#include <iostream>
#include <algorithm>

namespace engine {

std::string Chord::toString() const {
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (rootNote < 0 || rootNote > 11) return "No Chord";
    
    std::string result = noteNames[rootNote] + type;
    
    // If Fingered On Bass detects a different bass note than the root!
    if (bassNote != rootNote && bassNote >= 0 && bassNote <= 11) {
        result += "/" + std::string(noteNames[bassNote]);
    }
    
    return result;
}

ChordRecognizer::ChordRecognizer() {}
ChordRecognizer::~ChordRecognizer() {}

void ChordRecognizer::noteOn(int noteNumber) {
    m_heldNotes.insert(noteNumber);
}

void ChordRecognizer::noteOff(int noteNumber) {
    m_heldNotes.erase(noteNumber);
}

Chord ChordRecognizer::detectChord() {
    Chord chord = {-1, "", -1};
    
    // STRICT FINGERED MODE: Require at least 3 notes. 
    // (Prevents muddy sound from accidental single/double key presses)
    if (m_heldNotes.size() < 3) {
        return chord; // Returns "No Chord" (Sequencer will hold previous valid chord)
    }

    // Convert MIDI notes (0-127) to pitch classes (0-11)
    std::vector<int> pitches;
    for (int note : m_heldNotes) {
        pitches.push_back(note % 12);
    }
    
    // Remove duplicate pitch classes
    std::sort(pitches.begin(), pitches.end());
    pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());

    // The absolute lowest note played physically is our Bass Note (Fingered on Bass logic)
    int lowestNote = *m_heldNotes.begin();
    chord.bassNote = lowestNote % 12;

    // --- ADVANCED RECOGNITION MATH ---
    // We will test each note as a potential "Root" to correctly identify inversions.
    // Pass 1: Try to see if the physical Bass note is the Root.
    // Pass 2: Try all other notes as the Root (for inversions like CMaj/E).
    
    std::vector<int> candidateRoots = { chord.bassNote };
    for (int p : pitches) {
        if (p != chord.bassNote) candidateRoots.push_back(p);
    }
    
    for (int candidateRoot : candidateRoots) {
        std::vector<int> intervals;
        for (int p : pitches) {
            intervals.push_back((p - candidateRoot + 12) % 12);
        }
        std::sort(intervals.begin(), intervals.end());
        
        // 5-note chords
        if (intervals == std::vector<int>{0, 2, 4, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "9"; break; }
        if (intervals == std::vector<int>{0, 2, 4, 7, 11}) { chord.rootNote = candidateRoot; chord.type = "M9"; break; }
        if (intervals == std::vector<int>{0, 2, 3, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "m9"; break; }
        if (intervals == std::vector<int>{0, 2, 5, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "11"; break; }
        if (intervals == std::vector<int>{0, 2, 4, 9, 10}) { chord.rootNote = candidateRoot; chord.type = "13"; break; }
        
        // 4-note chords
        if (intervals == std::vector<int>{0, 4, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "7"; break; } // Dominant 7
        if (intervals == std::vector<int>{0, 4, 7, 11}) { chord.rootNote = candidateRoot; chord.type = "M7"; break; } // Major 7
        if (intervals == std::vector<int>{0, 3, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "m7"; break; } // Minor 7
        if (intervals == std::vector<int>{0, 3, 7, 11}) { chord.rootNote = candidateRoot; chord.type = "mM7"; break; } // Minor Major 7
        if (intervals == std::vector<int>{0, 3, 6, 9})  { chord.rootNote = candidateRoot; chord.type = "dim7"; break; } // Diminished 7
        if (intervals == std::vector<int>{0, 3, 6, 10}) { chord.rootNote = candidateRoot; chord.type = "m7b5"; break; } // Half-diminished
        if (intervals == std::vector<int>{0, 5, 7, 10}) { chord.rootNote = candidateRoot; chord.type = "7sus4"; break; } // 7sus4
        if (intervals == std::vector<int>{0, 4, 7, 9})  { chord.rootNote = candidateRoot; chord.type = "6"; break; } // Major 6
        if (intervals == std::vector<int>{0, 3, 7, 9})  { chord.rootNote = candidateRoot; chord.type = "m6"; break; } // Minor 6
        if (intervals == std::vector<int>{0, 2, 4, 7})  { chord.rootNote = candidateRoot; chord.type = "add9"; break; } // Add 9
        if (intervals == std::vector<int>{0, 2, 3, 7})  { chord.rootNote = candidateRoot; chord.type = "m(add9)"; break; } // Minor Add 9
        if (intervals == std::vector<int>{0, 4, 6, 10}) { chord.rootNote = candidateRoot; chord.type = "7b5"; break; } // 7 flat 5
        
        // 3-note chords (Including Omitted 5ths for 7th chords)
        if (intervals == std::vector<int>{0, 4, 7}) { chord.rootNote = candidateRoot; chord.type = ""; break; } // Major
        if (intervals == std::vector<int>{0, 3, 7}) { chord.rootNote = candidateRoot; chord.type = "m"; break; } // Minor
        if (intervals == std::vector<int>{0, 3, 6}) { chord.rootNote = candidateRoot; chord.type = "dim"; break; } // Diminished
        if (intervals == std::vector<int>{0, 4, 8}) { chord.rootNote = candidateRoot; chord.type = "aug"; break; } // Augmented
        if (intervals == std::vector<int>{0, 5, 7}) { chord.rootNote = candidateRoot; chord.type = "sus4"; break; } // Sus4
        if (intervals == std::vector<int>{0, 2, 7}) { chord.rootNote = candidateRoot; chord.type = "sus2"; break; } // Sus2
        if (intervals == std::vector<int>{0, 4, 10}) { chord.rootNote = candidateRoot; chord.type = "7"; break; } // 7 (no 5th)
        if (intervals == std::vector<int>{0, 4, 11}) { chord.rootNote = candidateRoot; chord.type = "M7"; break; } // M7 (no 5th)
        if (intervals == std::vector<int>{0, 3, 10}) { chord.rootNote = candidateRoot; chord.type = "m7"; break; } // m7 (no 5th)
        if (intervals == std::vector<int>{0, 3, 11}) { chord.rootNote = candidateRoot; chord.type = "mM7"; break; } // mM7 (no 5th)
        
        // 2-note partial chords (Yamaha supports 2-finger chords)
        if (intervals == std::vector<int>{0, 7}) { chord.rootNote = candidateRoot; chord.type = "5"; break; } // Power chord
        if (intervals == std::vector<int>{0, 4}) { chord.rootNote = candidateRoot; chord.type = ""; break; } // Major 3rd
        if (intervals == std::vector<int>{0, 3}) { chord.rootNote = candidateRoot; chord.type = "m"; break; } // Minor 3rd
        if (intervals == std::vector<int>{0, 10}) { chord.rootNote = candidateRoot; chord.type = "7"; break; } // Root + b7
        if (intervals == std::vector<int>{0, 6}) { chord.rootNote = candidateRoot; chord.type = "dim"; break; } // Root + b5
    }

    if (chord.rootNote == -1) {
        // Fallback if absolutely no shape matches
        chord.rootNote = chord.bassNote;
        chord.type = "???";
    }

    return chord;
}

} // namespace engine
