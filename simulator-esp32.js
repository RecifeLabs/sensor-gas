const API_URL = 'http://localhost:3000/alerta';

async function simularAlerta() {
    console.log('🧪 Iniciando simulação de alerta...');
    const payload = {
        local: "Simulador de Bancada",
        valor: Math.floor(Math.random() * 500 + 1200) // Sempre acima do threshold
    };

    try {
        const res = await fetch(API_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        console.log(`✅ Resposta da API: ${res.status}`, data);
    } catch (err) {
        console.error('❌ Erro ao conectar na API:', err.message);
    }
}

simularAlerta();