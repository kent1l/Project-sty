#pragma once
#include "RtMidi.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace engine {

class MidiOutput {
public:
    MidiOutput();
    ~MidiOutput();

    void listPorts();
    unsigned int getPortCount();
    std::string getPortName(unsigned int portNumber);
    bool openPort(unsigned int portNumber);
    
    // Core MIDI Commands
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendNoteOff(uint8_t channel, uint8_t note);
    void sendProgramChange(uint8_t channel, uint8_t program);
    void sendControlChange(uint8_t channel, uint8_t cc, uint8_t value);
    void sendPitchBend(uint8_t channel, uint8_t lsb, uint8_t msb);

private:
    std::unique_ptr<RtMidiOut> m_midiOut;
};

} // namespace engine
