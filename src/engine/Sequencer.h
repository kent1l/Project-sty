#pragma once
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "../reader/SFF2Parser.h"
#include "../listener/MidiOutput.h"
#include "TranspositionBrain.h"
#include "MegaVoiceTranslator.h"
#include "ChordRecognizer.h"

namespace engine {

class Sequencer {
public:
    Sequencer(SFF2Parser& parser, MidiOutput& midiOut, ChordRecognizer& chordRecognizer);
    ~Sequencer();

    void setSection(const std::string& sectionName);
    void tick(uint32_t absoluteTick);
    void clearNoteMemory();

    void updateLiveChord(const Chord& newChord);
    bool isPlaying() const;

    // Channel Override Map: remap any CASM destChannel to a different output channel.
    void setChannelMap(uint8_t fromChannel, uint8_t toChannel) {
        if (fromChannel < 16 && toChannel < 16)
            m_channelMap[fromChannel] = toChannel;
    }
    uint8_t mapChannel(uint8_t ch) const {
        return (ch < 16) ? m_channelMap[ch] : ch;
    }

    // Bass Output Channel: ALL tracks identified as bass (by NTT==3 or track name)
    // are force-routed to this channel, overriding CASM destChannel entirely.
    // 0-based: MIDI ch 11 = index 10.  Default = 10.
    void setBassOutputChannel(uint8_t ch) { m_bassOutputChannel = ch; }
    uint8_t getBassOutputChannel() const  { return m_bassOutputChannel; }

    // Helper: returns true if a CASM rule identifies a bass track
    static bool isBassRule(const CasmRule& rule) {
        std::string lower = rule.trackName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return (lower.find("bass") != std::string::npos ||
                lower.find("bs")   != std::string::npos ||
                rule.ntt == 3);
    }

private:
    SFF2Parser& m_parser;
    MidiOutput& m_midiOut;
    ChordRecognizer& m_chordRecognizer;
    TranspositionBrain m_transpositionBrain;
    MegaVoiceTranslator m_megaVoiceTranslator;

    std::string m_currentSection;
    uint32_t m_sectionStartTick;
    uint32_t m_sectionEndTick;
    uint32_t m_relativeTick;

    // Index in m_midiEvents array to avoid O(N) scanning every tick
    size_t m_eventIndex;

    // Chord Memory (Latch mode)
    Chord m_lastValidChord;

    // 2D Array to track exact transposed note pitches [channel][original_note]
    int m_playingNotes[16][128];
    int m_playingVelocities[16][128];

    // Cache for Bank Select MSB/LSB per destination channel
    uint8_t m_cachedMSB[16];
    uint8_t m_cachedLSB[16];

    // Channel override map: m_channelMap[casmDest] = actualOutputChannel
    uint8_t m_channelMap[16];

    // Dedicated bass output channel (0-based). All bass tracks are routed here.
    uint8_t m_bassOutputChannel;

    // Thread Safety & State Cache
    std::mutex m_chordMutex;
    Chord m_activeChord;
    std::unordered_map<uint32_t, int> m_activeNoteMap;
    std::unordered_map<uint32_t, int> m_activeVelocityMap;
    SFF2Parser* m_styleData;

    void killAllActiveNotes();
    void killAllActiveNotesLocked(); // Internal: assumes m_chordMutex is already held
};

} // namespace engine
