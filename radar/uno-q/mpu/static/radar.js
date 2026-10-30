const state = {
  link: false,
  ready: false,
  mode: "idle",
  pulse: null,
  zero: Number(localStorage.getItem("servoZero")) || 1300,
  offset: 0,
  lunaCal: true,
  seenCm: null,
  angle: null,
  dist: null,
  amp: null,
  weak: false,
  sweeps: [],
  current: [],
  sweepId: 0,
  contacts: [],
  hz: Number(localStorage.getItem("buzzerHz")) || 2000,
};

const scope = document.getElementById("scope");
const map = document.getElementById("map");
const statusEl = document.getElementById("status");

function saveCal() {
  localStorage.setItem("servoZero", String(state.zero || 0));
  if (state.lunaCal && state.offset !== null) {
    localStorage.setItem("lunaOffset", String(state.offset));
    localStorage.setItem("lunaCal", "1");
  }
  if (state.hz >= 100 && state.hz <= 5000) {
    localStorage.setItem("buzzerHz", String(state.hz));
  }
}

function servoReady() {
  return state.zero >= 500 && state.zero <= 2500;
}

function setStatus(text, warn) {
  statusEl.textContent = text;
  statusEl.classList.toggle("warn", Boolean(warn));
}

function hzForCm(cm) {
  const hz = Math.round((cm * 80000) / 3048 - 700);
  if (hz < 100) {
    return 100;
  }
  if (hz > 5000) {
    return 5000;
  }
  return hz;
}

function renderSummaries() {
  const servo = document.getElementById("servo-summary");
  const luna = document.getElementById("luna-summary");
  servo.textContent = servoReady()
    ? `0° center ${state.zero} µs`
    : "0° center not marked";
  luna.textContent = "Using the sensor reading.";
  const buzzer = document.getElementById("buzzer-summary");
  const alerting = (state.mode === "radar" || state.mode === "luna-run") && !state.weak && state.dist !== null;
  if (state.mode === "buzzer-cal" || state.mode === "buzzer-run") {
    buzzer.textContent = `Pitch ${state.hz} Hz`;
  } else if (alerting) {
    buzzer.textContent = `${formatFeet(state.dist)} sounds at ${hzForCm(state.dist)} Hz`;
  } else {
    buzzer.textContent = "1 ft is 100 Hz. Each half foot adds 400 Hz, up to 5000 Hz.";
  }
  const buzzHz = document.getElementById("buzz-hz");
  if (buzzHz) {
    buzzHz.textContent = String(state.hz);
  }
  document.getElementById("servo-run").disabled = !servoReady();
  document.getElementById("luna-run").disabled = !state.lunaCal;
  document.getElementById("map-run").disabled = !(servoReady() && state.lunaCal);
  document.getElementById("pulse").textContent = state.pulse === null ? "—" : String(state.pulse);
  const live = document.getElementById("luna-live");
  if (live) {
    live.textContent = state.seenCm === null ? "—" : (state.seenCm / 30.48).toFixed(1);
  }
  document.getElementById("read-angle").textContent = state.angle === null ? "—°" : `${state.angle}°`;
  document.getElementById("read-dist").textContent = state.weak || state.dist === null
    ? (state.weak ? "weak" : "— ft")
    : (alerting
      ? `${formatFeet(state.dist)} · ${hzForCm(state.dist)} Hz`
      : formatFeet(state.dist));

  document.getElementById("servo-tools").classList.toggle("hidden", state.mode !== "servo-cal");
  document.getElementById("luna-tools").classList.toggle("hidden", state.mode !== "luna-cal");
  document.getElementById("buzzer-tools").classList.toggle("hidden", state.mode !== "buzzer-cal");
  for (const id of ["servo-cal", "servo-run", "luna-cal", "luna-run", "map-run", "buzzer-cal", "buzzer-run"]) {
    const button = document.getElementById(id);
    const on = (id === "servo-cal" && state.mode === "servo-cal") ||
      (id === "servo-run" && state.mode === "servo-run") ||
      (id === "luna-cal" && state.mode === "luna-cal") ||
      (id === "luna-run" && state.mode === "luna-run") ||
      (id === "map-run" && state.mode === "radar") ||
      (id === "buzzer-cal" && state.mode === "buzzer-cal") ||
      (id === "buzzer-run" && state.mode === "buzzer-run");
    button.classList.toggle("active", on);
  }

  const list = document.getElementById("contacts");
  list.replaceChildren();
  for (const hit of state.contacts) {
    const item = document.createElement("li");
    const angle = document.createElement("span");
    const dist = document.createElement("span");
    angle.textContent = `${hit.angle}°`;
    dist.textContent = formatFeet(hit.dist);
    item.append(angle, dist);
    list.append(item);
  }
}

async function send(line) {
  const response = await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ line }),
  });
  const body = await response.json();
  if (!body.ok) {
    setStatus(body.error || "Command was not sent", true);
  }
  return body.ok;
}

async function runMode(mode) {
  if (mode === "servo-run" || mode === "radar") {
    if (!servoReady() || !(await send(`zero ${state.zero}`))) {
      return;
    }
  }
  if (mode === "luna-run" || mode === "radar") {
    if (!state.lunaCal || !(await send(`cal luna ${state.offset}`))) {
      return;
    }
  }
  if (mode === "buzzer-cal" || mode === "buzzer-run") {
    if (state.hz < 100 || state.hz > 5000 || !(await send(`buzz ${state.hz}`))) {
      return;
    }
  }
  if (await send(`mode ${mode}`)) {
    state.mode = mode;
    renderSummaries();
    return true;
  }
  return false;
}

function rememberContact(angle, dist) {
  state.contacts.unshift({ angle, dist });
  state.contacts = state.contacts.slice(0, 12);
}

function pushPlot(angle, dist, amp) {
  const prev = state.current[state.current.length - 1];
  const before = state.current[state.current.length - 2];
  if (prev && before) {
    const wasRising = prev.angle >= before.angle;
    const rising = angle >= prev.angle;
    if (wasRising !== rising) {
      state.sweeps.push(state.current);
      state.sweeps = state.sweeps.slice(-5);
      state.current = [];
    }
  }
  state.current.push({ angle, dist, amp });
}

function applyLine(text) {
  const parts = text.trim().split(/\s+/);
  const head = parts[0];
  if (head === "ready") {
    state.ready = true;
    setStatus("Radar sketch is ready.");
  } else if (head === "mode" && parts[1]) {
    state.mode = parts[1];
    if (parts[1] === "luna-cal") {
      setStatus("TF-Luna is reading. The number is the distance in feet.");
    } else if (parts[1] === "buzzer-cal") {
      setStatus("Pick a pitch you can hear.");
    } else if (parts[1] === "buzzer-run") {
      setStatus("Buzzer is repeating.");
    } else {
      setStatus(`Mode ${parts[1]}.`);
    }
  } else if (head === "pulse" && parts[1]) {
    state.pulse = Number(parts[1]);
  } else if (head === "zero" && parts[1]) {
    state.zero = Number(parts[1]);
    saveCal();
    setStatus(`0° is ${state.zero} µs.`);
  } else if (head === "angle" && parts[1]) {
    state.angle = Number(parts[1]);
    state.weak = false;
  } else if (head === "dist" && parts[1]) {
    state.seenCm = Number(parts[1]);
    state.dist = state.seenCm;
    state.amp = parts[2] ? Number(parts[2]) : null;
    state.weak = false;
  } else if (head === "weak") {
    state.weak = true;
    state.dist = null;
  } else if (head === "overexposed") {
    state.weak = true;
    state.dist = null;
    setStatus("TF-Luna is overexposed.", true);
  } else if (head === "measured" && parts[1]) {
    state.seenCm = Number(parts[1]);
    setStatus(`Sensor reads ${formatFeet(state.seenCm)}.`);
  } else if (head === "offset" && parts[1]) {
    state.offset = Number(parts[1]);
    state.lunaCal = true;
    saveCal();
    setStatus("Using the sensor reading as the distance.");
  } else if (head === "buzz" && parts[1] === "hold") {
    setStatus("Pin 6 held high.");
  } else if (head === "buzz" && parts[1]) {
    state.hz = Number(parts[1]);
    saveCal();
    setStatus(`Pitch ${state.hz} Hz.`);
  } else if (head === "radar" && parts[1]) {
    state.angle = Number(parts[1]);
    if (parts[2] && parts[2] !== "weak") {
      state.dist = Number(parts[2]);
      state.amp = parts[3] ? Number(parts[3]) : null;
      state.weak = false;
      pushPlot(state.angle, state.dist, state.amp);
      rememberContact(state.angle, state.dist);
    } else {
      state.weak = true;
      state.dist = null;
    }
  } else if (head === "err") {
    const why = parts.slice(1).join(" ");
    const text = {
      servo: "Mark both servo ends before running it.",
      luna: "Calibrate the TF-Luna before running it.",
      lidar: "TF-Luna did not answer. Pin 5 has to stay on GND.",
      capture: "Not enough steady returns. Aim at a flat target and capture again.",
      tape: "Tape distance has to be 0.7 to 26.2 ft, and the offset inside ±3.3 ft.",
      buzz: "Pitch has to be 100 to 5000 Hz.",
    }[why] || `Board reported ${why || "an error"}.`;
    setStatus(text, true);
  } else if (head === "sweep" && parts[1] === "done") {
    setStatus("Test sweep finished.");
  }
  renderSummaries();
}

function fit(canvas) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.floor(rect.width));
  const height = Math.max(1, Math.floor(rect.height));
  if (canvas.width !== Math.floor(width * dpr) || canvas.height !== Math.floor(height * dpr)) {
    canvas.width = Math.floor(width * dpr);
    canvas.height = Math.floor(height * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, width, height };
}

function polar(cx, cy, radius, angleDeg, distCm) {
  const rad = (angleDeg * Math.PI) / 180;
  const scale = radius / 800;
  const r = Math.max(0, distCm) * scale;
  return {
    x: cx - r * Math.sin(rad),
    y: cy - r * Math.cos(rad),
  };
}

const plots = { scope: [], map: [] };
let hover = null;

function formatFeet(cm) {
  return `${(cm / 30.48).toFixed(1)} ft`;
}

function rememberDot(which, x, y, angle, dist) {
  plots[which].push({ x, y, angle, dist });
}

function labelAnchor(cx, cy, dot, width, height, textWidth) {
  const dx = dot.x - cx;
  const dy = dot.y - cy;
  const len = Math.hypot(dx, dy) || 1;
  let x = dot.x + (dx / len) * 12;
  let y = dot.y + (dy / len) * 12;
  if (x + textWidth + 8 > width - 4) {
    x = Math.max(4, dot.x - textWidth - 14);
  }
  if (x < 4) {
    x = 4;
  }
  if (y < 10) {
    y = 10;
  }
  if (y > height - 8) {
    y = height - 8;
  }
  return { x, y };
}

function paintCm(ctx, x, y, text, emphasize) {
  ctx.font = "11px ui-monospace, monospace";
  ctx.textBaseline = "middle";
  const width = ctx.measureText(text).width;
  ctx.fillStyle = emphasize ? "rgba(32, 8, 8, 0.92)" : "rgba(6, 20, 13, 0.82)";
  ctx.fillRect(x, y - 8, width + 8, 16);
  ctx.fillStyle = emphasize ? "#fff4f2" : "#ffd0cc";
  ctx.fillText(text, x + 4, y);
  return width + 8;
}

function drawDistances(ctx, which, cx, cy, width, height, priority) {
  const placed = [];
  const dots = plots[which].slice().reverse();
  if (priority) {
    dots.unshift(priority);
  }
  ctx.font = "11px ui-monospace, monospace";
  for (const dot of dots) {
    const text = formatFeet(dot.dist);
    const textWidth = ctx.measureText(text).width;
    const anchor = labelAnchor(cx, cy, dot, width, height, textWidth);
    const box = { x: anchor.x, y: anchor.y, w: textWidth + 8 };
    const crowded = placed.some((spot) => {
      return Math.abs(spot.x - box.x) < (spot.w + box.w) / 2 + 6 && Math.abs(spot.y - box.y) < 18;
    });
    if (crowded) {
      continue;
    }
    placed.push(box);
    paintCm(ctx, anchor.x, anchor.y, text, Boolean(priority) && dot === priority);
  }
}

function drawHover(ctx, which, cx, cy) {
  if (!hover || hover.which !== which) {
    return;
  }
  ctx.strokeStyle = "rgba(255, 208, 204, 0.9)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(hover.x, hover.y);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(hover.x, hover.y, 7, 0, Math.PI * 2);
  ctx.stroke();
  paintCm(ctx, hover.x + 12, hover.y - 16, formatFeet(hover.dist), true);
}

function bindHover(canvas, which) {
  canvas.addEventListener("mousemove", (event) => {
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    let best = null;
    let bestD = 16;
    for (const dot of plots[which]) {
      const d = Math.hypot(dot.x - x, dot.y - y);
      if (d < bestD) {
        best = dot;
        bestD = d;
      }
    }
    if (best) {
      hover = { which, x: best.x, y: best.y, dist: best.dist, angle: best.angle };
    } else if (hover && hover.which === which) {
      hover = null;
    }
  });
  canvas.addEventListener("mouseleave", () => {
    if (hover && hover.which === which) {
      hover = null;
    }
  });
}

function drawScope() {
  const { ctx, width, height } = fit(scope);
  ctx.clearRect(0, 0, width, height);
  plots.scope = [];
  const cx = width / 2;
  const cy = height - 36;
  const radius = Math.min(width * 0.46, height - 70);

  const glow = ctx.createRadialGradient(cx, cy, 20, cx, cy, radius);
  glow.addColorStop(0, "rgba(182, 255, 122, 0.09)");
  glow.addColorStop(1, "rgba(182, 255, 122, 0)");
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, Math.PI, 0, true);
  ctx.fill();

  ctx.strokeStyle = "rgba(111, 191, 98, 0.45)";
  ctx.lineWidth = 1;
  ctx.font = "12px ui-monospace, monospace";
  ctx.fillStyle = "#8eae96";
  for (const cm of [200, 400, 600, 800]) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * (cm / 800), Math.PI, 0, true);
    ctx.stroke();
    ctx.fillText(formatFeet(cm), cx + 8, cy - radius * (cm / 800) + 14);
  }
  for (const angle of [-90, -45, 0, 45, 90]) {
    const end = polar(cx, cy, radius, angle, 800);
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(end.x, end.y);
    ctx.stroke();
    ctx.fillText(`${angle}°`, end.x - 12, end.y - 6);
  }

  const sweeps = state.sweeps.concat([state.current]);
  sweeps.forEach((sweep, index) => {
    if (!sweep.length) {
      return;
    }
    const newest = index === sweeps.length - 1;
    ctx.fillStyle = newest ? "#ff3b30" : "rgba(255, 59, 48, 0.55)";
    for (const point of sweep) {
      const p = polar(cx, cy, radius, point.angle, point.dist);
      rememberDot("scope", p.x, p.y, point.angle, point.dist);
      ctx.beginPath();
      ctx.arc(p.x, p.y, newest ? 4 : 3, 0, Math.PI * 2);
      ctx.fill();
    }
  });

  if (state.angle !== null) {
    const end = polar(cx, cy, radius, state.angle, 800);
    ctx.strokeStyle = "rgba(240, 195, 106, 0.95)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(end.x, end.y);
    ctx.stroke();
    if (!state.weak && state.dist !== null) {
      const hit = polar(cx, cy, radius, state.angle, state.dist);
      ctx.fillStyle = "rgba(255, 59, 48, 0.35)";
      ctx.beginPath();
      ctx.arc(hit.x, hit.y, 9, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = "#ff3b30";
      ctx.beginPath();
      ctx.arc(hit.x, hit.y, 5, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  ctx.fillStyle = "#b6ff7a";
  ctx.beginPath();
  ctx.arc(cx, cy, 5, 0, Math.PI * 2);
  ctx.fill();

  let live = null;
  if (state.angle !== null && !state.weak && state.dist !== null) {
    const hit = polar(cx, cy, radius, state.angle, state.dist);
    live = { x: hit.x, y: hit.y, dist: state.dist };
  }
  drawDistances(ctx, "scope", cx, cy, width, height, live);
  drawHover(ctx, "scope", cx, cy);
}

function drawMap() {
  const { ctx, width, height } = fit(map);
  ctx.clearRect(0, 0, width, height);
  plots.map = [];
  const cx = width / 2;
  const cy = height - 28;
  const radius = Math.min(width * 0.46, height - 58);

  ctx.strokeStyle = "rgba(111, 191, 98, 0.25)";
  ctx.fillStyle = "#8eae96";
  ctx.font = "12px ui-monospace, monospace";
  ctx.lineWidth = 1;
  for (let cm = 200; cm <= 800; cm += 200) {
    const y = cy - radius * (cm / 800);
    ctx.beginPath();
    ctx.moveTo(28, y);
    ctx.lineTo(width - 16, y);
    ctx.stroke();
    ctx.fillText(formatFeet(cm), 8, y + 4);
  }

  const sweeps = state.sweeps.concat([state.current]).filter((sweep) => sweep.length);
  sweeps.forEach((sweep, index) => {
    const newest = index === sweeps.length - 1;
    ctx.fillStyle = newest ? "#ff3b30" : "rgba(255, 59, 48, 0.55)";
    for (const point of sweep) {
      const p = polar(cx, cy, radius, point.angle, point.dist);
      rememberDot("map", p.x, p.y, point.angle, point.dist);
      ctx.beginPath();
      ctx.arc(p.x, p.y, newest ? 4 : 3, 0, Math.PI * 2);
      ctx.fill();
    }
  });

  ctx.fillStyle = "#f0c36a";
  ctx.beginPath();
  ctx.moveTo(cx, cy - 10);
  ctx.lineTo(cx - 7, cy + 6);
  ctx.lineTo(cx + 7, cy + 6);
  ctx.closePath();
  ctx.fill();
  ctx.fillStyle = "#8eae96";
  ctx.fillText("ahead", cx - 18, 18);
  drawDistances(ctx, "map", cx, cy, width, height, null);
  drawHover(ctx, "map", cx, cy);
}

function frame() {
  drawScope();
  drawMap();
  requestAnimationFrame(frame);
}

document.getElementById("servo-cal").addEventListener("click", () => runMode("servo-cal"));
document.getElementById("servo-run").addEventListener("click", () => runMode("servo-run"));
document.getElementById("luna-cal").addEventListener("click", async () => {
  const ok = await runMode("luna-cal");
  document.getElementById("luna-panel").scrollIntoView({ block: "nearest" });
  if (!ok) {
    setStatus("Calibrate did not start. The board is not answering.", true);
    return;
  }
  await send("cal luna 0");
});
document.getElementById("luna-run").addEventListener("click", () => runMode("luna-run"));
document.getElementById("map-run").addEventListener("click", () => runMode("radar"));

function nudgeBuzz(delta) {
  state.hz = Math.min(5000, Math.max(100, state.hz + delta));
  saveCal();
  renderSummaries();
  send(`buzz ${state.hz}`);
}

document.getElementById("buzzer-cal").addEventListener("click", async () => {
  const ok = await runMode("buzzer-cal");
  document.getElementById("buzzer-panel").scrollIntoView({ block: "nearest" });
  if (!ok) {
    setStatus("Calibrate did not start. The board is not answering.", true);
  }
});
document.getElementById("buzzer-run").addEventListener("click", () => runMode("buzzer-run"));
document.getElementById("buzz-down-c").addEventListener("click", () => nudgeBuzz(-400));
document.getElementById("buzz-down").addEventListener("click", () => nudgeBuzz(-50));
document.getElementById("buzz-up").addEventListener("click", () => nudgeBuzz(50));
document.getElementById("buzz-up-c").addEventListener("click", () => nudgeBuzz(400));
document.getElementById("buzz-beep").addEventListener("click", () => send(`buzz ${state.hz}`));
document.getElementById("buzz-hold").addEventListener("click", () => send("buzz hold"));
document.getElementById("buzz-mark").addEventListener("click", () => {
  saveCal();
  setStatus(`Using ${state.hz} Hz.`);
  send(`buzz ${state.hz}`);
});

document.getElementById("servo-tools").addEventListener("click", (event) => {
  const key = event.target.dataset.key;
  if (key) {
    send(`key ${key}`);
  }
});

document.getElementById("map-clear").addEventListener("click", () => {
  state.sweeps = [];
  state.current = [];
  state.contacts = [];
  renderSummaries();
});

document.addEventListener("keydown", (event) => {
  if (state.mode !== "servo-cal") {
    return;
  }
  if (event.target instanceof HTMLInputElement) {
    return;
  }
  if ("adADczmt".includes(event.key)) {
    event.preventDefault();
    send(`key ${event.key}`);
  }
});

const linkEl = document.getElementById("link");
const events = new EventSource("/events");
events.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.type === "link") {
    state.link = message.up;
    linkEl.textContent = message.up ? "MCU linked" : "MCU offline";
    linkEl.classList.toggle("up", message.up);
    linkEl.classList.toggle("down", !message.up);
    if (!message.up) {
      state.ready = false;
      setStatus("MCU offline. The page is up; the board is not answering on port 7500.", true);
    }
    return;
  }
  if (message.type === "line") {
    applyLine(message.text);
  }
};
events.onerror = () => {
  linkEl.textContent = "Page disconnected";
  linkEl.classList.remove("up");
};

saveCal();
renderSummaries();
bindHover(scope, "scope");
bindHover(map, "map");
requestAnimationFrame(frame);
window.__radarApply = applyLine;
