#include "websocket_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <cerrno>
#include <netinet/tcp.h>
#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdio>

static bool setNoDelay(int sock) {
    int flag = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        std::cerr << "setsockopt(TCP_NODELAY) failed: " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool WebSocketServer::sendAll(int clientSocket, const char* data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(clientSocket, data + sent, length - sent, 0);
        if (n < 0) {
            int err = errno;
            return false;
        }
        if (n == 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

WebSocketServer::WebSocketServer(int port) : port(port), running(false) {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        throw std::runtime_error("Не удалось создать сокет");
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(serverSocket);
        throw std::runtime_error("Не удалось привязать сокет к порту");
    }

    if (listen(serverSocket, SOMAXCONN) < 0) {
        close(serverSocket);
        throw std::runtime_error("Ошибка при переводе сокета в режим прослушивания");
    }
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::run() {
    running = true;
    acceptConnections();
}

void WebSocketServer::stop() {
    running = false;

    shutdown(serverSocket, SHUT_RDWR);
    close(serverSocket);

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& [socket, client] : clients) {
            shutdown(socket, SHUT_RDWR);
            close(socket);
        }
    }

    for (auto& thread : workerThreads) {
        if (thread.joinable()) {
            thread.detach();
        }
    }

    {
        std::lock_guard<std::mutex> lock(historyMutex);
        messagesHistory.clear();
    }
}

void WebSocketServer::acceptConnections() {
    while (running) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        std::cout << "Accepting new connection..." << std::endl;
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (running) {
                if (errno == EINTR) continue;
                std::cerr << "Ошибка при принятии соединения: " << strerror(errno) << std::endl;
            }
            continue;
        }

        std::cout << "Новое подключение: " << clientSocket << std::endl;

        setNoDelay(clientSocket);

        std::thread t([this, clientSocket]() {
            handleClient(clientSocket);
        });
        workerThreads.emplace_back(std::move(t));
    }
}

static std::string computeAcceptValue(const std::string& key) {
    std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(reinterpret_cast<const unsigned char*>(acceptKey .c_str()), acceptKey .size(), hash);

    int outlen = 4 * ((SHA_DIGEST_LENGTH + 2) / 3);
    std::string enc;
    enc.resize(outlen);
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&enc[0]), hash, SHA_DIGEST_LENGTH);
    if (len <= 0) return std::string();
    enc.resize(len);
    return enc;
}

bool WebSocketServer::performHandshake(int clientSocket) {
    const int HANDSHAKE_TIMEOUT_SEC = 5;
    std::string request;
    char buf[1024];

    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = HANDSHAKE_TIMEOUT_SEC;
    tv.tv_usec = 0;

    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
        FD_ZERO(&readfds);
        FD_SET(clientSocket, &readfds);
        int sel = select(clientSocket + 1, &readfds, nullptr, nullptr, &tv);
        if (sel == 0) {
            std::cerr << "Handshake timeout for fd=" << clientSocket << std::endl;
            return false;
        } else if (sel < 0) {
            std::cerr << "select() error during handshake: " << strerror(errno) << std::endl;
            return false;
        }

        ssize_t r = recv(clientSocket, buf, sizeof(buf), 0);
        if (r <= 0) {
            std::cerr << "recv() error during handshake fd=" << clientSocket << " r=" << r << " errno=" << errno << std::endl;
            return false;
        }
        request.append(buf, buf + r);

        tv.tv_sec = HANDSHAKE_TIMEOUT_SEC;
        tv.tv_usec = 0;
    }

    size_t keyPos = request.find("Sec-WebSocket-Key:");
    if (keyPos == std::string::npos) {
        std::cerr << "No Sec-WebSocket-Key header" << std::endl;
        return false;
    }

    keyPos = request.find(':', keyPos);
    if (keyPos == std::string::npos) return false;
    keyPos++; 
 
    while (keyPos < request.size() && (request[keyPos] == ' ' || request[keyPos] == '\t')) keyPos++;
    size_t keyEnd = request.find("\r\n", keyPos);
    if (keyEnd == std::string::npos) return false;
    std::string key = request.substr(keyPos, keyEnd - keyPos);

    std::string acceptValue = computeAcceptValue(key);
    if (acceptValue.empty()) return false;

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << acceptValue << "\r\n\r\n";

    std::string responseStr = response.str();
    return sendAll(clientSocket, responseStr.c_str(), responseStr.size());
}

void WebSocketServer::handleClient(int clientSocket) {
    if (!performHandshake(clientSocket)) {
        std::cerr << "Ошибка handshake для клиента " << clientSocket << std::endl;
        close(clientSocket);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients[clientSocket] = {clientSocket, "", false};
    }

    {
        std::lock_guard<std::mutex> lock(historyMutex);
        for (const auto& msg : messagesHistory) {
            if (!msg.empty()) {
                std::vector<char> frame = encodeFrame(msg);
                sendAll(clientSocket, frame.data(), frame.size());
            }
        }
    }

    char buffer[4096];
    while (running) {
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            } else {
                std::cerr << "recv error on fd=" << clientSocket << ": " << strerror(errno) << std::endl;
                break;
            }
        } else if (bytesRead == 0) {
            break;
        }

        std::string message = decodeFrame(buffer, bytesRead);
        if (!message.empty()) {
            processMessage(clientSocket, message);
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.erase(clientSocket);
    }

    close(clientSocket);
    std::cout << "Клиент отключен: " << clientSocket << std::endl;
}

std::string WebSocketServer::decodeFrame(const char* data, size_t length) {
    if (length < 2) return "";

    bool fin = (data[0] & 0x80) != 0;
    int opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payloadLength = data[1] & 0x7F;

    if (opcode == 0x8 || opcode == 0x9 || opcode == 0xA) {
        return "";
    }

    if (opcode != 0x1 && opcode != 0x0) {
        return "";
    }

    size_t pos = 2;

    if (payloadLength == 126) {
        if (length < 4) return "";
        payloadLength = (static_cast<unsigned char>(data[2]) << 8) | static_cast<unsigned char>(data[3]);
        pos = 4;
    } else if (payloadLength == 127) {
        if (length < 10) return "";
        payloadLength = 0;
        for (int i = 0; i < 8; i++) {
            payloadLength = (payloadLength << 8) | static_cast<unsigned char>(data[2 + i]);
        }
        pos = 10;
    }

    if (!masked) return "";

    if (length < pos + 4) return "";

    char maskingKey[4];
    memcpy(maskingKey, data + pos, 4);
    pos += 4;

    if (length < pos + payloadLength) return "";

    std::string decoded;
    decoded.reserve(payloadLength);
    for (size_t i = 0; i < payloadLength; i++) {
        decoded.push_back(data[pos + i] ^ maskingKey[i % 4]);
    }

    return decoded;
}

std::vector<char> WebSocketServer::encodeFrame(const std::string& message) {
    std::vector<char> frame;

    frame.push_back(0x81);

    size_t length = message.length();
    if (length <= 125) {
        frame.push_back(static_cast<char>(length));
    } else if (length <= 65535) {
        frame.push_back(126);
        frame.push_back((length >> 8) & 0xFF);
        frame.push_back(length & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((length >> (i * 8)) & 0xFF);
        }
    }

    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

void WebSocketServer::processMessage(int clientSocket, const std::string& message) {
    if (message.empty()) {
        return;
    }

    std::cout << "Получено сообщение от " << clientSocket << ": " << message << std::endl;

    {
        std::lock_guard<std::mutex> lock(historyMutex);
        messagesHistory.push_back(message);

        if (messagesHistory.size() > 100) {
            messagesHistory.erase(messagesHistory.begin());
        }
    }

    broadcastMessage(message, clientSocket);
}

void WebSocketServer::broadcastMessage(const std::string& message, int excludeSocket) {
    std::vector<char> frame = encodeFrame(message);

    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& [socket, client] : clients) {
        if (socket != excludeSocket) {
            if (!sendAll(socket, frame.data(), frame.size())) {
                shutdown(socket, SHUT_RDWR);
                close(socket);
            }
        }
    }
}
