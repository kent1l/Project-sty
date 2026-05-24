#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include "reader/SFF2Parser.h"
#include "clock/MasterClock.h"
#include "listener/MidiListener.h"
#include "listener/MidiOutput.h"
#include "listener/WebSocketServer.h"
#include "engine/Sequencer.h"
#include "engine/StyleController.h"
#include "engine/RegistrationMemory.h"

// Global flag & MIDI output pointer used by the signal handler to silence
// all notes when the user presses Ctrl+C (or the process is terminated).
static std::atomic<bool> g_running{true};
static engine::MidiOutput* g_midiOut = nullptr;
static engine::MasterClock* g_clock   = nullptr;

void shutdownHandler(int /*signal*/) {
    g_running = false;
    if (g_clock) g_clock->stop();

    std::cout << "\n[ENGINE] Caught signal — sending All Sound Off to all channels..." << std::endl;
    if (g_midiOut) {
        for (int ch = 0; ch < 16; ++ch) {
            g_midiOut->sendControlChange(ch, 120, 0); // All Sound Off  (kills sustaining VST voices)
            g_midiOut->sendControlChange(ch, 123, 0); // All Notes Off  (clears held MIDI note states)
        }
    }
    std::cout << "[ENGINE] Shutdown complete." << std::endl;
}

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
    
    // Set split point (MIDI Note 59 / B2)
    liveListener.setSplitPoint(59);
    std::cout << "Split Point active at MIDI Note " << liveListener.getSplitPoint() << " (B2)" << std::endl;
    
    std::cout << "\n--- MIDI Output Settings (loopMIDI/VST) ---" << std::endl;
    engine::MidiOutput midiOut;
    midiOut.listPorts();
    
    unsigned int outPort = 0;
    if (midiOut.getPortCount() > 1) {
        std::cout << "Select loopMIDI Output Port: ";
        std::cin >> outPort;
    }
    midiOut.openPort(outPort);

    // Register signal handlers AFTER midiOut is alive so the handler can use it
    g_midiOut = &midiOut;
    std::signal(SIGINT,  shutdownHandler);
    std::signal(SIGTERM, shutdownHandler);

    std::cout << "\n--- Booting Playback Sequencer ---" << std::endl;
    engine::Sequencer sequencer(parser, midiOut, liveListener.getChordRecognizer());

    // --- CHANNEL OVERRIDE MAP ---
    // The CASM rules in the style file define their own destination channels.
    // Use setChannelMap(casmChannel, outputChannel) to reroute any track to
    // a different MIDI channel to match your VST/Carla setup.
    //
    // Both values are 0-based (MIDI ch 11 = index 10).
    // Yamaha style bass tracks are typically on CASM dest channel 2 (MIDI ch 3).
    // Redirect it to index 10 (MIDI ch 11) where your bass VST listens.
    sequencer.setChannelMap(2, 10);  // CASM ch3 (bass) -> MIDI ch11
    // Also cover edge cases where the style uses a different bass dest channel:
    sequencer.setChannelMap(3, 10);  // CASM ch4 -> MIDI ch11 (some styles)
    // Add more lines here if other tracks need rerouting, e.g.:
    // sequencer.setChannelMap(5, 14); // CASM ch6 (pad) -> MIDI ch15

    
    engine::StyleController styleController;
    styleController.setSequencer(&sequencer);
    
    liveListener.setChordCallback([&styleController](const engine::Chord& newChord) {
        styleController.onInputChordDetected(newChord);
    });
    
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
    g_clock = &clock;
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
        else if (cmd == "start") {
            if (clock.isRunning()) {
                clock.stop();
                styleController.stop();
                sequencer.clearNoteMemory();
                for (int ch = 0; ch < 16; ch++) {
                    midiOut.sendControlChange(ch, 123, 0);
                }
            } else {
                styleController.buttonMain('A');
                styleController.processMeasureBoundary();
                sequencer.setSection(styleController.getCurrentSectionName());
                clock.start();
            }
        }
        else if (cmd == "fill") {
            if (!val.empty()) {
                styleController.buttonMain(val[0]);
            }
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
    
    // Main thread loops and sends periodic state updates to the UI.
    // g_running is set to false by shutdownHandler() on Ctrl+C / SIGTERM.
    while (g_running) {
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

    // Graceful shutdown: stop clock and WebSocket server, then do a final
    // All Sound Off sweep in case the signal handler already sent notes off
    // but the sequencer still had something queued.
    clock.stop();
    sequencer.clearNoteMemory();
    for (int ch = 0; ch < 16; ++ch) {
        midiOut.sendControlChange(ch, 120, 0); // All Sound Off
        midiOut.sendControlChange(ch, 123, 0); // All Notes Off
    }
    wsServer.stop();
    return 0;
}
