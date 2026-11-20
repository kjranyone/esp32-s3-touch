/**
 * SpatialAudio.js — Hardware-Anchored IR Convolution & 3D HRTF Audio Engine
 *
 * Streams real-time PCM from the active WASM cartridge through:
 *   1. Physical Speaker / Enclosure IR (ConvolverNode)
 *   2. Front-Glass vs Rear-PCB Acoustic Diffraction Filter (BiquadFilterNode)
 *   3. 3D HRTF Panner synced to the active board's speakerAnchor & Three.js camera
 *   4. Dual Stereo Room IR Convolvers (STUDIO / HALL / DRY click-free crossfade)
 *   5. Master DynamicsCompressor
 *
 * Also synthesizes subtle mechanical foley clicks (tactile switches, solder jumpers,
 * relays) directly in 3D space.
 */
import * as THREE from 'three';

const AUDIO_CHUNK = 1024;
export const IR_MODES = ['studio', 'hall', 'dry'];

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6D2B79F5) >>> 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function buildSpeakerIR(ctx) {
  const sr = ctx.sampleRate;
  const len = Math.max(256, Math.round(sr * 0.012));
  const buf = ctx.createBuffer(1, len, sr);
  const data = buf.getChannelData(0);
  const modes = [
    { f: 880,  q: 4.5, g: 0.24 },
    { f: 2650, q: 5.2, g: 0.19 },
    { f: 5200, q: 3.8, g: 0.11 },
  ];
  data[0] = 0.88;
  data[1] = -0.18;
  for (let i = 1; i < len; i++) {
    const t = i / sr;
    let s = 0;
    for (const m of modes) {
      const tau = m.q / (Math.PI * m.f);
      s += m.g * Math.exp(-t / tau) * Math.sin(2 * Math.PI * m.f * t);
    }
    const refl = Math.round(sr * 0.0011);
    if (i === refl) s += 0.14;
    data[i] += s;
  }
  let e = 0;
  for (let i = 0; i < len; i++) e += data[i] * data[i];
  const norm = 1 / Math.max(0.001, Math.sqrt(e));
  for (let i = 0; i < len; i++) data[i] *= norm;
  return buf;
}

function buildRoomIR(ctx, mode) {
  const sr = ctx.sampleRate;
  const isHall = (mode === 'hall');
  const dur = isHall ? 2.25 : 1.05;
  const len = Math.round(sr * dur);
  const buf = ctx.createBuffer(2, len, sr);

  const early = isHall ? [
    [13.5,  0.52,  0.34,  0.42],
    [19.2,  0.32,  0.50, -0.38],
    [27.8,  0.40,  0.38,  0.22],
    [36.4, -0.36,  0.26,  0.55],
    [44.1,  0.24, -0.35, -0.48],
    [53.7,  0.30,  0.28,  0.31],
    [64.2, -0.22, -0.25, -0.25],
    [75.8,  0.21,  0.19,  0.40],
  ] : [
    [ 1.8,  0.46,  0.46,  0.00],
    [ 4.7,  0.44,  0.24,  0.36],
    [ 6.1,  0.22,  0.42, -0.34],
    [ 9.4, -0.30, -0.28,  0.12],
    [13.8,  0.26, -0.18,  0.44],
    [18.5, -0.17,  0.24, -0.39],
    [24.2,  0.16,  0.15,  0.22],
    [30.9, -0.12, -0.13, -0.18],
  ];

  const rtLow  = isHall ? 2.15 : 0.68;
  const rtMid  = isHall ? 1.75 : 0.50;
  const rtHigh = isHall ? 0.78 : 0.21;
  const preDelayS = Math.round(sr * (isHall ? 0.018 : 0.005));

  for (let ch = 0; ch < 2; ch++) {
    const out = buf.getChannelData(ch);
    const rand = mulberry32(ch === 0 ? (isHall ? 0x243F6A88 : 0x1337C0DE) : (isHall ? 0x85A308D3 : 0xBADC0FFE));

    for (const [ms, gL, gR, itd] of early) {
      const tMs = ms + (ch === 1 ? itd : 0);
      const idx = Math.round(sr * Math.max(0.0005, tMs * 0.001));
      const g = (ch === 0 ? gL : gR) * (isHall ? 0.58 : 0.45);
      if (idx >= 0 && idx + 2 < len) {
        out[idx]     += g * 0.72;
        out[idx + 1] += g * 0.28;
      }
    }

    const kLow  = 6.907755 / (rtLow * sr);
    const kMid  = 6.907755 / (rtMid * sr);
    const kHigh = 6.907755 / (rtHigh * sr);
    const aLo = 1 - Math.exp(-2 * Math.PI * 450 / sr);
    const aHi = 1 - Math.exp(-2 * Math.PI * 3200 / sr);
    let lpLo = 0, lpHi = 0;
    const tailGain = isHall ? 0.34 : 0.22;

    for (let i = preDelayS; i < len; i++) {
      const n = i - preDelayS;
      const build = Math.min(1, n / (sr * (isHall ? 0.032 : 0.016)));
      const w = (rand() + rand() - 1.0);
      lpLo += aLo * (w - lpLo);
      lpHi += aHi * (w - lpHi);
      const s = lpLo * Math.exp(-kLow * n) * 0.85
              + (lpHi - lpLo) * Math.exp(-kMid * n) * 1.00
              + (w - lpHi) * Math.exp(-kHigh * n) * 0.65;
      out[i] += s * build * tailGain;
    }
  }

  let energy = 0;
  for (let ch = 0; ch < 2; ch++) {
    const d = buf.getChannelData(ch);
    for (let i = 0; i < len; i++) energy += d[i] * d[i];
  }
  const norm = (isHall ? 1.15 : 0.92) / Math.max(0.01, Math.sqrt(energy * 0.5));
  for (let ch = 0; ch < 2; ch++) {
    const d = buf.getChannelData(ch);
    for (let i = 0; i < len; i++) d[i] *= norm;
  }
  return buf;
}

export class SpatialAudioEngine {
  constructor() {
    this.ctx = null;
    this.graph = null;
    this.nextTime = 0;
    this.timer = null;
    this.irModeIdx = 0; // 'studio'
    this.host = null;
    this.bus = null;

    this._spkPos = new THREE.Vector3();
    this._spkFwd = new THREE.Vector3();
    this._camFwd = new THREE.Vector3();
    this._camUp  = new THREE.Vector3();
    this._toCam  = new THREE.Vector3();
    this._devQuat = new THREE.Quaternion();
  }

  attach(host, bus) {
    this.host = host;
    this.bus = bus;
    if (this.ctx && this.host?.ready) {
      this.host.audioSetRate(this.ctx.sampleRate);
    }
  }

  get mode() { return IR_MODES[this.irModeIdx]; }

  cycleIrMode() {
    this.irModeIdx = (this.irModeIdx + 1) % IR_MODES.length;
    this.applyIrMode();
    return this.mode;
  }

  ensureStarted() {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    if (!this.ctx) {
      this.ctx = new AC();
      this._initGraph(this.ctx);
      if (this.host?.ready) this.host.audioSetRate(this.ctx.sampleRate);
      this.nextTime = this.ctx.currentTime + 0.02;
      this.timer = setInterval(() => this._pump(), 14);
    }
    if (this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    if (this.host?.ready) this.host.audioSetRate(this.ctx.sampleRate);
  }

  /** Synthesize a crisp physical tactile click (switch, jumper, relay coil) */
  playMechanicalClick(pitchHz = 1850, gainAmt = 0.16) {
    if (!this.ctx || !this.graph || this.ctx.state !== 'running') return;
    const t = this.ctx.currentTime;
    const osc = this.ctx.createOscillator();
    const g = this.ctx.createGain();
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(pitchHz, t);
    osc.frequency.exponentialRampToValueAtTime(pitchHz * 0.35, t + 0.014);
    g.gain.setValueAtTime(gainAmt, t);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 0.018);
    osc.connect(g);
    g.connect(this.graph.panner);
    osc.start(t);
    osc.stop(t + 0.02);
  }

  applyIrMode() {
    if (!this.graph || !this.ctx) return;
    const mode = this.mode;
    const t = this.ctx.currentTime;
    const g = this.graph;
    if (mode === 'dry') {
      g.cabDry.gain.setTargetAtTime(1.0, t, 0.025);
      g.cabWet.gain.setTargetAtTime(0.0, t, 0.025);
      g.directOut.gain.setTargetAtTime(0.0, t, 0.025);
      g.bypassOut.gain.setTargetAtTime(1.0, t, 0.025);
      g.studioSend.gain.setTargetAtTime(0.0, t, 0.025);
      g.hallSend.gain.setTargetAtTime(0.0, t, 0.025);
    } else if (mode === 'studio') {
      g.cabDry.gain.setTargetAtTime(0.58, t, 0.025);
      g.cabWet.gain.setTargetAtTime(0.46, t, 0.025);
      g.directOut.gain.setTargetAtTime(0.95, t, 0.025);
      g.bypassOut.gain.setTargetAtTime(0.0, t, 0.025);
      g.studioSend.gain.setTargetAtTime(0.38, t, 0.025);
      g.hallSend.gain.setTargetAtTime(0.0, t, 0.025);
    } else {
      g.cabDry.gain.setTargetAtTime(0.80, t, 0.025);
      g.cabWet.gain.setTargetAtTime(0.24, t, 0.025);
      g.directOut.gain.setTargetAtTime(0.86, t, 0.025);
      g.bypassOut.gain.setTargetAtTime(0.0, t, 0.025);
      g.studioSend.gain.setTargetAtTime(0.0, t, 0.025);
      g.hallSend.gain.setTargetAtTime(0.56, t, 0.025);
    }
  }

  _initGraph(ctx) {
    const input = ctx.createGain();
    input.gain.value = 1.0;

    const cabDry = ctx.createGain();
    const cabWet = ctx.createGain();
    const cabConv = ctx.createConvolver();
    cabConv.normalize = false;
    cabConv.buffer = buildSpeakerIR(ctx);

    const cabSum = ctx.createGain();
    input.connect(cabDry);
    input.connect(cabConv);
    cabConv.connect(cabWet);
    cabDry.connect(cabSum);
    cabWet.connect(cabSum);

    const diffFilter = ctx.createBiquadFilter();
    diffFilter.type = 'lowpass';
    diffFilter.frequency.value = 14000;
    diffFilter.Q.value = 0.65;

    const panner = ctx.createPanner();
    panner.panningModel = 'HRTF';
    panner.distanceModel = 'inverse';
    panner.refDistance = 0.18;
    panner.maxDistance = 4.0;
    panner.rolloffFactor = 0.65;
    panner.coneInnerAngle = 110;
    panner.coneOuterAngle = 250;
    panner.coneOuterGain = 0.68;

    const directOut = ctx.createGain();
    cabSum.connect(diffFilter);
    diffFilter.connect(panner);
    panner.connect(directOut);

    const studioSend = ctx.createGain();
    const studioConv = ctx.createConvolver();
    studioConv.normalize = false;
    studioConv.buffer = buildRoomIR(ctx, 'studio');
    cabSum.connect(studioSend);
    studioSend.connect(studioConv);

    const hallSend = ctx.createGain();
    const hallConv = ctx.createConvolver();
    hallConv.normalize = false;
    hallConv.buffer = buildRoomIR(ctx, 'hall');
    cabSum.connect(hallSend);
    hallSend.connect(hallConv);

    const bypassOut = ctx.createGain();
    bypassOut.gain.value = 0.0;
    input.connect(bypassOut);

    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -10;
    comp.knee.value = 12;
    comp.ratio.value = 4;
    comp.attack.value = 0.003;
    comp.release.value = 0.12;

    directOut.connect(comp);
    studioConv.connect(comp);
    hallConv.connect(comp);
    bypassOut.connect(comp);
    comp.connect(ctx.destination);

    this.graph = { input, cabDry, cabWet, diffFilter, panner, directOut, studioSend, hallSend, bypassOut };
    this.applyIrMode();
  }

  updateSpatial(camera, board) {
    if (!this.ctx || !this.graph || this.ctx.state !== 'running' || !board) return;
    const t = this.ctx.currentTime;
    const S = 0.01; // 1 Three.js unit = 10mm = 0.01m

    const L = this.ctx.listener;
    camera.getWorldDirection(this._camFwd);
    this._camUp.set(0, 1, 0).applyQuaternion(camera.quaternion).normalize();
    if (L.positionX) {
      L.positionX.setValueAtTime(camera.position.x * S, t);
      L.positionY.setValueAtTime(camera.position.y * S, t);
      L.positionZ.setValueAtTime(camera.position.z * S, t);
      L.forwardX.setValueAtTime(this._camFwd.x, t);
      L.forwardY.setValueAtTime(this._camFwd.y, t);
      L.forwardZ.setValueAtTime(this._camFwd.z, t);
      L.upX.setValueAtTime(this._camUp.x, t);
      L.upY.setValueAtTime(this._camUp.y, t);
      L.upZ.setValueAtTime(this._camUp.z, t);
    } else if (L.setPosition) {
      L.setPosition(camera.position.x * S, camera.position.y * S, camera.position.z * S);
      L.setOrientation(this._camFwd.x, this._camFwd.y, this._camFwd.z, this._camUp.x, this._camUp.y, this._camUp.z);
    }

    const spk = board.speakerAnchor;
    if (!spk) return;
    spk.getWorldPosition(this._spkPos);
    board.root.getWorldQuaternion(this._devQuat);
    this._spkFwd.set(0, 0, -1).applyQuaternion(this._devQuat).normalize();

    const P = this.graph.panner;
    if (P.positionX) {
      P.positionX.setValueAtTime(this._spkPos.x * S, t);
      P.positionY.setValueAtTime(this._spkPos.y * S, t);
      P.positionZ.setValueAtTime(this._spkPos.z * S, t);
      P.orientationX.setValueAtTime(this._spkFwd.x, t);
      P.orientationY.setValueAtTime(this._spkFwd.y, t);
      P.orientationZ.setValueAtTime(this._spkFwd.z, t);
    } else if (P.setPosition) {
      P.setPosition(this._spkPos.x * S, this._spkPos.y * S, this._spkPos.z * S);
      P.setOrientation(this._spkFwd.x, this._spkFwd.y, this._spkFwd.z);
    }

    this._toCam.copy(camera.position).sub(this._spkPos).normalize();
    const rearFacing = this._spkFwd.dot(this._toCam);
    const u = Math.max(0, Math.min(1, (rearFacing + 1) * 0.5));
    const cutHz = 9200 + u * 10200;
    this.graph.diffFilter.frequency.setTargetAtTime(cutHz, t, 0.04);
  }

  _pump() {
    if (!this.ctx || !this.graph || !this.host?.ready || this.ctx.state !== 'running') return;
    // Physically honor R21-23 (I2S) vs R25-27 (Relay) solder-bridge jumper!
    if (this.host.audioIsMuted() || (this.bus && !this.bus.isI2sAudioRouted())) {
      this.nextTime = this.ctx.currentTime;
      return;
    }
    const now = this.ctx.currentTime;
    if (this.nextTime < now) this.nextTime = now + 0.015;
    while (this.nextTime < now + 0.075) {
      const pcm = this.host.audioRender(AUDIO_CHUNK);
      if (!pcm) break;
      let peak = 0;
      for (let i = 0; i < AUDIO_CHUNK; i++) {
        const a = Math.abs(pcm[i]);
        if (a > peak) peak = a;
      }
      if (peak > 1e-5) {
        const ab = this.ctx.createBuffer(1, AUDIO_CHUNK, this.ctx.sampleRate);
        ab.getChannelData(0).set(pcm);
        const src = this.ctx.createBufferSource();
        src.buffer = ab;
        src.connect(this.graph.input);
        src.start(this.nextTime);
      }
      this.nextTime += AUDIO_CHUNK / this.ctx.sampleRate;
    }
  }
}
