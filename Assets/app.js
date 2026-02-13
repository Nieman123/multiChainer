(function () {
  const MIN_FREQ = 20;
  const MAX_FREQ = 20000;
  const DB_MIN = -120;
  const DB_MAX = 6;

  const state = {
    params: {},
    fftBins: [],
    appliedLowMidHz: 180,
    appliedMidHighHz: 2500
  };

  const controlSetters = new Map();
  const curveEditors = [];

  const backend = window.__JUCE__ && window.__JUCE__.backend ? window.__JUCE__.backend : null;

  const statusDot = document.getElementById("backendStatusDot");
  const statusText = document.getElementById("backendStatusText");

  const spectrumCanvas = document.getElementById("spectrumCanvas");
  const spectrumCtx = spectrumCanvas.getContext("2d");

  const crossoverOverlay = document.getElementById("crossoverOverlay");
  const crossoverLines = Array.from(document.querySelectorAll(".crossover-line"));

  const bandTemplate = document.getElementById("bandTemplate");
  const bandGrid = document.getElementById("bandGrid");

  const defaultBands = [
    { name: "Low Band", midiNoteMin: 36, midiNoteMax: 84 },
    { name: "Mid Band", midiNoteMin: 36, midiNoteMax: 84 },
    { name: "High Band", midiNoteMin: 36, midiNoteMax: 84 }
  ];

  function bandParamId(bandIndex, suffix) {
    return `band${bandIndex + 1}.${suffix}`;
  }

  function initDefaultParams() {
    state.params["crossover.f1"] = 180;
    state.params["crossover.f2"] = 2500;

    for (let band = 0; band < 3; band += 1) {
      state.params[bandParamId(band, "midiChannel")] = 0;
      state.params[bandParamId(band, "midiNoteMin")] = defaultBands[band].midiNoteMin;
      state.params[bandParamId(band, "midiNoteMax")] = defaultBands[band].midiNoteMax;
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

  function noteName(note) {
    const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const octave = Math.floor(note / 12) - 1;
    const name = names[note % 12];
    return `${note} (${name}${octave})`;
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

  function createSlider(parent, label, paramID, config) {
    const wrap = document.createElement("div");
    wrap.className = "slider-wrap";

    const top = document.createElement("div");
    top.className = "slider-top";

    const name = document.createElement("span");
    name.textContent = label;

    const valueLabel = document.createElement("span");
    valueLabel.className = "slider-value";

    top.appendChild(name);
    top.appendChild(valueLabel);

    const input = document.createElement("input");
    input.type = "range";
    input.min = String(config.min);
    input.max = String(config.max);
    input.step = String(config.step);
    input.value = String(state.params[paramID] ?? config.defaultValue);

    input.addEventListener("input", () => {
      const value = Number(input.value);
      valueLabel.textContent = config.format(value);
      emitParamChange(paramID, value);
      redrawCurves();
      if (paramID === "crossover.f1" || paramID === "crossover.f2") {
        updateCrossoverLines();
      }
    });

    wrap.appendChild(top);
    wrap.appendChild(input);
    parent.appendChild(wrap);

    registerSetter(paramID, (value) => {
      input.value = String(value);
      valueLabel.textContent = config.format(Number(value));
    });

    controlSetters.get(paramID)(state.params[paramID] ?? config.defaultValue);
  }

  function createMidiSelect(select, options, paramID) {
    options.forEach((option) => {
      const node = document.createElement("option");
      node.value = String(option.value);
      node.textContent = option.label;
      select.appendChild(node);
    });

    select.value = String(state.params[paramID] ?? 0);

    select.addEventListener("change", () => {
      emitParamChange(paramID, Number(select.value));
    });

    registerSetter(paramID, (value) => {
      select.value = String(Math.round(Number(value)));
    });
  }

  function populateNoteSelect(select) {
    for (let note = 0; note <= 127; note += 1) {
      const option = document.createElement("option");
      option.value = String(note);
      option.textContent = noteName(note);
      select.appendChild(option);
    }
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
    defaultBands.forEach((bandInfo, bandIndex) => {
      const fragment = bandTemplate.content.cloneNode(true);
      const card = fragment.querySelector(".band-card");

      card.querySelector(".band-title").textContent = bandInfo.name;

      const channelSelect = card.querySelector('select[data-param="midiChannel"]');
      const noteMinSelect = card.querySelector('select[data-param="midiNoteMin"]');
      const noteMaxSelect = card.querySelector('select[data-param="midiNoteMax"]');

      createMidiSelect(
        channelSelect,
        [{ value: 0, label: "Omni" }].concat(
          Array.from({ length: 16 }, (_, index) => ({ value: index + 1, label: `Ch ${index + 1}` }))
        ),
        bandParamId(bandIndex, "midiChannel")
      );

      populateNoteSelect(noteMinSelect);
      populateNoteSelect(noteMaxSelect);

      const noteMinID = bandParamId(bandIndex, "midiNoteMin");
      const noteMaxID = bandParamId(bandIndex, "midiNoteMax");

      noteMinSelect.value = String(state.params[noteMinID]);
      noteMaxSelect.value = String(state.params[noteMaxID]);

      noteMinSelect.addEventListener("change", () => {
        let noteMin = Number(noteMinSelect.value);
        let noteMax = Number(noteMaxSelect.value);

        if (noteMin > noteMax) {
          noteMax = noteMin;
          noteMaxSelect.value = String(noteMax);
          emitParamBatch([
            { id: noteMinID, value: noteMin },
            { id: noteMaxID, value: noteMax }
          ]);
        } else {
          emitParamChange(noteMinID, noteMin);
        }
      });

      noteMaxSelect.addEventListener("change", () => {
        let noteMin = Number(noteMinSelect.value);
        let noteMax = Number(noteMaxSelect.value);

        if (noteMax < noteMin) {
          noteMin = noteMax;
          noteMinSelect.value = String(noteMin);
          emitParamBatch([
            { id: noteMinID, value: noteMin },
            { id: noteMaxID, value: noteMax }
          ]);
        } else {
          emitParamChange(noteMaxID, noteMax);
        }
      });

      registerSetter(noteMinID, (value) => {
        noteMinSelect.value = String(Math.round(Number(value)));
      });

      registerSetter(noteMaxID, (value) => {
        noteMaxSelect.value = String(Math.round(Number(value)));
      });

      const sliders = card.querySelector('[data-grid="sliders"]');

      createSlider(sliders, "Depth", bandParamId(bandIndex, "depthDb"), {
        min: 0,
        max: 60,
        step: 0.1,
        defaultValue: 12,
        format: (v) => `-${v.toFixed(1)} dB`
      });

      createSlider(sliders, "Delay", bandParamId(bandIndex, "delayMs"), {
        min: 0,
        max: 200,
        step: 0.1,
        defaultValue: 0,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createSlider(sliders, "Attack", bandParamId(bandIndex, "attackMs"), {
        min: 0,
        max: 1000,
        step: 0.1,
        defaultValue: 20,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createSlider(sliders, "Hold", bandParamId(bandIndex, "holdMs"), {
        min: 0,
        max: 1000,
        step: 0.1,
        defaultValue: 30,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createSlider(sliders, "Release", bandParamId(bandIndex, "releaseMs"), {
        min: 1,
        max: 3000,
        step: 0.1,
        defaultValue: 180,
        format: (v) => `${v.toFixed(1)} ms`
      });

      createSlider(sliders, "Curve Shape", bandParamId(bandIndex, "curveShape"), {
        min: 0.1,
        max: 10,
        step: 0.01,
        defaultValue: 1,
        format: (v) => `${v.toFixed(2)}x`
      });

      createSlider(sliders, "Smoothing", bandParamId(bandIndex, "smoothing"), {
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
