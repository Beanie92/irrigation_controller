import './style.css';

const dayNames = ["SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"];
let zoneNames = [];
let autoRefreshInterval = null;
let zoneCount = 0; // Will be updated from API

function toggleAutoRefresh(is_enabled) {
  if (is_enabled && !autoRefreshInterval) {
    autoRefreshInterval = setInterval(fetchStatus, 5000);
  } else if (!is_enabled && autoRefreshInterval) {
    clearInterval(autoRefreshInterval);
    autoRefreshInterval = null;
  }
}

function showMessage(text, type) {
  const msgDiv = document.getElementById('message');
  msgDiv.textContent = text;
  msgDiv.className = type + ' show';
  setTimeout(() => {
    msgDiv.className = msgDiv.className.replace('show', '');
  }, 4500);
}

function validateInput(element, min, max, name) {
  const value = parseInt(element.value);
  if (isNaN(value) || value < min || value > max) {
    showMessage(`${name} must be between ${min} and ${max}.`, 'error');
    element.focus();
    return false;
  }
  return true;
}

function fetchStatus() {
  fetch('/api/status')
    .then(response => response.json())
    .then(data => {
      zoneCount = data.relays.length - 1; // Update zone count, excluding pump
      const dt = data.dateTime;
      document.getElementById('time-display').textContent = 
        `${dt.year}-${String(dt.month).padStart(2,'0')}-${String(dt.day).padStart(2,'0')} ${String(dt.hour).padStart(2,'0')}:${String(dt.minute).padStart(2,'0')}:${String(dt.second).padStart(2,'0')} (${data.dayOfWeek})`;
      
      updateBatteryIcon(data.batteryLevel);
      updateWifiIcon(data.wifiRSSI);

      const runningStatusDiv = document.getElementById('runningStatus');
      const runningDescription = document.getElementById('runningDescription');
      const runningProgressBar = document.getElementById('runningProgressBar');
      const runningTimeInfo = document.getElementById('runningTimeInfo');

      if (data.runningInfo.operation !== "OP_NONE") {
        runningDescription.textContent = data.runningInfo.description;
        
        if (data.runningInfo.total_duration_s > 0) {
          const progress = (data.runningInfo.elapsed_s / data.runningInfo.total_duration_s) * 100;
          runningProgressBar.style.width = `${progress}%`;
          runningProgressBar.textContent = `${Math.round(progress)}%`;
          runningProgressBar.parentElement.style.display = 'block'; // Show container
          
          if (data.runningInfo.is_delay) {
            runningProgressBar.style.backgroundColor = '#ffc107'; // Yellow for delay
          } else {
            runningProgressBar.style.backgroundColor = '#4f8cb6'; // Blue for regular zone
          }

        } else {
          runningProgressBar.style.width = '0%';
          runningProgressBar.textContent = '';
          runningProgressBar.parentElement.style.display = 'none'; // Hide container if no duration
        }

        let timeInfoText = '';
        if (data.runningInfo.time_elapsed) {
          timeInfoText += `Elapsed: ${data.runningInfo.time_elapsed}`;
        }
        if (data.runningInfo.time_remaining) {
          if (timeInfoText) timeInfoText += ' | ';
          timeInfoText += `Remaining: ${data.runningInfo.time_remaining}`;
        }
        runningTimeInfo.textContent = timeInfoText;
        runningStatusDiv.style.display = 'block';
      } else {
        runningStatusDiv.style.display = 'none';
      }

      const relaysDiv = document.getElementById('statusRelays');
      relaysDiv.innerHTML = '';
      data.relays.forEach(relay => {
        const d = document.createElement('div');
        d.className = 'zone' + (relay.state ? ' active' : '');
        d.textContent = `${relay.name}: ${relay.state ? 'ON' : 'OFF'}`;
        relaysDiv.appendChild(d);
      });
      // Populate manual zone select
      const manualZoneSelect = document.getElementById('manualZone');
      if (manualZoneSelect.options.length <= 1) { // Populate only if not already done
          manualZoneSelect.innerHTML = '<option value="-1">Select Zone</option>'; // Default option
          data.relays.slice(1).forEach((relay, index) => { // Skip pump
              const option = document.createElement('option');
              option.value = index + 1; // Zone index 1-7
              option.textContent = relay.name;
              manualZoneSelect.appendChild(option);
          });
      }
    })
    .catch(err => {
      console.error('Error fetching status:', err);
      showMessage('Failed to load status.', 'error');
    });
}

function fetchCycles() {
  fetch('/api/cycles')
    .then(response => response.json())
    .then(data => {
      const cyclesDiv = document.getElementById('cyclesConfig');
      cyclesDiv.innerHTML = '';
      data.cycles.forEach((cycle, index) => {
        const details = document.createElement('details');
        details.className = 'cycle-config';
        
        const summary = document.createElement('summary');
        summary.textContent = cycle.name;
        details.appendChild(summary);

        const cycleDiv = document.createElement('div');
        let daysHtml = '';
        dayNames.forEach((day, dayIdx) => {
          const isActive = (cycle.daysActive & (1 << dayIdx)) ? 'active' : '';
          daysHtml += `<button class="${isActive}" onclick="toggleDay(${index}, ${1 << dayIdx}, this)">${day.substring(0,2)}</button>`;
        });

        let zonesHtml = '';
        cycle.zoneDurations.forEach((dur, zIdx) => {
          const zoneName = zoneNames.length > zIdx ? zoneNames[zIdx] : `Zone ${zIdx + 1}`;
          zonesHtml += `<div class="form-row"><label for="cycle${index}_zone${zIdx}">${zoneName} (min):</label>
                        <input type="number" id="cycle${index}_zone${zIdx}" value="${dur}" min="0" max="120"></div>`;
        });
        
        cycleDiv.innerHTML = `
          <input type="hidden" id="cycle${index}_name" value="${cycle.name}">
          <div class="form-row">
            <label for="cycle${index}_enabled">Enabled:</label>
            <select id="cycle${index}_enabled">
              <option value="true" ${cycle.enabled ? 'selected' : ''}>Yes</option>
              <option value="false" ${!cycle.enabled ? 'selected' : ''}>No</option>
            </select>
          </div>
          <div class="form-row">
            <label for="cycle${index}_starthour">Start Hour:</label>
            <input type="number" id="cycle${index}_starthour" value="${cycle.startTime.hour}" min="0" max="23">
          </div>
          <div class="form-row">
            <label for="cycle${index}_startminute">Start Minute:</label>
            <input type="number" id="cycle${index}_startminute" value="${cycle.startTime.minute}" min="0" max="59">
          </div>
          <div class="form-row">
            <label for="cycle${index}_delay">Inter-Zone Delay (min):</label>
            <input type="number" id="cycle${index}_delay" value="${cycle.interZoneDelay}" min="0" max="60">
          </div>
          <label>Days Active:</label><div class="days" id="cycle${index}_days" data-days="${cycle.daysActive}">${daysHtml}</div>
          <div class="zones">${zonesHtml}</div>
          <button onclick="saveCycle(${index})">Save ${cycle.name}</button>
          <button onclick="runCycle(${index})">Run ${cycle.name} Now</button>
        `;
        details.appendChild(cycleDiv);
        cyclesDiv.appendChild(details);
      });
    })
    .catch(err => {
      console.error('Error fetching cycles:', err);
      showMessage('Failed to load cycle configurations.', 'error');
    });
}

function toggleDay(cycleIndex, dayValue, button) {
  const daysDiv = document.getElementById(`cycle${cycleIndex}_days`);
  let currentDays = parseInt(daysDiv.getAttribute('data-days'));
  currentDays ^= dayValue; // Toggle bit
  daysDiv.setAttribute('data-days', currentDays);
  button.classList.toggle('active');
}

function saveCycle(index) {
  const hourEl = document.getElementById(`cycle${index}_starthour`);
  const minuteEl = document.getElementById(`cycle${index}_startminute`);
  const delayEl = document.getElementById(`cycle${index}_delay`);

  if (!validateInput(hourEl, 0, 23, "Start Hour")) return;
  if (!validateInput(minuteEl, 0, 59, "Start Minute")) return;
  if (!validateInput(delayEl, 0, 60, "Inter-Zone Delay")) return;

  const zoneDurations = [];
  for(let i=0; i < zoneCount; i++) {
      const zoneEl = document.getElementById(`cycle${index}_zone${i}`);
      if (!validateInput(zoneEl, 0, 120, `Zone ${i+1} Duration`)) return;
      zoneDurations.push(parseInt(zoneEl.value));
  }

  const data = {
    cycleIndex: index,
    name: document.getElementById(`cycle${index}_name`).value,
    enabled: document.getElementById(`cycle${index}_enabled`).value === 'true',
    startTime: {
      hour: parseInt(hourEl.value),
      minute: parseInt(minuteEl.value)
    },
    interZoneDelay: parseInt(delayEl.value),
    daysActive: parseInt(document.getElementById(`cycle${index}_days`).getAttribute('data-days')),
    zoneDurations: zoneDurations
  };

  fetch('/api/cycles', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(data)
  })
  .then(response => response.json())
  .then(res => {
    if(res.success) {
      showMessage('Cycle ' + data.name + ' updated successfully!', 'success');
      fetchCycles(); // Refresh
    } else {
      showMessage('Failed to update cycle ' + data.name + ': ' + (res.message || ''), 'error');
    }
  })
  .catch(err => {
      console.error('Error saving cycle:', err);
      showMessage('Error saving cycle ' + data.name + '.', 'error');
  });
}

function startManualZone() {
  const zoneId = parseInt(document.getElementById('manualZone').value);
  const durationEl = document.getElementById('manualDuration');
  
  if (zoneId < 1) {
      showMessage('Please select a zone.', 'error');
      return;
  }
  if (!validateInput(durationEl, 1, 120, "Duration")) return;

  const duration = parseInt(durationEl.value);
  fetch('/api/manual', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ action: 'start_zone', zone: zoneId, duration: duration })
  })
  .then(response => response.json())
  .then(res => {
    if(res.success) {
      showMessage('Zone ' + zoneId + ' started for ' + duration + ' minutes.', 'success');
      fetchStatus();
    } else {
      showMessage('Failed to start zone: ' + (res.message || ''), 'error');
    }
  });
}

function runCycle(cycleIndex) {
  fetch('/api/manual', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ action: 'start_cycle', cycle: cycleIndex })
  })
  .then(response => response.json())
  .then(res => {
    if(res.success) {
      // Assuming 'cycles' global array is available from fetchCycles() for name lookup
      // This might need adjustment if 'cycles' array isn't populated globally in JS
      showMessage('Cycle started.', 'success'); 
      fetchStatus();
    } else {
      showMessage('Failed to start cycle: ' + (res.message || ''), 'error');
    }
  });
}

function stopAll() {
  fetch('/api/manual', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ action: 'stop_all' })
  })
  .then(response => response.json())
  .then(res => {
    if(res.success) {
      showMessage('All operations stopped.', 'success');
      fetchStatus();
    } else {
      showMessage('Failed to stop operations: ' + (res.message || ''), 'error');
    }
  });
}

function fetchZoneNames() {
  return fetch('/api/zonenames') // Return the fetch promise
    .then(response => response.json())
    .then(data => {
      zoneNames = data.zoneNames; // Store names in global variable
      const container = document.getElementById('zoneNamesConfig');
      container.innerHTML = '';
      data.zoneNames.forEach((name, index) => {
        const itemDiv = document.createElement('div');
        itemDiv.className = 'form-row';

        const label = document.createElement('label');
        label.setAttribute('for', `zoneName_${index}`);
        label.textContent = `Zone ${index + 1}:`;
        itemDiv.appendChild(label);

        const input = document.createElement('input');
        input.type = 'text';
        input.id = `zoneName_${index}`;
        input.value = name;
        input.maxLength = 16;
        itemDiv.appendChild(input);
        
        container.appendChild(itemDiv);
      });
    })
    .catch(err => {
      console.error('Error fetching zone names:', err);
      showMessage('Failed to load zone names.', 'error');
    });
}

function saveZoneNames() {
  const names = [];
  for (let i = 0; i < zoneCount; i++) {
    const nameEl = document.getElementById(`zoneName_${i}`);
    const name = nameEl.value.trim();
    if (name.length === 0 || name.length > 16) {
      showMessage(`Zone ${i+1} name must be between 1 and 16 characters.`, 'error');
      nameEl.focus();
      return;
    }
    names.push(name);
  }
  
  fetch('/api/zonenames', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ zoneNames: names })
  })
  .then(response => response.json())
  .then(res => {
    if(res.success) {
      showMessage('Zone names updated successfully!', 'success');
      fetchStatus(); // Refresh status to update names everywhere
    } else {
      showMessage('Failed to update zone names: ' + (res.message || ''), 'error');
    }
  })
  .catch(err => {
      console.error('Error saving zone names:', err);
      showMessage('Error saving zone names.', 'error');
  });
}

function restartDevice() {
  if (confirm("Are you sure you want to restart the device?")) {
    fetch('/api/reset', { method: 'POST' })
      .then(response => {
        if (response.ok) {
          showMessage('Device is restarting...', 'success');
        } else {
          showMessage('Failed to send restart command.', 'error');
        }
      })
      .catch(err => {
        console.error('Error sending restart command:', err);
        showMessage('Error sending restart command.', 'error');
      });
  }
}

function listFiles() {
  fetch('/api/listfiles')
    .then(response => response.json())
    .then(data => {
      let fileList = 'Files on device:\n\n';
      data.forEach(file => {
        fileList += `${file.name} (${file.size} bytes)\n`;
      });
      alert(fileList);
    })
    .catch(err => {
      console.error('Error fetching file list:', err);
      showMessage('Error fetching file list.', 'error');
    });
}

// Initial data fetch
window.onload = () => {
  fetchStatus();
  fetchZoneNames().then(() => {
    fetchCycles(); // Fetch cycles only after zone names are loaded
  });
  toggleAutoRefresh(true);
};

// Assign functions to window object to be accessible from HTML onclick attributes
window.toggleAutoRefresh = toggleAutoRefresh;
window.startManualZone = startManualZone;
window.stopAll = stopAll;
window.listFiles = listFiles;
window.restartDevice = restartDevice;
window.saveZoneNames = saveZoneNames;
window.saveCycle = saveCycle;
window.runCycle = runCycle;
window.toggleDay = toggleDay;

// Sticky header logic
window.addEventListener('scroll', () => {
  const header = document.querySelector('.header');
  const stickyThreshold = 50; // Adjust this value as needed

  if (window.pageYOffset > stickyThreshold) {
    header.classList.add('sticky');
  } else {
    header.classList.remove('sticky');
  }
});

function testClick() {
  fetch('/api/test')
    .then(response => response.text())
    .then(text => console.log(text));
}

function updateBatteryIcon(level) {
  const batteryIcon = document.getElementById('battery-icon');
  batteryIcon.innerHTML = `<div id="battery-level" style="width: ${level}%;"></div>`;
  const levelDiv = document.getElementById('battery-level');
  if (level > 50) {
    levelDiv.style.backgroundColor = '#7eb659';
  } else if (level > 20) {
    levelDiv.style.backgroundColor = '#ffc107';
  } else {
    levelDiv.style.backgroundColor = '#dc3545';
  }
}

function updateWifiIcon(rssi) {
  const wifiIcon = document.getElementById('wifi-icon');
  wifiIcon.innerHTML = '';
  let numBars = 0;
  if (rssi >= -60) numBars = 4;
  else if (rssi >= -70) numBars = 3;
  else if (rssi >= -80) numBars = 2;
  else if (rssi < -80 && rssi != 0) numBars = 1;

  for (let i = 0; i < 4; i++) {
    const bar = document.createElement('div');
    bar.className = 'wifi-bar';
    bar.style.height = `${(i + 1) * 4}px`;
    if (i < numBars) {
      if (numBars >= 3) {
        bar.style.backgroundColor = '#7eb659';
      } else if (numBars >= 2) {
        bar.style.backgroundColor = '#ffc107';
      } else {
        bar.style.backgroundColor = '#dc3545';
      }
    }
    wifiIcon.appendChild(bar);
  }
}
