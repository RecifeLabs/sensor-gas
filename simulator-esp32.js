const API_URL = 'http://localhost:3000/alerta';
const THRESHOLD = 1100;
const INTERVAL_MS = Number(process.env.SIM_INTERVAL_MS || 4000);

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

console.log('🧪 Simulador contínuo iniciado');
console.log(`📈 Threshold: ${THRESHOLD} | Intervalo: ${INTERVAL_MS}ms`);
console.log('🔁 Enviando alertas com valor acima do limite para acionar LED via MQTT...\n');

simularAlerta();
setInterval(simularAlerta, INTERVAL_MS);