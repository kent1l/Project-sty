// WebSocket connection to C++ Engine
let ws = null;
let isConnected = false;

function connectWebSocket() {
    // We will host the websocket server on the C++ side on port 9090
    ws = new WebSocket('ws://localhost:9090');

    ws.onopen = () => {
        isConnected = true;
        document.getElementById('connection-indicator').className = 'indicator connected';
        document.getElementById('connection-text').innerText = 'Connected to C++ Engine';
        console.log("Connected to Arranger Engine");
    };

    ws.onclose = () => {
        isConnected = false;
        document.getElementById('connection-indicator').className = 'indicator disconnected';
        document.getElementById('connection-text').innerText = 'Disconnected';
        // Auto-reconnect every 2 seconds if the engine goes offline
        setTimeout(connectWebSocket, 2000);
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'state_update') {
                updateUI(data);
            }
        } catch(e) { console.error("Error parsing message", e); }
    };
}

function updateUI(data) {
    // Update live chord readout
    if (data.chord !== undefined) {
        document.getElementById('chord-val').innerText = data.chord === "" ? "---" : data.chord;
    }
    
    // Update style section text and button glows
    if (data.section) {
        document.getElementById('section-val').innerText = data.section.toUpperCase();
        
        // Remove glow from all buttons
        document.querySelectorAll('.main-btn').forEach(btn => btn.classList.remove('active'));
        
        // Add glow to the currently active section
        if (data.section === 'Main A') document.getElementById('btn-main-a').classList.add('active');
        if (data.section === 'Main B') document.getElementById('btn-main-b').classList.add('active');
        if (data.section === 'Main C') document.getElementById('btn-main-c').classList.add('active');
        if (data.section === 'Main D') document.getElementById('btn-main-d').classList.add('active');
    }
    
    // Update tempo readout if the engine forces a tempo change (e.g. from loading a Memory Bank)
    if (data.tempo) {
        document.getElementById('tempo-val').innerText = Math.round(data.tempo);
        document.getElementById('tempo-slider').value = data.tempo;
    }
}

function sendCmd(cmd, val) {
    if (!isConnected) {
        console.warn("Cannot send command, engine is disconnected.");
        return;
    }
    // Send the button press as a tiny JSON packet over the network to the C++ core
    ws.send(JSON.stringify({ cmd: cmd, val: val }));
}

// Tempo Slider Event (Drags send continuous updates)
document.getElementById('tempo-slider').addEventListener('input', (e) => {
    const newTempo = e.target.value;
    document.getElementById('tempo-val').innerText = newTempo;
    sendCmd('tempo', parseInt(newTempo));
});

// Memory Banks Event
document.querySelectorAll('.bank-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
        const bankNum = e.target.getAttribute('data-bank');
        sendCmd('load_bank', parseInt(bankNum));
        
        // Add a temporary glow effect to the pressed bank
        e.target.style.boxShadow = "0 0 20px rgba(157, 78, 221, 0.8)";
        setTimeout(() => { e.target.style.boxShadow = ""; }, 200);
    });
});

// MIDI Note to Name Helper
function getMidiNoteName(note) {
    const notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const octave = Math.floor(note / 12) - 1;
    const name = notes[note % 12];
    return name + octave;
}

// Split Point Slider Event
document.getElementById('split-slider').addEventListener('input', (e) => {
    const newSplit = parseInt(e.target.value);
    document.getElementById('split-display-note').innerText = newSplit;
    document.getElementById('split-display-name').innerText = getMidiNoteName(newSplit);
    sendCmd('split', newSplit);
});

// Start the connection loop
connectWebSocket();
