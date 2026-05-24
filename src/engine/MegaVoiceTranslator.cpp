#include "MegaVoiceTranslator.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>

namespace engine {

MegaVoiceTranslator::MegaVoiceTranslator() {
    // Standard MegaVoice Guitar rules (e.g. steel guitar, nylon guitar, clean guitar)
    // MegaVoice high velocities (>= 120) typically trigger slides, slaps, open/mute, or strum sounds
    ArticulationRule slideUp = {120, 24, 100, true, "Fret Slide Up"};
    ArticulationRule fretNoise = {115, 25, 90, true, "Fret Noise"};
    ArticulationRule harmonic = {110, 26, 95, true, "Harmonics"};
    ArticulationRule choke = {105, 27, 85, true, "Choke"};
    
    // We register rules for common track names in lowercase
    std::vector<std::string> gtrTracks = {"guitar", "guitar1", "guitar2", "gtr", "gtr1", "gtr2", "lguitar", "rguitar"};
    for (const auto& track : gtrTracks) {
        addRule(track, slideUp);
        addRule(track, fretNoise);
        addRule(track, harmonic);
        addRule(track, choke);
    }
    
    // Bass rules (Acoustic, Fingered, Picked)
    ArticulationRule bassSlide = {120, 12, 100, true, "Bass Slide"};
    ArticulationRule bassSlap = {110, 13, 90, true, "Bass Slap"};
    std::vector<std::string> bassTracks = {"bass", "bass1", "bass2", "bs", "bs1", "bs2", "abass", "fbass", "pbass"};
    for (const auto& track : bassTracks) {
        addRule(track, bassSlide);
        addRule(track, bassSlap);
    }
}

MegaVoiceTranslator::~MegaVoiceTranslator() {}

void MegaVoiceTranslator::addRule(const std::string& trackName, ArticulationRule rule) {
    std::string lowerTrack = trackName;
    for (auto& c : lowerTrack) c = std::tolower(c);
    m_rules[lowerTrack].push_back(rule);
}

bool MegaVoiceTranslator::translate(const std::string& trackName, int& note, int& velocity, std::string& outArticulation) {
    std::string lowerTrack = trackName;
    for (auto& c : lowerTrack) c = std::tolower(c);
    
    bool isGuitar = (lowerTrack.find("gtr") != std::string::npos || 
                     lowerTrack.find("gt") != std::string::npos || 
                     lowerTrack.find("guitar") != std::string::npos);
    bool isBass = (lowerTrack.find("bass") != std::string::npos || 
                   lowerTrack.find("bs") != std::string::npos || 
                   lowerTrack.find("ba") != std::string::npos);

    // High-Velocity FX Translator (Ample Sound Translation for Guitar tracks)
    if (isGuitar && velocity >= 115) {
        int fxNote = 77;
        std::string articulation = "Scratch";
        
        // Check track context (keywords in trackName)
        if (lowerTrack.find("mute") != std::string::npos || lowerTrack.find("choke") != std::string::npos) {
            // Mute context: Muting Noise, Hit Top (Mute), Scratch
            int r = std::rand() % 3;
            if (r == 0) {
                fxNote = 79; // Muting Noise (G5)
                articulation = "Muting Noise";
            } else if (r == 1) {
                fxNote = 90; // Hit Top (Mute) (F#6)
                articulation = "Hit Top (Mute)";
            } else {
                fxNote = 77; // Scratch (F5)
                articulation = "Scratch";
            }
        } else {
            // Open/Standard context: Scratch, Muting Noise, Hit Top (Open), Hit Top (Mute)
            int r = std::rand() % 4;
            if (r == 0) {
                fxNote = 77; // Scratch (F5)
                articulation = "Scratch";
            } else if (r == 1) {
                fxNote = 79; // Muting Noise (G5)
                articulation = "Muting Noise";
            } else if (r == 2) {
                fxNote = 89; // Hit Top (Open) (F6)
                articulation = "Hit Top (Open)";
            } else {
                fxNote = 90; // Hit Top (Mute) (F#6)
                articulation = "Hit Top (Mute)";
            }
        }
        
        note = fxNote;
        velocity = 100; // Standard keyswitch velocity
        outArticulation = "Ample " + articulation;
        return true;
    }

    // Velocity Clamping for all normal notes (velocity < 115) on Guitar/Bass tracks
    if ((isGuitar || isBass) && velocity < 115) {
        if (velocity > 100) {
            velocity = 100;
        }
    }

    // Fallback to Yamaha proprietary MegaVoice translations
    auto it = m_rules.find(lowerTrack);
    if (it == m_rules.end()) {
        return false; // No MegaVoice rules exist for this track
    }

    // Check the rules. (Assume rules are sorted by highest velocity first)
    for (const auto& rule : it->second) {
        if (velocity >= rule.velocityThreshold) {
            outArticulation = rule.articulationName;
            
            if (rule.muteOriginalNote) {
                // Completely swap the high-pitched Yamaha note for the VST Keyswitch
                note = rule.keyswitchNote;
                velocity = rule.keyswitchVelocity;
            }
            
            return true; // We successfully translated the MegaVoice!
        }
    }

    return false;
}

bool MegaVoiceTranslator::translatePatch(const std::string& trackName, uint8_t& bankMSB, uint8_t& bankLSB, uint8_t& program) {
    std::string lowerTrack = trackName;
    for (auto& c : lowerTrack) c = std::tolower(c);
    
    // Preserve banks for drums (which often require MSB 127 or 120)
    if (lowerTrack.find("dr") != std::string::npos || lowerTrack.find("drum") != std::string::npos) {
        return true; 
    }

    // Default bank select to 0 for melodic tracks to fallback to General MIDI
    bankMSB = 0;
    bankLSB = 0;
    
    if (lowerTrack.find("gtr") != std::string::npos || lowerTrack.find("gt") != std::string::npos || lowerTrack.find("guitar") != std::string::npos) {
        // It's a guitar track! Map program to the GM acoustic steel guitar (PC 25) or keep it in the guitar range (24-31)
        if (program < 24 || program > 31) {
            program = 25; // Acoustic Steel Guitar
        }
        return true;
    }
    
    if (lowerTrack.find("bass") != std::string::npos || lowerTrack.find("bs") != std::string::npos || lowerTrack.find("ba") != std::string::npos) {
        // It's a bass track! Map program to GM fingered bass (PC 33) or keep it in the bass range (32-39)
        if (program < 32 || program > 39) {
            program = 33; // Electric Fingered Bass
        }
        return true;
    }

    return false;
}

} // namespace engine
