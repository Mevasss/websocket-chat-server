#include "websocket_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

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
    
    if (listen(serverSocket, 10) < 0) {
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
        
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (running) {
                std::cerr << "Ошибка при принятии соединения" << std::endl;
            }
            continue;
        }
        
        std::cout << "Новое подключение: " << clientSocket << std::endl;
        
        workerThreads.emplace_back([this, clientSocket]() {
            handleClient(clientSocket);
        });
    }
}

bool WebSocketServer::performHandshake(int clientSocket) {
    char buffer[4096];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead <= 0) {
        return false;
    }
    
    buffer[bytesRead] = '\0';
    std::string request(buffer);
    
    size_t keyPos = request.find("Sec-WebSocket-Key: ");
    if (keyPos == std::string::npos) {
        return false;
    }
    
    keyPos += 19;
    size_t keyEnd = request.find("\r\n", keyPos);
    std::string key = request.substr(keyPos, keyEnd - keyPos);
    
    std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)acceptKey.c_str(), acceptKey.length(), hash);
    
    BIO *bio = BIO_new(BIO_s_mem());
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    BIO_write(bio, hash, SHA_DIGEST_LENGTH);
    BIO_flush(bio);
    
    BUF_MEM *bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string acceptValue(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << acceptValue << "\r\n\r\n";
    
    std::string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
    
    return true;
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
                send(clientSocket, frame.data(), frame.size(), 0);
            }
        }
    }

    char buffer[4096];
    while (running) {
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        
        if (bytesRead <= 0 || !running) {
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
        payloadLength = (data[2] << 8) | data[3];
        pos = 4;
    } else if (payloadLength == 127) {
        payloadLength = 0;
        for (int i = 0; i < 8; i++) {
            payloadLength = (payloadLength << 8) | data[2 + i];
        }
        pos = 10;
    }
    
    if (!masked) return "";
    
    char maskingKey[4];
    memcpy(maskingKey, data + pos, 4);
    pos += 4;
    
    std::string decoded;
    for (size_t i = 0; i < payloadLength; i++) {
        decoded += data[pos + i] ^ maskingKey[i % 4];
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
            send(socket, frame.data(), frame.size(), 0);
        }
    }
}
