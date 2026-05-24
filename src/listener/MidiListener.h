#pragma once
#include "RtMidi.h"
#include "../engine/ChordRecognizer.h"
#include <memory>
#include <string>
#include <vector>

namespace engine {

class MidiListener {
public:
    MidiListener();
    ~MidiListener();

    bool openDefaultPort();
    bool openPort(unsigned int portNumber);
    void listPorts();
    
    unsigned int getPortCount();
    std::string getPortName(unsigned int portNumber);
    
    ChordRecognizer& getChordRecognizer() { return m_chordRecognizer; }
    
    void setSplitPoint(int splitPoint) { m_splitPoint = splitPoint; }
    int getSplitPoint() const { return m_splitPoint; }

    // The callback must be static for RtMidi
    static void midiCallback(double deltatime, std::vector<unsigned char> *message, void *userData);

private:
    void handleMidiMessage(const std::vector<unsigned char>& message);

    std::unique_ptr<RtMidiIn> m_midiIn;
    ChordRecognizer m_chordRecognizer;
    std::string m_lastChordString;
    int m_splitPoint = 54; // Default to F#2 (MIDI Note 54)
};

} // namespace engine
