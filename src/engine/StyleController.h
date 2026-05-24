#pragma once
#include <string>
#include <iostream>
#include <mutex>

namespace engine {

enum class StyleSection {
    STOPPED,
    INTRO_A, INTRO_B, INTRO_C,
    MAIN_A, MAIN_B, MAIN_C, MAIN_D,
    FILL_IN_A, FILL_IN_B, FILL_IN_C, FILL_IN_D,
    BREAK,
    ENDING_A, ENDING_B, ENDING_C
};

class StyleController {
public:
    StyleController();
    ~StyleController();

    // Simulating button presses on the physical keyboard
    void buttonIntro(char variation); // 'A', 'B', 'C'
    void buttonMain(char variation);  // 'A', 'B', 'C', 'D'
    void buttonBreak();
    void buttonEnding(char variation); // 'A', 'B', 'C'
    void stop();

    // Called by the MasterClock on the downbeat (tick 0 of a measure)
    // Returns true if the section changed
    bool processMeasureBoundary();
    
    std::string getCurrentSectionName() const;

private:
    std::mutex m_mutex; // Protects state between UI thread and Clock thread
    StyleSection m_currentSection;
    StyleSection m_queuedSection;
    StyleSection m_targetAfterFill; // Where to go after a fill-in finishes
    
    std::string sectionToString(StyleSection section) const;
};

} // namespace engine
