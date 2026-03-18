const button = document.getElementById('controlButton');
const statusLabel = document.getElementById('status');
const sensorHeadline = document.getElementById('sensorHeadline');
const sensorMeta = document.getElementById('sensorMeta');
const streamStatus = document.getElementById('streamStatus');
const sensorLog = document.getElementById('sensorLog');
let ledState = 'off';
const uiLogs = [];
const UI_LOG_LIMIT = 40;

function renderState() {
  button.textContent = `LED D2: ${ledState.toUpperCase()}`;
}

function formatTime(iso) {
  if (!iso) return '--:--:--';
  const date = new Date(iso);
  return date.toLocaleTimeString('pt-BR', { hour12: false });
}

function formatSensorTime(sensor) {
  if (sensor?.updatedAt) {
    return formatTime(sensor.updatedAt);
  }

  if (Number.isFinite(sensor?.timestampMs)) {
    const bootRelative = Math.floor(sensor.timestampMs / 1000);
    return `t+${bootRelative}s`;
  }

  return '--:--:--';
}

function addSensorLog(message, critical = false) {
  uiLogs.push({ message, critical });
  if (uiLogs.length > UI_LOG_LIMIT) {
    uiLogs.shift();
  }

  sensorLog.innerHTML = uiLogs.map((item) => {
    const klass = item.critical ? 'sensor-log-item critical' : 'sensor-log-item';
    return `<p class="${klass}">${item.message}</p>`;
  }).join('');
}

function renderSensorState(sensor) {
  if (!sensor || typeof sensor.nivel !== 'number') {
    return;
  }

  const increasePct = Number.isFinite(sensor.increasePermille)
    ? (sensor.increasePermille / 10).toFixed(1)
    : '0.0';
  const baseline = Number.isFinite(sensor.baseline) ? sensor.baseline : '--';
  const stamp = formatSensorTime(sensor);
  const local = sensor.local || 'Local não informado';
  const thresholdAbs = Number.isFinite(sensor.thresholdAbsolute) ? sensor.thresholdAbsolute : '--';
  const thresholdRel = Number.isFinite(sensor.thresholdRelative) ? (sensor.thresholdRelative / 10).toFixed(1) : '--';

  if (sensor.calibrating) {
    sensorHeadline.textContent = `Sensor GLP: calibrando | Nível ${sensor.nivel} ADC | ${stamp}`;
  } else if (sensor.critical) {
    sensorHeadline.textContent = `Sensor GLP: CRÍTICO | Nível ${sensor.nivel} ADC | ${stamp}`;
  } else {
    sensorHeadline.textContent = `Sensor GLP: normal | Nível ${sensor.nivel} ADC | ${stamp}`;
  }

  sensorMeta.textContent = `Local: ${local} | Baseline: ${baseline} ADC | Limiar abs: ${thresholdAbs} | Limiar rel: ${thresholdRel}%`;

  addSensorLog(
    `[${stamp}] Nível=${sensor.nivel} ADC | Baseline=${baseline} ADC | Aumento=${increasePct}%`,
    Boolean(sensor.critical)
  );
}

async function bootstrapSensorStatus() {
  try {
    const response = await fetch('/sensor/status');
    const data = await response.json();

    if (Array.isArray(data.logs)) {
      const recent = data.logs.slice(-12);
      for (const item of recent) {
        const stamp = formatTime(item.updatedAt);
        const increasePct = Number.isFinite(item.increasePermille)
          ? (item.increasePermille / 10).toFixed(1)
          : '0.0';
        addSensorLog(
          `[${stamp}] Nível=${item.nivel ?? '--'} ADC | Baseline=${item.baseline ?? '--'} ADC | Aumento=${increasePct}%`,
          Boolean(item.critical)
        );
      }
    }

    renderSensorState(data.sensor);
  } catch (error) {
    addSensorLog(`[erro] Falha ao carregar status inicial: ${error.message}`, true);
  }
}

function connectSensorStream() {
  const source = new EventSource('/sensor/stream');

  source.addEventListener('ready', (event) => {
    streamStatus.className = 'status ok';
    streamStatus.textContent = 'Stream: conectado';
    try {
      const data = JSON.parse(event.data);
      renderSensorState(data.sensor);
    } catch (error) {
      addSensorLog(`[erro] Stream ready inválido: ${error.message}`, true);
    }
  });

  source.addEventListener('sensor', (event) => {
    try {
      const sensor = JSON.parse(event.data);
      renderSensorState(sensor);
    } catch (error) {
      addSensorLog(`[erro] Telemetria inválida: ${error.message}`, true);
    }
  });

  source.addEventListener('alert', (event) => {
    try {
      const data = JSON.parse(event.data);
      const stamp = formatTime(data.timestamp);
      addSensorLog(`[${stamp}] ALERTA: ${data.valor} ADC em ${data.local} | ${data.razao || 'sem razão'}`, true);
    } catch (error) {
      addSensorLog(`[erro] Evento de alerta inválido: ${error.message}`, true);
    }
  });

  source.onerror = () => {
    streamStatus.className = 'status err';
    streamStatus.textContent = 'Stream: desconectado, reconectando...';
    sensorHeadline.textContent = 'Sensor GLP: conexão em tempo real instável, tentando reconectar...';
  };
}

async function syncLedState() {
  try {
    const response = await fetch('/led/state');
    const data = await response.json();
    ledState = data?.led?.state === 'on' ? 'on' : 'off';
    renderState();
    statusLabel.className = 'status';
    statusLabel.textContent = `Estado atual: ${ledState.toUpperCase()} (${data?.led?.source || 'desconhecido'})`;
  } catch (error) {
    statusLabel.className = 'status err';
    statusLabel.textContent = `Erro ao sincronizar: ${error.message}`;
  }
}

button.addEventListener('click', async () => {
  button.disabled = true;
  statusLabel.className = 'status';
  statusLabel.textContent = 'Enviando comando de LED...';

  try {
    const nextState = ledState === 'on' ? 'off' : 'on';
    const response = await fetch('/led/state', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        state: nextState,
        source: 'frontend',
        reason: 'user-button'
      })
    });

    const data = await response.json();

    if (!response.ok) {
      throw new Error(data.error || 'Falha ao enviar comando');
    }

    ledState = data?.led?.state === 'on' ? 'on' : 'off';
    renderState();
    statusLabel.className = 'status ok';
    statusLabel.textContent = `Comando aplicado: LED ${ledState.toUpperCase()}.`;
  } catch (error) {
    statusLabel.className = 'status err';
    statusLabel.textContent = `Erro: ${error.message}`;
  } finally {
    button.disabled = false;
  }
});

renderState();
syncLedState();
bootstrapSensorStatus();
connectSensorStream();
setInterval(syncLedState, 3000);
