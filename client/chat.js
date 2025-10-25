let ws = null;
let username = '';

const statusEl = document.getElementById('status');
const messagesEl = document.getElementById('messages');
const usernameInput = document.getElementById('username');
const messageInput = document.getElementById('messageInput');
const sendButton = document.getElementById('sendButton');

function connect() {
    ws = new WebSocket('ws://178.16.52.213:8080');
    
    ws.onopen = () => {
        statusEl.textContent = 'Подключено';
        statusEl.classList.add('connected');
        console.log('Соединение установлено');
    };
    
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            displayMessage(data);
        } catch (e) {
            console.error('Ошибка парсинга сообщения:', e);
        }
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket ошибка:', error);
        statusEl.textContent = 'Ошибка соединения';
        statusEl.classList.remove('connected');
    };
    
    ws.onclose = () => {
        statusEl.textContent = 'Отключено';
        statusEl.classList.remove('connected');
        messageInput.disabled = true;
        sendButton.disabled = true;
        console.log('Соединение закрыто');
        
        setTimeout(connect, 3000);
    };
}

function displayMessage(data) {
    const messageDiv = document.createElement('div');
    messageDiv.className = 'message';
    
    const usernameDiv = document.createElement('div');
    usernameDiv.className = 'username';
    usernameDiv.textContent = data.user;
    
    const textDiv = document.createElement('div');
    textDiv.className = 'text';
    textDiv.textContent = data.text;
    
    const timestampDiv = document.createElement('div');
    timestampDiv.className = 'timestamp';
    const date = new Date(data.timestamp * 1000);
    timestampDiv.textContent = date.toLocaleTimeString();
    
    messageDiv.appendChild(usernameDiv);
    messageDiv.appendChild(textDiv);
    messageDiv.appendChild(timestampDiv);
    
    messagesEl.appendChild(messageDiv);
    messagesEl.scrollTop = messagesEl.scrollHeight;
}

function sendMessage() {
    const text = messageInput.value.trim();
    if (!text || !username) return;
    
    const message = {
        type: 'message',
        user: username,
        text: text,
        timestamp: Math.floor(Date.now() / 1000)
    };
    
    ws.send(JSON.stringify(message));
    displayMessage(message);
    messageInput.value = '';
}

usernameInput.addEventListener('input', (e) => {
    username = e.target.value.trim();
    messageInput.disabled = !username;
    sendButton.disabled = !username;
});

messageInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        sendMessage();
    }
});

sendButton.addEventListener('click', sendMessage);

connect();
