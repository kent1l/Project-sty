#include "MidiOutput.h"
#include <iostream>

namespace engine {

MidiOutput::MidiOutput() {
    try {
        m_midiOut = std::make_unique<RtMidiOut>();
    } catch (RtMidiError &error) {
        error.printMessage();
    }
}

MidiOutput::~MidiOutput() {
    if (m_midiOut) m_midiOut->closePort();
}

void MidiOutput::listPorts() {
    if (!m_midiOut) return;
    unsigned int nPorts = m_midiOut->getPortCount();
    std::cout << "\nThere are " << nPorts << " MIDI output ports available.\n";
    for (unsigned int i = 0; i < nPorts; i++) {
        std::cout << "  Output Port #" << i << ": " << m_midiOut->getPortName(i) << '\n';
    }
}

unsigned int MidiOutput::getPortCount() {
    if (!m_midiOut) return 0;
    return m_midiOut->getPortCount();
}

std::string MidiOutput::getPortName(unsigned int portNumber) {
    if (!m_midiOut || portNumber >= m_midiOut->getPortCount()) return "";
    return m_midiOut->getPortName(portNumber);
}

bool MidiOutput::openPort(unsigned int portNumber) {
    if (!m_midiOut || portNumber >= m_midiOut->getPortCount()) return false;
    
    std::string portName = m_midiOut->getPortName(portNumber);
    m_midiOut->openPort(portNumber);
    std::cout << "Opened MIDI Output to: " << portName << std::endl;
    return true;
}

void MidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!m_midiOut || !m_midiOut->isPortOpen()) return;
    std::vector<unsigned char> message;
    message.push_back(0x90 | (channel & 0x0F));
    message.push_back(note & 0x7F);
    message.push_back(velocity & 0x7F);
    m_midiOut->sendMessage(&message);
}

void MidiOutput::sendNoteOff(uint8_t channel, uint8_t note) {
    if (!m_midiOut || !m_midiOut->isPortOpen()) return;
    std::vector<unsigned char> message;
    message.push_back(0x80 | (channel & 0x0F));
    message.push_back(note & 0x7F);
    message.push_back(0); // Velocity 0 for note off
    m_midiOut->sendMessage(&message);
}

void MidiOutput::sendProgramChange(uint8_t channel, uint8_t program) {
    if (!m_midiOut || !m_midiOut->isPortOpen()) return;
    std::vector<unsigned char> message;
    message.push_back(0xC0 | (channel & 0x0F));
    message.push_back(program & 0x7F);
    m_midiOut->sendMessage(&message);
}

void MidiOutput::sendControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    if (!m_midiOut || !m_midiOut->isPortOpen()) return;
    std::vector<unsigned char> message;
    message.push_back(0xB0 | (channel & 0x0F));
    message.push_back(cc & 0x7F);
    message.push_back(value & 0x7F);
    m_midiOut->sendMessage(&message);
}

void MidiOutput::sendPitchBend(uint8_t channel, uint8_t lsb, uint8_t msb) {
    if (!m_midiOut || !m_midiOut->isPortOpen()) return;
    std::vector<unsigned char> message;
    message.push_back(0xE0 | (channel & 0x0F));
    message.push_back(lsb & 0x7F);
    message.push_back(msb & 0x7F);
    m_midiOut->sendMessage(&message);
}

} // namespace engine
