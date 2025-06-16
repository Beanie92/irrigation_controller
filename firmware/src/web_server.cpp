#include "web_server.h"
#include <WebServer.h>    // Changed from ESPAsyncWebServer
#include <ArduinoJson.h>  // For JSON parsing and generation

// Define the web server object
WebServer server(80); // Changed from AsyncWebServer

// HTML Page (served at root)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Irrigation Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f4f4f4; color: #333; }
    .container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
    h1, h2 { color: #0056b3; }
    .section { margin-bottom: 20px; padding: 15px; border: 1px solid #ddd; border-radius: 4px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type="text"], input[type="number"], select {
      width: calc(100% - 22px); padding: 10px; margin-bottom: 10px; border: 1px solid #ccc; border-radius: 4px;
    }
    button {
      background-color: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-right: 5px;
    }
    button:hover { background-color: #0056b3; }
    .status { padding: 10px; background-color: #e9ecef; border-radius: 4px; margin-bottom:15px; }
    .zone-status { display: flex; flex-wrap: wrap; gap: 10px; }
    .zone { padding: 8px; border: 1px solid #ccc; border-radius: 4px; width: 120px; text-align: center; }
    .zone.active { background-color: #28a745; color: white; }
    .cycle-config { margin-bottom: 15px; }
    .days button { padding: 5px 8px; margin: 2px; background-color: #6c757d; }
    .days button.active { background-color: #28a745; }
    #message { margin-top: 15px; padding: 10px; border-radius: 4px; }
    .success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    .error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Irrigation Controller</h1>

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
      <h2>Set System Time</h2>
      <label for="year">Year:</label><input type="number" id="year">
      <label for="month">Month:</label><input type="number" id="month">
      <label for="day">Day:</label><input type="number" id="day">
      <label for="hour">Hour:</label><input type="number" id="hour">
      <label for="minute">Minute:</label><input type="number" id="minute">
      <button onclick="setTime()">Set Time</button>
    </div>

    <div class="section">
      <h2>Manual Control</h2>
      <label for="manualZone">Select Zone:</label>
      <select id="manualZone">
        <!-- Options will be populated by JS -->
      </select>
      <label for="manualDuration">Duration (minutes):</label>
      <input type="number" id="manualDuration" value="5" min="1" max="120">
      <button onclick="startManualZone()">Start Zone</button>
      <button onclick="stopAll()">Stop All</button>
    </div>
    
    <div class="section">
      <h2>Irrigation Cycles</h2>
      <div id="cyclesConfig">Loading...</div>
      <button onclick="fetchCycles()">Refresh Cycles</button>
    </div>

    <div id="message"></div>
  </div>

<script>
  const dayNames = ["SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"];

  function showMessage(text, type) {
    const msgDiv = document.getElementById('message');
    msgDiv.textContent = text;
    msgDiv.className = type; // 'success' or 'error'
    setTimeout(() => { msgDiv.textContent = ''; msgDiv.className = ''; }, 5000);
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

  function setTime() {
    const data = {
      year: parseInt(document.getElementById('year').value),
      month: parseInt(document.getElementById('month').value),
      day: parseInt(document.getElementById('day').value),
      hour: parseInt(document.getElementById('hour').value),
      minute: parseInt(document.getElementById('minute').value)
    };
    fetch('/api/time', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(res => {
      if(res.success) {
        showMessage('Time updated successfully!', 'success');
        fetchStatus(); // Refresh status
      } else {
        showMessage('Failed to update time: ' + (res.message || ''), 'error');
      }
    })
    .catch(err => {
        console.error('Error setting time:', err);
        showMessage('Error setting time.', 'error');
    });
  }

  function fetchCycles() {
    fetch('/api/cycles')
      .then(response => response.json())
      .then(data => {
        const cyclesDiv = document.getElementById('cyclesConfig');
        cyclesDiv.innerHTML = '';
        data.cycles.forEach((cycle, index) => {
          const cycleDiv = document.createElement('div');
          cycleDiv.className = 'cycle-config section';
          let daysHtml = '';
          dayNames.forEach((day, dayIdx) => {
            const isActive = (cycle.daysActive & (1 << dayIdx)) ? 'active' : '';
            daysHtml += `<button class="${isActive}" onclick="toggleDay(${index}, ${1 << dayIdx}, this)">${day.substring(0,2)}</button>`;
          });

          let zonesHtml = '';
          cycle.zoneDurations.forEach((dur, zIdx) => {
            zonesHtml += `<label for="cycle${index}_zone${zIdx}">Zone ${zIdx+1} (min):</label>
                          <input type="number" id="cycle${index}_zone${zIdx}" value="${dur}" min="0" max="120"><br>`;
          });
          
          cycleDiv.innerHTML = `
            <h3>${cycle.name}</h3>
            <input type="hidden" id="cycle${index}_name" value="${cycle.name}">
            <label for="cycle${index}_enabled">Enabled:</label>
            <select id="cycle${index}_enabled">
              <option value="true" ${cycle.enabled ? 'selected' : ''}>Yes</option>
              <option value="false" ${!cycle.enabled ? 'selected' : ''}>No</option>
            </select><br>
            <label for="cycle${index}_starthour">Start Hour:</label>
            <input type="number" id="cycle${index}_starthour" value="${cycle.startTime.hour}" min="0" max="23"><br>
            <label for="cycle${index}_startminute">Start Minute:</label>
            <input type="number" id="cycle${index}_startminute" value="${cycle.startTime.minute}" min="0" max="59"><br>
            <label for="cycle${index}_delay">Inter-Zone Delay (min):</label>
            <input type="number" id="cycle${index}_delay" value="${cycle.interZoneDelay}" min="0" max="60"><br>
            <label>Days Active:</label><div class="days" id="cycle${index}_days" data-days="${cycle.daysActive}">${daysHtml}</div>
            <div class="zones">${zonesHtml}</div>
            <button onclick="saveCycle(${index})">Save ${cycle.name}</button>
            <button onclick="runCycle(${index})">Run ${cycle.name} Now</button>
          `;
          cyclesDiv.appendChild(cycleDiv);
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
    const zoneDurations = [];
    for(let i=0; i<${ZONE_COUNT}; i++) { // ZONE_COUNT will be replaced by its value in the HTML
        zoneDurations.push(parseInt(document.getElementById(`cycle${index}_zone${i}`).value));
    }
    const data = {
      cycleIndex: index,
      name: document.getElementById(`cycle${index}_name`).value,
      enabled: document.getElementById(`cycle${index}_enabled`).value === 'true',
      startTime: {
        hour: parseInt(document.getElementById(`cycle${index}_starthour`).value),
        minute: parseInt(document.getElementById(`cycle${index}_startminute`).value)
      },
      interZoneDelay: parseInt(document.getElementById(`cycle${index}_delay`).value),
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
    const duration = parseInt(document.getElementById('manualDuration').value);
    if (zoneId < 1) {
        showMessage('Please select a zone.', 'error');
        return;
    }
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

  // Initial data fetch
  window.onload = () => {
    fetchStatus();
    fetchCycles();
  };
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

void handleRoot() {
    server.send_P(200, "text/html", index_html);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

void handleGetStatus() {
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
        relayObj["name"] = relayLabels[i];
        relayObj["state"] = relayStates[i];
    }
    doc["currentOperation"] = currentOperation;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleGetTime() {
    StaticJsonDocument<256> doc;
    doc["year"] = currentDateTime.year;
    doc["month"] = currentDateTime.month;
    doc["day"] = currentDateTime.day;
    doc["hour"] = currentDateTime.hour;
    doc["minute"] = currentDateTime.minute;
    doc["second"] = currentDateTime.second;
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleSetTime() {
    if (server.hasArg("plain")) { // JSON body
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }
        currentDateTime.year = doc["year"] | currentDateTime.year;
        currentDateTime.month = doc["month"] | currentDateTime.month;
        currentDateTime.day = doc["day"] | currentDateTime.day;
        currentDateTime.hour = doc["hour"] | currentDateTime.hour;
        currentDateTime.minute = doc["minute"] | currentDateTime.minute;
        currentDateTime.second = 0; 
        server.send(200, "application/json", "{\"success\":true, \"message\":\"Time updated via JSON\"}");
    } else if (server.hasArg("year") && server.hasArg("month") && 
               server.hasArg("day") && server.hasArg("hour") && 
               server.hasArg("minute")) { // Form data (less likely with current JS)
        currentDateTime.year = server.arg("year").toInt();
        currentDateTime.month = server.arg("month").toInt();
        currentDateTime.day = server.arg("day").toInt();
        currentDateTime.hour = server.arg("hour").toInt();
        currentDateTime.minute = server.arg("minute").toInt();
        currentDateTime.second = 0;
        server.send(200, "application/json", "{\"success\":true, \"message\":\"Time updated\"}");
    } else {
        server.send(400, "application/json", "{\"success\":false, \"message\":\"Missing time parameters\"}");
    }
}

void handleGetCycles() {
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
    server.send(200, "application/json", output);
}

void handleSetCycle() {
    if (server.method() == HTTP_POST && server.hasArg("plain")) { 
        StaticJsonDocument<512> doc; 
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        int cycleIndex = doc["cycleIndex"];
        if (cycleIndex < 0 || cycleIndex >= NUM_CYCLES) {
            server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
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
        server.send(200, "application/json", "{\"success\":true, \"message\":\"Cycle updated\"}");
    } else {
        server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid request\"}");
    }
}

void handleManualControl() {
    if (server.method() == HTTP_POST && server.hasArg("plain")) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
            return;
        }

        const char* action = doc["action"];
        if (strcmp(action, "start_zone") == 0) {
            int zone = doc["zone"]; 
            int duration = doc["duration"]; 
            if (zone >= 1 && zone <= ZONE_COUNT && duration > 0 && duration <= 120) { 
                selectedManualDuration = duration; 
                startManualZone(zone); 
                server.send(200, "application/json", "{\"success\":true, \"message\":\"Manual zone start requested for " + String(duration) + " minutes\"}");
            } else {
                server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid zone or duration (1-120 min)\"}");
            }
        } else if (strcmp(action, "start_cycle") == 0) {
            int cycleIdx = doc["cycle"];
            if (cycleIdx >= 0 && cycleIdx < NUM_CYCLES) {
                startCycleRun(cycleIdx, OP_MANUAL_CYCLE);
                server.send(200, "application/json", "{\"success\":true, \"message\":\"Cycle start requested\"}");
            } else {
                server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid cycle index\"}");
            }
        } else if (strcmp(action, "stop_all") == 0) {
            stopAllActivity();
            server.send(200, "application/json", "{\"success\":true, \"message\":\"Stop all requested\"}");
        } else {
            server.send(400, "application/json", "{\"success\":false, \"message\":\"Unknown action\"}");
        }
    } else {
        server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid request\"}");
    }
}

void initWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/time", HTTP_GET, handleGetTime); 
    server.on("/api/time", HTTP_POST, handleSetTime); 
    server.on("/api/cycles", HTTP_GET, handleGetCycles); 
    server.on("/api/cycle", HTTP_POST, handleSetCycle);  
    server.on("/api/manual", HTTP_POST, handleManualControl); 
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started using WebServer library");
}


void initWebServer() {
    // Route for root / web page
    server.on("/", HTTP_GET, handleRoot);

    // API routes
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/time", HTTP_GET, handleGetTime); // Get current time
    server.on("/api/time", HTTP_POST, handleSetTime); // Set time (supports form-data or JSON body)
    server.on("/api/cycles", HTTP_GET, handleGetCycles); // Get all cycle configs
    server.on("/api/cycle", HTTP_POST, handleSetCycle);  // Set a specific cycle config (JSON body)
    server.on("/api/manual", HTTP_POST, handleManualControl); // Manual zone/cycle start/stop (JSON body)


    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println("HTTP server started");
}
