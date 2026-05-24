#include "SFF2Parser.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace engine {

SFF2Parser::SFF2Parser() : m_sourceRoot(0), m_sourceChordType(2) {}
SFF2Parser::~SFF2Parser() {}

uint32_t SFF2Parser::readBigEndian32(const char* buffer) {
    return ((static_cast<uint8_t>(buffer[0]) << 24) |
            (static_cast<uint8_t>(buffer[1]) << 16) |
            (static_cast<uint8_t>(buffer[2]) << 8)  |
             static_cast<uint8_t>(buffer[3]));
}

uint32_t SFF2Parser::readVariableLength(const char*& data, const char* end) {
    uint32_t value = 0;
    uint8_t byte;
    do {
        if (data >= end) break;
        byte = static_cast<uint8_t>(*data++);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

bool SFF2Parser::loadFile(const std::string& filepath) {
    std::cout << "\n--- Opening Style File: " << filepath << " ---" << std::endl;
    
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filepath << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        std::cerr << "Error: File is empty or could not read size." << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        std::cerr << "Error: Failed to read file data." << std::endl;
        return false;
    }

    std::cout << "File size: " << size << " bytes." << std::endl;

    // Scan through the file to find chunks (MThd, MTrk, CASM, etc.)
    size_t offset = 0;
    while (offset + 8 <= buffer.size()) {
        std::string chunkId(buffer.data() + offset, 4);
        uint32_t chunkLength = readBigEndian32(buffer.data() + offset + 4);

        std::cout << "Found Chunk: [" << chunkId << "] Length: " << chunkLength << " bytes." << std::endl;

        if (offset + 8 + chunkLength > buffer.size()) {
            std::cerr << "Warning: Chunk " << chunkId << " extends past end of file. File might be corrupted." << std::endl;
            break;
        }

        const char* chunkData = buffer.data() + offset + 8;

        if (chunkId == "MThd") {
            parseMThd(chunkData, chunkLength);
        } else if (chunkId == "MTrk") {
            parseMTrk(chunkData, chunkLength);
        } else if (chunkId == "CASM") {
            parseCASM(chunkData, chunkLength);
        }

        // Move to the next chunk
        offset += 8 + chunkLength;
    }

    std::cout << "--- Parsing Complete ---\n" << std::endl;
    return true;
}

void SFF2Parser::parseMThd(const char* data, uint32_t length) {
    if (length < 6) return;
    
    uint16_t format = (static_cast<uint8_t>(data[0]) << 8) | static_cast<uint8_t>(data[1]);
    uint16_t tracks = (static_cast<uint8_t>(data[2]) << 8) | static_cast<uint8_t>(data[3]);
    uint16_t division = (static_cast<uint8_t>(data[4]) << 8) | static_cast<uint8_t>(data[5]);
    
    m_ppqn = division; // Store native PPQN
    std::cout << "  -> MIDI Header: Format " << format << ", Tracks: " << tracks << ", PPQN: " << division << std::endl;
}

void SFF2Parser::parseMTrk(const char* data, uint32_t length) {
    std::cout << "  -> Decoding MTrk (MIDI Track) Events (" << length << " bytes)..." << std::endl;
    const char* ptr = data;
    const char* end = data + length;
    uint32_t absoluteTick = 0;
    uint8_t runningStatus = 0;

    int noteCount = 0;
    int markerCount = 0;

    double scaleFactor = 1920.0 / m_ppqn;

    while (ptr < end) {
        uint32_t delta = readVariableLength(ptr, end);
        absoluteTick += delta;

        if (ptr >= end) break;
        uint8_t status = static_cast<uint8_t>(*ptr);

        if (status >= 0x80) {
            runningStatus = status;
            ptr++;
        } else {
            status = runningStatus;
        }

        MidiEvent ev;
        ev.absoluteTick = static_cast<uint32_t>(absoluteTick * scaleFactor);
        ev.status = status;
        ev.data1 = 0;
        ev.data2 = 0;

        uint8_t type = status & 0xF0;

        if (status == 0xFF) {
            // Meta Event
            if (ptr >= end) break;
            ev.data1 = static_cast<uint8_t>(*ptr++); // Meta Type
            uint32_t len = readVariableLength(ptr, end);
            
            if (ev.data1 == 0x06 || ev.data1 == 0x01) { // Marker or Text
                ev.metaText = std::string(ptr, len);
                std::cout << "     [MARKER] Section: " << ev.metaText << " (Native Tick " << absoluteTick << " -> Scaled Tick " << ev.absoluteTick << ")" << std::endl;
                markerCount++;
            }
            ptr += len;
        } else if (status == 0xF0 || status == 0xF7) {
            // SysEx
            uint32_t len = readVariableLength(ptr, end);
            ptr += len;
        } else if (type == 0xC0) {
            // Program Change (0xC0 - 0xCF)
            if (ptr >= end) break;
            ev.data1 = static_cast<uint8_t>(*ptr++);
        } else if (type == 0xD0) {
            // Channel Aftertouch (0xD0 - 0xDF)
            if (ptr >= end) break;
            ev.data1 = static_cast<uint8_t>(*ptr++);
        } else if (type == 0xB0) {
            // Control Change (0xB0 - 0xBF)
            if (ptr + 1 >= end) break;
            ev.data1 = static_cast<uint8_t>(*ptr++);
            ev.data2 = static_cast<uint8_t>(*ptr++);
        } else if (type >= 0x80 && type <= 0xE0) {
            // Other Voice Messages (Note On, Note Off, Poly Aftertouch, Pitch Bend)
            if (ptr + 1 >= end) break;
            ev.data1 = static_cast<uint8_t>(*ptr++);
            ev.data2 = static_cast<uint8_t>(*ptr++);
            if (type == 0x90 && ev.data2 > 0) noteCount++; // Note On with velocity > 0
        }
        
        m_midiEvents.push_back(ev);
    }
    
    std::cout << "     >>> Extracted " << noteCount << " Musical Notes and " << markerCount << " Markers! <<<" << std::endl;
}

void SFF2Parser::parseCASM(const char* data, uint32_t length) {
    std::cout << "  -> Analyzing CASM Internal Structure (" << length << " bytes)..." << std::endl;
    
    size_t offset = 0;
    while (offset + 8 <= length) {
        std::string subId(data + offset, 4);
        uint32_t subLen = readBigEndian32(data + offset + 4);
        
        std::cout << "     CASM Sub-chunk: [" << subId << "] Length: " << subLen << " bytes." << std::endl;
        
        if (offset + 8 + subLen > length) {
            std::cerr << "     Warning: CASM Sub-chunk extends past CASM boundaries." << std::endl;
            break;
        }

        const char* subData = data + offset + 8;
        
        if (subId == "CSEG") {
            parseCSEG(subData, subLen);
        } else if (subId == "Sdec") {
            parseSdec(subData, subLen);
        }

        // Move to the next sub-chunk
        offset += 8 + subLen;
    }
}

void SFF2Parser::parseCSEG(const char* data, uint32_t length) {
    std::cout << "       -> Decoding CSEG (" << length << " bytes)..." << std::endl;
    
    size_t offset = 0;
    while (offset + 8 <= length) {
        std::string subId(data + offset, 4);
        uint32_t subLen = readBigEndian32(data + offset + 4);
        
        if (offset + 8 + subLen > length) {
            std::cerr << "          Warning: CSEG Sub-chunk " << subId << " extends past boundaries." << std::endl;
            break;
        }

        const char* subData = data + offset + 8;
        
        if (subId == "Sdec") {
            parseSdec(subData, subLen);
        } else if (subId == "Ctab" || subId == "Ctb2") {
            parseCtab(subData, subLen);
        } else {
            std::cout << "          Unknown CSEG Sub-chunk: [" << subId << "] Length: " << subLen << std::endl;
        }

        offset += 8 + subLen;
    }
}

void SFF2Parser::parseSdec(const char* data, uint32_t length) {
    // Sdec in SFF2 can append the source root and source chord type at the end of the chunk.
    m_sourceRoot = 0;       // Default C
    m_sourceChordType = 2;  // Default Maj7 (SFF standard default is CMaj7)

    // Find the end of the ASCII comma-separated section list (ends at a null byte or non-printable character)
    size_t strLen = 0;
    while (strLen < length && data[strLen] != '\0' && static_cast<uint8_t>(data[strLen]) >= 32) {
        strLen++;
    }
    m_currentSections = std::string(data, strLen);

    // If there is extra binary data, extract the source root and source chord type
    bool valid = false;
    if (length >= strLen + 2) {
        uint8_t root = static_cast<uint8_t>(data[length - 2]);
        uint8_t chordType = static_cast<uint8_t>(data[length - 1]);
        
        // Strict bounds checking: root must be between 0 and 11 inclusive,
        // and chordType should be within valid Yamaha SFF chord type range (typically <= 36)
        if (root <= 11 && chordType <= 36) {
            m_sourceRoot = root;
            m_sourceChordType = chordType;
            valid = true;
        }
    }

    if (!valid) {
        // Fallback safely to Yamaha standard default of 0 (C Major / Maj7)
        m_sourceRoot = 0;
        m_sourceChordType = 2;
    }

    std::cout << "          -> Section Context: " << m_currentSections 
              << " | Source Root: " << (int)m_sourceRoot 
              << " | Source Chord Type: " << (int)m_sourceChordType << std::endl;
}

void SFF2Parser::parseCtab(const char* data, uint32_t length) {
    if (length < 18) return; // Parameter block is 18 bytes

    CasmRule rule;
    rule.appliedSections = m_currentSections;
    rule.sourceChannel = static_cast<uint8_t>(data[0]);
    
    // Extract 8-character track name (bytes 1 to 8)
    rule.trackName = std::string(data + 1, 8);
    size_t lastChar = rule.trackName.find_last_not_of(" ");
    if (lastChar != std::string::npos) {
        rule.trackName.erase(lastChar + 1);
    } else {
        rule.trackName.clear();
    }

    rule.destChannel = static_cast<uint8_t>(data[9]);

    // Yamaha standard default settings for CASM rules
    rule.playNote = 1;  // Transpose notes by default
    rule.playChord = 1; // Adapt notes to chord type by default
    rule.highKey = 0xFF;
    rule.noteLimitLow = 0;
    rule.noteLimitHigh = 127;
    rule.retriggerRule = 0;
    rule.ntr = 0;
    rule.ntt = 0;
    rule.sourceRoot = m_sourceRoot;
    rule.sourceChordType = m_sourceChordType;

    // Correctly map parameter bytes according to exact reverse-engineered offsets
    if (length >= 25) {
        rule.highKey = static_cast<uint8_t>(data[17]);
        rule.sourceRoot = static_cast<uint8_t>(data[18]);
        rule.sourceChordType = static_cast<uint8_t>(data[19]);
        rule.ntr = static_cast<uint8_t>(data[20]);
        rule.ntt = static_cast<uint8_t>(data[21]);
        rule.retriggerRule = static_cast<uint8_t>(data[22]);
        rule.noteLimitLow = static_cast<uint8_t>(data[23]);
        rule.noteLimitHigh = static_cast<uint8_t>(data[24]);
    }

    // Store the rest of the raw bytes
    for (uint32_t i = 18; i < length; i++) {
        rule.rawRuleBytes.push_back(static_cast<uint8_t>(data[i]));
    }

    m_casmRules.push_back(rule);
    
    std::cout << "             [Stored CASM Rule] " << rule.appliedSections 
              << " | Track: " << rule.trackName 
              << " (Ch " << (int)rule.destChannel + 1 << ")"
              << " | Src: " << (int)rule.sourceChannel + 1
              << " | HighKey: 0x" << std::hex << (int)rule.highKey << std::dec
              << " | NTR: " << (int)rule.ntr << " | NTT: " << (int)rule.ntt << std::endl;
}

CasmRule SFF2Parser::getCasmRuleForChannel(const std::string& section, uint8_t channel) const {
    // Build lowercase version of the requested section name once
    std::string sectionLower = section;
    std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(), ::tolower);

    for (const auto& rule : m_casmRules) {
        if (rule.sourceChannel != channel) continue;
        if (rule.trackName.find("CC") != std::string::npos) continue;

        // Case-insensitive section match so "Main A" == "main a" etc.
        std::string rulesLower = rule.appliedSections;
        std::transform(rulesLower.begin(), rulesLower.end(), rulesLower.begin(), ::tolower);

        if (rulesLower.find(sectionLower) != std::string::npos) {
            return rule;
        }
    }

    // Return a safe default rule — pass the note straight through unchanged
    CasmRule defaultRule;
    defaultRule.sourceChannel  = channel;
    defaultRule.destChannel    = channel;
    defaultRule.playNote       = 1;
    defaultRule.playChord      = 0;   // No chord adaptation on unknown tracks
    defaultRule.highKey        = 0xFF;
    defaultRule.noteLimitLow   = 0;   // No folding limits — pass everything
    defaultRule.noteLimitHigh  = 127;
    defaultRule.retriggerRule  = 1;   // Legato retrigger (least disruptive default)
    defaultRule.ntr            = 0;
    defaultRule.ntt            = 0;
    defaultRule.sourceRoot     = 0;
    defaultRule.sourceChordType = 2;
    return defaultRule;
}

} // namespace engine
