const button = document.getElementById('controlButton');
const statusLabel = document.getElementById('status');
let ledState = 'off';

function renderState() {
  button.textContent = `LED D2: ${ledState.toUpperCase()}`;
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
setInterval(syncLedState, 3000);
