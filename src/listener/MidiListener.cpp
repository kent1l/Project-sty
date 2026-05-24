#include "MidiListener.h"
#include <iostream>

namespace engine {

MidiListener::MidiListener() {
    try {
        m_midiIn = std::make_unique<RtMidiIn>();
    } catch (RtMidiError &error) {
        error.printMessage();
    }
}

MidiListener::~MidiListener() {
    if (m_midiIn) m_midiIn->closePort();
}

void MidiListener::listPorts() {
    if (!m_midiIn) return;
    unsigned int nPorts = m_midiIn->getPortCount();
    std::cout << "\nThere are " << nPorts << " MIDI input sources available.\n";
    for (unsigned int i = 0; i < nPorts; i++) {
        std::cout << "  Input Port #" << i << ": " << m_midiIn->getPortName(i) << '\n';
    }
}

unsigned int MidiListener::getPortCount() {
    if (!m_midiIn) return 0;
    return m_midiIn->getPortCount();
}

std::string MidiListener::getPortName(unsigned int portNumber) {
    if (!m_midiIn || portNumber >= m_midiIn->getPortCount()) return "";
    return m_midiIn->getPortName(portNumber);
}

bool MidiListener::openDefaultPort() {
    if (!m_midiIn) return false;
    if (m_midiIn->getPortCount() == 0) {
        std::cout << "No MIDI input devices currently available. Waiting for keyboard connection..." << std::endl;
        return false;
    }
    return openPort(0);
}

bool MidiListener::openPort(unsigned int portNumber) {
    if (!m_midiIn || portNumber >= m_midiIn->getPortCount()) return false;
    
    std::string portName = m_midiIn->getPortName(portNumber);
    m_midiIn->openPort(portNumber);
    m_midiIn->setCallback(&MidiListener::midiCallback, this);
    
    // Ignore sysex, timing, or active sensing messages to focus only on notes
    m_midiIn->ignoreTypes(true, true, true);
    
    std::cout << "Listening for live chords on: " << portName << "\n" << std::endl;
    return true;
}

void MidiListener::midiCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
    MidiListener* listener = static_cast<MidiListener*>(userData);
    if (listener && message) {
        listener->handleMidiMessage(*message);
    }
}

void MidiListener::handleMidiMessage(const std::vector<unsigned char>& message) {
    // Standard MIDI Note On/Off messages are 3 bytes long
    if (message.size() < 3) return;

    unsigned char status = message[0] & 0xF0; // Mask out the channel
    unsigned char note = message[1];
    unsigned char velocity = message[2];

    bool chordChanged = false;

    // Note On (sometimes Note On with velocity 0 is used as Note Off)
    if (status == 0x90) {
        if (velocity > 0) {
            if (note <= m_splitPoint) {
                m_chordRecognizer.noteOn(note);
                std::cout << "\n[MIDI IN] Key Pressed: " << (int)note << " -> Chord is now: " << m_chordRecognizer.detectChord().toString() << std::endl;
                chordChanged = true;
            } else {
                std::cout << "\n[MIDI IN] Melody Key Pressed: " << (int)note << " (Bypassed Chord Recognition)" << std::endl;
            }
        } else {
            if (note <= m_splitPoint) {
                m_chordRecognizer.noteOff(note);
                chordChanged = true;
            }
        }
    } 
    // Note Off
    else if (status == 0x80) {
        if (note <= m_splitPoint) {
            m_chordRecognizer.noteOff(note);
            chordChanged = true;
        }
    }

    if (chordChanged) {
        Chord currentChord = m_chordRecognizer.detectChord();
        if (m_chordCallback) {
            m_chordCallback(currentChord);
        }
        std::string newChordStr = currentChord.toString();
        
        // Only print if the chord actually changed to avoid spamming the console
        if (newChordStr != m_lastChordString) {
            if (newChordStr == "No Chord") {
                std::cout << "\r[Live] Chord: ---                  " << std::flush;
            } else {
                std::cout << "\r[Live] Chord: " << newChordStr << "                  " << std::flush;
            }
            m_lastChordString = newChordStr;
        }
    }
}

} // namespace engine
