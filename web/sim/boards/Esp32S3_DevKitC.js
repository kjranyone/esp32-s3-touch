/**
 * Esp32S3_DevKitC.js — Physically Accurate ESP32-S3-DevKitC-1-N16R8 + 400-Tie Breadboard + 0.96" OLED
 *
 * Implements the identical Board contract as Esp32_4848S040C so StudioScene,
 * Workbench, SpatialAudioEngine, and VirtualBus hot-swap entire boards cleanly!
 */
import * as THREE from 'three';
import { OledSsd1306 } from '../peripherals/OledSsd1306.js';

function makeBreadboardTexture() {
  const c = document.createElement('canvas');
  c.width = 1024;
  c.height = 768;
  const g = c.getContext('2d');

  g.fillStyle = '#e4e0d5';
  g.fillRect(0, 0, 1024, 768);

  // Power rail stripes (+ red, - blue)
  const drawRail = (yPlus, yMinus) => {
    g.fillStyle = '#cc3838'; g.fillRect(40, yPlus, 944, 6);
    g.fillStyle = '#2d68b8'; g.fillRect(40, yMinus, 944, 6);
  };
  drawRail(30, 118);
  drawRail(644, 732);

  // Center DIP channel groove
  g.fillStyle = '#cbc5b8';
  g.fillRect(0, 362, 1024, 44);

  // Tie-point holes (30 columns x 5 rows top + 5 rows bottom + power rails)
  g.fillStyle = '#1b1e24';
  for (let col = 0; col < 30; col++) {
    const x = 66 + col * 30.8;
    // Top power rail (groups of 5)
    if (col % 6 !== 5) {
      g.fillRect(x - 4, 52, 9, 9);
      g.fillRect(x - 4, 88, 9, 9);
      g.fillRect(x - 4, 668, 9, 9);
      g.fillRect(x - 4, 704, 9, 9);
    }
    for (let r = 0; r < 5; r++) {
      g.fillRect(x - 4, 168 + r * 36, 9, 9);
      g.fillRect(x - 4, 434 + r * 36, 9, 9);
    }
  }

  g.fillStyle = '#6c685e';
  g.font = '700 18px ui-monospace, monospace';
  for (const col of [1, 5, 10, 15, 20, 25, 30]) {
    const x = 66 + (col - 1) * 30.8 - 6;
    g.fillText(String(col), x, 152);
    g.fillText(String(col), x, 622);
  }

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  return tex;
}

function makeDevKitPcbTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 1024;
  const g = c.getContext('2d');

  // Authentic matte black Espressif DevKitC-1 solder mask
  g.fillStyle = '#14171d';
  g.fillRect(0, 0, 512, 1024);

  g.strokeStyle = 'rgba(240,246,255,0.65)';
  g.lineWidth = 3;
  g.strokeRect(10, 10, 492, 1004);

  // Subtle gold copper traces
  g.strokeStyle = '#252b36';
  g.lineWidth = 4;
  for (let i = 0; i < 22; i++) {
    const py = 128 + i * 38;
    g.beginPath(); g.moveTo(46, py); g.lineTo(148, py); g.stroke();
    g.beginPath(); g.moveTo(466, py); g.lineTo(364, py); g.stroke();
  }

  // 22-pin left & right header pads + real ESP32-S3-DevKitC-1 silkscreen pinout!
  const leftPins  = ['3V3','3V3','RST','4','5','6','7','15','16','17','18','8','3','46','9','10','11','12','13','14','5V','G'];
  const rightPins = ['G','TX','RX','1','2','42','41','40','39','38','37','36','35','0','45','48','47','21','20','19','G','G'];

  g.font = '700 18px ui-monospace, monospace';
  for (let i = 0; i < 22; i++) {
    const py = 128 + i * 38;
    // Left pad
    g.fillStyle = '#dfb958'; g.beginPath(); g.arc(34, py, 12, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#090b0f'; g.beginPath(); g.arc(34, py, 5, 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(242,248,255,0.88)'; g.fillText(leftPins[i], 54, py + 6);

    // Right pad
    g.fillStyle = '#dfb958'; g.beginPath(); g.arc(478, py, 12, 0, Math.PI * 2); g.fill();
    g.fillStyle = '#090b0f'; g.beginPath(); g.arc(478, py, 5, 0, Math.PI * 2); g.fill();
    g.fillStyle = 'rgba(242,248,255,0.88)';
    const rw = g.measureText(rightPins[i]).width;
    g.fillText(rightPins[i], 456 - rw, py + 6);
  }

  g.fillStyle = 'rgba(244,250,255,0.92)';
  g.font = '700 22px ui-monospace, monospace';
  g.fillText('ESP32-S3-DevKitC-1', 124, 598);
  g.font = '600 17px ui-monospace, monospace';
  g.fillStyle = 'rgba(200,218,238,0.8)';
  g.fillText('RGB IO48   PWR', 162, 742);
  g.fillText('BOOT          RST', 142, 868);
  g.fillText('USB          UART', 146, 988);

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 16;
  return tex;
}

function makeWroomShieldTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 512;
  const g = c.getContext('2d');
  const grad = g.createLinearGradient(0, 0, 512, 512);
  grad.addColorStop(0, '#cfd6df'); grad.addColorStop(0.5, '#b2bac6'); grad.addColorStop(1, '#98a1af');
  g.fillStyle = grad; g.fillRect(0, 0, 512, 512);
  g.strokeStyle = '#333944'; g.lineWidth = 6; g.strokeRect(18, 18, 476, 476);
  g.fillStyle = '#333944';
  g.font = '700 26px ui-monospace, monospace'; g.fillText('ESPRESSIF', 136, 110);
  g.font = '700 38px ui-monospace, monospace'; g.fillText('ESP32-S3', 56, 218);
  g.font = '700 30px ui-monospace, monospace'; g.fillText('WROOM-1', 56, 260);
  g.font = '600 22px ui-monospace, monospace'; g.fillText('N16R8  WiFi/BLE', 56, 314);
  g.fillText('FCC: 2AC7Z-ESPS3WROOM1', 56, 356);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

function makeMifaTexture() {
  const c = document.createElement('canvas');
  c.width = 512; c.height = 192;
  const g = c.getContext('2d');
  g.fillStyle = '#12161b'; g.fillRect(0, 0, 512, 192);
  g.strokeStyle = '#dfb958'; g.fillStyle = '#dfb958'; g.lineWidth = 18;
  g.fillRect(36, 128, 22, 64); g.fillRect(96, 128, 22, 64);
  g.beginPath();
  g.moveTo(47, 134); g.lineTo(47, 34); g.lineTo(192, 34); g.lineTo(192, 128);
  g.lineTo(266, 128); g.lineTo(266, 34); g.lineTo(348, 34); g.lineTo(348, 128);
  g.lineTo(432, 128); g.lineTo(432, 42); g.lineTo(474, 42); g.stroke();
  g.beginPath(); g.moveTo(107, 134); g.lineTo(107, 34); g.stroke();
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

function makeWire(p0, p1, lift, col) {
  // Dip cleanly down into the breadboard hole at p0 and arch up to p1
  const p0Up = p0.clone(); p0Up.y += 0.42;
  const m1 = p0Up.clone().lerp(p1, 0.35); m1.y = Math.max(m1.y, lift);
  const m2 = p0Up.clone().lerp(p1, 0.72); m2.y = Math.max(m2.y, lift);
  const curve = new THREE.CatmullRomCurve3([p0, p0Up, m1, m2, p1]);
  const grp = new THREE.Group();
  const mesh = new THREE.Mesh(
    new THREE.TubeGeometry(curve, 26, 0.036, 8, false),
    new THREE.MeshStandardMaterial({ color: col, roughness: 0.6, metalness: 0.05 })
  );
  mesh.castShadow = true;
  grp.add(mesh);
  // Black Dupont pin housing seated cleanly into breadboard tie-point
  const plug = new THREE.Mesh(
    new THREE.BoxGeometry(0.12, 0.34, 0.12),
    new THREE.MeshStandardMaterial({ color: '#181b22', roughness: 0.45 })
  );
  plug.position.set(p0.x, p0.y + 0.14, p0.z);
  grp.add(plug);
  return grp;
}

export class Esp32S3_DevKitC {
  constructor() {
    this.id = 'esp32s3_devkitc';
    this.name = 'ESP32-S3-DevKitC-1';
    this.root = new THREE.Group();
    this.interactables = [];

    this.oled = new OledSsd1306();
    this.dispW = this.oled.dispW;
    this.dispH = this.oled.dispH;
    this.lcdMesh = this.oled.lcdMesh;

    this._buildRig();
  }

  _buildRig() {
    /* 1. 400-Tie Half-Size Breadboard resting cleanly on the cutting mat */
    const bbMat = new THREE.MeshStandardMaterial({ map: makeBreadboardTexture(), roughness: 0.58, metalness: 0.04 });
    const bbSide = new THREE.MeshStandardMaterial({ color: '#d8d3c8', roughness: 0.62 });
    const breadboard = new THREE.Mesh(
      new THREE.BoxGeometry(10.6, 0.82, 6.8),
      [bbSide, bbSide, bbMat, bbSide, bbSide, bbSide]
    );
    breadboard.position.set(0, 0.41, 0.1);
    breadboard.castShadow = true;
    breadboard.receiveShadow = true;
    this.root.add(breadboard);

    /* 2. ESP32-S3-DevKitC-1-N16R8 seated on the left half of the breadboard */
    const dkGroup = new THREE.Group();
    dkGroup.position.set(-2.35, 1.16, 0.10);
    this.root.add(dkGroup);

    const dkSide = new THREE.MeshStandardMaterial({ color: '#101318', roughness: 0.6 });
    const dkTop = new THREE.MeshStandardMaterial({ map: makeDevKitPcbTexture(), roughness: 0.36, metalness: 0.18 });
    const dkPcb = new THREE.Mesh(
      new THREE.BoxGeometry(2.85, 0.14, 6.15),
      [dkSide, dkSide, dkTop, dkSide, dkSide, dkSide]
    );
    dkPcb.castShadow = true;
    dkPcb.receiveShadow = true;
    dkGroup.add(dkPcb);

    const metalMat = new THREE.MeshStandardMaterial({ color: '#c8cfd9', metalness: 0.88, roughness: 0.24 });
    const goldMat = new THREE.MeshStandardMaterial({ color: '#dfb958', metalness: 0.92, roughness: 0.22 });
    const icMat = new THREE.MeshStandardMaterial({ color: '#191b20', roughness: 0.38, metalness: 0.14 });

    // 44 Gold Header Pins plugging straight into the breadboard
    const pinGeo = new THREE.BoxGeometry(0.055, 0.44, 0.055);
    for (let i = 0; i < 22; i++) {
      const pz = -2.30 + i * 0.228;
      const pL = new THREE.Mesh(pinGeo, goldMat); pL.position.set(-1.24, -0.14, pz); dkGroup.add(pL);
      const pR = new THREE.Mesh(pinGeo, goldMat); pR.position.set( 1.24, -0.14, pz); dkGroup.add(pR);
    }

    // ESP32-S3-WROOM-1-N16R8 Module (antenna protruding cleanly at rear edge!)
    const wroomCarrier = new THREE.Mesh(new THREE.BoxGeometry(1.80, 0.09, 2.55), icMat);
    wroomCarrier.position.set(0, 0.11, -1.88);
    dkGroup.add(wroomCarrier);

    const ant = new THREE.Mesh(
      new THREE.PlaneGeometry(1.74, 0.68),
      new THREE.MeshStandardMaterial({ map: makeMifaTexture(), roughness: 0.42, metalness: 0.4 })
    );
    ant.rotation.x = -Math.PI / 2;
    ant.position.set(0, 0.16, -2.80);
    dkGroup.add(ant);

    const sSide = new THREE.MeshStandardMaterial({ color: '#b8c0cc', metalness: 0.9, roughness: 0.2 });
    const shield = new THREE.Mesh(
      new THREE.BoxGeometry(1.66, 0.28, 1.78),
      [sSide, sSide, new THREE.MeshStandardMaterial({ map: makeWroomShieldTexture(), metalness: 0.86, roughness: 0.22 }), sSide, sSide, sSide]
    );
    shield.position.set(0, 0.28, -1.52);
    shield.castShadow = true;
    dkGroup.add(shield);

    // CP2102N + AMS1117-3.3 LDO
    const cp2102 = new THREE.Mesh(new THREE.BoxGeometry(0.55, 0.10, 0.55), icMat);
    cp2102.position.set(0.45, 0.12, 1.55); dkGroup.add(cp2102);
    const ldo = new THREE.Mesh(new THREE.BoxGeometry(0.65, 0.14, 0.42), icMat);
    ldo.position.set(-0.48, 0.14, 1.48); dkGroup.add(ldo);

    // Dual USB-C Receptacles (USB OTG left + UART right)
    for (const ux of [-0.58, 0.58]) {
      const usbc = new THREE.Mesh(new THREE.BoxGeometry(0.84, 0.30, 0.68), metalMat);
      usbc.position.set(ux, 0.22, 2.86);
      usbc.castShadow = true;
      dkGroup.add(usbc);
    }

    // Onboard WS2812 RGB LED (GPIO 48) + Red Power LED
    this.rgbLedMat = new THREE.MeshBasicMaterial({ color: 0x70e2f4, fog: false });
    const rgbLed = new THREE.Mesh(new THREE.BoxGeometry(0.32, 0.11, 0.32), this.rgbLedMat);
    rgbLed.position.set(-0.22, 0.13, 0.88);
    dkGroup.add(rgbLed);
    this.rgbLight = new THREE.PointLight(0x70e2f4, 0.6, 4.5, 2.0);
    this.rgbLight.position.set(-0.22, 0.55, 0.88);
    dkGroup.add(this.rgbLight);

    const pwrLed = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.08, 0.12), new THREE.MeshBasicMaterial({ color: 0xff3b4d }));
    pwrLed.position.set(0.34, 0.12, 0.88);
    dkGroup.add(pwrLed);

    // Interactive BOOT & RST Tactile Switches
    for (const [bx, action, title] of [[-0.58, 'boot_btn', 'BOOT (GPIO0)'], [0.58, 'reset_btn', 'RST (Reboot Firmware)']]) {
      const swB = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.12, 0.30), metalMat);
      swB.position.set(bx, 0.13, 2.15); dkGroup.add(swB);
      const swC = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.08, 0.09, 12), icMat);
      swC.position.set(bx, 0.23, 2.15); dkGroup.add(swC);
      const hit = new THREE.Mesh(new THREE.BoxGeometry(0.54, 0.34, 0.46), new THREE.MeshBasicMaterial({ visible: false }));
      hit.position.set(bx, 0.22, 2.15);
      hit.userData = { action, title };
      dkGroup.add(hit);
      this.interactables.push(hit);
    }

    // Braided USB-C cable plugged into UART port
    const cableCurve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(-1.77, 1.38, 3.25),
      new THREE.Vector3(-1.77, 0.72, 4.65),
      new THREE.Vector3(-2.60, 0.16, 7.40),
      new THREE.Vector3(-4.20, 0.16, 14.0),
      new THREE.Vector3(-3.20, 0.16, 24.0),
    ]);
    const cable = new THREE.Mesh(
      new THREE.TubeGeometry(cableCurve, 48, 0.13, 10, false),
      new THREE.MeshStandardMaterial({ color: '#17191f', roughness: 0.78 })
    );
    cable.castShadow = true;
    this.root.add(cable);

    /* 3. Mounted 0.96" 128x64 SSD1306 OLED on angled cradle on the right half of breadboard */
    this.oledMount = new THREE.Group();
    this.oledMount.position.set(2.25, 2.62, 0.05);
    this.oledMount.rotation.x = -0.34;
    this.oledMount.add(this.oled.root);
    this.root.add(this.oledMount);

    // Sleek dark support bracket anchoring OLED to breadboard
    const bracket = new THREE.Mesh(
      new THREE.BoxGeometry(1.1, 1.65, 0.24),
      new THREE.MeshStandardMaterial({ color: '#1b1f27', metalness: 0.65, roughness: 0.38 })
    );
    bracket.position.set(2.25, 1.52, -0.42);
    bracket.rotation.x = -0.25;
    bracket.castShadow = true;
    this.root.add(bracket);

    /* 4. Small Piezo / Mini-Speaker Module on Breadboard (Spatial Audio Anchor!) */
    const spkBody = new THREE.Mesh(
      new THREE.CylinderGeometry(0.62, 0.62, 0.56, 24),
      new THREE.MeshStandardMaterial({ color: '#191c22', roughness: 0.42, metalness: 0.15 })
    );
    spkBody.position.set(2.25, 1.10, 2.25);
    spkBody.castShadow = true;
    this.root.add(spkBody);
    const spkHole = new THREE.Mesh(
      new THREE.CylinderGeometry(0.18, 0.18, 0.06, 16),
      new THREE.MeshStandardMaterial({ color: '#050608', roughness: 0.9 })
    );
    spkHole.position.set(2.25, 1.38, 2.25);
    this.root.add(spkHole);

    this.speakerAnchor = new THREE.Object3D();
    this.speakerAnchor.position.set(2.25, 1.42, 2.25);
    this.root.add(this.speakerAnchor);

    /* 5. 4 Colored I2C Dupont Jumper Wires connecting Breadboard Rails & DevKitC I2C -> 0.96" OLED */
    this.root.updateMatrixWorld(true);
    const wireColors = ['#22262e', '#d93838', '#34b868', '#e5c138']; // GND, VCC, SCL, SDA
    const dkTargets = [
      new THREE.Vector3(-0.68, 0.78,  2.72), // GND (bottom blue rail)
      new THREE.Vector3(-0.68, 0.78, -2.52), // 3V3 (top red rail)
      new THREE.Vector3(-0.68, 0.78,  1.42), // IO47 SCL tie-row
      new THREE.Vector3(-0.68, 0.78,  1.65), // IO21 SDA tie-row
    ];
    for (let i = 0; i < 4; i++) {
      const pEnd = new THREE.Vector3();
      this.oled.headerPins[i].getWorldPosition(pEnd);
      this.root.add(makeWire(dkTargets[i], pEnd, 3.8 + i * 0.16, wireColors[i]));
    }
  }

  setDisplaySize(w, h) {
    this.oled.setDisplaySize(w, h);
    this.dispW = this.oled.dispW;
    this.dispH = this.oled.dispH;
  }

  blitRgba(bytes, sampleGlow = false) {
    return this.oled.blitRgba(bytes, sampleGlow);
  }

  getFrontFitGeometry() {
    this.oledMount.updateWorldMatrix(true, false);
    const center = new THREE.Vector3(0, -0.16, 0.218).applyMatrix4(this.oledMount.matrixWorld);
    const q = new THREE.Quaternion();
    this.oledMount.getWorldQuaternion(q);
    const normal = new THREE.Vector3(0, 0, 1).applyQuaternion(q).normalize();
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(q).normalize();
    return { center, normal, up, frameSize: 4.35 };
  }

  getCameraPresets() {
    return {
      iso:   { pos: new THREE.Vector3(5.6, 8.8, 14.5), target: new THREE.Vector3(0.4, 1.7, 0.3) },
      front: { pos: new THREE.Vector3(2.25, 4.6, 9.8), target: new THREE.Vector3(2.25, 2.5, 0.05) },
      pcb:   { pos: new THREE.Vector3(-3.8, 8.6, 3.2), target: new THREE.Vector3(-2.1, 1.2, 0.0) },
    };
  }

  update(nowMs, bus) {
    // Mirror LED #0 onto the DevKitC's real onboard GPIO 48 WS2812 RGB LED!
    if (bus && bus.ledCount > 0) {
      const r = bus.ledRgb[0] / 255;
      const g = bus.ledRgb[1] / 255;
      const b = bus.ledRgb[2] / 255;
      this.rgbLedMat.color.setRGB(Math.min(1, r * 1.2 + 0.05), Math.min(1, g * 1.2 + 0.05), Math.min(1, b * 1.2 + 0.05));
      this.rgbLight.color.setRGB(r, g, b);
      this.rgbLight.intensity = Math.max(r, g, b) * 1.2;
    } else {
      const p = 0.4 + 0.6 * (0.5 + 0.5 * Math.sin(nowMs * 0.005));
      this.rgbLedMat.color.setRGB(0.15 * p, 0.75 * p, 1.0 * p);
      this.rgbLight.intensity = 0.4 * p;
    }
  }
}
