#include "StyleController.h"

namespace engine {

StyleController::StyleController() : 
    m_currentSection(StyleSection::STOPPED),
    m_queuedSection(StyleSection::STOPPED),
    m_targetAfterFill(StyleSection::STOPPED) {}

StyleController::~StyleController() {}

void StyleController::buttonIntro(char variation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (variation == 'A') m_queuedSection = StyleSection::INTRO_A;
    else if (variation == 'B') m_queuedSection = StyleSection::INTRO_B;
    else if (variation == 'C') m_queuedSection = StyleSection::INTRO_C;
    
    // Default to going to Main A after an Intro finishes (unless specified otherwise)
    m_targetAfterFill = StyleSection::MAIN_A;
    std::cout << "[UI] Pressed Intro " << variation << " (Queued)" << std::endl;
}

void StyleController::buttonMain(char variation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    StyleSection target = StyleSection::MAIN_A;
    StyleSection fill = StyleSection::FILL_IN_A;
    
    if (variation == 'A') { target = StyleSection::MAIN_A; fill = StyleSection::FILL_IN_A; }
    else if (variation == 'B') { target = StyleSection::MAIN_B; fill = StyleSection::FILL_IN_B; }
    else if (variation == 'C') { target = StyleSection::MAIN_C; fill = StyleSection::FILL_IN_C; }
    else if (variation == 'D') { target = StyleSection::MAIN_D; fill = StyleSection::FILL_IN_D; }

    if (m_currentSection == StyleSection::STOPPED || m_currentSection == StyleSection::INTRO_A || m_currentSection == StyleSection::INTRO_B || m_currentSection == StyleSection::INTRO_C) {
        // If stopped or in an intro, just queue the target Main for when it finishes
        m_queuedSection = target;
    } else {
        // If already playing a Main, queue the Fill-In immediately, and set the target for after the fill!
        m_queuedSection = fill;
        m_targetAfterFill = target;
    }
    std::cout << "[UI] Pressed Main " << variation << " (Queued Fill-In -> Main " << variation << ")" << std::endl;
}

void StyleController::buttonBreak() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queuedSection = StyleSection::BREAK;
    // Return to the current main section after the break
    m_targetAfterFill = (m_targetAfterFill != StyleSection::STOPPED) ? m_targetAfterFill : m_currentSection; 
    std::cout << "[UI] Pressed Break" << std::endl;
}

void StyleController::buttonEnding(char variation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (variation == 'A') m_queuedSection = StyleSection::ENDING_A;
    else if (variation == 'B') m_queuedSection = StyleSection::ENDING_B;
    else if (variation == 'C') m_queuedSection = StyleSection::ENDING_C;
    m_targetAfterFill = StyleSection::STOPPED; // Stop the style after ending
    std::cout << "[UI] Pressed Ending " << variation << " (Queued)" << std::endl;
}

void StyleController::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentSection = StyleSection::STOPPED;
    m_queuedSection = StyleSection::STOPPED;
    m_targetAfterFill = StyleSection::STOPPED;
    std::cout << "[UI] Style Controller Stopped." << std::endl;
}

bool StyleController::processMeasureBoundary() {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool changed = false;

    // 1. If we have a queued section (like a Fill-In or Ending that was just pressed), transition to it
    if (m_queuedSection != StyleSection::STOPPED && m_queuedSection != m_currentSection) {
        m_currentSection = m_queuedSection;
        m_queuedSection = StyleSection::STOPPED;
        changed = true;
    }
    // 2. If we just finished a Fill-In, Intro, or Break, automatically drop into the Target Main section
    else if (m_currentSection == StyleSection::FILL_IN_A || 
             m_currentSection == StyleSection::FILL_IN_B ||
             m_currentSection == StyleSection::FILL_IN_C ||
             m_currentSection == StyleSection::FILL_IN_D ||
             m_currentSection == StyleSection::BREAK ||
             m_currentSection == StyleSection::INTRO_A ||
             m_currentSection == StyleSection::INTRO_B ||
             m_currentSection == StyleSection::INTRO_C) {
        
        m_currentSection = m_targetAfterFill;
        changed = true;
    }
    // 3. If we were playing an ending, we stop.
    else if (m_currentSection == StyleSection::ENDING_A ||
             m_currentSection == StyleSection::ENDING_B ||
             m_currentSection == StyleSection::ENDING_C) {
             
        m_currentSection = StyleSection::STOPPED;
        changed = true;
    }

    if (changed) {
        std::cout << "\n>>> [ARRANGER] Now Playing: " << sectionToString(m_currentSection) << " <<<\n" << std::endl;
    }

    return changed;
}

std::string StyleController::getCurrentSectionName() const {
    // No lock needed if we assume reads are atomic enough for UI printing, 
    // but safer to lock.
    return sectionToString(m_currentSection);
}

std::string StyleController::sectionToString(StyleSection section) const {
    switch (section) {
        case StyleSection::STOPPED: return "STOPPED";
        case StyleSection::INTRO_A: return "Intro A";
        case StyleSection::INTRO_B: return "Intro B";
        case StyleSection::INTRO_C: return "Intro C";
        case StyleSection::MAIN_A: return "Main A";
        case StyleSection::MAIN_B: return "Main B";
        case StyleSection::MAIN_C: return "Main C";
        case StyleSection::MAIN_D: return "Main D";
        case StyleSection::FILL_IN_A: return "Fill In A";
        case StyleSection::FILL_IN_B: return "Fill In B";
        case StyleSection::FILL_IN_C: return "Fill In C";
        case StyleSection::FILL_IN_D: return "Fill In D";
        case StyleSection::BREAK: return "Break";
        case StyleSection::ENDING_A: return "Ending A";
        case StyleSection::ENDING_B: return "Ending B";
        case StyleSection::ENDING_C: return "Ending C";
        default: return "Unknown";
    }
}

} // namespace engine
