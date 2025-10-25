#include "websocket_server.h"
#include <iostream>
#include <csignal>

WebSocketServer* server = nullptr;

void signalHandler(int signal) {
    if (server) {
        std::cout << "\nЗавершение работы сервера..." << std::endl;
        server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        server = new WebSocketServer(port);
        std::cout << "Сервер запущен на порту " << port << std::endl;
        server->run();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }

    delete server;
    return 0;
}
