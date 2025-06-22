#include "web_server.h"
#include "config_manager.h"
#include "logo_webp.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Define the web server object
AsyncWebServer server(80);

// HTML Page (served at root)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Irrigation Controller</title>
  <link rel="icon" href="data:image/webp;base64,##FAVICON_BASE64##" type="image/webp">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial, sans-serif; 
      margin: 0; 
      background-color: #f4f4f4; 
      color: #333; 
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      overflow-x: hidden;
    }
    .container { 
      background-color: #fff; 
      padding: 20px; 
      border-radius: 8px; 
      box-shadow: 0 0 10px rgba(0,0,0,0.1); 
      max-width: 800px; 
      width: 100%;
      margin: 10px;
    }
    .header-img { max-width: 100px; max-height: 100px; vertical-align: middle; margin-right: 10px; object-fit: contain; }
    h1, h2 { color: #0056b3; }
    .section { margin-bottom: 20px; padding: 15px; padding-top: 0px; border: 1px solid #ddd; border-radius: 4px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    .form-row label { display: inline-block; margin-right: 10px; margin-bottom: 0; flex-shrink: 0; }
    input[type="text"], input[type="number"], select {
      width: 100%; 
      padding: 10px; 
      margin-bottom: 10px; 
      border: 1px solid #ccc; 
      border-radius: 4px;
      box-sizing: border-box;
    }
    .form-row input, .form-row select { width: auto; flex-grow: 1; margin-bottom: 0; }
    button {
      background-color: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-right: 5px;
    }
    button:hover { background-color: #0056b3; }
    .status { padding: 10px;  background-color: #e9ecef; border-radius: 4px; margin-bottom:15px; }
    .zone-status { display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; }
    .form-row { display: flex; align-items: center; margin-bottom: 10px; }
    .zone { 
      padding: 8px; 
      border: 1px solid #ccc; 
      border-radius: 4px; 
      width: 120px; 
      text-align: center; 
      display: flex; 
      justify-content: center; 
      align-items: center; 
    }
    .zone.active { background-color: #28a745; color: white; }
    .cycle-config { margin-bottom: 15px; }
    summary {
      cursor: pointer;
      padding: 10px;
      background-color: #f2f2f2;
      border: 1px solid #ddd;
      border-radius: 4px;
      margin-bottom: 5px;
      font-weight: bold;
    }
    details > div {
      padding: 10px;
      border: 1px solid #ddd;
      border-top: none;
      border-radius: 0 0 4px 4px;
    }
    .days { display: flex; justify-content: center; }
    .days button { padding: 5px 8px; margin: 2px; background-color: #6c757d; flex-grow: 1; text-align: center; min-width: 40px; }
    .days button.active { background-color: #28a745; }
    #message { 
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      padding: 10px; 
      border-radius: 0;
      margin-top: 0;
      z-index: 1000;
      text-align: center;
      box-shadow: 0 2px 5px rgba(0,0,0,0.2);
    }
    .success { background-color: #d4edda; color: #155724; border-bottom: 1px solid #c3e6cb; }
    .error { background-color: #f8d7da; color: #721c24; border-bottom: 1px solid #f5c6cb; }
  </style>
</head>
<body>
  <div class="container">
    <h1><img src="data:image/webp;base64,##FAVICON_BASE64##" class="header-img">Irrigation Controller</h1>

    <div class="section">
      <h2>System Status</h2>
      <div class="status" id="statusDateTime">Loading...</div>
      <div class="status">
        <strong>Relay States:</strong>
        <div class="zone-status" id="statusRelays">Loading...</div>
      </div>
      <button onclick="fetchStatus()">Refresh Status</button>
    </div>

    <div class="section">
      <h2>Manual Control</h2>
      <div class="form-row">
        <label for="manualZone">Select Zone:</label>
        <select id="manualZone">
          <!-- Options will be populated by JS -->
        </select>
      </div>
      <div class="form-row">
        <label for="manualDuration">Duration (minutes):</label>
        <input type="number" id="manualDuration" value="5" min="1" max="120">
      </div>
      <button onclick="startManualZone()">Start Zone</button>
      <button onclick="stopAll()">Stop All</button>
    </div>
    
    <div class="section">
      <h2>Irrigation Cycles</h2>
      <div id="cyclesConfig">Loading...</div>
      <button onclick="fetchCycles()">Refresh Cycles</button>
    </div>

    <div class="section">
      <h2>Zone Names</h2>
      <div id="zoneNamesConfig">Loading...</div>
      <button onclick="saveZoneNames()">Save Zone Names</button>
    </div>

  </div>
  <div id="message"></div>

<script>
  const dayNames = ["SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"];
  let zoneNames = [];

  function showMessage(text, type) {
    const msgDiv = document.getElementById('message');
    msgDiv.textContent = text;
    msgDiv.className = type; // 'success' or 'error'
    setTimeout(() => { msgDiv.textContent = ''; msgDiv.className = ''; }, 5000);
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
        const dt = data.dateTime;
        document.getElementById('statusDateTime').textContent = 
          `Current Time: ${dt.year}-${String(dt.month).padStart(2,'0')}-${String(dt.day).padStart(2,'0')} ${String(dt.hour).padStart(2,'0')}:${String(dt.minute).padStart(2,'0')}:${String(dt.second).padStart(2,'0')} (${data.dayOfWeek})`;
        
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
    for(let i=0; i<${ZONE_COUNT}; i++) {
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

    fetch('/api/cycle', {
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
          input.maxLength = 31;
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
    for (let i = 0; i < ${ZONE_COUNT}; i++) {
      const nameEl = document.getElementById(`zoneName_${i}`);
      const name = nameEl.value.trim();
      if (name.length === 0 || name.length > 31) {
        showMessage(`Zone ${i+1} name must be between 1 and 31 characters.`, 'error');
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

  // Initial data fetch
  window.onload = () => {
    fetchStatus();
    fetchZoneNames().then(() => {
      fetchCycles(); // Fetch cycles only after zone names are loaded
    });
  };

  function testClick() {
    fetch('/api/test')
      .then(response => response.text())
      .then(text => console.log(text));
  }
</script>
</body>
</html>
)rawliteral";

// Helper to convert DayOfWeek bitmask to string for JSON
String dayOfWeekToString(uint8_t daysActive) {
    String str = "";
    if (daysActive & SUNDAY)    str += "Su,";
    if (daysActive & MONDAY)    str += "Mo,";
    if (daysActive & TUESDAY)   str += "Tu,";
    if (daysActive & WEDNESDAY) str += "We,";
    if (daysActive & THURSDAY)  str += "Th,";
    if (daysActive & FRIDAY)    str += "Fr,";
    if (daysActive & SATURDAY)  str += "Sa,";
    if (str.length() > 0) str.remove(str.length() - 1); // Remove last comma
    return str;
}

// Helper to parse DayOfWeek string from JSON
uint8_t stringToDayOfWeek(String daysString) {
    uint8_t days = 0;
    if (daysString.indexOf("Su") != -1) days |= SUNDAY;
    if (daysString.indexOf("Mo") != -1) days |= MONDAY;
    if (daysString.indexOf("Tu") != -1) days |= TUESDAY;
    if (daysString.indexOf("We") != -1) days |= WEDNESDAY;
    if (daysString.indexOf("Th") != -1) days |= THURSDAY;
    if (daysString.indexOf("Fr") != -1) days |= FRIDAY;
    if (daysString.indexOf("Sa") != -1) days |= SATURDAY;
    return days;
}

void handleRoot(AsyncWebServerRequest *request) {
    Serial.println("Handling root request.");
    String html(index_html);
    html.replace("${ZONE_COUNT}", String(ZONE_COUNT));
    String favicon_b64 = FAVICON_BASE64;
    favicon_b64.replace("data:image/webp;base64,", "");
    html.replace("##FAVICON_BASE64##", favicon_b64);
    request->send(200, "text/html", html);
}

void handleNotFound(AsyncWebServerRequest *request) {
    Serial.printf("NOT FOUND: %s\n", request->url().c_str());
    request->send(404, "text/plain", "Not found");
}

void handleGetStatus(AsyncWebServerRequest *request) {
    Serial.println("Handling get status request.");
    StaticJsonDocument<512> doc;
    doc["firmwareVersion"] = "1.0";

    JsonObject dateTimeObj = doc.createNestedObject("dateTime");
    dateTimeObj["year"] = currentDateTime.year;
    dateTimeObj["month"] = currentDateTime.month;
    dateTimeObj["day"] = currentDateTime.day;
    dateTimeObj["hour"] = currentDateTime.hour;
    dateTimeObj["minute"] = currentDateTime.minute;
    dateTimeObj["second"] = currentDateTime.second;
    
    doc["dayOfWeek"] = dayOfWeekToString(getCurrentDayOfWeek());

    JsonArray relayStatusArray = doc.createNestedArray("relays");
    for (int i = 0; i < NUM_RELAYS; i++) {
        JsonObject relayObj = relayStatusArray.createNestedObject();
        if (i == 0) {
            relayObj["name"] = "Pump";
        } else {
            relayObj["name"] = systemConfig.zoneNames[i-1];
        }
        relayObj["state"] = relayStates[i];
    }
    doc["currentOperation"] = currentOperation;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleGetCycles(AsyncWebServerRequest *request) {
    Serial.println("Handling get cycles request.");
    StaticJsonDocument<1024> doc;
    JsonArray cyclesArray = doc.createNestedArray("cycles");

    for (int i = 0; i < NUM_CYCLES; i++) {
        JsonObject cycleObj = cyclesArray.createNestedObject();
        cycleObj["name"] = cycles[i]->name;
        cycleObj["enabled"] = cycles[i]->enabled;
        
        JsonObject startTimeObj = cycleObj.createNestedObject("startTime");
        startTimeObj["hour"] = cycles[i]->startTime.hour;
        startTimeObj["minute"] = cycles[i]->startTime.minute;
        
        cycleObj["daysActive"] = cycles[i]->daysActive;
        cycleObj["daysActiveString"] = dayOfWeekToString(cycles[i]->daysActive);
        cycleObj["interZoneDelay"] = cycles[i]->interZoneDelay;
        
        JsonArray durations = cycleObj.createNestedArray("zoneDurations");
        for (int j = 0; j < ZONE_COUNT; j++) {
            durations.add(cycles[i]->zoneDurations[j]);
        }
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleSetCycle(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling set cycle request.");
    if (index == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);

        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        int cycleIndex = doc["cycleIndex"];
        if (cycleIndex < 0 || cycleIndex >= NUM_CYCLES) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
            return;
        }

        CycleConfig* cfg = cycles[cycleIndex];
        cfg->enabled = doc["enabled"].as<bool>();
        
        JsonObjectConst startTime = doc["startTime"];
        if (startTime) {
            cfg->startTime.hour = startTime["hour"].as<uint8_t>();
            cfg->startTime.minute = startTime["minute"].as<uint8_t>();
        }
        
        cfg->daysActive = doc["daysActive"].as<uint8_t>();
        cfg->interZoneDelay = doc["interZoneDelay"].as<uint8_t>();

        JsonArrayConst zoneDurations = doc["zoneDurations"];
        if (zoneDurations) {
            for (int i = 0; i < ZONE_COUNT && i < zoneDurations.size(); i++) {
                cfg->zoneDurations[i] = zoneDurations[i].as<uint16_t>();
            }
        }
        
        if (saveConfig()) {
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Cycle updated\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false, \"message\":\"Failed to save config\"}");
        }
    }
}

void handleManualControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling manual control request.");
    if (index == 0) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        const char* action = doc["action"];
        if (strcmp(action, "start_zone") == 0) {
            int zone = doc["zone"];
            int duration = doc["duration"];
            if (zone >= 1 && zone <= ZONE_COUNT && duration > 0 && duration <= 120) {
                selectedManualDuration = duration;
                startManualZone(zone);
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Manual zone start requested\"}");
            } else {
                request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid zone or duration\"}");
            }
        } else if (strcmp(action, "start_cycle") == 0) {
            int cycleIdx = doc["cycle"];
            if (cycleIdx >= 0 && cycleIdx < NUM_CYCLES) {
                startCycleRun(cycleIdx, OP_MANUAL_CYCLE);
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Cycle start requested\"}");
            } else {
                request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
            }
        } else if (strcmp(action, "stop_all") == 0) {
            stopAllActivity();
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Stop all requested\"}");
        } else {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Unknown action\"}");
        }
    }
}


void handleGetZoneNames(AsyncWebServerRequest *request) {
    Serial.println("Handling get zone names request.");
    StaticJsonDocument<512> doc;
    JsonArray zoneNamesArray = doc.createNestedArray("zoneNames");
    for (int i = 0; i < ZONE_COUNT; i++) {
        zoneNamesArray.add(systemConfig.zoneNames[i]);
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleSetZoneNames(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("Handling set zone names request.");
    if (index == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        JsonArrayConst newNames = doc["zoneNames"];
        if (newNames && newNames.size() == ZONE_COUNT) {
            for (int i = 0; i < ZONE_COUNT; i++) {
                strlcpy(systemConfig.zoneNames[i], newNames[i].as<const char*>(), sizeof(systemConfig.zoneNames[i]));
            }
            if (saveConfig()) {
                request->send(200, "application/json", "{\"success\":true, \"message\":\"Zone names updated\"}");
            } else {
                request->send(500, "application/json", "{\"success\":false, \"message\":\"Failed to save config\"}");
            }
        } else {
            request->send(400, "application/json", "{\"success\":false, \"message\":\"Invalid data\"}");
        }
    }
}

void initWebServer() {
    Serial.println("Initializing web server...");
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/cycles", HTTP_GET, handleGetCycles);
    server.on("/api/zonenames", HTTP_GET, handleGetZoneNames);

    // POST handlers with body
    server.on("/api/cycle", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleSetCycle);
    server.on("/api/manual", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleManualControl);
    server.on("/api/zonenames", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleSetZoneNames);

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started using ESPAsyncWebServer. Now confirming server is running...");
}
