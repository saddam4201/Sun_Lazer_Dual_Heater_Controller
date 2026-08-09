// Minimal JS for web UI — polls /status and calls control endpoints
async function fetchStatus(){
  try{
    const r = await fetch('/status');
    if(!r.ok) throw new Error('status fetch failed');
    const j = await r.json();
    document.getElementById('ipChip').textContent = 'IP: ' + (j.ip||'--');
    document.getElementById('sdChip').textContent = 'SD: ' + (j.sd? 'OK':'NO');
    document.getElementById('rtcChip').textContent = 'RTC: ' + (j.rtc||'--');
    document.getElementById('h1_set').textContent = j.h1_set ?? '--';
    document.getElementById('h1_act').textContent = j.h1_act ?? '--';
    document.getElementById('h2_set').textContent = j.h2_set ?? '--';
    document.getElementById('h2_act').textContent = j.h2_act ?? '--';
    document.getElementById('torque').textContent = j.torque ?? '--';
    document.getElementById('max_torque').textContent = j.max_torque ?? '--';
    document.getElementById('program').textContent = j.program ?? '--';
    document.getElementById('elapsed').textContent = j.elapsed ?? '--';
    document.getElementById('rtc_now').textContent = j.rtc ?? '--';
  }catch(e){ console.warn('fetchStatus error', e); }
}

async function setRTC(){
  const v = document.getElementById('rtc_iso').value.trim();
  if(!v) return showRTCMsg('Enter ISO datetime', true);
  try{
    const r = await fetch('/rtc/set?iso='+encodeURIComponent(v));
    const j = await r.json();
    if(r.ok){ showRTCMsg(j.result || 'RTC updated', false); fetchStatus(); }
    else { showRTCMsg(j.error || 'Failed to set RTC', true); }
  }catch(e){ showRTCMsg('Request error', true); }
}

function showRTCMsg(m, err){ const el = document.getElementById('rtc_msg'); el.textContent = m; el.style.color = err? '#a00':'#0b7a6f'; setTimeout(()=>el.textContent='',4000); }

async function toggleControl(){ try{ await fetch('/control/toggle'); fetchStatus(); }catch(e){ console.warn('toggleControl', e); } }

async function fetchRecentLogs(){
  try{
    const r = await fetch('/recent_logs');
    if(!r.ok) throw new Error('no recent logs');
    const arr = await r.json();
    const container = document.getElementById('recentLogs');
    if(!Array.isArray(arr) || arr.length===0){ container.textContent='(no recent logs)'; return; }
    const table = document.createElement('table');
    arr.slice(0,20).forEach(row => {
      const tr = document.createElement('tr');
      Object.values(row).forEach(v=>{ const td=document.createElement('td'); td.textContent = v; tr.appendChild(td); });
      table.appendChild(tr);
    });
    container.innerHTML=''; container.appendChild(table);
  }catch(e){ document.getElementById('recentLogs').textContent='(failed to load recent logs)'; }
}

async function serviceAction(path, onSuccess){
  try{ const r = await fetch(path); const j = await r.json(); document.getElementById('serviceMsg').textContent = j.result || j.error || 'OK'; if(onSuccess) onSuccess(j); setTimeout(()=>document.getElementById('serviceMsg').textContent='',4000);}catch(e){ document.getElementById('serviceMsg').textContent='Request failed'; }
}

// UI wiring
document.addEventListener('DOMContentLoaded', ()=>{
  document.querySelectorAll('.navbtn').forEach(b=>b.addEventListener('click', (ev)=>{ document.querySelectorAll('.navbtn').forEach(nb=>nb.classList.remove('active')); ev.target.classList.add('active'); const t=ev.target.dataset.target; document.querySelectorAll('main section.card').forEach(s=>s.classList.remove('visible')); document.getElementById(t).classList.add('visible'); }));
  document.getElementById('btnToggle').addEventListener('click', toggleControl);
  document.getElementById('btnSetRTC').addEventListener('click', setRTC);
  document.getElementById('btnFetchRecent').addEventListener('click', fetchRecentLogs);
  document.getElementById('btnMotorJog').addEventListener('click', ()=>serviceAction('/service/motor_jog'));
  document.getElementById('btnSSRTest').addEventListener('click', ()=>serviceAction('/service/ssr_test'));
  document.getElementById('btnTare').addEventListener('click', ()=>serviceAction('/service/tare'));
  // initial poll
  fetchStatus(); setInterval(fetchStatus,2000);
});