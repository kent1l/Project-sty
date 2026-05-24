#include "WebSocketServer.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define closesocket close
#endif

namespace engine {

// Helper to parse simple JSON like {"cmd": "tempo", "val": 130}
void parseSimpleJson(const std::string& json, std::string& outCmd, std::string& outVal) {
    outCmd.clear();
    outVal.clear();
    
    size_t cmdPos = json.find("\"cmd\"");
    if (cmdPos != std::string::npos) {
        size_t colonPos = json.find(":", cmdPos);
        if (colonPos != std::string::npos) {
            size_t startQuote = json.find("\"", colonPos);
            if (startQuote != std::string::npos) {
                size_t endQuote = json.find("\"", startQuote + 1);
                if (endQuote != std::string::npos) {
                    outCmd = json.substr(startQuote + 1, endQuote - startQuote - 1);
                }
            }
        }
    }
    
    size_t valPos = json.find("\"val\"");
    if (valPos != std::string::npos) {
        size_t colonPos = json.find(":", valPos);
        if (colonPos != std::string::npos) {
            size_t startVal = colonPos + 1;
            while (startVal < json.length() && (json[startVal] == ' ' || json[startVal] == '\t')) {
                startVal++;
            }
            if (startVal < json.length()) {
                if (json[startVal] == '\"') {
                    size_t endQuote = json.find("\"", startVal + 1);
                    if (endQuote != std::string::npos) {
                        outVal = json.substr(startVal + 1, endQuote - startVal - 1);
                    }
                } else {
                    size_t endVal = startVal;
                    while (endVal < json.length() && json[endVal] != ',' && json[endVal] != '}' && json[endVal] != ' ' && json[endVal] != '\r' && json[endVal] != '\n') {
                        endVal++;
                    }
                    outVal = json.substr(startVal, endVal - startVal);
                }
            }
        }
    }
}

WebSocketServer::WebSocketServer() 
    : m_port(9090), m_listenSocket(INVALID_SOCKET), m_clientSocket(INVALID_SOCKET), m_running(false) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebSocketServer::~WebSocketServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool WebSocketServer::start(int port) {
    m_port = port;
    m_running = true;
    
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        std::cerr << "[WS SERVER] Error: Socket creation failed." << std::endl;
        return false;
    }

    // Allow address reuse
    int optval = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[WS SERVER] Error: Bind failed on port " << m_port << std::endl;
        closesocket(m_listenSocket);
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[WS SERVER] Error: Listen failed." << std::endl;
        closesocket(m_listenSocket);
        return false;
    }

    std::cout << "[WS SERVER] Listening for Web UI connection on port " << m_port << "..." << std::endl;
    m_listenThread = std::thread(&WebSocketServer::listenLoop, this);
    return true;
}

void WebSocketServer::stop() {
    m_running = false;
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    
    SOCKET activeClient = m_clientSocket.exchange(INVALID_SOCKET);
    if (activeClient != INVALID_SOCKET) {
        closesocket(activeClient);
    }
    
    if (m_listenThread.joinable()) m_listenThread.join();
    if (m_clientThread.joinable()) m_clientThread.join();
}

void WebSocketServer::setCommandCallback(std::function<void(const std::string& cmd, const std::string& val)> cb) {
    m_commandCallback = cb;
}

void WebSocketServer::sendMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    SOCKET client = m_clientSocket.load();
    if (client == INVALID_SOCKET) return;

    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN = 1, Opcode = 1 (Text Frame)
    
    size_t len = message.length();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    
    frame.insert(frame.end(), message.begin(), message.end());
    send(client, (const char*)frame.data(), static_cast<int>(frame.size()), 0);
}

void WebSocketServer::sendStateUpdate(const std::string& chord, const std::string& section, double tempo) {
    std::stringstream ss;
    ss << "{\"type\":\"state_update\",\"chord\":\"" << chord 
       << "\",\"section\":\"" << section 
       << "\",\"tempo\":" << tempo << "}";
    sendMessage(ss.str());
}

void WebSocketServer::listenLoop() {
    while (m_running) {
        SOCKET client = accept(m_listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!m_running) break;
            continue;
        }

        std::cout << "[WS SERVER] Client connected! Performing handshake..." << std::endl;
        
        // Disconnect old client if already connected
        SOCKET oldClient = m_clientSocket.exchange(client);
        if (oldClient != INVALID_SOCKET) {
            closesocket(oldClient);
        }

        if (m_clientThread.joinable()) {
            m_clientThread.join();
        }

        m_clientThread = std::thread(&WebSocketServer::clientLoop, this, client);
    }
}

void WebSocketServer::clientLoop(SOCKET clientSocket) {
    std::string headers;
    if (!performHandshake(clientSocket, headers)) {
        std::cerr << "[WS SERVER] Handshake failed." << std::endl;
        closesocket(clientSocket);
        m_clientSocket.compare_exchange_strong(clientSocket, INVALID_SOCKET);
        return;
    }

    std::cout << "[WS SERVER] Handshake succeeded! WebSocket connection established." << std::endl;

    std::vector<uint8_t> buffer;
    uint8_t tempBuf[4096];

    while (m_running) {
        int bytesRecv = recv(clientSocket, (char*)tempBuf, sizeof(tempBuf), 0);
        if (bytesRecv <= 0) {
            std::cout << "[WS SERVER] Client disconnected." << std::endl;
            break;
        }

        buffer.insert(buffer.end(), tempBuf, tempBuf + bytesRecv);

        // Process all frames in the buffer
        while (buffer.size() >= 6) {
            uint8_t byte0 = buffer[0];
            uint8_t byte1 = buffer[1];
            
            bool fin = (byte0 & 0x80) != 0;
            uint8_t opcode = byte0 & 0x0F;
            bool masked = (byte1 & 0x80) != 0;
            uint64_t payloadLength = byte1 & 0x7F;
            
            size_t headerSize = 2;
            if (payloadLength == 126) {
                if (buffer.size() < 4) break;
                payloadLength = (buffer[2] << 8) | buffer[3];
                headerSize += 2;
            } else if (payloadLength == 127) {
                if (buffer.size() < 10) break;
                payloadLength = 0;
                for (int i = 0; i < 8; i++) {
                    payloadLength = (payloadLength << 8) | buffer[2 + i];
                }
                headerSize += 8;
            }
            
            if (opcode == 8) { // Connection Close
                std::cout << "[WS SERVER] Received close frame." << std::endl;
                closesocket(clientSocket);
                m_clientSocket.compare_exchange_strong(clientSocket, INVALID_SOCKET);
                return;
            }
            
            size_t maskSize = masked ? 4 : 0;
            if (buffer.size() < headerSize + maskSize + payloadLength) {
                break; // Incomplete frame, wait for more data
            }
            
            std::vector<uint8_t> mask(4, 0);
            if (masked) {
                for (int i = 0; i < 4; i++) {
                    mask[i] = buffer[headerSize + i];
                }
            }
            
            std::string payload;
            payload.resize(payloadLength);
            size_t payloadStart = headerSize + maskSize;
            for (size_t i = 0; i < payloadLength; i++) {
                uint8_t byte = buffer[payloadStart + i];
                if (masked) {
                    byte ^= mask[i % 4];
                }
                payload[i] = (char)byte;
            }
            
            // Trigger callback for text frame
            if (opcode == 1 && m_commandCallback) {
                std::string cmd, val;
                parseSimpleJson(payload, cmd, val);
                if (!cmd.empty()) {
                    m_commandCallback(cmd, val);
                }
            }

            // Remove processed frame from the buffer
            buffer.erase(buffer.begin(), buffer.begin() + payloadStart + payloadLength);
        }
    }

    closesocket(clientSocket);
    m_clientSocket.compare_exchange_strong(clientSocket, INVALID_SOCKET);
}

bool WebSocketServer::performHandshake(SOCKET clientSocket, std::string& outHeaders) {
    char buf[4096];
    int totalBytes = 0;
    while (true) {
        int bytes = recv(clientSocket, buf + totalBytes, sizeof(buf) - totalBytes - 1, 0);
        if (bytes <= 0) return false;
        totalBytes += bytes;
        buf[totalBytes] = '\0';
        
        outHeaders = buf;
        if (outHeaders.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        if (totalBytes >= sizeof(buf) - 1) return false;
    }

    std::string key = getHeaderValue(outHeaders, "Sec-WebSocket-Key");
    if (key.empty()) return false;

    // Standard WebSocket GUID
    std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string acceptSha1 = sha1(acceptKey);
    std::string acceptBase64 = base64Encode(acceptSha1);

    std::stringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << acceptBase64 << "\r\n\r\n";

    send(clientSocket, response.str().c_str(), static_cast<int>(response.str().length()), 0);
    return true;
}

std::string WebSocketServer::getHeaderValue(const std::string& headers, const std::string& headerName) {
    size_t pos = headers.find(headerName + ":");
    if (pos == std::string::npos) return "";
    
    size_t valStart = pos + headerName.length() + 1;
    while (valStart < headers.length() && (headers[valStart] == ' ' || headers[valStart] == '\t')) {
        valStart++;
    }
    
    size_t valEnd = headers.find("\r\n", valStart);
    if (valEnd == std::string::npos) return "";
    
    return headers.substr(valStart, valEnd - valStart);
}

// SHA-1 transform function
void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (buffer[i*4] << 24) | (buffer[i*4+1] << 16) | (buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        uint32_t val = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = (val << 1) | (val >> 31);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

std::string WebSocketServer::sha1(const std::string& input) {
    uint32_t state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t bit_len = input.length() * 8;
    std::string padded = input;
    padded.push_back((char)0x80);
    while ((padded.length() * 8) % 512 != 448) {
        padded.push_back(0);
    }
    for (int i = 7; i >= 0; i--) {
        padded.push_back((char)((bit_len >> (i * 8)) & 0xFF));
    }
    for (size_t i = 0; i < padded.length(); i += 64) {
        sha1_transform(state, (const uint8_t*)&padded[i]);
    }
    std::string result;
    for (int i = 0; i < 5; i++) {
        result.push_back((char)((state[i] >> 24) & 0xFF));
        result.push_back((char)((state[i] >> 16) & 0xFF));
        result.push_back((char)((state[i] >> 8) & 0xFF));
        result.push_back((char)(state[i] & 0xFF));
    }
    return result;
}

std::string WebSocketServer::base64Encode(const std::string& input) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string ret;
    int i = 0, j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    for (char c : input) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; i < 4 ; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
        while((i++ < 3)) ret += '=';
    }
    return ret;
}

} // namespace engine
