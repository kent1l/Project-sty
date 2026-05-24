#include "Sequencer.h"
#include <iostream>
#include <algorithm>
#include <cctype>

namespace engine {

Sequencer::Sequencer(SFF2Parser& parser, MidiOutput& midiOut, ChordRecognizer& chordRecognizer)
    : m_parser(parser), m_midiOut(midiOut), m_chordRecognizer(chordRecognizer),
      m_sectionStartTick(0), m_sectionEndTick(0), m_relativeTick(0), m_eventIndex(0),
      m_styleData(&parser) {
    m_lastValidChord.rootNote = -1;
    m_activeChord.rootNote = -1;

    // Cache bank select MSB/LSB per destination channel
    for (int ch = 0; ch < 16; ++ch) {
        m_cachedMSB[ch] = 0;
        m_cachedLSB[ch] = 0;
    }
}

Sequencer::~Sequencer() {}

bool Sequencer::isPlaying() const {
    return !m_currentSection.empty() && m_currentSection != "STOPPED";
}

void Sequencer::updateLiveChord(const Chord& newChord) {
    std::lock_guard<std::mutex> lock(m_chordMutex);
    
    bool chordChanged = false;
    if (newChord.rootNote != -1) {
        if (m_activeChord.rootNote == -1 || 
            m_activeChord.rootNote != newChord.rootNote || 
            m_activeChord.type != newChord.type) {
            chordChanged = true;
        }
    }
    
    Chord oldChord = m_activeChord;
    m_activeChord = newChord;
    
    if (chordChanged && isPlaying()) {
        std::cout << "[Sequencer] Real-time Chord Change: " 
                  << (oldChord.rootNote == -1 ? "NONE" : oldChord.toString()) 
                  << " -> " << newChord.toString() << std::endl;
                  
        // Perform note retrigger / pitch shift on m_activeNoteMap
        std::vector<uint32_t> keysToErase;
        std::vector<std::pair<uint32_t, int>> keysToUpdate;
        
        for (auto& pair : m_activeNoteMap) {
            uint32_t trackingKey = pair.first;
            int oldTransposed = pair.second;
            
            uint8_t channel = (trackingKey >> 16) & 0x0F;
            uint8_t originalNote = (trackingKey >> 8) & 0xFF;
            
            CasmRule matchedRule = m_styleData->getCasmRuleForChannel(m_currentSection, channel);
            uint8_t destChannel = matchedRule.destChannel;
            
            int velocity = m_activeVelocityMap[trackingKey];
            uint8_t rtr = matchedRule.retriggerRule;
            
            std::string lowerTrackName = matchedRule.trackName;
            std::transform(lowerTrackName.begin(), lowerTrackName.end(), lowerTrackName.begin(), ::tolower);

            bool isGuitar = (lowerTrackName.find("gtr") != std::string::npos || 
                             lowerTrackName.find("guitar") != std::string::npos ||
                             matchedRule.ntt == 4);
                             
            bool isBass = (lowerTrackName.find("bass") != std::string::npos || 
                           lowerTrackName.find("bs") != std::string::npos ||
                           matchedRule.ntt == 3);
            
            if (rtr == 0) {
                m_midiOut.sendNoteOff(destChannel, oldTransposed);
                keysToErase.push_back(trackingKey);
            } 
            else if (rtr == 1) {
                int newTransposed = m_transpositionBrain.calculateTransposition(originalNote, m_activeChord, matchedRule);
                
                if (isGuitar) {
                    while (newTransposed < 40) newTransposed += 12;
                    while (newTransposed > 84) newTransposed -= 12;
                }
                else if (isBass) {
                    while (newTransposed < 28) newTransposed += 12;
                    while (newTransposed > 67) newTransposed -= 12;
                }
                                       
                if ((isGuitar && newTransposed < 40) || (isBass && newTransposed < 28)) {
                    m_midiOut.sendNoteOff(destChannel, oldTransposed);
                    keysToErase.push_back(trackingKey);
                    continue;
                }
                
                if (!matchedRule.trackName.empty()) {
                    std::string articulation;
                    m_megaVoiceTranslator.translate(matchedRule.trackName, newTransposed, velocity, articulation);
                }
                
                m_midiOut.sendNoteOn(destChannel, newTransposed, velocity);
                m_midiOut.sendNoteOff(destChannel, oldTransposed);
                
                keysToUpdate.push_back({trackingKey, newTransposed});
            }
            else {
                int newTransposed = m_transpositionBrain.calculateTransposition(originalNote, m_activeChord, matchedRule);
                
                if (isGuitar) {
                    while (newTransposed < 40) newTransposed += 12;
                    while (newTransposed > 84) newTransposed -= 12;
                }
                else if (isBass) {
                    while (newTransposed < 28) newTransposed += 12;
                    while (newTransposed > 67) newTransposed -= 12;
                }
                                       
                if ((isGuitar && newTransposed < 40) || (isBass && newTransposed < 28)) {
                    m_midiOut.sendNoteOff(destChannel, oldTransposed);
                    keysToErase.push_back(trackingKey);
                    continue;
                }
                
                if (!matchedRule.trackName.empty()) {
                    std::string articulation;
                    m_megaVoiceTranslator.translate(matchedRule.trackName, newTransposed, velocity, articulation);
                }
                
                m_midiOut.sendNoteOff(destChannel, oldTransposed);
                m_midiOut.sendNoteOn(destChannel, newTransposed, velocity);
                
                keysToUpdate.push_back({trackingKey, newTransposed});
            }
        }
        
        for (uint32_t key : keysToErase) {
            m_activeNoteMap.erase(key);
            m_activeVelocityMap.erase(key);
        }
        for (const auto& p : keysToUpdate) {
            m_activeNoteMap[p.first] = p.second;
        }
    }
}

void Sequencer::killAllActiveNotes() {
    std::lock_guard<std::mutex> lock(m_chordMutex);
    for (const auto& pair : m_activeNoteMap) {
        uint32_t trackingKey = pair.first;
        int transformedNote = pair.second;
        uint8_t channel = (trackingKey >> 16) & 0x0F;
        CasmRule rule = m_styleData->getCasmRuleForChannel(m_currentSection, channel);
        m_midiOut.sendNoteOff(rule.destChannel, transformedNote);
    }
    m_activeNoteMap.clear();
    m_activeVelocityMap.clear();
}

void Sequencer::setSection(const std::string& sectionName) {
    m_currentSection = sectionName;
    const auto& events = m_parser.getMidiEvents();
    const auto& rules = m_parser.getCasmRules();
    
    m_sectionStartTick = 0;
    m_sectionEndTick = 0;
    m_eventIndex = 0;
    
    if (sectionName == "STOPPED") {
        killAllActiveNotes();
        std::cout << "[Sequencer] Sequencer Stopped." << std::endl;
        return;
    }
    
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
    
    if (!foundStart) {
        std::cout << "[Sequencer] WARNING: Section [" << sectionName << "] not found in style! Searching for fallback..." << std::endl;
        for (const auto& ev : events) {
            if (ev.status == 0xFF && (ev.data1 == 0x06 || ev.data1 == 0x01)) {
                if (!ev.metaText.empty() && ev.metaText.find("fn:") == std::string::npos) {
                    std::cout << "[Sequencer] Auto-selected fallback section: [" << ev.metaText << "]" << std::endl;
                    m_currentSection = ev.metaText;
                    setSection(ev.metaText); // Recurse with fallback
                    return;
                }
            }
        }
        std::cerr << "[Sequencer] ERROR: No valid section markers found in style file!" << std::endl;
        return;
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
                    if (rule.sourceChannel == channel && rule.appliedSections.find(m_currentSection) != std::string::npos) {
                        if (rule.trackName.find("CC") != std::string::npos) continue;
                        destChannel = rule.destChannel;
                        trackName = rule.trackName;
                        ruleMatched = true;
                        break;
                    }
                }
                
                if (ruleMatched) {
                    if (type == 0xC0) {
                        uint8_t program = ev.data1;
                        uint8_t bankMSB = channelMSB[channel];
                        uint8_t bankLSB = channelLSB[channel];
                        
                        m_megaVoiceTranslator.translatePatch(trackName, bankMSB, bankLSB, program);
                        
                        m_midiOut.sendControlChange(destChannel, 0, bankMSB);
                        m_midiOut.sendControlChange(destChannel, 32, bankLSB);
                        m_cachedMSB[destChannel] = bankMSB;
                        m_cachedLSB[destChannel] = bankLSB;
                        m_midiOut.sendProgramChange(destChannel, program);
                        
                        std::cout << "[Sequencer] Flushed Channel setup: Track=" << trackName 
                                  << " (Ch " << (int)destChannel + 1 << ") -> Bank: " 
                                  << (int)bankMSB << ":" << (int)bankLSB 
                                  << ", PC: " << (int)program << std::endl;
                    }
                    else if (type == 0xB0 && ev.data1 != 0 && ev.data1 != 32) {
                        m_midiOut.sendControlChange(destChannel, ev.data1, ev.data2);
                    }
                }
            }
        }
    }
}

void Sequencer::tick(uint32_t currentTick) {
    if (!isPlaying()) return;

    // Advance relative playhead by 1 clock pulse
    m_relativeTick++; 
    
    if (m_relativeTick >= m_sectionEndTick) {
        killAllActiveNotes();
        
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
    
    // Debug: Print active chord every downbeat (tick 0 of a measure)
    if (m_relativeTick % 1920 == 0) {
        Chord activeChordCopy;
        {
            std::lock_guard<std::mutex> lock(m_chordMutex);
            activeChordCopy = m_activeChord;
        }
        std::cout << "[Sequencer] Beat " << (m_relativeTick / 1920) + 1 
                  << " | Current Target Chord: " << activeChordCopy.toString() << std::endl;
    }

    const auto& events = m_parser.getMidiEvents();
    
    while (m_eventIndex < events.size() && events[m_eventIndex].absoluteTick <= m_relativeTick) {
        const MidiEvent& ev = events[m_eventIndex];
        
        uint8_t status = ev.status;
        uint8_t type = status & 0xF0;
        uint8_t channel = status & 0x0F;
        
        CasmRule channelRule = m_styleData->getCasmRuleForChannel(m_currentSection, channel);
        
        // 5. Bypass Rules for Global Data
        if (type == 0xE0) {
            m_midiOut.sendPitchBend(channelRule.destChannel, ev.data1, ev.data2);
            m_eventIndex++;
            continue;
        }
        else if (type == 0xB0) {
            m_midiOut.sendControlChange(channelRule.destChannel, ev.data1, ev.data2);
            if (ev.data1 == 0) {
                m_cachedMSB[channelRule.destChannel] = ev.data2;
            } else if (ev.data1 == 32) {
                m_cachedLSB[channelRule.destChannel] = ev.data2;
            }
            m_eventIndex++;
            continue;
        }
        else if (type == 0xC0) {
            uint8_t program = ev.data1;
            uint8_t bankMSB = m_cachedMSB[channelRule.destChannel];
            uint8_t bankLSB = m_cachedLSB[channelRule.destChannel];
            m_megaVoiceTranslator.translatePatch(channelRule.trackName, bankMSB, bankLSB, program);
            
            m_cachedMSB[channelRule.destChannel] = bankMSB;
            m_cachedLSB[channelRule.destChannel] = bankLSB;
            
            m_midiOut.sendControlChange(channelRule.destChannel, 0, bankMSB);
            m_midiOut.sendControlChange(channelRule.destChannel, 32, bankLSB);
            m_midiOut.sendProgramChange(channelRule.destChannel, program);
            m_eventIndex++;
            continue;
        }
        else if (type == 0xF0) {
            m_eventIndex++;
            continue;
        }
        
        // 3. Note-On Interception Hook
        if (type == 0x90 && ev.data2 > 0) {
            int originalNote = ev.data1;
            int velocity = ev.data2;
            
            Chord activeChordCopy;
            {
                std::lock_guard<std::mutex> lock(m_chordMutex);
                activeChordCopy = m_activeChord;
            }
            
            bool isDrumTrack = (channelRule.destChannel == 9 || channelRule.destChannel == 8 || 
                                channelRule.trackName.find("Rhy") != std::string::npos || 
                                channelRule.trackName.find("dr") != std::string::npos);

            if (activeChordCopy.rootNote == -1 && !isDrumTrack) {
                m_eventIndex++;
                continue;
            }

            std::string lowerTrackName = channelRule.trackName;
            std::transform(lowerTrackName.begin(), lowerTrackName.end(), lowerTrackName.begin(), ::tolower);

            bool isGuitar = (lowerTrackName.find("gtr") != std::string::npos || 
                             lowerTrackName.find("guitar") != std::string::npos ||
                             channelRule.ntt == 4);
                             
            bool isBass = (lowerTrackName.find("bass") != std::string::npos || 
                           lowerTrackName.find("bs") != std::string::npos ||
                           channelRule.ntt == 3);

            // Cap velocity at 100 for normal notes (velocity < 115). High-velocity triggers are processed in MegaVoiceTranslator.
            if ((isGuitar || isBass) && velocity < 115 && velocity > 100) {
                velocity = 100;
            }

            int transformedNote = originalNote;
            if (!isDrumTrack) {
                transformedNote = m_transpositionBrain.calculateTransposition(originalNote, activeChordCopy, channelRule);
            }
            
            if (transformedNote == -1) {
                m_eventIndex++;
                continue;
            }

            // Fold notes to stay inside VST physical playable range
            if (isGuitar) {
                while (transformedNote < 40) transformedNote += 12;
                while (transformedNote > 84) transformedNote -= 12;
            }
            else if (isBass) {
                while (transformedNote < 28) transformedNote += 12;
                while (transformedNote > 67) transformedNote -= 12;
            }
            
            // Intercept and discard keyswitches
            if (isGuitar && transformedNote < 40) {
                m_eventIndex++;
                continue;
            }
            if (isBass && transformedNote < 28) {
                m_eventIndex++;
                continue;
            }

            if (!channelRule.trackName.empty()) {
                std::string articulation;
                m_megaVoiceTranslator.translate(channelRule.trackName, transformedNote, velocity, articulation);
            }
            
            {
                std::lock_guard<std::mutex> lock(m_chordMutex);
                uint32_t trackingKey = (channel << 16) | (originalNote << 8);
                m_activeNoteMap[trackingKey] = transformedNote;
                m_activeVelocityMap[trackingKey] = velocity;
            }

            m_midiOut.sendNoteOn(channelRule.destChannel, transformedNote, velocity);
        }
        // 4. Note-Off Alignment Hook
        else if (type == 0x80 || (type == 0x90 && ev.data2 == 0)) {
            int originalNote = ev.data1;
            uint32_t trackingKey = (channel << 16) | (originalNote << 8);
            
            int noteToTurnOff = -1;
            {
                std::lock_guard<std::mutex> lock(m_chordMutex);
                auto it = m_activeNoteMap.find(trackingKey);
                if (it != m_activeNoteMap.end()) {
                    noteToTurnOff = it->second;
                    m_activeNoteMap.erase(it);
                    m_activeVelocityMap.erase(trackingKey);
                }
            }
            
            if (noteToTurnOff != -1) {
                m_midiOut.sendNoteOff(channelRule.destChannel, noteToTurnOff);
            } else {
                m_midiOut.sendNoteOff(channelRule.destChannel, originalNote);
            }
        }
        
        m_eventIndex++;
    }
}

void Sequencer::clearNoteMemory() {
    killAllActiveNotes();
    m_lastValidChord.rootNote = -1;
}

} // namespace engine
