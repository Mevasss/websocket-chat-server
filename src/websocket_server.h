#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

class WebSocketServer {
public:
    explicit WebSocketServer(int port);
    ~WebSocketServer();
    
    void run();
    void stop();

private:
    int serverSocket;
    int port;
    std::atomic<bool> running;
    
    struct Client {
        int socket;
        std::string username;
        bool authenticated;
    };
    
    std::map<int, Client> clients;
    std::mutex clientsMutex;
    std::vector<std::thread> workerThreads;
    std::vector<std::string> messagesHistory;
    std::mutex historyMutex;
    
    void acceptConnections();
    void handleClient(int clientSocket);
    bool performHandshake(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);
    void broadcastMessage(const std::string& message, int excludeSocket = -1);
    std::string decodeFrame(const char* data, size_t length);
    std::vector<char> encodeFrame(const std::string& message);
};

#endif
