#include "MasterClock.h"
#include <iostream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace engine {

MasterClock::MasterClock() : m_running(false), m_bpm(120.0), m_ppqn(1920), m_currentTick(0) {
#ifdef _WIN32
    // Request 1ms resolution for Windows timers
    timeBeginPeriod(1);
#endif
}

MasterClock::~MasterClock() {
    stop();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void MasterClock::setTempo(double bpm) {
    if (bpm > 0) m_bpm = bpm;
}

void MasterClock::setPPQN(uint16_t ppqn) {
    if (ppqn > 0) m_ppqn = ppqn;
}

void MasterClock::setTickCallback(std::function<void(uint64_t)> callback) {
    m_tickCallback = callback;
}

void MasterClock::start() {
    if (m_running) return;
    m_running = true;
    m_currentTick = 0;
    m_thread = std::thread(&MasterClock::threadLoop, this);
    std::cout << "Master Clock started at " << m_bpm << " BPM (" << m_ppqn << " PPQN)." << std::endl;
}

void MasterClock::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::cout << "Master Clock stopped." << std::endl;
}

bool MasterClock::isRunning() const {
    return m_running;
}

double MasterClock::getTempo() const {
    return m_bpm.load();
}

void MasterClock::threadLoop() {
    using namespace std::chrono;
    
    auto nextTickTime = high_resolution_clock::now();

    while (m_running) {
        // Calculate dynamic tick duration to allow live tempo changes
        // tickDurationNs = 60,000,000,000 / (BPM * PPQN)
        double tickDurationNs = 60000000000.0 / (m_bpm * m_ppqn);
        
        auto now = high_resolution_clock::now();
        if (now < nextTickTime) {
            auto sleepDuration = nextTickTime - now;
            
            // If the wait is longer than 2ms, we sleep to save CPU.
            // Otherwise, we spin-wait for extreme microsecond precision.
            if (sleepDuration > milliseconds(2)) {
                std::this_thread::sleep_for(sleepDuration - milliseconds(1));
            }
            
            // Spin-wait the remaining <1ms to guarantee perfect MIDI timing without jitter
            while (high_resolution_clock::now() < nextTickTime) {
                // Empty spin
            }
        }
        
        // Execute the sequencer callback
        if (m_tickCallback) {
            m_tickCallback(m_currentTick);
        }
        
        m_currentTick++;
        nextTickTime += nanoseconds(static_cast<long long>(tickDurationNs));
    }
}

} // namespace engine
