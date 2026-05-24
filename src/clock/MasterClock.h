#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

namespace engine {

class MasterClock {
public:
    MasterClock();
    ~MasterClock();

    // Configure the sequencer
    void setTempo(double bpm);
    void setPPQN(uint16_t ppqn);

    // Provide a callback to run on every tick
    void setTickCallback(std::function<void(uint64_t)> callback);

    // Control
    void start();
    void stop();
    bool isRunning() const;
    double getTempo() const;

private:
    void threadLoop();

    std::atomic<bool> m_running;
    std::atomic<double> m_bpm;
    std::atomic<uint16_t> m_ppqn;
    
    std::thread m_thread;
    std::function<void(uint64_t)> m_tickCallback;
    
    uint64_t m_currentTick;
};

} // namespace engine
