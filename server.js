require('dotenv').config();
const express = require('express');
const fs = require('fs');
const path = require('path');
const nodemailer = require('nodemailer');

const app = express();
const PORT = process.env.PORT || 3000;
const CONTACTS_PATH = path.join(__dirname, 'contatos.json');

app.use(express.json());

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
    const { local, valor } = req.body;

    try {
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
        res.status(200).json({ status: 'sucesso' });

    } catch (error) {
        console.error('❌ [ERRO]', error.message);
        res.status(500).json({ error: error.message });
    }
});

app.listen(PORT, () => {
    console.log(`🚀 Servidor Recife Labs em http://localhost:${PORT}`);
    if(!process.env.CHAVE_SMTP_BREVO) console.warn("⚠️ AVISO: Variável CHAVE_SMTP_BREVO não definida no .env");
});