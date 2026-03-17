require('dotenv').config();
const express = require('express');
const fs = require('fs');
const path = require('path');
const nodemailer = require('nodemailer');
const mqtt = require('mqtt');

const app = express();
const PORT = process.env.PORT || 3000;
const CONTACTS_PATH = path.join(__dirname, 'contatos.json');
const WEB_DIR = path.join(__dirname, 'public');
const GAS_THRESHOLD = Number(process.env.GAS_THRESHOLD || 1100);
const CONTROL_MUTE_MINUTES = Number(process.env.CONTROL_MUTE_MINUTES || 30);
const MQTT_BROKER_URL = process.env.MQTT_BROKER_URL || 'mqtt://test.mosquitto.org:1883';
const MQTT_TOPIC_ALERT = process.env.MQTT_TOPIC_ALERT || 'recifelabs/sensor-gas/alerta';
const MQTT_TOPIC_LED_STATE = process.env.MQTT_TOPIC_LED_STATE || 'recifelabs/sensor-gas/led/state';
const LED_ALERT_ON_SECONDS = Number(process.env.LED_ALERT_ON_SECONDS || 10);

app.use(express.json());
app.use(express.static(WEB_DIR));

const mqttClient = mqtt.connect(MQTT_BROKER_URL, {
    reconnectPeriod: 3000,
    connectTimeout: 10000,
});

let mqttConnected = false;
const controlledAlerts = new Map();
let ledAutoOffTimer = null;
const ledState = {
    state: 'off',
    source: 'init',
    updatedAt: new Date().toISOString(),
};

function normalizeLedState(value) {
    if (typeof value !== 'string') {
        return null;
    }

    const normalized = value.toLowerCase();
    if (normalized === 'on' || normalized === 'off') {
        return normalized;
    }

    return null;
}

function updateLedStateCache(state, source = 'unknown') {
    ledState.state = state;
    ledState.source = source;
    ledState.updatedAt = new Date().toISOString();
}

function publishLedState(state, source = 'server', extra = {}) {
    if (!mqttConnected) {
        return false;
    }

    const normalized = normalizeLedState(state);
    if (!normalized) {
        return false;
    }

    const payload = JSON.stringify({
        state: normalized,
        source,
        updatedAt: new Date().toISOString(),
        ...extra,
    });

    mqttClient.publish(MQTT_TOPIC_LED_STATE, payload, { qos: 0, retain: false });
    updateLedStateCache(normalized, source);
    console.log(`💡 [MQTT] LED => ${normalized.toUpperCase()} (${source})`);
    return true;
}

mqttClient.on('connect', () => {
    mqttConnected = true;
    console.log(`✅ [MQTT] Conectado em ${MQTT_BROKER_URL}`);
    mqttClient.subscribe(MQTT_TOPIC_LED_STATE, { qos: 0 });
    console.log(`📡 [MQTT] Inscrito em ${MQTT_TOPIC_LED_STATE}`);
});

mqttClient.on('reconnect', () => {
    console.log('🔄 [MQTT] Reconectando...');
});

mqttClient.on('error', (error) => {
    mqttConnected = false;
    console.error(`❌ [MQTT] ${error.message}`);
});

mqttClient.on('close', () => {
    mqttConnected = false;
});

mqttClient.on('message', (topic, payloadBuffer) => {
    if (topic !== MQTT_TOPIC_LED_STATE) {
        return;
    }

    const raw = payloadBuffer.toString('utf8');
    let parsed = null;
    try {
        parsed = JSON.parse(raw);
    } catch (error) {
        parsed = { state: raw, source: 'raw' };
    }

    const nextState = normalizeLedState(parsed.state);
    if (!nextState) {
        console.warn(`⚠️ [MQTT] Mensagem de LED inválida: ${raw}`);
        return;
    }

    updateLedStateCache(nextState, parsed.source || 'mqtt');
    console.log(`📩 [MQTT] Estado LED atualizado por tópico: ${nextState.toUpperCase()} (${ledState.source})`);
});

// Configuração do Transportador SMTP (Brevo)
const transporter = nodemailer.createTransport({
    host: 'smtp-relay.brevo.com',
    port: 587,
    auth: {
        user: '3e4ac3003@smtp-brevo.com', // Seu usuário Brevo
        pass: process.env.CHAVE_SMTP_BREVO // Sua Master Password/API Key
    }
});

// Middleware de Logs Unificado
app.use((req, res, next) => {
    const ip = req.headers['x-forwarded-for'] || req.socket.remoteAddress;
    const isSimulated = req.headers['user-agent']?.includes('node-fetch');
    const origin = isSimulated ? '🧪 SIMULADOR' : '📡 ESP32 REAL';
    
    if (req.method === 'POST' && req.path === '/alerta') {
        console.log(`\n[${origin}] Alerta recebido de IP: ${ip}`);
    }
    next();
});

app.post('/alerta', async (req, res) => {
    const local = typeof req.body?.local === 'string' ? req.body.local : 'Local não informado';
    const valor = Number(req.body?.valor);

    if (!Number.isFinite(valor) || valor < 0) {
        return res.status(400).json({ error: 'Campo valor inválido' });
    }

    try {
        const now = Date.now();
        const controlState = controlledAlerts.get(local);
        if (controlState && controlState.muteUntil > now) {
            const remainingSeconds = Math.ceil((controlState.muteUntil - now) / 1000);
            console.log(`🛑 [ALERTA SUPRIMIDO] ${local} está como controlado por ${remainingSeconds}s`);
            return res.status(202).json({
                status: 'suprimido',
                motivo: 'incidente-controlado',
                local,
                muteRemainingSeconds: remainingSeconds,
            });
        }

        if (controlState && controlState.muteUntil <= now) {
            controlledAlerts.delete(local);
        }

        if (!fs.existsSync(CONTACTS_PATH)) throw new Error('contatos.json não encontrado');
        const config = JSON.parse(fs.readFileSync(CONTACTS_PATH, 'utf8'));

        console.log(`[DADO] Local: ${local} | Nível: ${valor}`);

        const mailOptions = {
            from: 'Recife Labs Monitor <alerta@recifelabs.com>',
            to: config.destinatarios.join(', '),
            subject: `⚠️ URGENTE: Vazamento de Gás - ${config.escola}`,
            html: `<h2>Alerta de Segurança</h2><p>O sensor em <b>${local}</b> detectou nível <b>${valor}</b>.</p>`
        };

        await transporter.sendMail(mailOptions);
        console.log('✅ [EMAIL] Enviado com sucesso para a lista.');

        let mqttPublished = false;
        if (valor > GAS_THRESHOLD && mqttConnected) {
            const mqttPayload = JSON.stringify({
                type: 'ALERTA_GAS',
                local,
                valor,
                threshold: GAS_THRESHOLD,
                led: {
                    blinkPerSecond: 3,
                    durationSeconds: 10,
                },
                timestamp: new Date().toISOString(),
            });

            mqttClient.publish(MQTT_TOPIC_ALERT, mqttPayload, { qos: 0, retain: false });
            mqttPublished = true;
            console.log(`💡 [MQTT] Comando de LED publicado em ${MQTT_TOPIC_ALERT}`);

            publishLedState('on', 'server-alerta', { local, valor, reason: 'gas-high' });
            if (ledAutoOffTimer) {
                clearTimeout(ledAutoOffTimer);
            }
            ledAutoOffTimer = setTimeout(() => {
                publishLedState('off', 'server-auto-off', { reason: 'timer-expired' });
            }, LED_ALERT_ON_SECONDS * 1000);
        }

        res.status(200).json({ status: 'sucesso', mqttPublished, threshold: GAS_THRESHOLD });

    } catch (error) {
        console.error('❌ [ERRO]', error.message);
        res.status(500).json({ error: error.message });
    }
});

app.post('/alerta/controlado', (req, res) => {
    const local = typeof req.body?.local === 'string' ? req.body.local : 'Local não informado';
    const requestedMinutes = Number(req.body?.muteMinutes);
    const muteMinutes = Number.isFinite(requestedMinutes) && requestedMinutes > 0
        ? requestedMinutes
        : CONTROL_MUTE_MINUTES;

    const muteUntil = Date.now() + muteMinutes * 60 * 1000;
    controlledAlerts.set(local, {
        muteUntil,
        source: req.body?.source || 'manual',
    });

    console.log(`✅ [CONTROLE] ${local} marcado como controlado por ${muteMinutes} minuto(s)`);
    publishLedState('off', 'controle-manual', { local, reason: 'incidente-controlado' });
    return res.status(200).json({
        status: 'controlado',
        local,
        muteMinutes,
        muteUntil: new Date(muteUntil).toISOString(),
    });
});

app.post('/alerta/reativar', (req, res) => {
    const local = typeof req.body?.local === 'string' ? req.body.local : 'Local não informado';
    const removed = controlledAlerts.delete(local);

    if (removed) {
        console.log(`🔔 [REATIVADO] ${local} voltou a enviar alertas`);
    }

    return res.status(200).json({
        status: 'reativado',
        local,
        removed,
    });
});

app.get('/alerta/status', (req, res) => {
    const entries = Array.from(controlledAlerts.entries()).map(([local, state]) => ({
        local,
        muteUntil: new Date(state.muteUntil).toISOString(),
        source: state.source,
    }));

    return res.status(200).json({
        controlledCount: entries.length,
        controlled: entries,
    });
});

app.post('/led/state', (req, res) => {
    const desiredState = normalizeLedState(req.body?.state);
    const source = typeof req.body?.source === 'string' ? req.body.source : 'http';

    if (!desiredState) {
        return res.status(400).json({ error: 'state deve ser on ou off' });
    }

    if (!mqttConnected) {
        return res.status(503).json({ error: 'MQTT indisponível no momento' });
    }

    const published = publishLedState(desiredState, source, {
        reason: req.body?.reason || 'manual-command',
    });

    return res.status(published ? 200 : 500).json({
        ok: published,
        led: ledState,
    });
});

app.get('/led/state', (req, res) => {
    return res.status(200).json({ led: ledState });
});

app.get('/health', (req, res) => {
    res.status(200).json({
        status: 'ok',
        service: 'sensor-gas-api',
        mqtt: {
            connected: mqttConnected,
            broker: MQTT_BROKER_URL,
            topic: MQTT_TOPIC_ALERT,
        },
        led: {
            topic: MQTT_TOPIC_LED_STATE,
            state: ledState.state,
            source: ledState.source,
            updatedAt: ledState.updatedAt,
        },
        threshold: GAS_THRESHOLD,
    });
});

app.listen(PORT, () => {
    console.log(`🚀 Servidor Recife Labs em http://localhost:${PORT}`);
    if(!process.env.CHAVE_SMTP_BREVO) console.warn("⚠️ AVISO: Variável CHAVE_SMTP_BREVO não definida no .env");
    console.log(`📡 [MQTT] Topic de alerta: ${MQTT_TOPIC_ALERT}`);
    console.log(`💡 [MQTT] Topic de estado LED: ${MQTT_TOPIC_LED_STATE}`);
});