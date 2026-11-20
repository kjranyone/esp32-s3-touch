/**
 * RotaryEncoder.js — Interactive 3D EC11 Detented Rotary Encoder + Push Switch Module
 *
 * Drag horizontally (or scroll mouse wheel) on the knurled aluminum knob to turn it
 * with 24-detent snapping and tactile mechanical click sounds. Short-click the knob
 * to depress the EC11 shaft push-switch. Companion tactile switch BTN-A toggles GPIO47.
 */
import * as THREE from 'three';

function makeDupontWire(pStart, pEnd, midLift, colorHex) {
  const mid1 = pStart.clone().lerp(pEnd, 0.35);
  mid1.y = Math.max(mid1.y, midLift);
  const mid2 = pStart.clone().lerp(pEnd, 0.70);
  mid2.y = Math.max(mid2.y, midLift * 0.7);
  const curve = new THREE.CatmullRomCurve3([pStart, mid1, mid2, pEnd]);
  const mesh = new THREE.Mesh(
    new THREE.TubeGeometry(curve, 22, 0.038, 8, false),
    new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.62, metalness: 0.05 })
  );
  mesh.castShadow = true;
  return mesh;
}

function makeEncoderSilkTexture() {
  const c = document.createElement('canvas');
  c.width = 256;
  c.height = 256;
  const g = c.getContext('2d');
  g.fillStyle = '#142032';
  g.fillRect(0, 0, 256, 256);
  g.strokeStyle = 'rgba(230,242,255,0.75)';
  g.lineWidth = 4;
  g.strokeRect(10, 10, 236, 236);
  g.fillStyle = 'rgba(235,245,255,0.92)';
  g.font = '700 22px ui-monospace, monospace';
  g.fillText('EC11 ROTARY', 24, 42);
  g.font = '600 15px ui-monospace, monospace';
  g.fillStyle = 'rgba(176,205,235,0.85)';
  g.fillText('DRAG/WHEEL · CLICK', 24, 234);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  return tex;
}

export class RotaryEncoder {
  constructor(board, { position = [5.5, 0, 3.1], rotationY = -0.32, wireTarget = null, encoderId = 0 } = {}) {
    this.id = 'rotary_encoder';
    this.name = 'EC11 Rotary Encoder';
    this.encoderId = encoderId;
    this._wireTarget = wireTarget;
    this.root = new THREE.Group();
    this.interactables = [];

    this.targetAngle = 0;
    this.curAngle = 0;
    this.pressOffset = 0;
    this.btnOffset = 0;

    this.baseGroup = new THREE.Group();
    this.baseGroup.position.set(position[0], position[1], position[2]);
    this.baseGroup.rotation.y = rotationY;
    this.root.add(this.baseGroup);

    this._buildHardware();
    this._buildWiring(board);
  }

  _buildHardware() {
    const pcbMat = new THREE.MeshStandardMaterial({ map: makeEncoderSilkTexture(), roughness: 0.42, metalness: 0.16 });
    const pcbSide = new THREE.MeshStandardMaterial({ color: '#0f1928', roughness: 0.65 });
    const pcb = new THREE.Mesh(
      new THREE.BoxGeometry(2.55, 0.14, 2.35),
      [pcbSide, pcbSide, pcbMat, pcbSide, pcbSide, pcbSide]
    );
    pcb.position.y = 0.15;
    pcb.castShadow = true;
    pcb.receiveShadow = true;
    this.baseGroup.add(pcb);

    // 4 rubber feet
    const rubberMat = new THREE.MeshStandardMaterial({ color: '#14171d', roughness: 0.9 });
    for (const [fx, fz] of [[-1.05, -0.95], [1.05, -0.95], [-1.05, 0.95], [1.05, 0.95]]) {
      const f = new THREE.Mesh(new THREE.CylinderGeometry(0.14, 0.14, 0.08, 12), rubberMat);
      f.position.set(fx, 0.04, fz);
      this.baseGroup.add(f);
    }

    // EC11 metal housing & threaded bushing
    const metalMat = new THREE.MeshStandardMaterial({ color: '#c8d0dc', metalness: 0.88, roughness: 0.24 });
    const ecBase = new THREE.Mesh(new THREE.BoxGeometry(1.15, 0.42, 1.15), metalMat);
    ecBase.position.set(-0.28, 0.43, 0.05);
    ecBase.castShadow = true;
    this.baseGroup.add(ecBase);

    const bushing = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 0.26, 20), metalMat);
    bushing.position.set(-0.28, 0.74, 0.05);
    this.baseGroup.add(bushing);

    // Knurled anodized aluminum rotary knob
    this.knobPivot = new THREE.Group();
    this.knobPivot.position.set(-0.28, 1.05, 0.05);
    this.baseGroup.add(this.knobPivot);

    const knobMat = new THREE.MeshStandardMaterial({ color: '#262c38', metalness: 0.82, roughness: 0.26 });
    const knobBody = new THREE.Mesh(new THREE.CylinderGeometry(0.68, 0.72, 0.68, 28), knobMat);
    knobBody.castShadow = true;
    knobBody.receiveShadow = true;
    this.knobPivot.add(knobBody);

    // Subtle machined chamfer cap
    const knobTop = new THREE.Mesh(
      new THREE.CylinderGeometry(0.62, 0.68, 0.08, 28),
      new THREE.MeshStandardMaterial({ color: '#394254', metalness: 0.86, roughness: 0.20 })
    );
    knobTop.position.y = 0.36;
    this.knobPivot.add(knobTop);

    // Crisp cyan-white position indicator line
    const ind = new THREE.Mesh(
      new THREE.BoxGeometry(0.08, 0.06, 0.34),
      new THREE.MeshBasicMaterial({ color: 0x70e2f4 })
    );
    ind.position.set(0, 0.39, -0.34);
    this.knobPivot.add(ind);

    // Raycast hit cylinder for drag-rotation & click-press
    const knobHit = new THREE.Mesh(
      new THREE.CylinderGeometry(0.84, 0.84, 0.96, 16),
      new THREE.MeshBasicMaterial({ visible: false })
    );
    knobHit.position.set(0, 0.06, 0);
    knobHit.userData = {
      kind: 'rotary',
      action: 'encoder_click',
      encoderId: this.encoderId,
      peripheral: this,
      title: 'EC11 Encoder: Drag or Scroll to Rotate · Click to Switch Mode',
    };
    this.knobPivot.add(knobHit);
    this.interactables.push(knobHit);

    // Companion Tactile Button (BTN A -> cycles/triggers quick test)
    const swBase = new THREE.Mesh(new THREE.BoxGeometry(0.54, 0.26, 0.54), metalMat);
    swBase.position.set(0.76, 0.35, 0.12);
    this.baseGroup.add(swBase);

    this.btnCap = new THREE.Mesh(
      new THREE.CylinderGeometry(0.18, 0.20, 0.22, 16),
      new THREE.MeshStandardMaterial({ color: '#e65a4f', roughness: 0.38, metalness: 0.1 })
    );
    this.btnCap.position.set(0.76, 0.54, 0.12);
    this.btnCap.castShadow = true;
    this.baseGroup.add(this.btnCap);

    const btnHit = new THREE.Mesh(new THREE.BoxGeometry(0.70, 0.56, 0.70), new THREE.MeshBasicMaterial({ visible: false }));
    btnHit.position.set(0.76, 0.45, 0.12);
    btnHit.userData = {
      action: 'tactile_btn',
      pin: 47,
      peripheral: this,
      title: 'Tactile Button (GPIO 47)',
    };
    this.baseGroup.add(btnHit);
    this.interactables.push(btnHit);

    // Wire anchor points along left-rear edge
    this.wireAnchors = [];
    for (let k = 0; k < 4; k++) {
      const a = new THREE.Object3D();
      a.position.set(-1.18, 0.26, -0.62 + k * 0.28);
      this.baseGroup.add(a);
      this.wireAnchors.push(a);
    }
  }

  _buildWiring(board) {
    if (!board) return;
    this.root.updateMatrixWorld(true);
    const cols = ['#d93838', '#22262e', '#f09236', '#46d1e8']; // 3V3, GND, CLK, DT
    const wt = this._wireTarget || [2.4, 0.22, -1.28];
    const plugMat = new THREE.MeshStandardMaterial({ color: '#181b22', roughness: 0.45 });
    for (let k = 0; k < 4; k++) {
      const p0 = new THREE.Vector3();
      this.wireAnchors[k].getWorldPosition(p0);
      const p1 = new THREE.Vector3(wt[0], wt[1], wt[2] + k * 0.24);
      this.root.add(makeDupontWire(p0, p1, Math.max(0.36, wt[1] + 0.42) + k * 0.05, cols[k]));
      if (this._wireTarget) {
        const plug = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.34, 0.12), plugMat);
        plug.position.set(p1.x, p1.y + 0.04, p1.z);
        this.root.add(plug);
      }
    }
  }

  onStep(delta) {
    // 24 detents per revolution = 15 deg per step
    this.targetAngle -= delta * (Math.PI * 2 / 24);
  }

  onPress() {
    this.pressOffset = -0.14;
  }

  onBtnPress() {
    this.btnOffset = -0.10;
  }

  update(dtSec) {
    this.curAngle += (this.targetAngle - this.curAngle) * Math.min(1, dtSec * 22);
    this.knobPivot.rotation.y = this.curAngle;

    this.pressOffset += (0 - this.pressOffset) * Math.min(1, dtSec * 18);
    this.knobPivot.position.y = 1.05 + this.pressOffset;

    this.btnOffset += (0 - this.btnOffset) * Math.min(1, dtSec * 18);
    this.btnCap.position.y = 0.54 + this.btnOffset;
  }
}
