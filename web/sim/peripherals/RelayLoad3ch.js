/**
 * RelayLoad3ch.js — 3-Channel SRD-05VDC Relay Module + Real Driven Desk Loads
 *
 * Wired via 5 colored Dupont jumper leads directly to ESP32-4848S040C J3 8P RELAY
 * (5V, GND, IO1, IO2, IO40). Physically obeys the R25-27 vs R21-23 solder jumper:
 * relays ONLY energize when R25-27 is active (bus.isRelayRouted()).
 *
 * Driven Loads:
 *   CH1 (GPIO 1)  -> Warm Filament Edison Lamp (glowing coil + golden PointLight)
 *   CH2 (GPIO 2)  -> 40mm Brushless Cooling Fan (7-blade impeller w/ spin-up inertia)
 *   CH3 (GPIO 40) -> Amber Rotating Beacon (spinning reflector + amber PointLight)
 */
import * as THREE from 'three';

function makeDupontWire(pStart, pEnd, midLift, colorHex) {
  const mid1 = pStart.clone().lerp(pEnd, 0.32);
  mid1.y = Math.max(mid1.y, midLift);
  const mid2 = pStart.clone().lerp(pEnd, 0.68);
  mid2.y = Math.max(mid2.y, midLift * 0.72);
  const curve = new THREE.CatmullRomCurve3([pStart, mid1, mid2, pEnd]);
  const mesh = new THREE.Mesh(
    new THREE.TubeGeometry(curve, 28, 0.042, 8, false),
    new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.62, metalness: 0.05 })
  );
  mesh.castShadow = true;
  return mesh;
}

function makeRelaySilkTexture() {
  const c = document.createElement('canvas');
  c.width = 512;
  c.height = 512;
  const g = c.getContext('2d');
  g.fillStyle = '#0e3c24';
  g.fillRect(0, 0, 512, 512);
  g.strokeStyle = 'rgba(240,248,242,0.75)';
  g.lineWidth = 4;
  g.strokeRect(14, 14, 484, 484);

  g.fillStyle = 'rgba(244,250,246,0.92)';
  g.font = '700 26px ui-monospace, monospace';
  g.fillText('3CH RELAY + LOAD BAY', 28, 52);
  g.font = '600 18px ui-monospace, monospace';
  g.fillStyle = 'rgba(220,242,230,0.82)';
  g.fillText('J3: IO1/K1  IO2/K2  IO40/K3', 28, 82);
  g.fillText('K1 LAMP', 38, 486);
  g.fillText('K2 FAN', 202, 486);
  g.fillText('K3 BEACON', 344, 486);

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

export class RelayLoad3ch {
  constructor(board, { position = [-7.1, 0, -0.8], rotationY = 0.24 } = {}) {
    this.id = 'relay_load_3ch';
    this.name = '3ch Relay + Loads';
    this.root = new THREE.Group();
    this.interactables = [];

    this.board = board;
    this.pins = [1, 2, 40];
    this.state = [false, false, false];
    this.fanOmega = 0;
    this.beaconAngle = 0;

    this.baseGroup = new THREE.Group();
    this.baseGroup.position.set(position[0], position[1], position[2]);
    this.baseGroup.rotation.y = rotationY;
    this.root.add(this.baseGroup);

    this._buildAssembly();
    this._buildWiring();
  }

  _buildAssembly() {
    const pcbMat = new THREE.MeshStandardMaterial({ map: makeRelaySilkTexture(), roughness: 0.42, metalness: 0.16 });
    const pcbSide = new THREE.MeshStandardMaterial({ color: '#0c2e1c', roughness: 0.65 });
    const pcb = new THREE.Mesh(
      new THREE.BoxGeometry(4.6, 0.14, 3.6),
      [pcbSide, pcbSide, pcbMat, pcbSide, pcbSide, pcbSide]
    );
    pcb.position.set(0, 0.18, 0);
    pcb.castShadow = true;
    pcb.receiveShadow = true;
    this.baseGroup.add(pcb);

    // 4 Brass standoffs resting on cutting mat
    const brassMat = new THREE.MeshStandardMaterial({ color: '#cfa648', metalness: 0.85, roughness: 0.28 });
    for (const [sx, sz] of [[-2.05, -1.55], [2.05, -1.55], [-2.05, 1.55], [2.05, 1.55]]) {
      const st = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.18, 12), brassMat);
      st.position.set(sx, 0.09, sz);
      this.baseGroup.add(st);
    }

    // 3x Blue Songle SRD-05VDC-SL-C Electromagnetic Relays + Coil Indicator LEDs
    const relayBlueMat = new THREE.MeshStandardMaterial({ color: '#1b6dc8', roughness: 0.36, metalness: 0.08 });
    const termGreenMat = new THREE.MeshStandardMaterial({ color: '#1e8a4a', roughness: 0.45 });
    const silverMat = new THREE.MeshStandardMaterial({ color: '#cfd6e0', metalness: 0.88, roughness: 0.22 });
    const darkMat = new THREE.MeshStandardMaterial({ color: '#1a1e26', roughness: 0.48, metalness: 0.18 });

    this.coilLedMats = [];
    const xs = [-1.42, 0.0, 1.42];

    for (let i = 0; i < 3; i++) {
      const rx = xs[i];
      const relay = new THREE.Mesh(new THREE.BoxGeometry(1.06, 0.96, 1.18), relayBlueMat);
      relay.position.set(rx, 0.73, -0.58);
      relay.castShadow = true;
      relay.receiveShadow = true;
      this.baseGroup.add(relay);

      // Green 3-pin screw terminal block
      const term = new THREE.Mesh(new THREE.BoxGeometry(0.96, 0.46, 0.46), termGreenMat);
      term.position.set(rx, 0.48, -1.42);
      term.castShadow = true;
      this.baseGroup.add(term);
      for (const tx of [-0.28, 0, 0.28]) {
        const screw = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.08, 0.06, 10), silverMat);
        screw.position.set(rx + tx, 0.72, -1.42);
        this.baseGroup.add(screw);
      }

      // Red coil indicator SMD LED
      const ledMat = new THREE.MeshBasicMaterial({ color: 0x331215 });
      const led = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.08, 0.12), ledMat);
      led.position.set(rx + 0.38, 0.29, 0.16);
      this.baseGroup.add(led);
      this.coilLedMats.push(ledMat);
    }

    // 5-Pin Input Header on right edge (faces directly toward ESP32 J3 8P RELAY!)
    const hdr = new THREE.Mesh(new THREE.BoxGeometry(0.28, 0.34, 1.35), darkMat);
    hdr.position.set(2.06, 0.42, -0.30);
    this.baseGroup.add(hdr);
    this.headerLocalPins = [];
    for (let i = 0; i < 5; i++) {
      const p = new THREE.Object3D();
      p.position.set(2.06, 0.56, -0.82 + i * 0.26);
      this.baseGroup.add(p);
      this.headerLocalPins.push(p);
    }

    /* ---- LOAD 1 (K1 / GPIO 1): Warm Filament Edison Desk Lamp ---- */
    const lampGrp = new THREE.Group();
    lampGrp.position.set(xs[0], 0.25, 0.88);
    this.baseGroup.add(lampGrp);

    const lampSocket = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.38, 0.34, 18), brassMat);
    lampSocket.position.y = 0.17;
    lampSocket.castShadow = true;
    lampGrp.add(lampSocket);

    this.filamentMat = new THREE.MeshBasicMaterial({ color: 0x4a3418 });
    const filament = new THREE.Mesh(new THREE.TorusGeometry(0.14, 0.032, 8, 18), this.filamentMat);
    filament.position.y = 0.72;
    lampGrp.add(filament);

    this.bulbMat = new THREE.MeshPhysicalMaterial({
      color: '#fff3dc',
      emissive: '#ff9e2c',
      emissiveIntensity: 0.0,
      transparent: true,
      opacity: 0.42,
      roughness: 0.08,
      metalness: 0.0,
      clearcoat: 1.0,
    });
    const bulb = new THREE.Mesh(new THREE.SphereGeometry(0.46, 20, 16), this.bulbMat);
    bulb.scale.set(1, 1.22, 1);
    bulb.position.y = 0.78;
    lampGrp.add(bulb);

    this.lampLight = new THREE.PointLight(0xffaa44, 0.0, 14, 1.6);
    this.lampLight.position.set(0, 1.15, 0);
    lampGrp.add(this.lampLight);

    /* ---- LOAD 2 (K2 / GPIO 2): 40mm Brushless DC Cooling Fan ---- */
    const fanGrp = new THREE.Group();
    fanGrp.position.set(xs[1], 0.25, 0.88);
    this.baseGroup.add(fanGrp);

    // Outer open-frame housing
    const fanRing = new THREE.Mesh(new THREE.TorusGeometry(0.56, 0.09, 10, 24), darkMat);
    fanRing.rotation.x = Math.PI / 2;
    fanRing.position.y = 0.24;
    fanRing.castShadow = true;
    fanGrp.add(fanRing);

    const fanBase = new THREE.Mesh(new THREE.BoxGeometry(1.18, 0.12, 1.18), darkMat);
    fanBase.position.y = 0.06;
    fanGrp.add(fanBase);

    this.fanRotor = new THREE.Group();
    this.fanRotor.position.y = 0.25;
    fanGrp.add(this.fanRotor);

    const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.18, 0.16, 16),
      new THREE.MeshStandardMaterial({ color: '#384252', metalness: 0.6, roughness: 0.3 }));
    this.fanRotor.add(hub);

    const bladeMat = new THREE.MeshStandardMaterial({ color: '#69c2e8', roughness: 0.35, metalness: 0.15 });
    for (let b = 0; b < 7; b++) {
      const ang = (b / 7) * Math.PI * 2;
      const blade = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.03, 0.16), bladeMat);
      blade.position.set(Math.cos(ang) * 0.32, 0, Math.sin(ang) * 0.32);
      blade.rotation.y = -ang;
      blade.rotation.x = 0.42;
      this.fanRotor.add(blade);
    }

    /* ---- LOAD 3 (K3 / GPIO 40): Amber Industrial Rotating Beacon ---- */
    const beaconGrp = new THREE.Group();
    beaconGrp.position.set(xs[2], 0.25, 0.88);
    this.baseGroup.add(beaconGrp);

    const bBase = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.46, 0.24, 18), darkMat);
    bBase.position.y = 0.12;
    bBase.castShadow = true;
    beaconGrp.add(bBase);

    this.beaconRotor = new THREE.Group();
    this.beaconRotor.position.y = 0.56;
    beaconGrp.add(this.beaconRotor);

    this.beaconCoreMat = new THREE.MeshBasicMaterial({ color: 0x4a2a08 });
    const bCore = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.32, 0.14), this.beaconCoreMat);
    bCore.position.z = 0.10;
    this.beaconRotor.add(bCore);

    this.beaconDomeMat = new THREE.MeshPhysicalMaterial({
      color: '#ff9d1a',
      emissive: '#ff7b00',
      emissiveIntensity: 0.0,
      transparent: true,
      opacity: 0.58,
      roughness: 0.14,
      clearcoat: 1.0,
    });
    const bDome = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.38, 0.62, 18), this.beaconDomeMat);
    bDome.position.y = 0.55;
    beaconGrp.add(bDome);

    this.beaconLight = new THREE.PointLight(0xff8c1a, 0.0, 12, 1.7);
    this.beaconLight.position.set(0, 1.05, 0);
    beaconGrp.add(this.beaconLight);
  }

  _buildWiring() {
    if (!this.board) return;
    this.board.root.updateMatrixWorld(true);
    this.root.updateMatrixWorld(true);

    const pinMap = [
      { j3: '5V',   idx: 0, col: '#d93838' }, // Red 5V
      { j3: 'GND',  idx: 1, col: '#22262e' }, // Black GND
      { j3: 'IO1',  idx: 2, col: '#e5c138' }, // Yellow IO1 -> K1
      { j3: 'IO2',  idx: 3, col: '#34b868' }, // Green IO2 -> K2
      { j3: 'IO40', idx: 4, col: '#3a8ae8' }, // Blue IO40 -> K3
    ];

    if (this.board.j3Pins) {
      for (let k = 0; k < pinMap.length; k++) {
        const m = pinMap[k];
        const srcPin = this.board.j3Pins[m.j3];
        const dstPin = this.headerLocalPins[m.idx];
        if (!srcPin || !dstPin) continue;
        const p0 = new THREE.Vector3();
        const p1 = new THREE.Vector3();
        srcPin.getWorldPosition(p0);
        dstPin.getWorldPosition(p1);
        this.root.add(makeDupontWire(p0, p1, 1.05 + k * 0.14, m.col));
      }
    } else {
      // Fallback breadboard Dupont wiring (e.g. ESP32-S3-DevKitC-1)
      const plugMat = new THREE.MeshStandardMaterial({ color: '#181b22', roughness: 0.45 });
      for (let k = 0; k < pinMap.length; k++) {
        const m = pinMap[k];
        const dstPin = this.headerLocalPins[m.idx];
        if (!dstPin) continue;
        const p0 = new THREE.Vector3(-4.5, 0.80, -1.6 + k * 0.36);
        const p1 = new THREE.Vector3();
        dstPin.getWorldPosition(p1);
        this.root.add(makeDupontWire(p0, p1, 1.25 + k * 0.10, m.col));
        const plug = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.34, 0.12), plugMat);
        plug.position.set(p0.x, p0.y + 0.04, p0.z);
        this.root.add(plug);
      }
    }
  }

  update(dtSec, bus, audio) {
    const routed = bus ? bus.isRelayRouted() : false;

    for (let i = 0; i < 3; i++) {
      const active = routed && bus && (bus.gpioOut[this.pins[i]] !== 0);
      if (active !== this.state[i]) {
        this.state[i] = active;
        // Authentic Songle electromagnetic relay click!
        audio?.playMechanicalClick(active ? 1480 : 1080, 0.22);
      }
      this.coilLedMats[i].color.setHex(active ? 0xff3b48 : 0x2d1216);
    }

    // CH1: Edison Lamp thermal glow easing
    const lampTarget = this.state[0] ? 1.0 : 0.0;
    const lampCur = this.lampLight.intensity / 4.2;
    const lampNext = lampCur + (lampTarget - lampCur) * Math.min(1, dtSec * 12);
    this.lampLight.intensity = lampNext * 4.2;
    this.bulbMat.emissiveIntensity = lampNext * 1.9;
    this.filamentMat.color.setRGB(
      0.28 + 0.72 * lampNext,
      0.20 + 0.68 * lampNext,
      0.10 + 0.36 * lampNext
    );

    // CH2: Brushless Cooling Fan with physical spin-up & coast-down inertia
    const targetOmega = this.state[1] ? 24.0 : 0.0;
    const accel = this.state[1] ? 4.2 : 1.6;
    this.fanOmega += (targetOmega - this.fanOmega) * Math.min(1, dtSec * accel);
    this.fanRotor.rotation.y -= this.fanOmega * dtSec;

    // CH3: Rotating Amber Industrial Beacon
    if (this.state[2]) {
      this.beaconAngle += dtSec * 8.5;
      this.beaconRotor.rotation.y = this.beaconAngle;
      const pulse = 0.72 + 0.28 * Math.sin(this.beaconAngle * 2.0);
      this.beaconDomeMat.emissiveIntensity = 1.6 * pulse;
      this.beaconLight.intensity = 3.2 * pulse;
      this.beaconCoreMat.color.setHex(0xffd266);
    } else {
      this.beaconDomeMat.emissiveIntensity *= Math.max(0, 1 - dtSec * 10);
      this.beaconLight.intensity *= Math.max(0, 1 - dtSec * 10);
      this.beaconCoreMat.color.setHex(0x4a2a08);
    }
  }
}
