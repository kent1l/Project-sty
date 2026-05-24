#include "Sequencer.h"
#include <iostream>

namespace engine {

Sequencer::Sequencer(SFF2Parser& parser, MidiOutput& midiOut, ChordRecognizer& chordRecognizer)
    : m_parser(parser), m_midiOut(midiOut), m_chordRecognizer(chordRecognizer),
      m_sectionStartTick(0), m_sectionEndTick(0), m_relativeTick(0), m_eventIndex(0) {
    m_lastValidChord.rootNote = -1;

    // Initialize note memory to -1 (empty)
    for (int ch = 0; ch < 16; ++ch) {
        for (int n = 0; n < 128; ++n) {
            m_playingNotes[ch][n] = -1;
        }
    }
}

Sequencer::~Sequencer() {}

void Sequencer::setSection(const std::string& sectionName) {
    m_currentSection = sectionName;
    const auto& events = m_parser.getMidiEvents();
    const auto& rules = m_parser.getCasmRules();
    
    m_sectionStartTick = 0;
    m_sectionEndTick = 0;
    m_eventIndex = 0;
    
    // Find the section markers
    bool foundStart = false;
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].status == 0xFF && (events[i].data1 == 0x06 || events[i].data1 == 0x01)) {
            if (!foundStart && events[i].metaText == sectionName) {
                m_sectionStartTick = events[i].absoluteTick;
                m_eventIndex = i;
                foundStart = true;
            } else if (foundStart && !events[i].metaText.empty() && events[i].metaText.find("fn:") == std::string::npos) {
                // Next major section marker means the end of this section!
                if (events[i].absoluteTick > m_sectionStartTick) {
                    m_sectionEndTick = events[i].absoluteTick;
                    break;
                }
            }
        }
    }
    
    if (foundStart && m_sectionEndTick == 0) {
        if (!events.empty()) m_sectionEndTick = events.back().absoluteTick;
    }
    
    m_relativeTick = m_sectionStartTick;
    std::cout << "\n[Sequencer] Jumped to Section: " << sectionName << " (Tick " << m_sectionStartTick << " to " << m_sectionEndTick << ")" << std::endl;

    // Track the bank select MSB/LSB per channel to flush them alongside Program Changes
    uint8_t channelMSB[16] = {0};
    uint8_t channelLSB[16] = {0};

    // First pass: scan for Bank Select CC0/CC32 to resolve the voice banks
    for (const auto& ev : events) {
        bool isSInt = (ev.absoluteTick < 100);
        bool isInSection = (ev.absoluteTick >= m_sectionStartTick && ev.absoluteTick < m_sectionEndTick);
        
        if (isSInt || isInSection) {
            uint8_t type = ev.status & 0xF0;
            uint8_t channel = ev.status & 0x0F;
            if (type == 0xB0) {
                if (ev.data1 == 0) {
                    channelMSB[channel] = ev.data2;
                } else if (ev.data1 == 32) {
                    channelLSB[channel] = ev.data2;
                }
            }
        }
    }

    // Second pass: Send the Program Changes and other Control Changes mapped through CASM
    for (const auto& ev : events) {
        bool isSInt = (ev.absoluteTick < 100);
        bool isInSection = (ev.absoluteTick >= m_sectionStartTick && ev.absoluteTick < m_sectionEndTick);
        
        if (isSInt || isInSection) {
            uint8_t type = ev.status & 0xF0;
            uint8_t channel = ev.status & 0x0F;
            
            if (type == 0xC0 || type == 0xB0) {
                // Find matching CASM rule for this channel and section
                uint8_t destChannel = channel;
                bool ruleMatched = false;
                std::string trackName = "";
                
                for (const auto& rule : rules) {
                    if (rule.destChannel == channel && rule.appliedSections.find(m_currentSection) != std::string::npos) {
                        if (rule.trackName.find("CC") != std::string::npos) continue;
                        destChannel = rule.destChannel;
                        trackName = rule.trackName;
                        ruleMatched = true;
                    }
                }
                
                if (ruleMatched) {
                    if (type == 0xC0) {
                        uint8_t program = ev.data1;
                        uint8_t bankMSB = channelMSB[channel];
                        uint8_t bankLSB = channelLSB[channel];
                        
                        // Remap MegaVoice or proprietary patches to GM equivalents
                        m_megaVoiceTranslator.translatePatch(trackName, bankMSB, bankLSB, program);
                        
                        // Flush Bank Select first
                        m_midiOut.sendControlChange(destChannel, 0, bankMSB);
                        m_midiOut.sendControlChange(destChannel, 32, bankLSB);
                        // Then Program Change
                        m_midiOut.sendProgramChange(destChannel, program);
                        
                        std::cout << "[Sequencer] Flushed Channel setup: Track=" << trackName 
                                  << " (Ch " << (int)destChannel + 1 << ") -> Bank: " 
                                  << (int)bankMSB << ":" << (int)bankLSB 
                                  << ", PC: " << (int)program << std::endl;
                    }
                    else if (type == 0xB0 && ev.data1 != 0 && ev.data1 != 32) {
                        // Forward other general CCs (volume, pan, etc.)
                        m_midiOut.sendControlChange(destChannel, ev.data1, ev.data2);
                    }
                }
            }
        }
    }
}

void Sequencer::tick(uint32_t currentTick) {
    if (m_currentSection.empty() || m_sectionEndTick <= m_sectionStartTick) return;

    // Advance relative playhead by 1 clock pulse (since tick is called per master pulse)
    m_relativeTick++; 
    
    if (m_relativeTick >= m_sectionEndTick) {
        // Stop all hanging notes before looping!
        for (int ch = 0; ch < 16; ch++) {
            m_midiOut.sendControlChange(ch, 123, 0); // All Notes Off CC
        }
        
        m_relativeTick = m_sectionStartTick;
        
        const auto& events = m_parser.getMidiEvents();
        for (size_t i = 0; i < events.size(); i++) {
            if (events[i].absoluteTick >= m_sectionStartTick) {
                m_eventIndex = i;
                break;
            }
        }
        std::cout << "\n[Sequencer] Looping Section: " << m_currentSection << std::endl;
    }
    
    // Debug: Print current chord every downbeat (tick 0 of a measure)
    if (m_relativeTick % 1920 == 0) {
        std::cout << "[Sequencer] Beat " << (m_relativeTick / 1920) + 1 << " | Current Target Chord: " << m_chordRecognizer.detectChord().toString() << std::endl;
    }

    const auto& events = m_parser.getMidiEvents();
    const auto& rules = m_parser.getCasmRules();
    
    while (m_eventIndex < events.size() && events[m_eventIndex].absoluteTick <= m_relativeTick) {
        const MidiEvent& ev = events[m_eventIndex];
        
        uint8_t type = ev.status & 0xF0;
        uint8_t channel = ev.status & 0x0F;
        
        // 1. CASM Channel Mapping (Applies to all events)
        uint8_t destChannel = channel;
        std::string trackName = "";
        bool ruleMatched = false;
        CasmRule matchedRule;
        
        for (const auto& rule : rules) {
            if (rule.destChannel == channel && rule.appliedSections.find(m_currentSection) != std::string::npos) {
                if (rule.trackName.find("CC") != std::string::npos) continue;
                destChannel = rule.destChannel;
                trackName = rule.trackName;
                matchedRule = rule;
                ruleMatched = true;
            }
        }
        
        if (!ruleMatched) {
            m_eventIndex++;
            continue;
        }

        if (type == 0x90 || type == 0x80) {
            int originalNote = ev.data1;
            int velocity = ev.data2;
            
            Chord currentChord = m_chordRecognizer.detectChord();
            if (currentChord.rootNote != -1) {
                m_lastValidChord = currentChord; // Update memory!
            }
            
            // Note On
            if (type == 0x90 && velocity > 0) {
                int transposedNote = originalNote;
                
                if (m_lastValidChord.rootNote != -1) {
                    transposedNote = m_transpositionBrain.calculateTransposition(originalNote, m_lastValidChord, matchedRule);
                }
                
                // MegaVoice Translation
                if (!trackName.empty()) {
                    std::string articulation;
                    m_megaVoiceTranslator.translate(trackName, transposedNote, velocity, articulation);
                }
                
                // Store exact transposed note in memory
                m_playingNotes[destChannel][originalNote] = transposedNote;
                m_midiOut.sendNoteOn(destChannel, transposedNote, velocity);
            } 
            // Note Off (Type 0x80 OR Note On with 0 velocity)
            else {
                int noteToTurnOff = m_playingNotes[destChannel][originalNote];
                
                // Only send Note Off if we actually mapped and started this note
                if (noteToTurnOff != -1) {
                    m_midiOut.sendNoteOff(destChannel, noteToTurnOff);
                    m_playingNotes[destChannel][originalNote] = -1; // Clear memory
                }
            }
        } 
        else if (type == 0xB0) {
            // Forward Control Changes (including CC0 and CC32 Bank Selects) unaltered
            m_midiOut.sendControlChange(destChannel, ev.data1, ev.data2);
        }
        else if (type == 0xC0) {
            uint8_t program = ev.data1;
            uint8_t bankMSB = 0;
            uint8_t bankLSB = 0;
            m_megaVoiceTranslator.translatePatch(trackName, bankMSB, bankLSB, program);
            
            m_midiOut.sendControlChange(destChannel, 0, bankMSB);
            m_midiOut.sendControlChange(destChannel, 32, bankLSB);
            m_midiOut.sendProgramChange(destChannel, program);
        }
        
        m_eventIndex++;
    }
}

} // namespace engine
