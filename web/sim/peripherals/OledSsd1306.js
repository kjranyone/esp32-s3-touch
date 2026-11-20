/**
 * OledSsd1306.js — Pluggable 0.96" 128x64 SSD1306 I2C OLED Display Module
 *
 * Authentic royal-blue 27.3mm x 27.8mm breakout PCB with 4-pin header
 * (GND / VCC / SCL / SDA), ribbon flex fold, black bezel glass, and razor-sharp
 * nearest-neighbor 128x64 emissive CanvasTexture (fog: false).
 */
import * as THREE from 'three';

function makeOledPcbTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 512;
  const g = c.getContext('2d');
  g.fillStyle = '#0f3668';
  g.fillRect(0, 0, 512, 512);
  g.strokeStyle = 'rgba(240,248,255,0.78)';
  g.lineWidth = 4;
  g.strokeRect(14, 14, 484, 484);

  // 4 mounting holes
  for (const [mx, my] of [[44, 44], [468, 44], [44, 468], [468, 468]]) {
    g.fillStyle = '#dfc060';
    g.beginPath(); g.arc(mx, my, 22, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#080c12';
    g.beginPath(); g.arc(mx, my, 12, 0, Math.PI * 2); g.fill();
  }

  // 4-pin I2C header labels at top
  const lbls = ['GND', 'VCC', 'SCL', 'SDA'];
  g.font = '700 24px ui-monospace, monospace';
  g.fillStyle = 'rgba(244,250,255,0.92)';
  for (let i = 0; i < 4; i++) {
    const px = 166 + i * 60;
    g.fillStyle = '#e0be5a';
    g.beginPath(); g.arc(px, 44, 13, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#0a0e14';
    g.beginPath(); g.arc(px, 44, 6, 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(244,250,255,0.92)';
    g.fillText(lbls[i], px - 22, 86);
  }

  g.font = '700 22px ui-monospace, monospace';
  g.fillText('0.96" I2C OLED 128x64', 102, 486);

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

export class OledSsd1306 {
  constructor() {
    this.root = new THREE.Group();
    this.dispW = 128;
    this.dispH = 64;

    // 128x64 emissive CanvasTexture
    this.canvas = document.createElement('canvas');
    this.canvas.width = this.dispW;
    this.canvas.height = this.dispH;
    this.ctx = this.canvas.getContext('2d', { alpha: false });
    this.ctx.fillStyle = '#030508';
    this.ctx.fillRect(0, 0, this.dispW, this.dispH);
    this.imgData = this.ctx.createImageData(this.dispW, this.dispH);

    this.texture = new THREE.CanvasTexture(this.canvas);
    this.texture.colorSpace = THREE.SRGBColorSpace;
    this.texture.minFilter = THREE.NearestFilter;
    this.texture.magFilter = THREE.NearestFilter;
    this.texture.generateMipmaps = false;

    // Physical scale slightly hero-sized on breadboard (~38mm x 38mm carrier, 34mm x 17mm active area)
    this.pcbW = 4.2;
    this.pcbH = 3.6;
    this.activeW = 3.64;
    this.activeH = 1.82;

    this._build();
  }

  _build() {
    const pcbSide = new THREE.MeshStandardMaterial({ color: '#0b284d', roughness: 0.6 });
    const pcbFace = new THREE.MeshStandardMaterial({ map: makeOledPcbTexture(), roughness: 0.38, metalness: 0.16 });
    const pcb = new THREE.Mesh(
      new THREE.BoxGeometry(this.pcbW, this.pcbH, 0.14),
      [pcbSide, pcbSide, pcbSide, pcbSide, pcbFace, pcbSide]
    );
    pcb.castShadow = true;
    pcb.receiveShadow = true;
    this.root.add(pcb);

    // 4-pin gold header along top edge
    const goldMat = new THREE.MeshStandardMaterial({ color: '#dfb958', metalness: 0.9, roughness: 0.22 });
    const darkMat = new THREE.MeshStandardMaterial({ color: '#181b22', roughness: 0.45 });
    const hdrBase = new THREE.Mesh(new THREE.BoxGeometry(1.72, 0.28, 0.24), darkMat);
    hdrBase.position.set(0, 1.48, 0.18);
    this.root.add(hdrBase);

    this.headerPins = [];
    for (let i = 0; i < 4; i++) {
      const px = -0.66 + i * 0.44;
      const pin = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.06, 0.46), goldMat);
      pin.position.set(px, 1.48, 0.26);
      this.root.add(pin);
      this.headerPins.push(pin);
    }

    // Black OLED glass bezel
    const glassBezel = new THREE.Mesh(
      new THREE.BoxGeometry(3.92, 2.34, 0.14),
      new THREE.MeshPhysicalMaterial({ color: '#06070a', roughness: 0.08, clearcoat: 1.0, clearcoatRoughness: 0.05 })
    );
    glassBezel.position.set(0, -0.18, 0.14);
    this.root.add(glassBezel);

    // Active 128x64 Self-Emissive OLED Pixel Surface (fog: false!)
    this.lcdMesh = new THREE.Mesh(
      new THREE.PlaneGeometry(this.activeW, this.activeH),
      new THREE.MeshBasicMaterial({ map: this.texture, toneMapped: false, fog: false })
    );
    this.lcdMesh.position.set(0, -0.16, 0.218);
    this.lcdMesh.name = 'oled-surface';
    this.root.add(this.lcdMesh);

    // Amber Kapton bottom fold ribbon
    const ribbon = new THREE.Mesh(
      new THREE.BoxGeometry(1.45, 0.28, 0.04),
      new THREE.MeshStandardMaterial({ color: '#c46a18', roughness: 0.38, metalness: 0.25 })
    );
    ribbon.position.set(0, -1.42, 0.11);
    this.root.add(ribbon);
  }

  setDisplaySize(w, h) {
    if (w === this.dispW && h === this.dispH) return;
    this.dispW = w;
    this.dispH = h;
    this.canvas.width = w;
    this.canvas.height = h;
    this.imgData = this.ctx.createImageData(w, h);
    this.texture.needsUpdate = true;
  }

  blitRgba(bytes, sampleGlow = false) {
    if (!bytes) return null;
    this.imgData.data.set(bytes);
    this.ctx.putImageData(this.imgData, 0, 0);
    this.texture.needsUpdate = true;
    if (!sampleGlow) return null;
    let r = 0, g = 0, b = 0, n = 0;
    for (let i = 0; i < bytes.length; i += 16) {
      r += bytes[i]; g += bytes[i + 1]; b += bytes[i + 2]; n++;
    }
    if (n === 0) return null;
    return [
      Math.min(1, (r / n) / 110 + 0.06),
      Math.min(1, (g / n) / 110 + 0.12),
      Math.min(1, (b / n) / 110 + 0.16),
    ];
  }
}
