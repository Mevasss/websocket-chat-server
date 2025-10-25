# WebSocket Чат-Сервер

Простой чат-сервер на C++ с WebSocket и веб-клиентом.

## Структура проекта

```
chat_server/
├── src/                      # Исходный код сервера
│   ├── main.cpp             # Точка входа
│   ├── websocket_server.h   # Заголовочный файл
│   └── websocket_server.cpp # Реализация сервера
├── client/                   # Веб-клиент
│   ├── index.html           # HTML интерфейс
│   ├── style.css            # Стили
│   └── chat.js              # Клиентская логика
├── CMakeLists.txt           # Конфигурация сборки
└── README.md                # Документация
```

## Требования

- C++17 компилятор (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.10+
- OpenSSL
- Современный веб-браузер

## Сборка (Linux/macOS)

```bash
mkdir build
cd build
cmake ..
make
```

## Сборка (Windows)

```bash
mkdir build
cd build
cmake -G "Visual Studio 16 2019" ..
cmake --build . --config Release
```

## Запуск

```bash
# Linux/macOS
./chat_server 8080

# Windows
Release\chat_server.exe 8080
```

## Использование

1. Запустите сервер
2. Откройте `client/index.html` в браузере
3. Введите имя пользователя
4. Начните общение!

## Возможности

- Обмен сообщениями в реальном времени
- Многопользовательский чат
- Автоматическое переподключение
- Современный веб-интерфейс

## Технологии

- **Сервер**: C++17, WebSocket, многопоточность
- **Клиент**: HTML5, CSS3, JavaScript (ES6+)
- **Протокол**: WebSocket (RFC 6455)
