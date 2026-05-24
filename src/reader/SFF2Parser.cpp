#include "SFF2Parser.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

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
        ev.absoluteTick = absoluteTick;
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
                std::cout << "     [MARKER] Section: " << ev.metaText << " (Tick " << absoluteTick << ")" << std::endl;
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
        } else if (subId == "Ctab") {
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
    if (length >= strLen + 2) {
        m_sourceRoot = static_cast<uint8_t>(data[length - 2]);
        m_sourceChordType = static_cast<uint8_t>(data[length - 1]);
    }

    std::cout << "          -> Section Context: " << m_currentSections 
              << " | Source Root: " << (int)m_sourceRoot 
              << " | Source Chord Type: " << (int)m_sourceChordType << std::endl;
}

void SFF2Parser::parseCtab(const char* data, uint32_t length) {
    if (length < 27) return; // Ignore incomplete rules

    CasmRule rule;
    rule.appliedSections = m_currentSections;
    // FIX: data[0] is Source Channel, data[9] is Dest Channel
    rule.sourceChannel = static_cast<uint8_t>(data[0]);
    
    // Extract the 8-character track name
    rule.trackName = std::string(data + 1, 8);
    // Trim trailing spaces from the track name safely
    size_t lastChar = rule.trackName.find_last_not_of(" ");
    if (lastChar != std::string::npos) {
        rule.trackName.erase(lastChar + 1);
    } else {
        rule.trackName.clear(); // Track name was entirely spaces
    }

    rule.destChannel = static_cast<uint8_t>(data[9]);
    rule.playNote = static_cast<uint8_t>(data[10]);
    rule.playChord = static_cast<uint8_t>(data[11]);
    rule.highKey = static_cast<uint8_t>(data[12]);
    rule.noteLimitLow = static_cast<uint8_t>(data[13]);
    rule.noteLimitHigh = static_cast<uint8_t>(data[14]);
    rule.retriggerRule = static_cast<uint8_t>(data[15]);
    rule.ntr = static_cast<uint8_t>(data[16]);
    rule.ntt = static_cast<uint8_t>(data[17]);
    rule.sourceRoot = m_sourceRoot;
    rule.sourceChordType = m_sourceChordType;

    // Store the rest of the raw bytes
    for (uint32_t i = 18; i < length; i++) {
        rule.rawRuleBytes.push_back(static_cast<uint8_t>(data[i]));
    }

    // Save the rule to the engine's memory
    m_casmRules.push_back(rule);

    std::cout << "             [Stored Rule] " << rule.appliedSections 
              << " | Track: " << rule.trackName 
              << " (Ch " << (int)rule.destChannel + 1 << ")"
              << " | Src: " << (int)rule.sourceChannel + 1
              << " | HighKey: " << std::hex << (int)rule.highKey << std::dec
              << " | NTR: " << (int)rule.ntr << " | NTT: " << (int)rule.ntt << std::endl;
}

} // namespace engine
