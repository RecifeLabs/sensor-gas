const API_URL = 'http://localhost:3000/alerta';
const LED_API_URL = 'http://localhost:3000/led/state';
const THRESHOLD = 1100;
const INTERVAL_MS = Number(process.env.SIM_INTERVAL_MS || 4000);
const SIM_MODE = process.env.SIM_MODE || 'alert';
let simulatedLedState = 'off';

async function simularAlerta() {
    const payload = {
        local: "Simulador de Bancada",
        valor: Math.floor(Math.random() * 700 + THRESHOLD + 1)
    };

    try {
        const res = await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        console.log(`✅ Resposta da API: ${res.status} | valor=${payload.valor}`, data);
    } catch (err) {
        console.error('❌ Erro ao conectar na API:', err.message);
    }
}

async function simularLedToggle() {
    simulatedLedState = simulatedLedState === 'on' ? 'off' : 'on';

    try {
        const res = await fetch(LED_API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                state: simulatedLedState,
                source: 'simulator',
                reason: 'sim-mode-led-toggle',
            }),
        });
        const data = await res.json();
        console.log(`💡 Simulador LED => ${simulatedLedState.toUpperCase()} | API ${res.status}`, data);
    } catch (err) {
        console.error('❌ Erro no toggle de LED:', err.message);
    }
}

console.log('🧪 Simulador contínuo iniciado');
console.log(`⚙️ Modo: ${SIM_MODE} | Intervalo: ${INTERVAL_MS}ms`);

if (SIM_MODE === 'led') {
    console.log('🔁 Alternando LED ON/OFF via API...\n');
    simularLedToggle();
    setInterval(simularLedToggle, INTERVAL_MS);
} else {
    console.log(`📈 Threshold: ${THRESHOLD}`);
    console.log('🔁 Enviando alertas com valor acima do limite para acionar LED via MQTT...\n');
    simularAlerta();
    setInterval(simularAlerta, INTERVAL_MS);
}