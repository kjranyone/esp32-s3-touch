/**
 * Esp32_4848S040C.js — Physically Accurate JingCai / Guition ESP32-4848S040C_I_Y_3 Board Module
 *
 * 1 Three.js unit = 10mm (86mm x 86mm x 10.2mm enclosure, 79.2mm x 79.2mm emerald PCB,
 * 4.0" 480x480 ST7701 RGB565 LCD + GT911 touch + NS4168 I2S amp / 3ch relay header).
 *
 * Interactive 3D hardware elements:
 *   - Front LCD touchscreen (direct UV raycast -> GT911 touch coordinates)
 *   - Rear SW2 RST tactile switch (click to hardware-reset running cartridge)
 *   - Rear R21-23 (I2S) <-> R25-27 (RLY) 0-ohm solder jumpers (click to re-route GPIO 1/2/40!)
 */
import * as THREE from 'three';
import { JUMPER_AUDIO_RELAY } from '../core/VirtualBus.js';

const PCB_SIZE = 7.92;
const PCB_TEX_N = 2048;
const pcbX = (x) => ((x + PCB_SIZE * 0.5) / PCB_SIZE) * PCB_TEX_N;
const pcbY = (y) => ((PCB_SIZE * 0.5 - y) / PCB_SIZE) * PCB_TEX_N;
const pcbLen = (u) => (u / PCB_SIZE) * PCB_TEX_N;

function roundedRectShape(w, h, r) {
  const s = new THREE.Shape();
  const x = -w / 2, y = -h / 2;
  s.moveTo(x + r, y);
  s.lineTo(x + w - r, y);
  s.quadraticCurveTo(x + w, y, x + w, y + r);
  s.lineTo(x + w, y + h - r);
  s.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  s.lineTo(x + r, y + h);
  s.quadraticCurveTo(x, y + h, x, y + h - r);
  s.lineTo(x, y + r);
  s.quadraticCurveTo(x, y, x + r, y);
  return s;
}

function makePcbTexture() {
  const c = document.createElement('canvas');
  c.width = PCB_TEX_N;
  c.height = PCB_TEX_N;
  const g = c.getContext('2d');

  const drawTrace = (pts, wMm = 0.20, col = '#1b6a42', clearance = true) => {
    if (pts.length < 2) return;
    const wPx = Math.max(2, pcbLen(wMm * 0.1));
    g.lineCap = 'round';
    g.lineJoin = 'round';
    if (clearance) {
      g.strokeStyle = '#082616';
      g.lineWidth = wPx + 5;
      g.beginPath();
      g.moveTo(pcbX(pts[0][0]), pcbY(pts[0][1]));
      for (let i = 1; i < pts.length; i++) g.lineTo(pcbX(pts[i][0]), pcbY(pts[i][1]));
      g.stroke();
    }
    g.strokeStyle = col;
    g.lineWidth = wPx;
    g.beginPath();
    g.moveTo(pcbX(pts[0][0]), pcbY(pts[0][1]));
    for (let i = 1; i < pts.length; i++) g.lineTo(pcbX(pts[i][0]), pcbY(pts[i][1]));
    g.stroke();
  };

  const drawVia = (x, y, rMm = 0.28, tented = false) => {
    const px = pcbX(x), py = pcbY(y), r = pcbLen(rMm * 0.1);
    g.fillStyle = tented ? '#1b643f' : '#d8b454';
    g.beginPath(); g.arc(px, py, r, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#050b07';
    g.beginPath(); g.arc(px, py, r * 0.44, 0, Math.PI * 2); g.fill();
  };

  const drawPad = (x, y, w, h, col = '#d8b454') => {
    const px = pcbX(x), py = pcbY(y), pw = pcbLen(w), ph = pcbLen(h);
    g.fillStyle = '#072214';
    g.fillRect(px - pw * 0.5 - 3, py - ph * 0.5 - 3, pw + 6, ph + 6);
    g.fillStyle = col;
    g.fillRect(px - pw * 0.5, py - ph * 0.5, pw, ph);
  };

  const drawOutline = (x, y, w, h, lbl = '', lx = null, ly = null) => {
    const px = pcbX(x), py = pcbY(y), pw = pcbLen(w), ph = pcbLen(h);
    g.strokeStyle = 'rgba(240, 246, 242, 0.78)';
    g.lineWidth = 3;
    g.strokeRect(px - pw * 0.5, py - ph * 0.5, pw, ph);
    if (lbl) {
      g.fillStyle = 'rgba(242, 248, 244, 0.88)';
      g.font = '700 22px ui-monospace, monospace';
      g.fillText(lbl, pcbX(lx !== null ? lx : x - w * 0.5), pcbY(ly !== null ? ly : y + h * 0.5 + 0.12));
    }
  };

  /* 1. Base FR4 Substrate & Emerald Ground Copper Pour */
  g.fillStyle = '#092b19';
  g.fillRect(0, 0, PCB_TEX_N, PCB_TEX_N);
  g.fillStyle = '#124b2e';
  const inset = pcbLen(0.14);
  g.fillRect(inset, inset, PCB_TEX_N - inset * 2, PCB_TEX_N - inset * 2);

  g.strokeStyle = 'rgba(255, 255, 255, 0.018)';
  g.lineWidth = 1;
  for (let i = 0; i < PCB_TEX_N; i += 16) {
    g.beginPath(); g.moveTo(i, 0); g.lineTo(i, PCB_TEX_N); g.stroke();
    g.beginPath(); g.moveTo(0, i); g.lineTo(PCB_TEX_N, i); g.stroke();
  }

  /* 2. Strict RF Antenna Keep-Out Zone under U1 */
  const koX0 = pcbX(0.12), koY0 = pcbY(3.92), koW = pcbLen(1.96), koH = pcbLen(1.98);
  g.fillStyle = '#072214';
  g.fillRect(koX0, koY0, koW, koH);
  g.save();
  g.beginPath(); g.rect(koX0, koY0, koW, koH); g.clip();
  g.strokeStyle = 'rgba(240, 246, 242, 0.16)';
  g.lineWidth = 2;
  for (let k = -koH; k < koW + koH; k += 22) {
    g.beginPath(); g.moveTo(koX0 + k, koY0); g.lineTo(koX0 + k - koH, koY0 + koH); g.stroke();
  }
  g.restore();
  g.strokeStyle = 'rgba(240, 246, 242, 0.45)';
  g.setLineDash([10, 8]);
  g.lineWidth = 2.5;
  g.strokeRect(koX0, koY0, koW, koH);
  g.setLineDash([]);
  g.fillStyle = 'rgba(240, 246, 242, 0.55)';
  g.font = '700 18px ui-monospace, monospace';
  g.fillText('ANTENNA KEEP-OUT', pcbX(0.24), pcbY(3.56));

  /* 3. 20-Line 45-deg Mitered ST7701 Parallel RGB565 Bus */
  for (let i = 0; i < 10; i++) {
    const uy = 1.62 - i * 0.127;
    const cx = -0.82 + i * 0.085;
    const turnY = -0.58 - i * 0.062;
    const turnX = -0.08 - i * 0.072;
    const pts = [[0.20, uy], [turnX, uy], [turnX - 0.14, uy - 0.14], [cx, turnY], [cx, -1.32]];
    if (i === 7) {
      pts.splice(3, 0, [cx + 0.04, turnY + 0.20], [cx - 0.04, turnY + 0.14], [cx + 0.04, turnY + 0.08]);
    }
    drawTrace(pts, 0.18, '#1f7a4c');
    drawVia(turnX - 0.07, uy - 0.07, 0.22, true);
  }
  for (let i = 0; i < 10; i++) {
    const ux = 0.40 + i * 0.127;
    const cx = 0.12 + i * 0.092;
    const midY = -0.56 - (9 - i) * 0.055;
    const pts = [[ux, -0.22], [ux, midY], [cx, midY - Math.abs(ux - cx) * 0.7], [cx, -1.32]];
    if (i === 8) {
      const mx = (ux + cx) * 0.5;
      pts.splice(2, 0, [mx + 0.06, midY - 0.08], [mx - 0.06, midY - 0.16], [mx + 0.06, midY - 0.24]);
    }
    drawTrace(pts, 0.18, '#228352');
  }

  /* 4. 3-Wire SPI Init + 19V Backlight Boost */
  drawTrace([[2.00, 0.86], [2.22, 0.86], [2.22, -1.02], [1.30, -1.02], [1.22, -1.32]], 0.18, '#1f7a4c');
  drawTrace([[2.00, 0.73], [2.30, 0.73], [2.30, -1.10], [1.38, -1.10], [1.30, -1.32]], 0.18, '#1f7a4c');
  drawTrace([[2.00, 0.60], [2.38, 0.60], [2.38, -2.52], [-1.32, -2.52], [-1.62, -2.22], [-1.62, -1.78]], 0.20, '#238a56');
  drawTrace([[-1.85, -1.55], [-1.32, -1.55], [-1.05, -1.55]], 0.45, '#2b9c62');

  /* 5. Shared I2S / 3ch Relay Mux Bus (IO1, IO2, IO40) */
  const i2sYs = [1.49, 1.36, 0.99];
  for (let i = 0; i < 3; i++) {
    const y0 = i2sYs[i];
    const rx = 2.08 + i * 0.09;
    const jx = -1.62 + i * 0.18;
    drawTrace([[2.00, y0], [rx + 0.36, y0], [rx + 0.36, -0.35 - i * 0.14], [2.52, -0.35 - i * 0.14]], 0.22, '#248c58');
    drawTrace([[2.00, y0], [rx, y0], [rx, -2.62 - i * 0.08], [jx, -2.62 - i * 0.08], [jx, -2.22]], 0.20, '#248c58');
    drawTrace([[jx, -2.14], [jx - 0.42, -2.14 - i * 0.08], [-2.35, -2.14 - i * 0.08]], 0.20, '#289860');
  }
  drawTrace([[-2.68, -2.42], [-2.68, -2.86]], 0.38, '#2ea468');
  drawTrace([[-2.46, -2.42], [-2.46, -2.86]], 0.38, '#2ea468');

  /* 6. USB-C Differential D+/D- -> CH340C -> 5V/3V3 Rails */
  drawTrace([[-0.12, -3.25], [-0.12, -2.84], [-1.72, -1.24], [-1.72, -0.52]], 0.22, '#289a62');
  drawTrace([[ 0.12, -3.25], [ 0.12, -2.76], [-1.58, -1.06], [-1.58, -0.52]], 0.22, '#289a62');
  drawTrace([[0.36, -3.28], [0.36, -3.02], [2.40, -3.02], [2.40, -2.18]], 0.72, '#2ea86a');
  drawTrace([[2.40, -1.72], [2.40, -0.95], [2.02, -0.95]], 0.62, '#2ea86a');
  drawTrace([[-1.45, -0.22], [-0.85, -0.22], [-0.55, 0.08], [0.20, 0.08]], 0.18, '#1f7a4c');
  drawTrace([[-1.45, -0.34], [-0.90, -0.34], [-0.55, -0.02], [0.20, -0.02]], 0.18, '#1f7a4c');

  /* 7. GT911 I2C & TF Card SPI */
  drawTrace([[0.20, 1.88], [-0.95, 1.88], [-1.42, 2.35], [-2.22, 2.35]], 0.20, '#228654');
  drawTrace([[0.20, 1.76], [-1.02, 1.76], [-1.49, 2.23], [-2.22, 2.23]], 0.20, '#228654');
  for (let i = 0; i < 4; i++) {
    const ty = 1.24 - i * 0.14;
    drawTrace([[0.20, ty], [-0.72 - i * 0.08, ty], [-1.18, ty - 0.12], [-1.85, ty - 0.12]], 0.18, '#1f784a');
  }

  /* 8. Perimeter GND Stitching Vias & RF Via Fence */
  for (let t = -3.45; t <= 3.45; t += 0.55) {
    if (Math.abs(t) < 2.8) { drawVia(t, 3.74, 0.26); drawVia(t, -3.74, 0.26); }
    if (Math.abs(t) < 2.9) { drawVia(-3.74, t, 0.26); drawVia(3.74, t, 0.26); }
  }
  for (let i = 0; i < 7; i++) drawVia(0.24 + i * 0.28, 1.94, 0.24);

  /* 9. 4x M2.5 Mounting Holes & 3 Fiducials */
  for (const [mx, my] of [[-3.52, 3.52], [3.52, 3.52], [-3.52, -3.52], [3.52, -3.52]]) {
    const px = pcbX(mx), py = pcbY(my);
    g.fillStyle = '#072214'; g.beginPath(); g.arc(px, py, pcbLen(0.32), 0, Math.PI * 2); g.fill();
    g.fillStyle = '#d8b454'; g.beginPath(); g.arc(px, py, pcbLen(0.26), 0, Math.PI * 2); g.fill();
    g.fillStyle = '#070a0d'; g.beginPath(); g.arc(px, py, pcbLen(0.145), 0, Math.PI * 2); g.fill();
    for (let k = 0; k < 8; k++) {
      const ang = (k * Math.PI) / 4;
      g.fillStyle = '#0b1014';
      g.beginPath();
      g.arc(px + Math.cos(ang) * pcbLen(0.205), py + Math.sin(ang) * pcbLen(0.205), 3.2, 0, Math.PI * 2);
      g.fill();
    }
  }
  for (const [fx, fy] of [[-3.48, 2.55], [3.25, 2.95], [-3.25, -2.95]]) {
    const px = pcbX(fx), py = pcbY(fy);
    g.fillStyle = '#072214'; g.beginPath(); g.arc(px, py, pcbLen(0.12), 0, Math.PI * 2); g.fill();
    g.fillStyle = '#e0bd5c'; g.beginPath(); g.arc(px, py, pcbLen(0.055), 0, Math.PI * 2); g.fill();
  }

  /* 10. R21-23 / R25-27 Pads, CC1/CC2, CN1 40P Fingers */
  for (let i = 0; i < 3; i++) {
    const rx = -1.62 + i * 0.18;
    drawPad(rx, -2.06, 0.08, 0.07); drawPad(rx, -2.20, 0.08, 0.07);
    drawPad(rx, -2.42, 0.08, 0.07); drawPad(rx, -2.56, 0.08, 0.07);
  }
  drawPad(-0.62, -3.26, 0.07, 0.07); drawPad(-0.62, -3.38, 0.07, 0.07);
  drawPad( 0.62, -3.26, 0.07, 0.07); drawPad( 0.62, -3.38, 0.07, 0.07);
  for (let i = 0; i < 32; i++) drawPad(-0.96 + i * 0.074, -1.32, 0.038, 0.14);

  /* 11. Silkscreen Legends */
  g.strokeStyle = 'rgba(240, 246, 242, 0.55)';
  g.lineWidth = 3;
  g.strokeRect(pcbX(-3.82), pcbY(3.82), pcbLen(7.64), pcbLen(7.64));

  g.fillStyle = 'rgba(244, 249, 246, 0.94)';
  g.font = '700 32px ui-monospace, monospace';
  g.fillText('ESP32-4848S040C_I_Y_3', pcbX(-2.96), pcbY(3.40));
  g.fillStyle = 'rgba(225, 240, 232, 0.78)';
  g.font = '600 19px ui-monospace, monospace';
  g.fillText('JCZN / GUITION  REV 1.2  2024', pcbX(-2.96), pcbY(3.16));
  g.fillText('ST7701 RGB565 · GT911 · NS4168', pcbX(-2.96), pcbY(2.94));

  drawOutline(1.10, 1.05, 1.88, 2.62, 'U1 ESP32-S3-WROOM-1', 0.12, -0.44);
  g.fillStyle = '#ffffff';
  g.beginPath(); g.arc(pcbX(0.08), pcbY(1.74), 7, 0, Math.PI * 2); g.fill();

  drawOutline(0.20, -1.55, 2.58, 0.58, 'CN1 40P RGB565 FPC', -1.08, -2.26);
  drawOutline(-2.55, 2.28, 0.96, 0.44, 'CN2 GT911 CTP', -3.05, 1.88);
  drawOutline(-2.55, 0.88, 1.52, 1.58, 'J1 TF_CARD', -3.28, -0.06);
  drawOutline(-1.95, -0.35, 1.08, 0.50, 'U2 CH340C', -2.50, -0.78);
  drawOutline(-1.72, -1.55, 0.86, 0.70, 'L1 22uH / U5 BL', -2.26, -1.10);
  drawOutline(-2.58, -2.25, 0.64, 0.54, 'U3 NS4168', -3.02, -1.86);
  drawOutline(-2.58, -2.96, 0.78, 0.54, 'J2 SPK', -2.96, -3.40);
  drawOutline(2.40, -1.95, 0.78, 0.72, 'U4 AMS1117-3.3', 1.80, -2.48);
  drawOutline(2.72, -0.35, 0.64, 2.18, 'J3 8P RELAY', 2.26, 0.90);

  g.fillStyle = 'rgba(244, 249, 246, 0.86)';
  g.font = '700 17px ui-monospace, monospace';
  g.fillText('R21-23:I2S', pcbX(-1.82), pcbY(-1.92));
  g.fillText('R25-27:RLY', pcbX(-1.82), pcbY(-2.74));
  g.fillText('Q1 Q2', pcbX(-1.24), pcbY(-0.52));
  g.fillText('5.1K CC1/CC2', pcbX(-0.76), pcbY(-2.92));
  g.fillText('USB1 TYPE-C', pcbX(-0.56), pcbY(-3.12));
  g.fillText('SW1 BOOT', pcbX(1.96), pcbY(-2.96));
  g.fillText('SW2 RST', pcbX(2.84), pcbY(-2.96));
  g.fillText('PWR  IO', pcbX(-1.14), pcbY(-3.04));

  g.font = '700 15px ui-monospace, monospace';
  const j3Lbls = ['5V', 'GND', 'IO40', 'IO2', 'IO1', '3V3', 'RX', 'TX'];
  for (let i = 0; i < 8; i++) g.fillText(j3Lbls[i], pcbX(3.12), pcbY(0.55 - i * 0.255));

  const tpNames = ['3V3', 'GND', 'TX', 'RX', 'IO0', 'EN'];
  for (let i = 0; i < tpNames.length; i++) {
    const tx = -2.02 + i * 0.22;
    const px = pcbX(tx), py = pcbY(-3.55);
    g.fillStyle = '#072214'; g.beginPath(); g.arc(px, py, pcbLen(0.08), 0, Math.PI * 2); g.fill();
    g.fillStyle = '#e0be5c'; g.beginPath(); g.arc(px, py, pcbLen(0.062), 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(244, 249, 246, 0.80)';
    g.font = '700 15px ui-monospace, monospace';
    g.fillText(tpNames[i], px - 13, py - 18);
  }

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 16;
  return tex;
}

function makeEspShieldTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 512;
  const g = c.getContext('2d');

  const grad = g.createLinearGradient(0, 0, 512, 512);
  grad.addColorStop(0, '#cfd6df');
  grad.addColorStop(0.5, '#b2bac6');
  grad.addColorStop(1, '#98a1af');
  g.fillStyle = grad;
  g.fillRect(0, 0, 512, 512);

  g.strokeStyle = 'rgba(255,255,255,0.09)';
  g.lineWidth = 2;
  for (let y = 0; y < 512; y += 5) {
    g.beginPath(); g.moveTo(0, y); g.lineTo(512, y); g.stroke();
  }
  g.strokeStyle = 'rgba(50, 56, 66, 0.22)';
  g.lineWidth = 6;
  g.strokeRect(18, 18, 476, 476);

  g.strokeStyle = '#333944';
  g.lineWidth = 7;
  g.beginPath(); g.arc(92, 108, 34, 0, Math.PI * 2); g.stroke();
  g.beginPath(); g.arc(92, 108, 18, -Math.PI * 0.75, Math.PI * 0.15); g.stroke();
  g.fillStyle = '#333944';
  g.beginPath(); g.arc(92, 108, 6, 0, Math.PI * 2); g.fill();

  g.font = '700 24px ui-monospace, monospace'; g.fillText('ESPRESSIF', 148, 100);
  g.font = '600 18px ui-monospace, monospace'; g.fillText('Wi-Fi · BLE 5.0', 148, 128);
  g.font = '700 36px ui-monospace, monospace'; g.fillText('ESP32-S3', 56, 212);
  g.font = '700 30px ui-monospace, monospace'; g.fillText('WROOM-1', 56, 254);

  g.font = '600 20px ui-monospace, monospace';
  g.fillStyle = '#454c58';
  g.fillText('N16R8  240MHz', 56, 304);
  g.fillText('FCC ID: 2AC7Z-ESPS3WROOM1', 56, 336);
  g.font = '700 22px ui-monospace, monospace';
  g.fillText('CE   RoHS', 56, 434);

  g.fillStyle = '#343a44';
  const qx = 342, qy = 352, qs = 10;
  for (let r = 0; r < 10; r++) {
    for (let col = 0; col < 10; col++) {
      const border = (col === 0 || r === 9) || (r === 0 && (col % 2 === 0)) || (col === 9 && (r % 2 === 1));
      const data = (((r * 11 + col * 5 + r * col) % 3) === 0);
      if (border || data) g.fillRect(qx + col * qs, qy + r * qs, qs - 1, qs - 1);
    }
  }

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 16;
  return tex;
}

function makeAntennaTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 192;
  const g = c.getContext('2d');
  g.fillStyle = '#12161b';
  g.fillRect(0, 0, 512, 192);
  g.strokeStyle = '#dfb958';
  g.fillStyle = '#dfb958';
  g.lineWidth = 18;
  g.lineCap = 'square';
  g.lineJoin = 'miter';
  g.fillRect(36, 128, 22, 64);
  g.fillRect(96, 128, 22, 64);
  g.beginPath();
  g.moveTo(47, 134); g.lineTo(47, 34); g.lineTo(192, 34); g.lineTo(192, 128);
  g.lineTo(266, 128); g.lineTo(266, 34); g.lineTo(348, 34); g.lineTo(348, 128);
  g.lineTo(432, 128); g.lineTo(432, 42); g.lineTo(474, 42);
  g.stroke();
  g.beginPath(); g.moveTo(107, 134); g.lineTo(107, 34); g.stroke();
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  return tex;
}

export class Esp32_4848S040C {
  constructor() {
    this.id = 'esp32_4848s040c';
    this.name = 'ESP32-4848S040C';
    this.bodyD = 0.92;
    this.lcdSize = 7.18;

    this.root = new THREE.Group();
    this.devicePivot = new THREE.Group();
    this.devicePivot.position.set(0, 4.38, 0);
    this.devicePivot.rotation.x = -0.14;
    this.root.add(this.devicePivot);

    this.interactables = [];

    // Offscreen Display Surface
    this.dispW = 480;
    this.dispH = 480;
    this.lcdCanvas = document.createElement('canvas');
    this.lcdCanvas.width = this.dispW;
    this.lcdCanvas.height = this.dispH;
    this.lcdCtx = this.lcdCanvas.getContext('2d', { alpha: false });
    this.lcdCtx.fillStyle = '#070a12';
    this.lcdCtx.fillRect(0, 0, this.dispW, this.dispH);
    this.imgData = this.lcdCtx.createImageData(this.dispW, this.dispH);

    this.lcdTexture = new THREE.CanvasTexture(this.lcdCanvas);
    this.lcdTexture.colorSpace = THREE.SRGBColorSpace;
    this.lcdTexture.minFilter = THREE.LinearFilter;
    this.lcdTexture.magFilter = THREE.NearestFilter;
    this.lcdTexture.generateMipmaps = false;

    this._buildHardware();
  }

  _buildHardware() {
    const bodyW = 8.6, bodyH = 8.6, bodyD = this.bodyD;

    // Physical NS4168 speaker acoustic anchor on rear PCB (J2 SPK)
    this.speakerAnchor = new THREE.Object3D();
    this.speakerAnchor.position.set(2.58, -2.96, -0.58);
    this.devicePivot.add(this.speakerAnchor);

    // Outer dark-metallic frame (86 x 86 x 10.2mm)
    const shellGeo = new THREE.ExtrudeGeometry(roundedRectShape(bodyW, bodyH, 0.3), {
      depth: bodyD, bevelEnabled: true, bevelThickness: 0.07, bevelSize: 0.06, bevelSegments: 4,
    });
    shellGeo.center();
    const shellMesh = new THREE.Mesh(
      shellGeo,
      new THREE.MeshStandardMaterial({ color: '#2a2e36', metalness: 0.62, roughness: 0.34 })
    );
    shellMesh.castShadow = true;
    shellMesh.receiveShadow = true;
    this.devicePivot.add(shellMesh);

    // Front cover glass bezel
    const bezelMesh = new THREE.Mesh(
      new THREE.ShapeGeometry(roundedRectShape(8.46, 8.46, 0.24)),
      new THREE.MeshPhysicalMaterial({
        color: '#0c0d11', roughness: 0.08, metalness: 0.06, clearcoat: 1.0, clearcoatRoughness: 0.06,
      })
    );
    bezelMesh.position.z = bodyD / 2 + 0.075;
    this.devicePivot.add(bezelMesh);

    // Active 4.0" RGB565 LCD (fog: false -> 100% distance-invariant brightness)
    this.lcdMesh = new THREE.Mesh(
      new THREE.PlaneGeometry(this.lcdSize, this.lcdSize),
      new THREE.MeshBasicMaterial({ map: this.lcdTexture, toneMapped: false, fog: false })
    );
    this.lcdMesh.position.z = bodyD / 2 + 0.082;
    this.lcdMesh.name = 'lcd-touch-surface';
    this.devicePivot.add(this.lcdMesh);

    // Subtle reflective cover glass over LCD
    const glassOverlay = new THREE.Mesh(
      new THREE.PlaneGeometry(this.lcdSize + 0.02, this.lcdSize + 0.02),
      new THREE.MeshPhysicalMaterial({
        color: '#ffffff', transparent: true, opacity: 0.035, roughness: 0.05, metalness: 0.0,
        clearcoat: 1.0, clearcoatRoughness: 0.04, depthWrite: false, fog: false,
      })
    );
    glassOverlay.position.z = bodyD / 2 + 0.092;
    this.devicePivot.add(glassOverlay);

    // Rear PCB Group
    const backGroup = new THREE.Group();
    backGroup.position.z = -(bodyD / 2 + 0.06);
    backGroup.rotation.y = Math.PI;
    this.devicePivot.add(backGroup);
    this.backGroup = backGroup;

    const pcbSideMat = new THREE.MeshStandardMaterial({ color: '#1b3824', roughness: 0.65, metalness: 0.08 });
    const pcbTopMat = new THREE.MeshStandardMaterial({ map: makePcbTexture(), roughness: 0.36, metalness: 0.22 });
    const pcbMesh = new THREE.Mesh(
      new THREE.BoxGeometry(7.92, 7.92, 0.14),
      [pcbSideMat, pcbSideMat, pcbSideMat, pcbSideMat, pcbTopMat, pcbSideMat]
    );
    pcbMesh.position.z = 0.07;
    pcbMesh.castShadow = true;
    pcbMesh.receiveShadow = true;
    backGroup.add(pcbMesh);

    const icMat = new THREE.MeshStandardMaterial({ color: '#191b20', roughness: 0.38, metalness: 0.14 });
    const metalMat = new THREE.MeshStandardMaterial({ color: '#c8cfd9', metalness: 0.88, roughness: 0.24 });
    const goldMat = new THREE.MeshStandardMaterial({ color: '#dfb958', metalness: 0.92, roughness: 0.22 });
    const kaptonMat = new THREE.MeshStandardMaterial({ color: '#c46a18', roughness: 0.34, metalness: 0.28 });
    const mlccMat = new THREE.MeshStandardMaterial({ color: '#9c6e3b', roughness: 0.42, metalness: 0.15 });
    const tantMat = new THREE.MeshStandardMaterial({ color: '#d8a632', roughness: 0.38, metalness: 0.10 });
    const fpcBeigeMat = new THREE.MeshStandardMaterial({ color: '#ded8c8', roughness: 0.50 });
    const whiteConnMat = new THREE.MeshStandardMaterial({ color: '#e9ecf0', roughness: 0.45 });

    const makeSmd0603 = (x, y, horiz, isRes = false, parent = backGroup) => {
      const grp = new THREE.Group();
      grp.position.set(x, y, 0);
      const w = horiz ? 0.14 : 0.075, h = horiz ? 0.075 : 0.14;
      const body = new THREE.Mesh(new THREE.BoxGeometry(w * 0.62, h * 0.95, 0.055), isRes ? icMat : mlccMat);
      body.position.z = 0.168;
      grp.add(body);
      const capGeo = new THREE.BoxGeometry(horiz ? w * 0.22 : w, horiz ? h : h * 0.22, 0.06);
      const off = (horiz ? w : h) * 0.38;
      const c1 = new THREE.Mesh(capGeo, metalMat); c1.position.set(horiz ? -off : 0, horiz ? 0 : -off, 0.17); grp.add(c1);
      const c2 = new THREE.Mesh(capGeo, metalMat); c2.position.set(horiz ? off : 0, horiz ? 0 : off, 0.17); grp.add(c2);
      parent.add(grp);
      return grp;
    };

    /* U1: ESP32-S3-WROOM-1-N16R8 */
    const espGroup = new THREE.Group();
    espGroup.position.set(1.10, 1.05, 0.14);
    backGroup.add(espGroup);

    const espCarrier = new THREE.Mesh(
      new THREE.BoxGeometry(1.80, 2.55, 0.085),
      new THREE.MeshStandardMaterial({ color: '#14181e', roughness: 0.52, metalness: 0.14 })
    );
    espCarrier.position.z = 0.042;
    espGroup.add(espCarrier);

    const antPlane = new THREE.Mesh(
      new THREE.PlaneGeometry(1.74, 0.68),
      new THREE.MeshStandardMaterial({ map: makeAntennaTexture(), roughness: 0.42, metalness: 0.42 })
    );
    antPlane.position.set(0, 0.91, 0.088);
    espGroup.add(antPlane);

    const shieldSideMat = new THREE.MeshStandardMaterial({ color: '#b8c0cc', metalness: 0.90, roughness: 0.20 });
    const shieldCan = new THREE.Mesh(
      new THREE.BoxGeometry(1.66, 1.78, 0.28),
      [shieldSideMat, shieldSideMat, shieldSideMat, shieldSideMat,
        new THREE.MeshStandardMaterial({ map: makeEspShieldTexture(), metalness: 0.86, roughness: 0.22 }),
        shieldSideMat]
    );
    shieldCan.position.set(0, -0.35, 0.22);
    shieldCan.castShadow = true;
    espGroup.add(shieldCan);

    const padSideGeo = new THREE.BoxGeometry(0.095, 0.055, 0.058);
    const padBotGeo = new THREE.BoxGeometry(0.055, 0.095, 0.058);
    for (let i = 0; i < 14; i++) {
      const py = 0.57 - i * 0.127;
      const pL = new THREE.Mesh(padSideGeo, goldMat); pL.position.set(-0.90, py, 0.046); espGroup.add(pL);
      const pR = new THREE.Mesh(padSideGeo, goldMat); pR.position.set( 0.90, py, 0.046); espGroup.add(pR);
    }
    for (let i = 0; i < 12; i++) {
      const px = -0.70 + i * 0.127;
      const pB = new THREE.Mesh(padBotGeo, goldMat); pB.position.set(px, -1.27, 0.046); espGroup.add(pB);
    }

    /* CN1 40P FPC + Ribbon */
    const cn1Body = new THREE.Mesh(new THREE.BoxGeometry(2.50, 0.44, 0.19), fpcBeigeMat);
    cn1Body.position.set(0.20, -1.55, 0.235); backGroup.add(cn1Body);
    const cn1Latch = new THREE.Mesh(new THREE.BoxGeometry(2.38, 0.15, 0.06), icMat);
    cn1Latch.position.set(0.20, -1.43, 0.34); backGroup.add(cn1Latch);
    const fpcRibbon = new THREE.Mesh(new THREE.BoxGeometry(2.16, 0.52, 0.025), kaptonMat);
    fpcRibbon.position.set(0.20, -1.86, 0.20); fpcRibbon.rotation.x = 0.18; backGroup.add(fpcRibbon);

    /* CN2 6P GT911 FPC + Ribbon */
    const cn2Body = new THREE.Mesh(new THREE.BoxGeometry(0.86, 0.36, 0.16), fpcBeigeMat);
    cn2Body.position.set(-2.55, 2.28, 0.22); backGroup.add(cn2Body);
    const cn2Latch = new THREE.Mesh(new THREE.BoxGeometry(0.76, 0.12, 0.05), icMat);
    cn2Latch.position.set(-2.55, 2.38, 0.31); backGroup.add(cn2Latch);
    const ctpRibbon = new THREE.Mesh(new THREE.BoxGeometry(0.64, 0.42, 0.02), kaptonMat);
    ctpRibbon.position.set(-2.55, 2.55, 0.19); ctpRibbon.rotation.x = -0.18; backGroup.add(ctpRibbon);

    /* J1 MicroSD Cage */
    const tfSlot = new THREE.Mesh(new THREE.BoxGeometry(1.42, 1.50, 0.19), metalMat);
    tfSlot.position.set(-2.55, 0.88, 0.235); tfSlot.castShadow = true; backGroup.add(tfSlot);
    const tfCutout = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.72, 0.04), icMat);
    tfCutout.position.set(-2.55, 0.88, 0.335); backGroup.add(tfCutout);

    /* U2 CH340C + Q1/Q2 */
    const ch340 = new THREE.Mesh(new THREE.BoxGeometry(0.98, 0.39, 0.14), icMat);
    ch340.position.set(-1.95, -0.35, 0.21); backGroup.add(ch340);
    const leadGeo = new THREE.BoxGeometry(0.045, 0.09, 0.04);
    for (let i = 0; i < 8; i++) {
      const lx = -2.35 + i * 0.114;
      const lT = new THREE.Mesh(leadGeo, metalMat); lT.position.set(lx, -0.12, 0.16); backGroup.add(lT);
      const lB = new THREE.Mesh(leadGeo, metalMat); lB.position.set(lx, -0.58, 0.16); backGroup.add(lB);
    }
    for (const qx of [-1.26, -1.00]) {
      const q = new THREE.Mesh(new THREE.BoxGeometry(0.18, 0.11, 0.08), icMat);
      q.position.set(qx, -0.35, 0.18); backGroup.add(q);
    }

    /* U5 + L1 + D1 Backlight Boost */
    const l1 = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.42, 0.22),
      new THREE.MeshStandardMaterial({ color: '#2b2e35', roughness: 0.48, metalness: 0.28 }));
    l1.position.set(-1.92, -1.48, 0.25); l1.castShadow = true; backGroup.add(l1);
    const d1 = new THREE.Mesh(new THREE.BoxGeometry(0.32, 0.18, 0.14), icMat);
    d1.position.set(-1.45, -1.42, 0.21); backGroup.add(d1);
    const d1Band = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.185, 0.145), metalMat);
    d1Band.position.set(-1.34, -1.42, 0.21); backGroup.add(d1Band);
    const u5 = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.18, 0.09), icMat);
    u5.position.set(-1.48, -1.72, 0.185); backGroup.add(u5);

    /* U3 NS4168 + Movable 0-ohm Jumper Bank (R21-23 <-> R25-27!) + J2 SPK */
    const ns4168 = new THREE.Mesh(new THREE.BoxGeometry(0.49, 0.39, 0.13), icMat);
    ns4168.position.set(-2.58, -2.25, 0.205); backGroup.add(ns4168);
    for (let i = 0; i < 4; i++) {
      const lx = -2.75 + i * 0.115;
      const lT = new THREE.Mesh(leadGeo, metalMat); lT.position.set(lx, -2.02, 0.16); backGroup.add(lT);
      const lB = new THREE.Mesh(leadGeo, metalMat); lB.position.set(lx, -2.48, 0.16); backGroup.add(lB);
    }

    // Dynamic 0-ohm resistor bank that shifts between y = -2.13 (R21-23 I2S) and y = -2.49 (R25-27 Relay)
    this.jumperResistors = [];
    for (let i = 0; i < 3; i++) {
      this.jumperResistors.push(makeSmd0603(-1.62 + i * 0.18, -2.13, false, true));
    }
    // Invisible hit-box over R21-23 / R25-27 jumper bank so user can click to re-solder in 3D!
    const jumperHit = new THREE.Mesh(
      new THREE.BoxGeometry(0.76, 0.88, 0.22),
      new THREE.MeshBasicMaterial({ visible: false })
    );
    jumperHit.position.set(-1.44, -2.31, 0.24);
    jumperHit.userData = { action: 'toggle_jumper', jumperId: JUMPER_AUDIO_RELAY, title: 'R21-23 (I2S) / R25-27 (Relay) 0Ω Jumper' };
    backGroup.add(jumperHit);
    this.interactables.push(jumperHit);

    const jst = new THREE.Mesh(new THREE.BoxGeometry(0.66, 0.44, 0.30), whiteConnMat);
    jst.position.set(-2.58, -2.96, 0.29); backGroup.add(jst);

    /* U4 AMS1117-3.3 + C1/C2 */
    const ldoBody = new THREE.Mesh(new THREE.BoxGeometry(0.65, 0.35, 0.15), icMat);
    ldoBody.position.set(2.40, -1.95, 0.215); backGroup.add(ldoBody);
    const ldoTab = new THREE.Mesh(new THREE.BoxGeometry(0.32, 0.16, 0.08), metalMat);
    ldoTab.position.set(2.40, -1.72, 0.18); backGroup.add(ldoTab);
    for (const lx of [2.17, 2.40, 2.63]) {
      const l = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.14, 0.06), metalMat);
      l.position.set(lx, -2.17, 0.17); backGroup.add(l);
    }
    for (const [cx, cy] of [[1.82, -2.02], [2.98, -1.92]]) {
      const tc = new THREE.Mesh(new THREE.BoxGeometry(0.30, 0.17, 0.15), tantMat);
      tc.position.set(cx, cy, 0.215); backGroup.add(tc);
    }

    /* J3 8-Pin Shrouded Relay Header (stores world anchor points for Phase 2 Dupont wiring!) */
    const j3Shroud = new THREE.Mesh(new THREE.BoxGeometry(0.46, 2.08, 0.42), icMat);
    j3Shroud.position.set(2.72, -0.35, 0.35); j3Shroud.castShadow = true; backGroup.add(j3Shroud);
    this.j3Pins = {};
    const j3Names = ['5V', 'GND', 'IO40', 'IO2', 'IO1', '3V3', 'RX', 'TX'];
    for (let i = 0; i < 8; i++) {
      const pin = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.06, 0.28), goldMat);
      pin.position.set(2.72, 0.54 - i * 0.255, 0.44);
      backGroup.add(pin);
      this.j3Pins[j3Names[i]] = pin;
    }

    /* USB-C Receptacle + CC1/CC2 */
    const usbcReceptacle = new THREE.Mesh(new THREE.BoxGeometry(0.90, 0.70, 0.32), metalMat);
    usbcReceptacle.position.set(0, -3.56, 0.30); backGroup.add(usbcReceptacle);
    makeSmd0603(-0.62, -3.32, false, true);
    makeSmd0603( 0.62, -3.32, false, true);

    /* Decoupling Caps & Pullups */
    makeSmd0603(-0.02, 1.80, true, false);
    makeSmd0603(-0.02, 1.62, true, false);
    makeSmd0603(-1.95, 2.35, false, true);
    makeSmd0603(-1.78, 2.35, false, true);
    makeSmd0603(-1.95, -0.78, true, false);
    makeSmd0603(-2.12, -2.25, false, false);
    makeSmd0603( 1.98, -3.28, true, true);
    makeSmd0603( 2.74, -3.28, true, true);

    /* SW1 (BOOT / IO0) & SW2 (RST / EN) Interactive Tactile Switches */
    for (const [bx, action, title] of [[2.32, 'boot_btn', 'SW1 BOOT (GPIO0)'], [3.06, 'reset_btn', 'SW2 RST (Reboot Firmware)']]) {
      const swBase = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.26, 0.13), metalMat);
      swBase.position.set(bx, -3.28, 0.205);
      backGroup.add(swBase);
      const swCap = new THREE.Mesh(new THREE.CylinderGeometry(0.075, 0.075, 0.08, 12), icMat);
      swCap.rotation.x = Math.PI / 2;
      swCap.position.set(bx, -3.28, 0.30);
      backGroup.add(swCap);
      const hit = new THREE.Mesh(new THREE.BoxGeometry(0.50, 0.44, 0.28), new THREE.MeshBasicMaterial({ visible: false }));
      hit.position.set(bx, -3.28, 0.26);
      hit.userData = { action, title, cap: swCap };
      backGroup.add(hit);
      this.interactables.push(hit);
    }

    /* 4 Corner M2.5 Standoff Screws */
    for (const [sx, sy] of [[-3.52, 3.52], [3.52, 3.52], [-3.52, -3.52], [3.52, -3.52]]) {
      const sc = new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.15, 0.06, 16), metalMat);
      sc.rotation.x = Math.PI / 2;
      sc.position.set(sx, sy, 0.17);
      backGroup.add(sc);
    }

    /* PWR & IO Status LEDs */
    const pwrLed = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.09, 0.07), new THREE.MeshBasicMaterial({ color: 0xff3b4d }));
    pwrLed.position.set(-1.02, -3.28, 0.175); backGroup.add(pwrLed);

    this.actLedMat = new THREE.MeshBasicMaterial({ color: 0x54f0ff });
    const actLed = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.09, 0.07), this.actLedMat);
    actLed.position.set(-0.72, -3.28, 0.175); backGroup.add(actLed);

    const pcbLedGlow = new THREE.PointLight(0xff4055, 0.50, 3.2, 2);
    pcbLedGlow.position.set(-0.88, -3.28, 0.55); backGroup.add(pcbLedGlow);

    /* Minimalist Aluminum Desk Stand */
    const standMat = new THREE.MeshStandardMaterial({ color: '#1b1e25', metalness: 0.72, roughness: 0.34 });
    const standBase = new THREE.Mesh(new THREE.BoxGeometry(4.4, 0.14, 2.2), standMat);
    standBase.position.set(0, 0.07, -0.35);
    standBase.castShadow = true; standBase.receiveShadow = true;
    this.root.add(standBase);

    for (const sx of [-1.55, 1.55]) {
      const foot = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.75, 0.42), standMat);
      foot.position.set(sx, 0.4, 0.48);
      foot.rotation.x = -0.14;
      foot.castShadow = true;
      this.root.add(foot);
    }

    /* Braided USB-C Cable */
    const cableCurve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(0, 0.85, -0.78),
      new THREE.Vector3(0, 0.36, -1.75),
      new THREE.Vector3(0.55, 0.15, -3.8),
      new THREE.Vector3(2.1, 0.15, -7.8),
      new THREE.Vector3(1.4, 0.15, -15.0),
      new THREE.Vector3(2.6, 0.15, -26.0),
    ]);
    const cableMesh = new THREE.Mesh(
      new THREE.TubeGeometry(cableCurve, 64, 0.14, 12, false),
      new THREE.MeshStandardMaterial({ color: '#17191f', roughness: 0.78, metalness: 0.08 })
    );
    cableMesh.castShadow = true; cableMesh.receiveShadow = true;
    this.root.add(cableMesh);

    const plugBoot = new THREE.Mesh(
      new THREE.BoxGeometry(0.64, 0.68, 0.34),
      new THREE.MeshStandardMaterial({ color: '#1b1e26', roughness: 0.55, metalness: 0.2 })
    );
    plugBoot.position.set(0, 0.94, -0.68);
    plugBoot.rotation.x = 0.28;
    plugBoot.castShadow = true;
    this.root.add(plugBoot);
  }

  setDisplaySize(w, h) {
    if (w === this.dispW && h === this.dispH) return;
    this.dispW = w;
    this.dispH = h;
    this.lcdCanvas.width = w;
    this.lcdCanvas.height = h;
    this.imgData = this.lcdCtx.createImageData(w, h);
    this.lcdTexture.needsUpdate = true;
  }

  blitRgba(bytes, sampleGlow = false) {
    if (!bytes) return null;
    this.imgData.data.set(bytes);
    this.lcdCtx.putImageData(this.imgData, 0, 0);
    this.lcdTexture.needsUpdate = true;
    if (!sampleGlow) return null;
    let r = 0, g = 0, b = 0, n = 0;
    const w = this.dispW, h = this.dispH;
    const stepY = Math.max(1, (h / 12) | 0);
    const stepX = Math.max(1, (w / 12) | 0);
    for (let y = (h * 0.15) | 0; y < (h * 0.85) | 0; y += stepY) {
      for (let x = (w * 0.1) | 0; x < (w * 0.9) | 0; x += stepX) {
        const idx = (y * w + x) * 4;
        r += bytes[idx]; g += bytes[idx + 1]; b += bytes[idx + 2]; n++;
      }
    }
    if (n === 0) return null;
    return [
      Math.min(1, (r / n) / 200),
      Math.min(1, (g / n) / 200),
      Math.min(1, (b / n) / 200),
    ];
  }

  getFrontFitGeometry() {
    this.devicePivot.updateWorldMatrix(true, false);
    const center = new THREE.Vector3(0, 0, this.bodyD / 2 + 0.082).applyMatrix4(this.devicePivot.matrixWorld);
    const normal = new THREE.Vector3(0, 0, 1).applyQuaternion(this.devicePivot.quaternion).normalize();
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.devicePivot.quaternion).normalize();
    return { center, normal, up, frameSize: this.lcdSize + 0.36 };
  }

  getCameraPresets() {
    return {
      iso:   { pos: new THREE.Vector3(6.8, 7.6, 18.4),  target: new THREE.Vector3(0, 4.1, 0) },
      front: { pos: new THREE.Vector3(0.0, 6.2, 16.8),  target: new THREE.Vector3(0, 4.15, 0) },
      pcb:   { pos: new THREE.Vector3(-5.6, 6.4, -17.4), target: new THREE.Vector3(0, 4.1, 0) },
    };
  }

  update(nowMs, bus) {
    // Heartbeat / GPIO activity indicator on IO LED
    const anyIo = bus && (bus.gpioOut[1] || bus.gpioOut[2] || bus.gpioOut[40]);
    const pulse = 0.45 + 0.55 * (0.5 + 0.5 * Math.sin(nowMs * 0.006));
    if (anyIo) {
      this.actLedMat.color.setRGB(0.30 + 0.70 * pulse, 0.95 * pulse, 0.52 * pulse);
    } else {
      this.actLedMat.color.setRGB(0.18 * pulse, 0.85 * pulse, 1.0 * pulse);
    }

    // Physically reflect R21-23 (I2S) vs R25-27 (Relay) 0-ohm resistor placement
    const relayMode = bus ? bus.isRelayRouted() : false;
    const targetY = relayMode ? -2.49 : -2.13;
    for (const r of this.jumperResistors) {
      r.position.y += (targetY - r.position.y) * 0.25;
    }
  }
}
