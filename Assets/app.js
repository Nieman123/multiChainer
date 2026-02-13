(function () {
  const MIN_FREQ = 20;
  const MAX_FREQ = 20000;
  const DB_MIN = -120;
  const DB_MAX = 6;

  const state = {
    params: {},
    fftBins: [],
    appliedLowMidHz: 180,
    appliedMidHighHz: 2500,
    midiActivityCounter: 0,
    availableMidiChannels: []
  };

  const controlSetters = new Map();
  const curveEditors = [];
  const midiChannelSelectBindings = [];

  let midiBlinkUntil = 0;

  const backend = window.__JUCE__ && window.__JUCE__.backend ? window.__JUCE__.backend : null;

  const statusDot = document.getElementById("backendStatusDot");
  const statusText = document.getElementById("backendStatusText");
  const midiInputDot = document.getElementById("midiInputDot");
  const midiInputText = document.getElementById("midiInputText");

  const spectrumCanvas = document.getElementById("spectrumCanvas");
  const spectrumCtx = spectrumCanvas.getContext("2d");

  const crossoverOverlay = document.getElementById("crossoverOverlay");
  const crossoverLines = Array.from(document.querySelectorAll(".crossover-line"));

  const bandTemplate = document.getElementById("bandTemplate");
  const bandGrid = document.getElementById("bandGrid");

  const bandNames = ["Low Band", "Mid Band", "High Band"];

  function bandParamId(bandIndex, suffix) {
    return `band${bandIndex + 1}.${suffix}`;
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function lerp(min, max, t) {
    return min + (max - min) * t;
  }

  function invLerp(min, max, value) {
    if (max === min) {
      return 0;
    }
    return (value - min) / (max - min);
  }

  function quantize(value, min, step) {
    if (!step || step <= 0) {
      return value;
    }

    const snapped = Math.round((value - min) / step) * step + min;
    const precision = Math.max(0, Math.ceil(-Math.log10(step)) + 1);
    return Number(snapped.toFixed(precision));
  }

  function defaultMidiChannels() {
    return Array.from({ length: 16 }, (_, index) => {
      const channel = index + 1;
      return { value: channel, name: `DAW Ch ${channel}` };
    });
  }

  function currentMidiChannels() {
    return state.availableMidiChannels.length > 0 ? state.availableMidiChannels : defaultMidiChannels();
  }

  function initDefaultParams() {
    state.params["crossover.f1"] = 180;
    state.params["crossover.f2"] = 2500;

    for (let band = 0; band < 3; band += 1) {
      state.params[bandParamId(band, "midiChannel")] = 0;
      state.params[bandParamId(band, "depthDb")] = 12;
      state.params[bandParamId(band, "delayMs")] = 0;
      state.params[bandParamId(band, "attackMs")] = 20;
      state.params[bandParamId(band, "holdMs")] = 30;
      state.params[bandParamId(band, "releaseMs")] = 180;
      state.params[bandParamId(band, "curveShape")] = 1;
      state.params[bandParamId(band, "smoothing")] = 0.2;
    }
  }

  function updateBackendStatus(online) {
    statusDot.classList.toggle("online", online);
    statusText.textContent = online ? "Backend connected" : "Backend unavailable (preview mode)";
  }

  function updateMidiIndicator() {
    const now = performance.now();
    const active = now <= midiBlinkUntil;

    midiInputDot.classList.toggle("active", active);
    midiInputText.textContent = active ? "MIDI input detected" : "MIDI idle";
  }

  function freqToNorm(freq) {
    const clamped = clamp(freq, MIN_FREQ, MAX_FREQ);
    const minL = Math.log10(MIN_FREQ);
    const maxL = Math.log10(MAX_FREQ);
    const valueL = Math.log10(clamped);
    return (valueL - minL) / (maxL - minL);
  }

  function normToFreq(norm) {
    const n = clamp(norm, 0, 1);
    const minL = Math.log10(MIN_FREQ);
    const maxL = Math.log10(MAX_FREQ);
    return Math.pow(10, lerp(minL, maxL, n));
  }

  function formatFreq(value) {
    if (value >= 1000) {
      return `${(value / 1000).toFixed(2)} kHz`;
    }

    return `${Math.round(value)} Hz`;
  }

  function emitParamChange(parameterID, value) {
    const numericValue = Number(value);
    state.params[parameterID] = numericValue;

    if (backend) {
      backend.emitEvent("paramChange", {
        id: parameterID,
        value: numericValue
      });
    }
  }

  function emitParamBatch(updates) {
    updates.forEach((update) => {
      state.params[update.id] = Number(update.value);
    });

    if (backend) {
      backend.emitEvent("paramChange", {
        updates
      });
    }
  }

  function registerSetter(paramID, setter) {
    controlSetters.set(paramID, setter);
  }

  function refreshMidiChannelSelectOptions(binding) {
    const { select, paramID } = binding;
    const channels = currentMidiChannels();

    const selectedValue = String(Math.round(Number(state.params[paramID] ?? 0)));

    select.innerHTML = "";

    const omniOption = document.createElement("option");
    omniOption.value = "0";
    omniOption.textContent = "Omni";
    select.appendChild(omniOption);

    channels.forEach((channel) => {
      const option = document.createElement("option");
      option.value = String(channel.value);
      option.textContent = channel.name;
      select.appendChild(option);
    });

    const hasValueOption = Array.from(select.options).some((option) => option.value === selectedValue);
    select.value = hasValueOption ? selectedValue : "0";
  }

  function refreshAllMidiChannelSelectOptions() {
    midiChannelSelectBindings.forEach((binding) => {
      refreshMidiChannelSelectOptions(binding);
    });
  }

  function createMidiChannelSelect(select, paramID) {
    const binding = { select, paramID };
    midiChannelSelectBindings.push(binding);

    refreshMidiChannelSelectOptions(binding);

    select.addEventListener("change", () => {
      emitParamChange(paramID, Number(select.value));
    });

    registerSetter(paramID, (value) => {
      state.params[paramID] = Number(value);
      refreshMidiChannelSelectOptions(binding);
    });
  }

  function createKnob(parent, label, paramID, config) {
    const wrap = document.createElement("div");
    wrap.className = "knob-wrap";

    const head = document.createElement("div");
    head.className = "knob-head";

    const name = document.createElement("span");
    name.className = "knob-name";
    name.textContent = label;

    const valueLabel = document.createElement("span");
    valueLabel.className = "knob-value";

    head.appendChild(name);
    head.appendChild(valueLabel);

    const hit = document.createElement("div");
    hit.className = "knob-hit";

    const knob = document.createElement("div");
    knob.className = "knob";

    const indicator = document.createElement("div");
    indicator.className = "knob-indicator";

    knob.appendChild(indicator);
    hit.appendChild(knob);

    wrap.appendChild(head);
    wrap.appendChild(hit);
    parent.appendChild(wrap);

    let dragStartY = 0;
    let dragStartValue = 0;
    let dragging = false;

    function setVisual(value) {
      const clampedValue = clamp(Number(value), config.min, config.max);
      const norm = invLerp(config.min, config.max, clampedValue);
      const angle = lerp(-140, 140, norm);

      knob.style.setProperty("--knob-angle", `${angle}deg`);
      valueLabel.textContent = config.format(clampedValue);
    }

    function applyValue(value, shouldEmit) {
      const clamped = clamp(Number(value), config.min, config.max);
      const snapped = quantize(clamped, config.min, config.step);

      state.params[paramID] = snapped;
      setVisual(snapped);

      if (shouldEmit) {
        emitParamChange(paramID, snapped);
      }

      redrawCurves();
    }

    function updateFromDelta(deltaY) {
      const range = config.max - config.min;
      const sensitivity = config.sensitivity ?? range / 220;
      const nextValue = dragStartValue + deltaY * sensitivity;
      applyValue(nextValue, true);
    }

    hit.addEventListener("pointerdown", (event) => {
      dragging = true;
      dragStartY = event.clientY;
      dragStartValue = Number(state.params[paramID] ?? config.defaultValue);
      hit.setPointerCapture(event.pointerId);
      event.preventDefault();
    });

    hit.addEventListener("pointermove", (event) => {
      if (!dragging) {
        return;
      }

      const deltaY = dragStartY - event.clientY;
      updateFromDelta(deltaY);
    });

    hit.addEventListener("pointerup", () => {
      dragging = false;
    });

    hit.addEventListener("pointercancel", () => {
      dragging = false;
    });

    hit.addEventListener("wheel", (event) => {
      event.preventDefault();
      const current = Number(state.params[paramID] ?? config.defaultValue);
      const wheelStep = config.step * (event.shiftKey ? 0.25 : 1.0);
      const next = current - Math.sign(event.deltaY) * wheelStep;
      applyValue(next, true);
    }, { passive: false });

    registerSetter(paramID, (value) => {
      applyValue(value, false);
    });

    controlSetters.get(paramID)(state.params[paramID] ?? config.defaultValue);
  }

  function createCurveEditor(canvas, bandIndex) {
    const ctx = canvas.getContext("2d");
    let dragging = false;

    function getParams() {
      return {
        attack: Number(state.params[bandParamId(bandIndex, "attackMs")]),
        hold: Number(state.params[bandParamId(bandIndex, "holdMs")]),
        release: Number(state.params[bandParamId(bandIndex, "releaseMs")]),
        curve: Number(state.params[bandParamId(bandIndex, "curveShape")])
      };
    }

    function getHandle(params) {
      const xNorm = lerp(0.2, 0.95, invLerp(30, 3000, params.release));
      const yNorm = lerp(0.9, 0.1, invLerp(0.1, 10, params.curve));
      return { xNorm, yNorm };
    }

    function canvasCoordsFromEvent(event) {
      const rect = canvas.getBoundingClientRect();
      return {
        x: event.clientX - rect.left,
        y: event.clientY - rect.top,
        width: rect.width,
        height: rect.height
      };
    }

    function draw() {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(1, Math.floor(rect.width * dpr));
      const height = Math.max(1, Math.floor(rect.height * dpr));

      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }

      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, rect.width, rect.height);

      const params = getParams();
      const handle = getHandle(params);

      const w = rect.width;
      const h = rect.height;
      const pad = 10;

      ctx.strokeStyle = "rgba(255,255,255,0.12)";
      ctx.lineWidth = 1;
      for (let i = 1; i < 4; i += 1) {
        const y = pad + ((h - pad * 2) * i) / 4;
        ctx.beginPath();
        ctx.moveTo(pad, y);
        ctx.lineTo(w - pad, y);
        ctx.stroke();
      }

      const attackNorm = clamp(params.attack / 1000, 0.02, 0.45);
      const holdNorm = clamp(params.hold / 1000, 0, 0.3);
      const releaseNorm = clamp(params.release / 3000, 0.05, 0.75);

      const x0 = pad;
      const x1 = pad + attackNorm * (w - pad * 2);
      const x2 = x1 + holdNorm * (w - pad * 2);
      const x3 = Math.min(w - pad, x2 + releaseNorm * (w - pad * 2));

      const yFloor = h - pad;
      const yPeak = pad;

      const handleX = pad + handle.xNorm * (w - pad * 2);
      const handleY = pad + handle.yNorm * (h - pad * 2);

      ctx.strokeStyle = "rgba(39,193,168,0.95)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x0, yFloor);
      ctx.lineTo(x1, yPeak);
      ctx.lineTo(x2, yPeak);
      ctx.quadraticCurveTo(handleX, handleY, x3, yFloor);
      ctx.lineTo(w - pad, yFloor);
      ctx.stroke();

      ctx.fillStyle = "rgba(255,188,81,0.95)";
      ctx.beginPath();
      ctx.arc(handleX, handleY, 5, 0, Math.PI * 2);
      ctx.fill();
    }

    function updateFromPointer(event) {
      const { x, y, width, height } = canvasCoordsFromEvent(event);
      const xNorm = clamp(x / width, 0.2, 0.95);
      const yNorm = clamp(y / height, 0.1, 0.9);

      const releaseMs = lerp(30, 3000, invLerp(0.2, 0.95, xNorm));
      const curveShape = lerp(0.1, 10, invLerp(0.9, 0.1, yNorm));

      emitParamBatch([
        { id: bandParamId(bandIndex, "releaseMs"), value: releaseMs },
        { id: bandParamId(bandIndex, "curveShape"), value: curveShape }
      ]);

      redrawCurves();
      refreshBoundControls([bandParamId(bandIndex, "releaseMs"), bandParamId(bandIndex, "curveShape")]);
    }

    canvas.addEventListener("pointerdown", (event) => {
      dragging = true;
      canvas.setPointerCapture(event.pointerId);
      updateFromPointer(event);
    });

    canvas.addEventListener("pointermove", (event) => {
      if (!dragging) {
        return;
      }
      updateFromPointer(event);
    });

    canvas.addEventListener("pointerup", () => {
      dragging = false;
    });

    canvas.addEventListener("pointercancel", () => {
      dragging = false;
    });

    return { draw };
  }

  function refreshBoundControls(parameterIDs) {
    parameterIDs.forEach((parameterID) => {
      const setter = controlSetters.get(parameterID);
      if (setter) {
        setter(state.params[parameterID]);
      }
    });
  }

  function redrawCurves() {
    curveEditors.forEach((editor) => editor.draw());
  }

  function buildBandPanels() {
    bandNames.forEach((bandName, bandIndex) => {
      const fragment = bandTemplate.content.cloneNode(true);
      const card = fragment.querySelector(".band-card");

      card.querySelector(".band-title").textContent = bandName;

      const channelSelect = card.querySelector('select[data-param="midiChannel"]');
      const midiChannelID = bandParamId(bandIndex, "midiChannel");
      createMidiChannelSelect(channelSelect, midiChannelID);

      const knobs = card.querySelector('[data-grid="knobs"]');

      createKnob(knobs, "Depth", bandParamId(bandIndex, "depthDb"), {
        min: 0,
        max: 60,
        step: 0.1,
        defaultValue: 12,
        format: (v) => `-${v.toFixed(1)} dB`
      });

      createKnob(knobs, "Delay", bandParamId(bandIndex, "delayMs"), {
        min: 0,
        max: 200,
        step: 0.1,
        defaultValue: 0,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createKnob(knobs, "Attack", bandParamId(bandIndex, "attackMs"), {
        min: 0,
        max: 1000,
        step: 0.1,
        defaultValue: 20,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createKnob(knobs, "Hold", bandParamId(bandIndex, "holdMs"), {
        min: 0,
        max: 1000,
        step: 0.1,
        defaultValue: 30,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createKnob(knobs, "Release", bandParamId(bandIndex, "releaseMs"), {
        min: 1,
        max: 3000,
        step: 0.1,
        defaultValue: 180,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createKnob(knobs, "Curve Shape", bandParamId(bandIndex, "curveShape"), {
        min: 0.1,
        max: 10,
        step: 0.01,
        defaultValue: 1,
        format: (v) => `${v.toFixed(2)}x`
      });

      createKnob(knobs, "Smoothing", bandParamId(bandIndex, "smoothing"), {
        min: 0,
        max: 1,
        step: 0.001,
        defaultValue: 0.2,
        format: (v) => v.toFixed(3)
      });

      const curveCanvas = card.querySelector(".curve-canvas");
      curveEditors.push(createCurveEditor(curveCanvas, bandIndex));

      bandGrid.appendChild(fragment);
    });
  }

  function refreshAllControls() {
    controlSetters.forEach((setter, id) => {
      if (id in state.params) {
        setter(state.params[id]);
      }
    });

    redrawCurves();
    updateCrossoverLines();
  }

  function updateCrossoverLines() {
    const f1 = Number(state.params["crossover.f1"] ?? 180);
    const f2 = Number(state.params["crossover.f2"] ?? 2500);

    crossoverLines.forEach((line) => {
      const param = line.dataset.param;
      const value = param === "crossover.f1" ? f1 : f2;
      const norm = freqToNorm(value);
      line.style.left = `${norm * 100}%`;

      const label = line.querySelector(".line-label");
      if (label) {
        label.textContent = formatFreq(value);
      }
    });
  }

  function setupCrossoverDragging() {
    let draggingParam = null;

    crossoverLines.forEach((line) => {
      line.addEventListener("pointerdown", (event) => {
        draggingParam = line.dataset.param;
        line.setPointerCapture(event.pointerId);
      });
    });

    function updateByPointer(event) {
      if (!draggingParam) {
        return;
      }

      const rect = crossoverOverlay.getBoundingClientRect();
      const norm = clamp((event.clientX - rect.left) / rect.width, 0, 1);
      let frequency = normToFreq(norm);

      const currentF1 = Number(state.params["crossover.f1"] ?? 180);
      const currentF2 = Number(state.params["crossover.f2"] ?? 2500);

      if (draggingParam === "crossover.f1") {
        frequency = clamp(frequency, MIN_FREQ, currentF2 - 20);
      } else {
        frequency = clamp(frequency, currentF1 + 20, MAX_FREQ);
      }

      emitParamChange(draggingParam, frequency);
      updateCrossoverLines();
    }

    window.addEventListener("pointermove", updateByPointer);
    window.addEventListener("pointerup", () => {
      draggingParam = null;
    });
    window.addEventListener("pointercancel", () => {
      draggingParam = null;
    });
  }

  function applyMidiStatus(payload) {
    if (!payload || typeof payload !== "object") {
      return;
    }

    if (typeof payload.activityCounter === "number") {
      const nextCounter = payload.activityCounter;
      if (nextCounter > state.midiActivityCounter) {
        midiBlinkUntil = performance.now() + 250;
      }
      state.midiActivityCounter = nextCounter;
    }

    if (Array.isArray(payload.channels)) {
      state.availableMidiChannels = payload.channels
        .map((channel) => ({
          value: Number(channel.value),
          name: String(channel.name)
        }))
        .filter((channel) => Number.isFinite(channel.value) && channel.value >= 1 && channel.value <= 16);

      refreshAllMidiChannelSelectOptions();
    }

    updateMidiIndicator();
  }

  function applyStateSnapshot(payload) {
    if (!payload || typeof payload !== "object") {
      return;
    }

    if (payload.params && typeof payload.params === "object") {
      Object.keys(payload.params).forEach((id) => {
        state.params[id] = Number(payload.params[id]);
      });
    }

    if (typeof payload.appliedLowMidHz === "number") {
      state.appliedLowMidHz = payload.appliedLowMidHz;
    }

    if (typeof payload.appliedMidHighHz === "number") {
      state.appliedMidHighHz = payload.appliedMidHighHz;
    }

    if (payload.midi && typeof payload.midi === "object") {
      applyMidiStatus(payload.midi);
    }

    refreshAllControls();
  }

  function handleFFT(payload) {
    if (payload && Array.isArray(payload.bins)) {
      state.fftBins = payload.bins.map((value) => Number(value));
    }
  }

  function drawSpectrum() {
    const rect = spectrumCanvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;

    const width = Math.max(1, Math.floor(rect.width * dpr));
    const height = Math.max(1, Math.floor(rect.height * dpr));

    if (spectrumCanvas.width !== width || spectrumCanvas.height !== height) {
      spectrumCanvas.width = width;
      spectrumCanvas.height = height;
    }

    spectrumCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
    spectrumCtx.clearRect(0, 0, rect.width, rect.height);

    const w = rect.width;
    const h = rect.height;

    spectrumCtx.fillStyle = "rgba(6, 14, 18, 0.7)";
    spectrumCtx.fillRect(0, 0, w, h);

    spectrumCtx.strokeStyle = "rgba(255, 255, 255, 0.1)";
    spectrumCtx.lineWidth = 1;

    const frequencyLines = [20, 40, 80, 160, 320, 640, 1250, 2500, 5000, 10000, 20000];
    frequencyLines.forEach((frequency) => {
      const x = freqToNorm(frequency) * w;
      spectrumCtx.beginPath();
      spectrumCtx.moveTo(x, 0);
      spectrumCtx.lineTo(x, h);
      spectrumCtx.stroke();
    });

    for (let db = -90; db <= 0; db += 15) {
      const y = h - ((db - DB_MIN) / (DB_MAX - DB_MIN)) * h;
      spectrumCtx.beginPath();
      spectrumCtx.moveTo(0, y);
      spectrumCtx.lineTo(w, y);
      spectrumCtx.stroke();
    }

    if (state.fftBins.length > 8) {
      const nyquist = 24000;
      const maxBin = state.fftBins.length - 1;

      spectrumCtx.beginPath();
      spectrumCtx.lineWidth = 2;
      spectrumCtx.strokeStyle = "rgba(39, 193, 168, 0.95)";

      let started = false;

      for (let index = 1; index < maxBin; index += 1) {
        const frequency = (index / maxBin) * nyquist;

        if (frequency < MIN_FREQ || frequency > MAX_FREQ) {
          continue;
        }

        const x = freqToNorm(frequency) * w;
        const db = clamp(Number(state.fftBins[index]), DB_MIN, DB_MAX);
        const y = h - ((db - DB_MIN) / (DB_MAX - DB_MIN)) * h;

        if (!started) {
          spectrumCtx.moveTo(x, y);
          started = true;
        } else {
          spectrumCtx.lineTo(x, y);
        }
      }

      spectrumCtx.stroke();
    }

    updateMidiIndicator();
    window.requestAnimationFrame(drawSpectrum);
  }

  function connectBackend() {
    if (!backend) {
      updateBackendStatus(false);
      return;
    }

    updateBackendStatus(true);

    backend.addEventListener("state", (payload) => {
      applyStateSnapshot(payload);
    });

    backend.addEventListener("fft", (payload) => {
      handleFFT(payload);
    });

    backend.addEventListener("midiStatus", (payload) => {
      applyMidiStatus(payload);
    });

    backend.emitEvent("requestState", {});
  }

  function boot() {
    initDefaultParams();
    buildBandPanels();
    setupCrossoverDragging();
    refreshAllControls();
    connectBackend();
    drawSpectrum();
  }

  boot();
})();
