#pragma once

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Motor & Propeller Test Stand</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(18, 25, 43, 0.75);
      --card-border: rgba(65, 85, 125, 0.35);
      --card-hover: rgba(70, 95, 145, 0.45);
      --accent: #00f2fe;
      --accent-gradient: linear-gradient(135deg, #00f2fe 0%, #4facfe 100%);
      --text: #e2e8f0;
      --text-muted: #8492a6;
      --danger: #ff0055;
      --danger-gradient: linear-gradient(135deg, #ff0055 0%, #ff5252 100%);
      --success: #10b981;
      --warning: #f59e0b;
      --font: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Inter", sans-serif;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; font-family: var(--font); }
    body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 20px; overflow-x: hidden; }

    /* Layout Header */
    .top-bar {
      display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center;
      gap: 15px; margin-bottom: 24px; padding-bottom: 16px; border-bottom: 1px solid var(--card-border);
    }
    .brand { display: flex; align-items: center; gap: 12px; }
    .brand-icon {
      width: 42px; height: 42px; border-radius: 10px; background: var(--accent-gradient);
      display: flex; align-items: center; justify-content: center; font-size: 22px; box-shadow: 0 0 15px rgba(0,242,254,0.4);
    }
    .brand-title h1 { font-size: 1.4rem; font-weight: 700; color: #fff; letter-spacing: -0.5px; }
    .brand-title p { font-size: 0.8rem; color: var(--text-muted); }

    .header-actions { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }
    .status-badge {
      display: flex; align-items: center; gap: 6px; padding: 6px 12px; border-radius: 20px;
      font-size: 0.8rem; background: rgba(255,255,255,0.05); border: 1px solid var(--card-border);
    }
    .status-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--success); }
    .status-dot.danger { background: var(--danger); box-shadow: 0 0 8px var(--danger); }
    .status-dot.warn { background: var(--warning); box-shadow: 0 0 8px var(--warning); }

    /* Prominent ABORT Button */
    .btn-abort {
      background: var(--danger-gradient); color: #fff; border: none; padding: 12px 28px;
      font-size: 1.05rem; font-weight: 800; letter-spacing: 0.5px; border-radius: 8px;
      cursor: pointer; box-shadow: 0 0 25px rgba(255,0,85,0.5); transition: all 0.2s;
      animation: pulse-danger 2s infinite ease-in-out;
    }
    .btn-abort:hover { transform: translateY(-2px); box-shadow: 0 0 35px rgba(255,0,85,0.8); }
    .btn-abort:active { transform: translateY(1px); }

    @keyframes pulse-danger {
      0%, 100% { box-shadow: 0 0 15px rgba(255,0,85,0.4); }
      50% { box-shadow: 0 0 30px rgba(255,0,85,0.8); }
    }

    /* Primary Grid */
    .main-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 16px; margin-bottom: 24px; }
    .card {
      background: var(--card-bg); backdrop-filter: blur(12px); border: 1px solid var(--card-border);
      border-radius: 12px; padding: 18px; transition: all 0.2s;
    }
    .card:hover { border-color: var(--card-hover); }
    .card-title { font-size: 0.8rem; text-transform: uppercase; letter-spacing: 1px; color: var(--text-muted); margin-bottom: 8px; }
    .card-value { font-size: 1.9rem; font-weight: 700; color: #fff; line-height: 1.1; }
    .card-unit { font-size: 0.85rem; font-weight: 400; color: var(--text-muted); margin-left: 4px; }
    .card-sub { font-size: 0.75rem; color: var(--text-muted); margin-top: 6px; }

    /* Middle Row: Config & Live Progress */
    .section-row { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 24px; }
    @media (max-width: 900px) { .section-row { grid-template-columns: 1fr; } }

    .panel {
      background: var(--card-bg); backdrop-filter: blur(12px); border: 1px solid var(--card-border);
      border-radius: 12px; padding: 20px;
    }
    .panel-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
    .panel-header h2 { font-size: 1.1rem; font-weight: 600; color: #fff; }

    /* Form Fields */
    .form-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }
    .form-group { display: flex; flex-direction: column; gap: 6px; }
    .form-group label { font-size: 0.8rem; color: var(--text-muted); font-weight: 500; }
    .form-group input {
      background: rgba(12, 17, 30, 0.8); border: 1px solid var(--card-border); border-radius: 8px;
      padding: 10px 12px; color: #fff; font-size: 0.95rem; outline: none; transition: border 0.2s;
    }
    .form-group input:focus { border-color: var(--accent); }
    .form-footer { margin-top: 18px; display: flex; flex-wrap: wrap; gap: 12px; align-items: center; justify-content: space-between; }

    .btn {
      padding: 10px 22px; border-radius: 8px; font-size: 0.9rem; font-weight: 600; border: none;
      cursor: pointer; transition: all 0.2s; display: inline-flex; align-items: center; gap: 8px;
    }
    .btn-primary { background: var(--accent-gradient); color: #051329; box-shadow: 0 0 15px rgba(0,242,254,0.3); }
    .btn-primary:hover { box-shadow: 0 0 25px rgba(0,242,254,0.5); transform: translateY(-1px); }
    .btn-primary:disabled { opacity: 0.4; cursor: not-allowed; transform: none; box-shadow: none; }
    .btn-secondary { background: rgba(255,255,255,0.08); color: var(--text); border: 1px solid var(--card-border); }
    .btn-secondary:hover { background: rgba(255,255,255,0.12); }

    /* Live Execution Progress Card */
    .progress-box { display: flex; flex-direction: column; gap: 16px; justify-content: center; height: 100%; }
    .state-banner {
      padding: 12px 16px; border-radius: 8px; background: rgba(0, 242, 254, 0.1); border: 1px solid rgba(0, 242, 254, 0.25);
      display: flex; justify-content: space-between; align-items: center;
    }
    .state-name { font-weight: 700; color: var(--accent); font-size: 1rem; }
    .state-step { font-size: 0.85rem; color: var(--text); }
    .progress-track {
      width: 100%; height: 14px; background: rgba(255,255,255,0.06); border-radius: 10px; overflow: hidden;
      border: 1px solid var(--card-border);
    }
    .progress-fill {
      height: 100%; width: 0%; background: var(--accent-gradient); transition: width 0.3s ease-out;
      box-shadow: 0 0 12px rgba(0,242,254,0.6);
    }
    .progress-meta { display: flex; justify-content: space-between; font-size: 0.85rem; color: var(--text-muted); }

    /* Interactive Graph Card */
    .chart-panel { margin-bottom: 24px; }
    .chart-controls { display: flex; gap: 8px; align-items: center; }
    .chart-select {
      background: rgba(12, 17, 30, 0.8); border: 1px solid var(--card-border); color: #fff;
      padding: 6px 12px; border-radius: 6px; font-size: 0.85rem; outline: none;
    }
    .svg-container { width: 100%; height: 380px; position: relative; margin-top: 14px; overflow: hidden; border-radius: 8px; }
    svg { width: 100%; height: 100%; overflow: hidden; }
    .chart-grid line { stroke: rgba(255,255,255,0.08); stroke-dasharray: 4; }
    .chart-axis text { fill: var(--text-muted); font-size: 11px; }
    .chart-line { fill: none; stroke: url(#chartGradient); stroke-width: 3.5; filter: drop-shadow(0 0 8px rgba(0,242,254,0.4)); }
    .chart-point { fill: #00f2fe; stroke: #0c111e; stroke-width: 2.5; cursor: pointer; transition: r 0.2s; }
    .chart-point:hover { r: 7; fill: #fff; }
    .chart-error-bar { stroke: rgba(255, 183, 3, 0.7); stroke-width: 1.5; }
    .chart-error-whisker { stroke: rgba(255, 183, 3, 0.9); stroke-width: 1.5; }

    /* Floating Tooltip */
    #chartTooltip {
      position: absolute; display: none; background: rgba(13, 20, 36, 0.95); backdrop-filter: blur(8px);
      border: 1px solid var(--accent); border-radius: 8px; padding: 10px 14px; font-size: 0.8rem;
      pointer-events: none; z-index: 100; box-shadow: 0 0 20px rgba(0,0,0,0.8); min-width: 170px;
    }
    #chartTooltip strong { color: var(--accent); display: block; margin-bottom: 4px; font-size: 0.9rem; }
    #chartTooltip div { margin: 2px 0; color: #cbd5e1; }

    /* Results Table */
    .table-container { width: 100%; overflow-x: auto; margin-top: 14px; }
    table { width: 100%; border-collapse: collapse; font-size: 0.85rem; text-align: left; }
    th {
      background: rgba(255,255,255,0.03); color: var(--text-muted); padding: 10px 12px;
      border-bottom: 1px solid var(--card-border); font-weight: 600; text-transform: uppercase; font-size: 0.75rem;
    }
    td { padding: 10px 12px; border-bottom: 1px solid rgba(255,255,255,0.04); color: var(--text); }
    tr:hover td { background: rgba(255,255,255,0.02); }

    .toast {
      position: fixed; bottom: 20px; right: 20px; padding: 12px 20px; border-radius: 8px;
      font-size: 0.9rem; z-index: 1000; display: none; box-shadow: 0 4px 20px rgba(0,0,0,0.5);
    }
    .toast.success { background: #065f46; color: #34d399; border: 1px solid #059669; }
    .toast.error { background: #881337; color: #fda4af; border: 1px solid #be123c; }
  </style>
</head>
<body>

  <!-- Top Navigation / Status / ABORT Button -->
  <header class="top-bar">
    <div class="brand">
      <div class="brand-icon">&#x2699;</div>
      <div class="brand-title">
        <h1>UglySpinner Propeller Test Stand</h1>
        <p>LILYGO TTGO T7 V1.3 • SEQURE 130A AM32 • HX711 UART</p>
      </div>
    </div>

    <div class="header-actions">
      <div class="status-badge" id="wifiBadge">
        <div class="status-dot" id="wifiDot"></div>
        <span id="wifiLabel">Connecting Wi-Fi...</span>
      </div>
      <div class="status-badge" id="tlmBadge">
        <div class="status-dot warn" id="tlmDot"></div>
        <span id="tlmLabel">ESC Telemetry: Waiting</span>
      </div>
      <div class="status-badge" id="lcBadge">
        <div class="status-dot warn" id="lcDot"></div>
        <span id="lcLabel">Load Cell: Init</span>
      </div>

      <button class="btn btn-secondary" onclick="tareLoadCell()" title="Zero out load cell sensor">
        &#x21BA; Tare Scale
      </button>

      <button class="btn btn-secondary" onclick="calibrateLoadCell()" title="Calibrate scale with known test weight">
        &#x2696; Calibrate
      </button>

      <button class="btn-abort" id="btnAbort" onclick="abortTest()">
        &#x26A0; ABORT / STOP
      </button>
    </div>
  </header>

  <!-- Live Telemetry KPI Cards -->
  <main>
    <section class="main-grid">
      <div class="card">
        <div class="card-title">Commanded Throttle</div>
        <div class="card-value" id="valThrottle">0<span class="card-unit">/ 2047</span></div>
        <div class="card-sub" id="valThrottlePct">0.0% Power (DShot600)</div>
      </div>

      <div class="card">
        <div class="card-title">Measured Thrust / Load</div>
        <div class="card-value" id="valLoad">0.0<span class="card-unit">g</span></div>
        <div class="card-sub" id="valThrustN">0.000 N</div>
      </div>

      <div class="card">
        <div class="card-title">Motor Speed</div>
        <div class="card-value" id="valRpm">0<span class="card-unit">RPM</span></div>
        <div class="card-sub" id="valErpm">eRPM: 0 (14 Poles)</div>
      </div>

      <div class="card">
        <div class="card-title">ESC Current & Power</div>
        <div class="card-value" id="valCurrent">0.0<span class="card-unit">A</span></div>
        <div class="card-sub" id="valPower">0.0 W • 0 mAh</div>
      </div>

      <div class="card">
        <div class="card-title">Battery & Temperature</div>
        <div class="card-value" id="valVoltage">0.0<span class="card-unit">V</span></div>
        <div class="card-sub" id="valTemp">ESC Temp: 0°C</div>
      </div>
    </section>

    <!-- Test Configuration & Live Sequence Progress -->
    <section class="section-row">
      <!-- Test Configuration Panel -->
      <div class="panel">
        <div class="panel-header">
          <h2>Test Configuration</h2>
          <span style="font-size: 0.8rem; color: var(--text-muted);" id="cfgSummary">Est: -- steps</span>
        </div>

        <form id="configForm" onsubmit="event.preventDefault(); startTest();">
          <div class="form-grid">
            <div class="form-group">
              <label for="cfgInitThrottle">Initial Throttle (48-2000)</label>
              <input type="number" id="cfgInitThrottle" value="100" min="48" max="2000" required onchange="updateEstimates()">
            </div>
            <div class="form-group">
              <label for="cfgEndThrottle">End Throttle (48-2000)</label>
              <input type="number" id="cfgEndThrottle" value="1200" min="48" max="2000" required onchange="updateEstimates()">
            </div>
            <div class="form-group">
              <label for="cfgStep">Throttle Step (5-500)</label>
              <input type="number" id="cfgStep" value="50" min="5" max="500" required onchange="updateEstimates()">
            </div>
            <div class="form-group">
              <label for="cfgPoles">Motor Poles (Even count)</label>
              <input type="number" id="cfgPoles" value="14" min="2" max="64" step="2" required>
            </div>
            <div class="form-group">
              <label for="cfgStabMs">Stabilization Time (ms)</label>
              <input type="number" id="cfgStabMs" value="2000" min="500" max="20000" step="100" required onchange="updateEstimates()">
            </div>
            <div class="form-group">
              <label for="cfgMeasureMs">Measurement Time (ms)</label>
              <input type="number" id="cfgMeasureMs" value="3000" min="1000" max="30000" step="100" required onchange="updateEstimates()">
            </div>
          </div>

          <div class="form-footer">
            <div id="cfgValidationMsg" style="font-size: 0.8rem; color: var(--danger);"></div>
            <button type="submit" class="btn btn-primary" id="btnStart">
              &#x25B6; START TEST
            </button>
          </div>
        </form>
      </div>

      <!-- Execution Status Panel -->
      <div class="panel">
        <div class="panel-header">
          <h2>Execution Progress</h2>
          <span class="status-badge" id="stateBadge">IDLE</span>
        </div>

        <div class="progress-box">
          <div class="state-banner">
            <div>
              <div class="state-name" id="liveStateText">Ready to Test</div>
              <div class="state-step" id="liveStepText">Configure parameters and click Start</div>
            </div>
            <div style="font-size: 1.3rem; font-weight: 700; color: #fff;" id="livePctText">0%</div>
          </div>

          <div>
            <div class="progress-track">
              <div class="progress-fill" id="progressBar"></div>
            </div>
          </div>

          <div class="progress-meta">
            <div>Elapsed: <strong id="lblElapsed">0.0s</strong></div>
            <div>Step Remainder: <strong id="lblStepRem">0.0s</strong></div>
            <div>Total Est: <strong id="lblEstTotal">--</strong></div>
          </div>
        </div>
      </div>
    </section>

    <!-- Interactive Graph Panel -->
    <section class="panel chart-panel">
      <div class="panel-header">
        <h2>Performance Curves & Dynamic Analysis</h2>
        <div class="chart-controls">
          <label style="font-size: 0.8rem; color: var(--text-muted);">Y-Axis:</label>
          <select class="chart-select" id="chartYAxis" onchange="renderChart()">
            <option value="load">Thrust / Load (grams)</option>
            <option value="rpm">Mechanical RPM</option>
            <option value="current">Current (Amps)</option>
            <option value="efficiency">Efficiency (g / Watt)</option>
          </select>
          <button class="btn btn-secondary" style="padding: 6px 14px; font-size: 0.8rem;" onclick="downloadJson()">
            &#x2B07; Download JSON
          </button>
        </div>
      </div>

      <div class="svg-container" id="chartWrapper">
        <div id="chartTooltip"></div>
        <svg id="chartSvg" viewBox="0 0 900 360">
          <defs>
            <linearGradient id="chartGradient" x1="0%" y1="0%" x2="100%" y2="0%">
              <stop offset="0%" stop-color="#00f2fe" />
              <stop offset="100%" stop-color="#4facfe" />
            </linearGradient>
            <linearGradient id="areaGradient" x1="0%" y1="0%" x2="0%" y2="100%">
              <stop offset="0%" stop-color="rgba(0, 242, 254, 0.25)" />
              <stop offset="100%" stop-color="rgba(0, 242, 254, 0.0)" />
            </linearGradient>
          </defs>
          <g id="chartGrid" class="chart-grid"></g>
          <g id="chartAxes" class="chart-axis"></g>
          <path id="chartArea" fill="url(#areaGradient)" d="" />
          <path id="chartLine" class="chart-line" d="" />
          <g id="chartErrorBars"></g>
          <g id="chartPoints"></g>
        </svg>
      </div>
    </section>

    <!-- Comprehensive Results Table -->
    <section class="panel">
      <div class="panel-header">
        <h2>Tabular Test Results</h2>
        <button class="btn btn-secondary" style="padding: 6px 14px; font-size: 0.8rem;" onclick="exportCsv()">
          Export CSV
        </button>
      </div>

      <div class="table-container">
        <table id="resultsTable">
          <thead>
            <tr>
              <th>Step</th>
              <th>Throttle</th>
              <th>Thrust Mean (g)</th>
              <th>Thrust ± StdDev</th>
              <th>Min / Max (g)</th>
              <th>Mech RPM</th>
              <th>eRPM</th>
              <th>Current (A)</th>
              <th>Voltage (V)</th>
              <th>Power (W)</th>
              <th>Efficiency (g/W)</th>
              <th>ESC Temp (°C)</th>
            </tr>
          </thead>
          <tbody id="tableBody">
            <tr><td colspan="12" style="text-align: center; color: var(--text-muted); padding: 24px;">No test data recorded yet. Complete a test sequence to populate results.</td></tr>
          </tbody>
        </table>
      </div>
    </section>
  </main>

  <div class="toast" id="toast"></div>

  <script>
    let testResultsData = null;
    let isTestRunning = false;
    let pollInterval = null;

    // Client-side initialization
    window.addEventListener('DOMContentLoaded', () => {
      updateEstimates();
      startPolling();
      fetchResults();
    });

    function showToast(msg, isSuccess = true) {
      const t = document.getElementById('toast');
      t.textContent = msg;
      t.className = 'toast ' + (isSuccess ? 'success' : 'error');
      t.style.display = 'block';
      setTimeout(() => { t.style.display = 'none'; }, 3500);
    }

    function updateEstimates() {
      const initTh = parseInt(document.getElementById('cfgInitThrottle').value) || 0;
      const endTh = parseInt(document.getElementById('cfgEndThrottle').value) || 0;
      const step = parseInt(document.getElementById('cfgStep').value) || 0;
      const stab = parseInt(document.getElementById('cfgStabMs').value) || 0;
      const meas = parseInt(document.getElementById('cfgMeasureMs').value) || 0;
      const btn = document.getElementById('btnStart');
      const valMsg = document.getElementById('cfgValidationMsg');

      if (initTh >= endTh) {
        valMsg.textContent = "Initial throttle must be strictly less than end throttle.";
        btn.disabled = true;
        return;
      }
      if (step <= 0 || step > (endTh - initTh)) {
        valMsg.textContent = "Step must be > 0 and <= (end - init).";
        btn.disabled = true;
        return;
      }
      valMsg.textContent = "";
      btn.disabled = isTestRunning;

      const totalSteps = Math.floor((endTh - initTh) / step) + 1;
      const totalSec = Math.round((totalSteps * (stab + meas) + 3500) / 1000);
      document.getElementById('cfgSummary').textContent = `${totalSteps} steps • ~${totalSec}s runtime`;
      document.getElementById('lblEstTotal').textContent = `${totalSec}s`;
    }

    // High frequency live status polling (500ms)
    function startPolling() {
      if (pollInterval) clearInterval(pollInterval);
      pollInterval = setInterval(pollStatus, 500);
      pollStatus();
    }

    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) return;
        const data = await res.json();
        updateUI(data);
      } catch (err) {
        console.warn("Poll status error:", err);
      }
    }

    function updateUI(data) {
      isTestRunning = (data.state_str !== "IDLE" && data.state_str !== "COMPLETED" && data.state_str !== "ABORTED" && data.state_str !== "ERROR");
      
      // Update Wi-Fi & Sensor Badges
      const wifiLabel = document.getElementById('wifiLabel');
      wifiLabel.textContent = data.wifi_ip ? `IP: ${data.wifi_ip} (${data.wifi_rssi} dBm)` : "Wi-Fi Connected";
      
      const tlmLabel = document.getElementById('tlmLabel');
      const tlmDot = document.getElementById('tlmDot');
      if (data.tlm_healthy) {
        tlmLabel.textContent = `AM32 Telemetry: Active (${data.tlm_packets} pkts, ${data.voltage_v.toFixed(1)}V)`;
        tlmDot.className = 'status-dot';
      } else if (data.tlm_bytes > 0) {
        tlmLabel.textContent = `AM32 Telemetry: Receiving (${data.tlm_bytes} B, ${data.tlm_crc_err || 0} err)`;
        tlmDot.className = 'status-dot warn';
      } else {
        tlmLabel.textContent = "AM32 Telemetry: No Signal (0 B)";
        tlmDot.className = 'status-dot danger';
      }

      const lcLabel = document.getElementById('lcLabel');
      const lcDot = document.getElementById('lcDot');
      if (data.lc_healthy) {
        lcLabel.textContent = `HX711: Active (${data.lc_bytes || 0} B)`;
        lcDot.className = 'status-dot';
      } else if (data.lc_bytes > 0) {
        lcLabel.textContent = `HX711: Receiving (${data.lc_bytes} B)`;
        lcDot.className = 'status-dot warn';
      } else {
        lcLabel.textContent = "HX711: No Signal (0 B)";
        lcDot.className = 'status-dot danger';
      }

      // Live metrics
      document.getElementById('valThrottle').innerHTML = `${data.throttle} <span class="card-unit">/ 2047</span>`;
      const thPct = data.throttle >= 48 ? (((data.throttle - 48) / (2047 - 48)) * 100).toFixed(1) : "0.0";
      document.getElementById('valThrottlePct').textContent = `${thPct}% Power (${data.dshot_mode || 'DShot600'})`;

      const thrustG = Math.abs(data.load_g);
      document.getElementById('valLoad').innerHTML = `${thrustG.toFixed(1)} <span class="card-unit">g</span>`;
      const thrustN = (thrustG * 0.00980665).toFixed(3);
      document.getElementById('valThrustN').textContent = `${thrustN} N • Raw ADC: ${data.lc_adc || 0}`;

      document.getElementById('valRpm').innerHTML = `${data.rpm} <span class="card-unit">RPM</span>`;
      document.getElementById('valErpm').textContent = `eRPM: ${data.erpm} (${data.motor_poles} Poles)`;

      document.getElementById('valCurrent').innerHTML = `${data.current_a.toFixed(1)} <span class="card-unit">A</span>`;
      const pWatts = (data.current_a * data.voltage_v).toFixed(1);
      document.getElementById('valPower').textContent = `${pWatts} W • ${data.consumption_mah || 0} mAh`;

      document.getElementById('valVoltage').innerHTML = `${data.voltage_v.toFixed(2)} <span class="card-unit">V</span>`;
      document.getElementById('valTemp').textContent = `ESC Temp: ${data.temp_c}°C`;

      // Execution Progress
      document.getElementById('stateBadge').textContent = data.state_str;
      document.getElementById('liveStateText').textContent = data.state_str;
      if (data.total_steps > 0) {
        document.getElementById('liveStepText').textContent = `Step ${data.step_index} of ${data.total_steps} (Throttle: ${data.throttle})`;
      } else {
        document.getElementById('liveStepText').textContent = data.state_str === 'COMPLETED' ? 'Test sequence completed!' : 'Ready for test run';
      }

      const pct = Math.min(100, Math.max(0, data.progress_pct || 0)).toFixed(0);
      document.getElementById('livePctText').textContent = `${pct}%`;
      document.getElementById('progressBar').style.width = `${pct}%`;

      document.getElementById('lblElapsed').textContent = `${(data.elapsed_ms / 1000).toFixed(1)}s`;
      document.getElementById('lblStepRem').textContent = `${(data.step_rem_ms / 1000).toFixed(1)}s`;

      document.getElementById('btnStart').disabled = isTestRunning;

      // When test transitions to COMPLETED, fetch full results once
      if (data.state_str === "COMPLETED" && (!testResultsData || testResultsData.metadata.status !== "COMPLETED")) {
        fetchResults();
      }
    }

    async function startTest() {
      if (isTestRunning) return;
      const payload = {
        initialThrottle: parseInt(document.getElementById('cfgInitThrottle').value),
        endThrottle: parseInt(document.getElementById('cfgEndThrottle').value),
        throttleStep: parseInt(document.getElementById('cfgStep').value),
        stabilizationMs: parseInt(document.getElementById('cfgStabMs').value),
        measurementMs: parseInt(document.getElementById('cfgMeasureMs').value),
        motorPoles: parseInt(document.getElementById('cfgPoles').value)
      };

      try {
        const res = await fetch('/api/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const result = await res.json();
        if (result.success) {
          showToast("Test sequence initiated!");
          isTestRunning = true;
          document.getElementById('btnStart').disabled = true;
        } else {
          showToast(result.error || "Failed to start test", false);
        }
      } catch (err) {
        showToast("Error communicating with ESP32", false);
      }
    }

    async function abortTest() {
      try {
        const res = await fetch('/api/abort', { method: 'POST' });
        const result = await res.json();
        showToast("ABORT TRIGGERED! Throttle forced to ZERO.", false);
        isTestRunning = false;
        document.getElementById('btnStart').disabled = false;
        pollStatus();
      } catch (err) {
        alert("Emergency Abort Sent (Network check)");
      }
    }

    async function tareLoadCell() {
      try {
        const res = await fetch('/api/tare', { method: 'POST' });
        const result = await res.json();
        showToast("Scale zeroed (Tared).");
      } catch (err) {
        showToast("Tare command failed", false);
      }
    }

    async function calibrateLoadCell() {
      const weightStr = prompt("Calibration Instructions:\n1. Click 'Tare Scale' first with no extra weight on stand.\n2. Place a known calibration weight onto the stand.\n\nEnter the known weight in grams:", "500");
      if (!weightStr) return;
      const weight = parseFloat(weightStr);
      if (isNaN(weight) || weight <= 0) {
        showToast("Invalid calibration weight entered.", false);
        return;
      }
      try {
        const res = await fetch('/api/loadcell/calibrate', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ known_weight_g: weight })
        });
        const data = await res.json();
        if (data.success) {
          showToast(`Calibrated! Factor: ${data.cal_factor.toFixed(4)} (Delta: ${data.delta_adc})`);
        } else {
          showToast(data.error || "Calibration failed", false);
        }
      } catch (e) {
        showToast("Calibration request failed", false);
      }
    }

    async function fetchResults() {
      try {
        const res = await fetch('/api/results');
        if (!res.ok) return;
        testResultsData = await res.json();
        renderChart();
        renderTable();
      } catch (err) {
        console.warn("Fetch results failed:", err);
      }
    }

    // Dynamic SVG Chart Renderer
    function renderChart() {
      if (!testResultsData || !testResultsData.points || testResultsData.points.length === 0) return;

      const yKey = document.getElementById('chartYAxis').value;
      const pts = testResultsData.points;
      const svg = document.getElementById('chartSvg');
      const gridG = document.getElementById('chartGrid');
      const axesG = document.getElementById('chartAxes');
      const linePath = document.getElementById('chartLine');
      const areaPath = document.getElementById('chartArea');
      const errorG = document.getElementById('chartErrorBars');
      const pointsG = document.getElementById('chartPoints');

      gridG.innerHTML = '';
      axesG.innerHTML = '';
      errorG.innerHTML = '';
      pointsG.innerHTML = '';

      const padL = 70, padR = 40, padT = 30, padB = 50;
      const w = 900 - padL - padR;
      const h = 360 - padT - padB;

      // Extract values with guaranteed positive magnitudes for physical metrics
      let xVals = pts.map(p => p.throttle);
      let yVals = pts.map(p => {
        if (yKey === 'load') return Math.abs(p.load.mean);
        if (yKey === 'rpm') return Math.max(0, p.rpm);
        if (yKey === 'current') return Math.max(0, p.current_a);
        if (yKey === 'efficiency') {
          const power = p.voltage_v * p.current_a;
          return power > 0.1 ? (Math.abs(p.load.mean) / power) : 0;
        }
        return Math.abs(p.load.mean);
      });

      let minX = Math.min(...xVals), maxX = Math.max(...xVals);
      if (minX === maxX) { minX = Math.max(0, minX - 100); maxX += 100; }

      // Bulletproof Y-axis scaling: guaranteed minY < maxY, no negative inversions
      let minY = 0;
      let maxY = Math.max(...yVals);
      if (maxY <= 0) {
        maxY = 10;
      } else {
        // Round maxY up to a clean, readable scale maximum
        const raw = maxY * 1.15;
        const pow = Math.pow(10, Math.floor(Math.log10(raw)));
        const frac = raw / pow;
        let niceFrac = 10;
        if (frac <= 1.2) niceFrac = 1.2;
        else if (frac <= 1.5) niceFrac = 1.5;
        else if (frac <= 2.0) niceFrac = 2.0;
        else if (frac <= 2.5) niceFrac = 2.5;
        else if (frac <= 3.0) niceFrac = 3.0;
        else if (frac <= 4.0) niceFrac = 4.0;
        else if (frac <= 5.0) niceFrac = 5.0;
        else if (frac <= 6.0) niceFrac = 6.0;
        else if (frac <= 8.0) niceFrac = 8.0;
        maxY = niceFrac * pow;
      }
      if (maxY <= minY) maxY = minY + 10;

      // Clamped coordinate projections ensuring all elements strictly stay within canvas
      const getX = th => {
        const ratio = (maxX > minX) ? ((th - minX) / (maxX - minX)) : 0.5;
        return padL + Math.max(0, Math.min(1, ratio)) * w;
      };

      const getY = val => {
        const span = maxY - minY;
        const ratio = (span > 0) ? ((val - minY) / span) : 0.5;
        const clampedRatio = Math.max(0, Math.min(1, ratio));
        return padT + h - clampedRatio * h;
      };

      // Draw Grid & Axes
      const numXTicks = 6;
      for (let i = 0; i <= numXTicks; i++) {
        const th = Math.round(minX + (i / numXTicks) * (maxX - minX));
        const px = getX(th);
        gridG.innerHTML += `<line x1="${px.toFixed(1)}" y1="${padT}" x2="${px.toFixed(1)}" y2="${padT + h}" />`;
        axesG.innerHTML += `<text x="${px.toFixed(1)}" y="${padT + h + 20}" text-anchor="middle">${th}</text>`;
      }
      axesG.innerHTML += `<text x="${padL + w / 2}" y="${padT + h + 42}" text-anchor="middle" font-weight="600">Commanded Throttle (DShot)</text>`;

      const numYTicks = 5;
      for (let i = 0; i <= numYTicks; i++) {
        const val = minY + (i / numYTicks) * (maxY - minY);
        const py = getY(val);
        gridG.innerHTML += `<line x1="${padL}" y1="${py.toFixed(1)}" x2="${padL + w}" y2="${py.toFixed(1)}" />`;
        const decimals = (yKey === 'efficiency' || yKey === 'current') ? 1 : 0;
        axesG.innerHTML += `<text x="${padL - 10}" y="${(py + 4).toFixed(1)}" text-anchor="end">${val.toFixed(decimals)}</text>`;
      }
      let yLabel = "Thrust / Load (g)";
      if (yKey === 'rpm') yLabel = "Motor Speed (RPM)";
      if (yKey === 'current') yLabel = "Current (Amps)";
      if (yKey === 'efficiency') yLabel = "Efficiency (g / Watt)";
      axesG.innerHTML += `<text x="${-padT - h/2}" y="20" transform="rotate(-90)" text-anchor="middle" font-weight="600">${yLabel}</text>`;

      // Line & Area Path
      let lineD = "";
      pts.forEach((pt, i) => {
        const px = getX(pt.throttle);
        const py = getY(yVals[i]);
        lineD += (i === 0 ? "M" : "L") + ` ${px.toFixed(1)} ${py.toFixed(1)} `;
      });
      linePath.setAttribute('d', lineD);

      const areaD = lineD + ` L ${getX(pts[pts.length - 1].throttle).toFixed(1)} ${padT + h} L ${getX(pts[0].throttle).toFixed(1)} ${padT + h} Z`;
      areaPath.setAttribute('d', areaD);

      // Points and Error Bars
      const tooltip = document.getElementById('chartTooltip');
      pts.forEach((pt, i) => {
        const px = getX(pt.throttle);
        const py = getY(yVals[i]);

        // Draw error bars if plotting load
        if (yKey === 'load') {
          const mean = Math.abs(pt.load.mean);
          const std = Math.abs(pt.load.stddev || 0);
          const yLow = getY(Math.max(0, mean - std)); // bottom whisker (larger SVG Y)
          const yHigh = getY(mean + std);              // top whisker (smaller SVG Y)
          errorG.innerHTML += `
            <line class="chart-error-bar" x1="${px.toFixed(1)}" y1="${yLow.toFixed(1)}" x2="${px.toFixed(1)}" y2="${yHigh.toFixed(1)}" />
            <line class="chart-error-whisker" x1="${(px - 4).toFixed(1)}" y1="${yLow.toFixed(1)}" x2="${(px + 4).toFixed(1)}" y2="${yLow.toFixed(1)}" />
            <line class="chart-error-whisker" x1="${(px - 4).toFixed(1)}" y1="${yHigh.toFixed(1)}" x2="${(px + 4).toFixed(1)}" y2="${yHigh.toFixed(1)}" />
          `;
        }

        const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
        circle.setAttribute('class', 'chart-point');
        circle.setAttribute('cx', px.toFixed(1));
        circle.setAttribute('cy', py.toFixed(1));
        circle.setAttribute('r', '4.5');

        circle.addEventListener('mouseenter', (e) => {
          const power = (pt.voltage_v * pt.current_a).toFixed(1);
          const thrustMean = Math.abs(pt.load.mean);
          const thrustStd = Math.abs(pt.load.stddev || 0);
          const thrustMin = Math.min(Math.abs(pt.load.min), Math.abs(pt.load.max));
          const thrustMax = Math.max(Math.abs(pt.load.min), Math.abs(pt.load.max));
          const eff = power > 0.1 ? (thrustMean / power).toFixed(2) : "0.00";
          tooltip.innerHTML = `
            <strong>Throttle: ${pt.throttle}</strong>
            <div>Thrust: <b>${thrustMean.toFixed(1)} g</b> (±${thrustStd.toFixed(2)})</div>
            <div>Min/Max: ${thrustMin.toFixed(1)} / ${thrustMax.toFixed(1)} g</div>
            <div>RPM: <b>${pt.rpm.toLocaleString()}</b> (eRPM: ${pt.erpm.toLocaleString()})</div>
            <div>Current: <b>${pt.current_a.toFixed(2)} A</b> @ ${pt.voltage_v.toFixed(1)} V</div>
            <div>Electrical Power: <b>${power} W</b></div>
            <div>Efficiency: <b>${eff} g/W</b></div>
            <div>ESC Temp: <b>${pt.temperature_c}°C</b></div>
            <div style="color:#64748b; font-size:10px; margin-top:4px;">${pt.load.samples} samples recorded</div>
          `;
          tooltip.style.display = 'block';
          tooltip.style.left = `${px + 15}px`;
          tooltip.style.top = `${py - 20}px`;
        });

        circle.addEventListener('mouseleave', () => {
          tooltip.style.display = 'none';
        });

        pointsG.appendChild(circle);
      });
    }

    function renderTable() {
      if (!testResultsData || !testResultsData.points) return;
      const tbody = document.getElementById('tableBody');
      tbody.innerHTML = '';

      testResultsData.points.forEach((pt, idx) => {
        const tr = document.createElement('tr');
        const power = (pt.voltage_v * pt.current_a).toFixed(1);
        const thrustMean = Math.abs(pt.load.mean);
        const thrustStd = Math.abs(pt.load.stddev || 0);
        const thrustMin = Math.min(Math.abs(pt.load.min), Math.abs(pt.load.max));
        const thrustMax = Math.max(Math.abs(pt.load.min), Math.abs(pt.load.max));
        const eff = power > 0.1 ? (thrustMean / power).toFixed(2) : "0.00";
        tr.innerHTML = `
          <td>${idx + 1}</td>
          <td><strong>${pt.throttle}</strong></td>
          <td><b>${thrustMean.toFixed(2)}</b></td>
          <td>± ${thrustStd.toFixed(3)}</td>
          <td>${thrustMin.toFixed(1)} / ${thrustMax.toFixed(1)}</td>
          <td>${pt.rpm.toLocaleString()}</td>
          <td>${pt.erpm.toLocaleString()}</td>
          <td>${pt.current_a.toFixed(2)}</td>
          <td>${pt.voltage_v.toFixed(2)}</td>
          <td>${power}</td>
          <td><strong style="color:var(--accent);">${eff}</strong></td>
          <td>${pt.temperature_c}°C</td>
        `;
        tbody.appendChild(tr);
      });
    }

    function downloadJson() {
      window.location.href = '/api/export.json';
    }

    function exportCsv() {
      if (!testResultsData || !testResultsData.points) {
        showToast("No data to export", false);
        return;
      }
      let csv = "Step,Throttle,Thrust_Mean_g,Thrust_StdDev_g,Thrust_Min_g,Thrust_Max_g,Samples,RPM,eRPM,Current_A,Voltage_V,Power_W,Efficiency_gpw,Temp_C\n";
      testResultsData.points.forEach((pt, i) => {
        const power = (pt.voltage_v * pt.current_a).toFixed(2);
        const thrustMean = Math.abs(pt.load.mean);
        const thrustStd = Math.abs(pt.load.stddev || 0);
        const thrustMin = Math.min(Math.abs(pt.load.min), Math.abs(pt.load.max));
        const thrustMax = Math.max(Math.abs(pt.load.min), Math.abs(pt.load.max));
        const eff = power > 0.1 ? (thrustMean / power).toFixed(3) : "0.000";
        csv += `${i+1},${pt.throttle},${thrustMean.toFixed(3)},${thrustStd.toFixed(4)},${thrustMin.toFixed(3)},${thrustMax.toFixed(3)},${pt.load.samples},${pt.rpm},${pt.erpm},${pt.current_a.toFixed(3)},${pt.voltage_v.toFixed(3)},${power},${eff},${pt.temperature_c}\n`;
      });
      const blob = new Blob([csv], { type: 'text/csv' });
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `propeller_test_${Date.now()}.csv`;
      a.click();
    }
  </script>
</body>
</html>
)rawliteral";
