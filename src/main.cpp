#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "reader/SFF2Parser.h"
#include "clock/MasterClock.h"
#include "listener/MidiListener.h"
#include "listener/MidiOutput.h"
#include "listener/WebSocketServer.h"
#include "engine/Sequencer.h"
#include "engine/StyleController.h"
#include "engine/RegistrationMemory.h"

int main(int argc, char* argv[]) {
    std::cout << "==========================================\n";
    std::cout << "   Yamaha Arranger Engine - PRO Version   \n";
    std::cout << "==========================================\n\n";
    
    std::string styleFile = (argc > 1) ? argv[1] : "LoveSong.S687.prs.bk";
    engine::SFF2Parser parser;
    if (!parser.loadFile(styleFile)) {
        std::cout << "Failed to load style file. Exiting." << std::endl;
        return 1;
    }

    std::cout << "\n--- MIDI Input Settings (Keyboard) ---" << std::endl;
    engine::MidiListener liveListener;
    liveListener.listPorts();
    
    unsigned int inPort = 0;
    if (liveListener.getPortCount() > 1) {
        std::cout << "Select Yamaha Keyboard Input Port: ";
        std::cin >> inPort;
    }
    liveListener.openPort(inPort);
    
    // Set split point (MIDI Note 54 / F#2)
    liveListener.setSplitPoint(54);
    std::cout << "Split Point active at MIDI Note " << liveListener.getSplitPoint() << " (F#2)" << std::endl;
    
    std::cout << "\n--- MIDI Output Settings (loopMIDI/VST) ---" << std::endl;
    engine::MidiOutput midiOut;
    midiOut.listPorts();
    
    unsigned int outPort = 0;
    if (midiOut.getPortCount() > 1) {
        std::cout << "Select loopMIDI Output Port: ";
        std::cin >> outPort;
    }
    midiOut.openPort(outPort);

    std::cout << "\n--- Booting Playback Sequencer ---" << std::endl;
    engine::Sequencer sequencer(parser, midiOut, liveListener.getChordRecognizer());
    
    engine::StyleController styleController;
    engine::RegistrationBank registrationBank;
    
    // Pre-populate some registration memory slots for demo/testing
    registrationBank.save(1, styleFile, 120.0, engine::StyleSection::MAIN_A);
    registrationBank.save(2, styleFile, 128.0, engine::StyleSection::MAIN_B);
    registrationBank.save(3, styleFile, 135.0, engine::StyleSection::MAIN_C);
    registrationBank.save(4, styleFile, 140.0, engine::StyleSection::MAIN_D);
    registrationBank.save(5, styleFile, 100.0, engine::StyleSection::MAIN_A);
    registrationBank.save(6, styleFile, 110.0, engine::StyleSection::MAIN_B);
    
    // Set the Sequencer to play Main C as the default starting section
    styleController.buttonMain('C');
    styleController.processMeasureBoundary(); // Apply immediately
    sequencer.setSection(styleController.getCurrentSectionName());
    
    engine::MasterClock clock;
    clock.setTempo(120.0);
    
    // The Grand Engine Loop!
    clock.setTickCallback([&sequencer, &styleController](uint64_t currentTick) {
        // Run variation/section transitions on measure boundaries
        // Standard style measure = 4 beats = 4 * 1920 ticks = 7680 ticks
        if (currentTick > 0 && currentTick % 7680 == 0) {
            if (styleController.processMeasureBoundary()) {
                std::string newSection = styleController.getCurrentSectionName();
                sequencer.setSection(newSection);
            }
        }
        
        sequencer.tick(1);
    });

    std::cout << "\n--- Initializing WebSocket Server ---" << std::endl;
    engine::WebSocketServer wsServer;
    
    // Register commands from Web UI
    wsServer.setCommandCallback([&](const std::string& cmd, const std::string& val) {
        std::cout << "[WebSocket] Command: " << cmd << " | Value: " << val << std::endl;
        
        if (cmd == "split") {
            try {
                int newSplit = std::stoi(val);
                liveListener.setSplitPoint(newSplit);
                std::cout << ">>> Split point updated to MIDI Note: " << newSplit << " <<<" << std::endl;
            } catch (...) {}
        }
        else if (cmd == "tempo") {
            try {
                double newTempo = std::stod(val);
                clock.setTempo(newTempo);
            } catch (...) {}
        }
        else if (cmd == "load_bank") {
            try {
                int slot = std::stoi(val);
                std::string outStyle;
                double outTempo = 120.0;
                engine::StyleSection outSection = engine::StyleSection::MAIN_A;
                
                if (registrationBank.load(slot, outStyle, outTempo, outSection)) {
                    clock.setTempo(outTempo);
                    
                    // Force variation immediately
                    char varChar = 'A';
                    if (outSection == engine::StyleSection::MAIN_B) varChar = 'B';
                    else if (outSection == engine::StyleSection::MAIN_C) varChar = 'C';
                    else if (outSection == engine::StyleSection::MAIN_D) varChar = 'D';
                    
                    styleController.buttonMain(varChar);
                    styleController.processMeasureBoundary();
                    sequencer.setSection(styleController.getCurrentSectionName());
                }
            } catch (...) {}
        }
        else if (cmd == "main") {
            if (!val.empty()) {
                styleController.buttonMain(val[0]);
            }
        }
        else if (cmd == "intro") {
            if (!val.empty()) {
                styleController.buttonIntro(val[0]);
            }
        }
        else if (cmd == "ending") {
            if (!val.empty()) {
                styleController.buttonEnding(val[0]);
            }
        }
        else if (cmd == "break") {
            styleController.buttonBreak();
        }
    });

    if (wsServer.start(9090)) {
        std::cout << "[WebSocket] Server running on ws://localhost:9090" << std::endl;
    } else {
        std::cerr << "[WebSocket] Failed to start server." << std::endl;
    }

    std::cout << "\n>>> ENGINE RUNNING! <<<" << std::endl;
    std::cout << "Play chords with your left hand. The sequencer is actively looping!" << std::endl;
    std::cout << "Open ui/index.html in your browser to interact via the Web UI." << std::endl;
    std::cout << "Press Ctrl+C to stop.\n" << std::endl;
    
    clock.start();
    
    // Main thread loops and sends periodic state updates to the UI
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Read the current chord detected by liveListener
        engine::Chord currentChord = liveListener.getChordRecognizer().detectChord();
        std::string chordStr = currentChord.toString();
        if (chordStr == "No Chord") {
            chordStr = ""; // Clear string matches UI expected format
        }
        
        std::string currentSection = styleController.getCurrentSectionName();
        double currentTempo = clock.getTempo();
        
        wsServer.sendStateUpdate(chordStr, currentSection, currentTempo);
    }
    
    clock.stop();
    wsServer.stop();
    return 0;
}
