#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace engine {

struct CasmRule {
    std::string appliedSections;
    std::string trackName;
    uint8_t destChannel;
    uint8_t sourceChannel;
    uint8_t playNote;
    uint8_t playChord;
    uint8_t highKey;
    uint8_t noteLimitLow;
    uint8_t noteLimitHigh;
    uint8_t retriggerRule;
    uint8_t ntr;
    uint8_t ntt;
    uint8_t sourceRoot = 0;       // Default C (0)
    uint8_t sourceChordType = 2;  // Default Maj7 (2)
    std::vector<uint8_t> rawRuleBytes;
};

// Represents a single musical event (Note, Marker, Program Change)
struct MidiEvent {
    uint32_t absoluteTick;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    std::string metaText;
};

class SFF2Parser {
public:
    SFF2Parser();
    ~SFF2Parser();

    // Loads and parses a .sty or .prs file
    bool loadFile(const std::string& filepath);
    
    const std::vector<MidiEvent>& getMidiEvents() const { return m_midiEvents; }
    const std::vector<CasmRule>& getCasmRules() const { return m_casmRules; }
    CasmRule getCasmRuleForChannel(const std::string& section, uint8_t channel) const;
    
private:
    std::string m_currentSections;
    uint8_t m_sourceRoot;
    uint8_t m_sourceChordType;
    uint16_t m_ppqn = 480; // Native PPQN from MThd
    std::vector<CasmRule> m_casmRules;
    std::vector<MidiEvent> m_midiEvents; // Stores the entire sequence of notes and section markers

    uint32_t readBigEndian32(const char* buffer);
    uint32_t readVariableLength(const char*& data, const char* end);
    
    void parseMThd(const char* data, uint32_t length);
    void parseMTrk(const char* data, uint32_t length);
    void parseCASM(const char* data, uint32_t length);
    
    // CASM Sub-chunk parsers
    void parseCSEG(const char* data, uint32_t length);
    void parseSdec(const char* data, uint32_t length);
    void parseCtab(const char* data, uint32_t length);
};

} // namespace engine
