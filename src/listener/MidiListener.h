#pragma once
#include "RtMidi.h"
#include "../engine/ChordRecognizer.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

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
    
    void setChordCallback(std::function<void(const Chord&)> cb) { m_chordCallback = cb; }

    // The callback must be static for RtMidi
    static void midiCallback(double deltatime, std::vector<unsigned char> *message, void *userData);

private:
    void handleMidiMessage(const std::vector<unsigned char>& message);

    std::unique_ptr<RtMidiIn> m_midiIn;
    ChordRecognizer m_chordRecognizer;
    std::string m_lastChordString;
    std::function<void(const Chord&)> m_chordCallback;
    int m_splitPoint = 59; // Default to B3 (MIDI Note 59) — everything at or below this is chord zone
};

} // namespace engine
