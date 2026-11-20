/**
 * Ws2812Ring.js — 16-LED WS2812B (NeoPixel) Ring Peripheral Module
 *
 * Reads real-time RGB pixel data from VirtualBus (GPIO 48 / RMT) and drives
 * 16 individual 5050 SMD LEDs + a live dynamic bounce PointLight onto the
 * cutting mat and neighboring hardware.
 */
import * as THREE from 'three';

function makeDupontWire(pStart, pEnd, midLift, colorHex) {
  const mid1 = pStart.clone().lerp(pEnd, 0.35);
  mid1.y = Math.max(mid1.y, midLift);
  const mid2 = pStart.clone().lerp(pEnd, 0.70);
  mid2.y = Math.max(mid2.y, midLift * 0.7);
  const curve = new THREE.CatmullRomCurve3([pStart, mid1, mid2, pEnd]);
  const mesh = new THREE.Mesh(
    new THREE.TubeGeometry(curve, 24, 0.04, 8, false),
    new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.62, metalness: 0.05 })
  );
  mesh.castShadow = true;
  return mesh;
}

export class Ws2812Ring {
  constructor(board, { position = [6.4, 0.0, -0.4], rotationY = -0.26, wireTarget = null, pin = 48, count = 16 } = {}) {
    this.id = 'ws2812_ring';
    this.name = 'WS2812B 16-LED Ring';
    this.pin = pin;
    this.count = count;
    this._tiltRotY = rotationY;
    this._wireTarget = wireTarget;
    this.root = new THREE.Group();
    this.interactables = [];

    this.ringGroup = new THREE.Group();
    this.ringGroup.position.set(position[0], position[1], position[2]);
    this.root.add(this.ringGroup);

    this.ledMats = [];
    this.ledCoreMats = [];

    this._buildHardware();
    this._buildWiring(board);
  }

  _buildHardware() {
    // Sleek angled aluminum cradle resting on cutting mat
    const standMat = new THREE.MeshStandardMaterial({ color: '#1b1e26', metalness: 0.7, roughness: 0.36 });
    const base = new THREE.Mesh(new THREE.CylinderGeometry(1.45, 1.65, 0.14, 28), standMat);
    base.position.y = 0.07;
    base.castShadow = true;
    base.receiveShadow = true;
    this.ringGroup.add(base);

    // Tilted PCB Ring Carrier (tilted slightly toward camera for gorgeous visibility)
    const tiltGrp = new THREE.Group();
    tiltGrp.position.set(0, 1.65, 0);
    tiltGrp.rotation.x = -0.32;
    tiltGrp.rotation.y = this._tiltRotY;
    this.ringGroup.add(tiltGrp);
    this.tiltGrp = tiltGrp;

    // Support post from base to ring
    const post = new THREE.Mesh(new THREE.BoxGeometry(0.42, 1.35, 0.24), standMat);
    post.position.set(0, 0.62, -0.32);
    post.rotation.x = -0.25;
    post.castShadow = true;
    this.ringGroup.add(post);

    // Matte-black annular PCB ring (outer R=2.05, inner R=1.34)
    const ringShape = new THREE.Shape();
    ringShape.absarc(0, 0, 2.05, 0, Math.PI * 2, false);
    const hole = new THREE.Path();
    hole.absarc(0, 0, 1.34, 0, Math.PI * 2, true);
    ringShape.holes.push(hole);

    const pcbGeo = new THREE.ExtrudeGeometry(ringShape, { depth: 0.14, bevelEnabled: true, bevelThickness: 0.02, bevelSize: 0.02, bevelSegments: 2 });
    const pcbMat = new THREE.MeshStandardMaterial({ color: '#12151c', roughness: 0.45, metalness: 0.18 });
    const pcb = new THREE.Mesh(pcbGeo, pcbMat);
    pcb.position.z = -0.07;
    pcb.castShadow = true;
    pcb.receiveShadow = true;
    tiltGrp.add(pcb);

    // Gold ENIG silkscreen ring accents
    const goldRing = new THREE.Mesh(
      new THREE.RingGeometry(1.36, 1.40, 48),
      new THREE.MeshBasicMaterial({ color: 0xc9a84a, side: THREE.DoubleSide })
    );
    goldRing.position.z = 0.095;
    tiltGrp.add(goldRing);

    // 16x 5050 SMD WS2812B LED Packages
    const pkgMat = new THREE.MeshStandardMaterial({ color: '#e6eaef', roughness: 0.42, metalness: 0.05 });
    const capMat = new THREE.MeshStandardMaterial({ color: '#9c6e3b', roughness: 0.45 });
    const rLed = 1.70;

    for (let i = 0; i < this.count; i++) {
      const ang = Math.PI * 0.5 - (i / this.count) * Math.PI * 2;
      const lx = Math.cos(ang) * rLed;
      const ly = Math.sin(ang) * rLed;

      const ledGrp = new THREE.Group();
      ledGrp.position.set(lx, ly, 0.09);
      ledGrp.rotation.z = ang;
      tiltGrp.add(ledGrp);

      // 5050 white ceramic package
      const pkg = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.36, 0.11), pkgMat);
      pkg.position.z = 0.055;
      ledGrp.add(pkg);

      // Emissive silicon die lens
      const coreMat = new THREE.MeshBasicMaterial({ color: 0x181c24, fog: false });
      const core = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.13, 0.04, 14), coreMat);
      core.rotation.x = Math.PI / 2;
      core.position.z = 0.115;
      ledGrp.add(core);
      this.ledCoreMats.push(coreMat);

      // Subtle translucent halo lens for rich bloom look
      const haloMat = new THREE.MeshBasicMaterial({
        color: 0x000000, transparent: true, opacity: 0.0, blending: THREE.AdditiveBlending, depthWrite: false, fog: false,
      });
      const halo = new THREE.Mesh(new THREE.CircleGeometry(0.28, 16), haloMat);
      halo.position.z = 0.138;
      ledGrp.add(halo);
      this.ledMats.push(haloMat);

      // Tiny 0603 decoupling capacitor beside each LED
      const cap = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.13, 0.05), capMat);
      cap.position.set(-0.26, 0, 0.03);
      ledGrp.add(cap);
    }

    // Dynamic ambient bounce light spilling the ring's real-time average color onto the desk
    this.glowLight = new THREE.PointLight(0x70e2f4, 0.0, 12, 1.7);
    this.glowLight.position.set(0, 0.1, 1.2);
    tiltGrp.add(this.glowLight);

    // Solder wire exit anchors at bottom rear of ring
    this.wireAnchors = [];
    for (let k = 0; k < 3; k++) {
      const a = new THREE.Object3D();
      a.position.set(-0.22 + k * 0.22, -1.85, -0.08);
      tiltGrp.add(a);
      this.wireAnchors.push(a);
    }
  }

  _buildWiring(board) {
    if (!board) return;
    this.root.updateMatrixWorld(true);
    const cols = ['#d93838', '#22262e', '#9b59e6']; // 5V Red, GND Black, DIN Purple
    const wt = this._wireTarget || [2.1, 0.24, -1.32];
    const plugMat = new THREE.MeshStandardMaterial({ color: '#181b22', roughness: 0.45 });
    for (let k = 0; k < 3; k++) {
      const p0 = new THREE.Vector3();
      this.wireAnchors[k].getWorldPosition(p0);
      const p1 = new THREE.Vector3(wt[0] + k * 0.24, wt[1], wt[2]);
      this.root.add(makeDupontWire(p0, p1, Math.max(0.36, wt[1] + 0.42) + k * 0.06, cols[k]));
      if (this._wireTarget) {
        const plug = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.34, 0.12), plugMat);
        plug.position.set(p1.x, p1.y + 0.04, p1.z);
        this.root.add(plug);
      }
    }
  }

  update(_dtSec, bus) {
    if (!bus || bus.ledCount <= 0) {
      this.glowLight.intensity = 0;
      for (let i = 0; i < this.count; i++) {
        this.ledCoreMats[i].color.setHex(0x181c24);
        this.ledMats[i].opacity = 0;
      }
      return;
    }

    let sumR = 0, sumG = 0, sumB = 0;
    const n = Math.min(this.count, bus.ledCount);
    for (let i = 0; i < this.count; i++) {
      const idx = (i < n ? i : 0) * 3;
      const r = bus.ledRgb[idx] / 255;
      const g = bus.ledRgb[idx + 1] / 255;
      const b = bus.ledRgb[idx + 2] / 255;
      const lum = Math.max(r, g, b);

      if (lum > 0.01) {
        // Brighten core slightly so active NeoPixels pop against studio lighting
        this.ledCoreMats[i].color.setRGB(
          Math.min(1, r * 1.15 + 0.08),
          Math.min(1, g * 1.15 + 0.08),
          Math.min(1, b * 1.15 + 0.08)
        );
        this.ledMats[i].color.setRGB(r, g, b);
        this.ledMats[i].opacity = lum * 0.45;
      } else {
        this.ledCoreMats[i].color.setHex(0x181c24);
        this.ledMats[i].opacity = 0;
      }
      sumR += r; sumG += g; sumB += b;
    }

    const avgR = sumR / this.count;
    const avgG = sumG / this.count;
    const avgB = sumB / this.count;
    const avgLum = Math.max(avgR, avgG, avgB);
    this.glowLight.color.setRGB(
      avgLum > 0.01 ? avgR / avgLum : 0,
      avgLum > 0.01 ? avgG / avgLum : 0,
      avgLum > 0.01 ? avgB / avgLum : 0
    );
    this.glowLight.intensity = avgLum * 3.2;
  }
}
