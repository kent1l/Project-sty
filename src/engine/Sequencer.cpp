#include "Sequencer.h"
#include <iostream>
#include <algorithm>
#include <cctype>

namespace engine {

Sequencer::Sequencer(SFF2Parser& parser, MidiOutput& midiOut, ChordRecognizer& chordRecognizer)
    : m_parser(parser), m_midiOut(midiOut), m_chordRecognizer(chordRecognizer),
      m_sectionStartTick(0), m_sectionEndTick(0), m_relativeTick(0), m_eventIndex(0),
      m_styleData(&parser), m_bassOutputChannel(10) {
    m_lastValidChord.rootNote = -1;
    m_activeChord.rootNote = -1;

    for (int ch = 0; ch < 16; ++ch) {
        m_cachedMSB[ch]  = 0;
        m_cachedLSB[ch]  = 0;
        m_channelMap[ch] = static_cast<uint8_t>(ch); // Identity: no remapping by default
    }
}

Sequencer::~Sequencer() {}

bool Sequencer::isPlaying() const {
    return !m_currentSection.empty() && m_currentSection != "STOPPED";
}

void Sequencer::updateLiveChord(const Chord& newChord) {
    std::lock_guard<std::mutex> lock(m_chordMutex);
    
    // --- CHORD LATCH: Only accept a new chord when a real one is detected.
    // When rootNote == -1 it means the user just released keys (a "No Chord"
    // release event from ChordRecognizer). We IGNORE this and keep playing
    // the last valid chord so the bass & instruments don't cut off mid-loop.
    if (newChord.rootNote == -1) {
        return;
    }

    bool chordChanged = (m_activeChord.rootNote == -1 ||
                         m_activeChord.rootNote != newChord.rootNote ||
                         m_activeChord.type != newChord.type);

    Chord oldChord = m_activeChord;
    m_activeChord = newChord; // Only reached when newChord is valid

    if (chordChanged && isPlaying()) {
        std::cout << "[Sequencer] Real-time Chord Change: " 
                  << (oldChord.rootNote == -1 ? "NONE" : oldChord.toString()) 
                  << " -> " << newChord.toString() << std::endl;

        // --- SMOOTH CHORD TRANSITION ---
        // Phase 1: Calculate all new transpositions first.
        // Phase 2: Send ALL note-offs for old pitches.
        // Phase 3: Send ALL note-ons for new pitches.
        // This two-phase approach prevents the clipping burst caused by
        // simultaneously sounding old + new notes in the VST audio buffer.

        struct RetrigEntry {
            uint32_t trackingKey;
            uint8_t  destChannel;
            int      oldTransposed;
            int      newTransposed;
            int      velocity;
            bool     shouldErase; // true = just kill, false = retrigger
        };
        std::vector<RetrigEntry> entries;

        for (auto& pair : m_activeNoteMap) {
            uint32_t trackingKey = pair.first;
            int      oldTransposed = pair.second;

            uint8_t channel      = (trackingKey >> 16) & 0x0F;
            uint8_t originalNote = (trackingKey >> 8)  & 0xFF;

            CasmRule matchedRule = m_styleData->getCasmRuleForChannel(m_currentSection, channel);
            // Bass tracks always go to m_bassOutputChannel; others use the channel map
            uint8_t  destChannel = isBassRule(matchedRule) ? m_bassOutputChannel
                                                           : mapChannel(matchedRule.destChannel);
            int      velocity    = m_activeVelocityMap[trackingKey];
            uint8_t  rtr         = matchedRule.retriggerRule;

            std::string lowerTrackName = matchedRule.trackName;
            std::transform(lowerTrackName.begin(), lowerTrackName.end(), lowerTrackName.begin(), ::tolower);

            bool isGuitar = (lowerTrackName.find("gtr")    != std::string::npos ||
                             lowerTrackName.find("guitar") != std::string::npos ||
                             matchedRule.ntt == 4);
            bool isBass   = (lowerTrackName.find("bass") != std::string::npos ||
                             lowerTrackName.find("bs")   != std::string::npos ||
                             matchedRule.ntt == 3 ||
                             destChannel == 10); // MIDI Ch 11 = index 10 = bass

            RetrigEntry entry;
            entry.trackingKey  = trackingKey;
            entry.destChannel  = destChannel;
            entry.oldTransposed = oldTransposed;
            entry.velocity     = velocity;
            entry.shouldErase  = false;

            if (rtr == 0) {
                // RTR=0: Stop — just kill the note
                entry.newTransposed = oldTransposed;
                entry.shouldErase   = true;
            } else {
                // RTR=1 (legato) or RTR>1 (retrigger): pitch-shift
                int newTransposed = m_transpositionBrain.calculateTransposition(originalNote, m_activeChord, matchedRule);

                if (isGuitar) {
                    while (newTransposed < 40) newTransposed += 12;
                    while (newTransposed > 84) newTransposed -= 12;
                } else if (isBass) {
                    while (newTransposed < 28) newTransposed += 12;
                    while (newTransposed > 67) newTransposed -= 12;
                }

                if ((isGuitar && newTransposed < 40) || (isBass && newTransposed < 28)) {
                    entry.shouldErase   = true;
                    entry.newTransposed = oldTransposed;
                } else {
                    // Global velocity soft-limit: keep within 80-100 to prevent clipping
                    if (velocity > 100) velocity = 100;
                    if (velocity < 1)   velocity = 1;
                    entry.velocity     = velocity;
                    entry.newTransposed = newTransposed;
                }

                if (!matchedRule.trackName.empty()) {
                    std::string articulation;
                    m_megaVoiceTranslator.translate(matchedRule.trackName, entry.newTransposed, velocity, articulation);
                }
            }
            entries.push_back(entry);
        }

        // Phase 2: Kill ALL old notes first (prevents polyphony clipping burst)
        for (const auto& e : entries) {
            m_midiOut.sendNoteOff(e.destChannel, e.oldTransposed);
        }

        // Phase 3: Start new notes (only for non-erased entries)
        std::vector<uint32_t> keysToErase;
        std::vector<std::pair<uint32_t, int>> keysToUpdate;

        for (const auto& e : entries) {
            if (e.shouldErase) {
                keysToErase.push_back(e.trackingKey);
            } else {
                m_midiOut.sendNoteOn(e.destChannel, e.newTransposed, e.velocity);
                keysToUpdate.push_back({e.trackingKey, e.newTransposed});
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
    // NOTE: This must NOT be called while m_chordMutex is already held.
    // Callers from within a locked scope should use the internal version.
    std::lock_guard<std::mutex> lock(m_chordMutex);
    killAllActiveNotesLocked();
}

void Sequencer::killAllActiveNotesLocked() {
    for (const auto& pair : m_activeNoteMap) {
        uint32_t trackingKey   = pair.first;
        int      transformedNote = pair.second;
        uint8_t  srcChannel    = (trackingKey >> 16) & 0x0F;
        CasmRule rule = m_styleData->getCasmRuleForChannel(m_currentSection, srcChannel);
        // Bass tracks were sent to m_bassOutputChannel, so NoteOff must go there too
        uint8_t outCh = isBassRule(rule) ? m_bassOutputChannel : mapChannel(rule.destChannel);
        m_midiOut.sendNoteOff(outCh, transformedNote);
    }
    m_activeNoteMap.clear();
    m_activeVelocityMap.clear();
}

void Sequencer::setSection(const std::string& sectionName) {
    // --- FIX: Kill all active notes from the previous section FIRST ---
    // This prevents instrument clash when switching between Main A-D patterns,
    // ensuring no sustained notes from the old section bleed into the new one.
    {
        std::lock_guard<std::mutex> lock(m_chordMutex);
        killAllActiveNotesLocked();
    }

    m_currentSection = sectionName;
    const auto& events = m_parser.getMidiEvents();
    const auto& rules = m_parser.getCasmRules();
    
    m_sectionStartTick = 0;
    m_sectionEndTick = 0;
    m_eventIndex = 0;
    
    if (sectionName == "STOPPED") {
        std::cout << "[Sequencer] Sequencer Stopped." << std::endl;
        return;
    }
    
    // Find the section markers
    bool foundStart = false;
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i].status == 0xFF && (events[i].data1 == 0x06 || events[i].data1 == 0x01)) {
            std::string metaLower = events[i].metaText;
            std::string sectionLower = sectionName;
            std::transform(metaLower.begin(), metaLower.end(), metaLower.begin(), ::tolower);
            std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);
            if (!foundStart && metaLower == sectionLower) {
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
    // Also build a diagnostic routing table so the user can see every track's output channel.
    std::cout << "\n[Sequencer] === TRACK ROUTING for [" << sectionName << "] ==="
              << "\n  Track Name      | CASM src | CASM dest | Output Ch (after remap)" << std::endl;
    std::cout << "  ----------------+----------+-----------+------------------------" << std::endl;

    std::string sectionLower = sectionName;
    std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);

    for (const auto& ev : events) {
        bool isSInt = (ev.absoluteTick < 100);
        bool isInSection = (ev.absoluteTick >= m_sectionStartTick && ev.absoluteTick < m_sectionEndTick);
        
        if (isSInt || isInSection) {
            uint8_t type = ev.status & 0xF0;
            uint8_t channel = ev.status & 0x0F;
            
            if (type == 0xC0 || type == 0xB0) {
                // Case-insensitive CASM rule lookup (mirrors getCasmRuleForChannel)
                uint8_t casmDest  = channel;
                bool ruleMatched  = false;
                std::string trackName = "";
                
                for (const auto& rule : rules) {
                    if (rule.sourceChannel != channel) continue;
                    if (rule.trackName.find("CC") != std::string::npos) continue;

                    std::string rulesLower = rule.appliedSections;
                    std::transform(rulesLower.begin(), rulesLower.end(), rulesLower.begin(), ::tolower);
                    if (rulesLower.find(sectionLower) != std::string::npos) {
                        casmDest  = rule.destChannel;
                        trackName = rule.trackName;
                        ruleMatched = true;
                        break;
                    }
                }
                
                if (ruleMatched) {
                    // Build a temporary CasmRule for isBassRule detection
                    CasmRule tempRule;
                    tempRule.trackName = trackName;
                    // Find ntt from the full rules vector
                    for (const auto& r : rules) {
                        if (r.sourceChannel == channel && r.trackName == trackName) {
                            tempRule.ntt = r.ntt;
                            break;
                        }
                    }
                    // Bass tracks always go to m_bassOutputChannel; others use channel map
                    uint8_t outCh = isBassRule(tempRule) ? m_bassOutputChannel
                                                         : mapChannel(casmDest);

                    if (type == 0xC0) {
                        uint8_t program = ev.data1;
                        uint8_t bankMSB = channelMSB[channel];
                        uint8_t bankLSB = channelLSB[channel];
                        
                        m_megaVoiceTranslator.translatePatch(trackName, bankMSB, bankLSB, program);
                        
                        m_midiOut.sendControlChange(outCh, 0, bankMSB);
                        m_midiOut.sendControlChange(outCh, 32, bankLSB);
                        m_cachedMSB[outCh] = bankMSB;
                        m_cachedLSB[outCh] = bankLSB;
                        m_midiOut.sendProgramChange(outCh, program);
                        
                        // Print routing row (highlights bass in bold via marker)
                        std::string bassTag = isBassRule(tempRule) ? " <<< BASS" : "";
                        std::cout << "  " << trackName;
                        for (int p = trackName.size(); p < 16; ++p) std::cout << ' ';
                        std::cout << "| src ch" << (int)channel+1
                                  << "  | casm ch" << (int)casmDest+1
                                  << "  | >>> MIDI Ch " << (int)outCh+1
                                  << "  (Bank " << (int)bankMSB << ":" << (int)bankLSB
                                  << ", PC " << (int)program << ")" << bassTag << std::endl;
                    }
                    else if (type == 0xB0 && ev.data1 != 0 && ev.data1 != 32) {
                        m_midiOut.sendControlChange(outCh, ev.data1, ev.data2);
                    }
                }
            }
        }
    }
    std::cout << "[Sequencer] =========================================" << std::endl;
}

void Sequencer::tick(uint32_t currentTick) {
    if (!isPlaying()) return;

    // Advance relative playhead by 1 clock pulse (since tick is called per master pulse)
    m_relativeTick++; 
    
    if (m_relativeTick >= m_sectionEndTick) {
        {
            std::lock_guard<std::mutex> lock(m_chordMutex);
            killAllActiveNotesLocked();
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
            m_midiOut.sendPitchBend(mapChannel(channelRule.destChannel), ev.data1, ev.data2);
            m_eventIndex++;
            continue;
        }
        else if (type == 0xB0) {
            uint8_t outCh = mapChannel(channelRule.destChannel);
            m_midiOut.sendControlChange(outCh, ev.data1, ev.data2);
            if (ev.data1 == 0) {
                m_cachedMSB[outCh] = ev.data2;
            } else if (ev.data1 == 32) {
                m_cachedLSB[outCh] = ev.data2;
            }
            m_eventIndex++;
            continue;
        }
        else if (type == 0xC0) {
            uint8_t outCh   = mapChannel(channelRule.destChannel);
            uint8_t program = ev.data1;
            uint8_t bankMSB = m_cachedMSB[outCh];
            uint8_t bankLSB = m_cachedLSB[outCh];
            m_megaVoiceTranslator.translatePatch(channelRule.trackName, bankMSB, bankLSB, program);
            
            m_cachedMSB[outCh] = bankMSB;
            m_cachedLSB[outCh] = bankLSB;
            
            m_midiOut.sendControlChange(outCh, 0, bankMSB);
            m_midiOut.sendControlChange(outCh, 32, bankLSB);
            m_midiOut.sendProgramChange(outCh, program);
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
            
            // --- FIX: Use m_activeChord (updated via chord callback) instead of polling
            // detectChord() directly. This ensures chords played in the left-hand zone
            // (C0 to split point) are always recognized and drive the bass + all patterns.
            Chord activeChordCopy;
            {
                std::lock_guard<std::mutex> lock(m_chordMutex);
                activeChordCopy = m_activeChord;
            }
            
            bool isDrumTrack = (channelRule.destChannel == 9 || channelRule.destChannel == 8 || 
                                channelRule.trackName.find("Rhy") != std::string::npos || 
                                channelRule.trackName.find("dr") != std::string::npos);

            // Mute non-drum notes if no chord is held
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
                           lowerTrackName.find("bs")   != std::string::npos ||
                           channelRule.ntt == 3 ||
                           channelRule.destChannel == 10); // MIDI Ch 11 = index 10 = bass

            // Global velocity soft-limit: prevent clipping from many simultaneous voices
            // High-velocity articulation triggers (>= 115) are exempt from limiting
            if (velocity < 115 && velocity > 100) {
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

            // Route bass tracks to m_bassOutputChannel regardless of CASM destChannel
            uint8_t outCh = isBassRule(channelRule) ? m_bassOutputChannel
                                                    : mapChannel(channelRule.destChannel);

            std::cout << "[Tick] NoteOn: Track='" << channelRule.trackName
                      << "' srcCh=" << (int)channel+1
                      << " casmDest=" << (int)channelRule.destChannel+1
                      << " outCh=" << (int)outCh+1
                      << " note=" << (int)transformedNote
                      << " vel=" << (int)velocity << std::endl;

            m_midiOut.sendNoteOn(outCh, transformedNote, velocity);
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
            
            uint8_t outCh = isBassRule(channelRule) ? m_bassOutputChannel
                                                    : mapChannel(channelRule.destChannel);
            if (noteToTurnOff != -1) {
                m_midiOut.sendNoteOff(outCh, noteToTurnOff);
            } else {
                m_midiOut.sendNoteOff(outCh, originalNote);
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
