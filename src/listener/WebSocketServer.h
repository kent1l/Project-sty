#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#else
// Define placeholder types for non-Windows platforms (though user is on Windows)
typedef int SOCKET;
#endif

namespace engine {

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    // Starts the WebSocket server on the specified port in a background thread
    bool start(int port);
    
    // Stops the server and closes all active connections
    void stop();

    // Register a callback to handle incoming JSON commands from the UI
    void setCommandCallback(std::function<void(const std::string& cmd, const std::string& val)> cb);

    // Sends a framed text message to the currently connected client
    void sendMessage(const std::string& message);

    // Sends a state update JSON back to the UI containing the current chord and section
    void sendStateUpdate(const std::string& chord, const std::string& section, double tempo);

private:
    void listenLoop();
    void clientLoop(SOCKET clientSocket);
    
    bool performHandshake(SOCKET clientSocket, std::string& outHeaders);
    std::string getHeaderValue(const std::string& headers, const std::string& headerName);
    
    std::string sha1(const std::string& input);
    std::string base64Encode(const std::string& input);

    int m_port;
    SOCKET m_listenSocket;
    std::atomic<SOCKET> m_clientSocket;
    std::atomic<bool> m_running;
    
    std::thread m_listenThread;
    std::thread m_clientThread;
    
    std::mutex m_sendMutex;
    std::function<void(const std::string& cmd, const std::string& val)> m_commandCallback;
};

} // namespace engine
