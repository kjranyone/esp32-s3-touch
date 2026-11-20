/**
 * StudioScene.js — Portable Three.js 3D Hardware Workbench Scene
 *
 * Provides studio lighting, cutting mat desk, OrbitControls, board & peripheral
 * mounting, auto-fit FRONT camera framing, dark-to-PCB welcome animation, and
 * unified raycast routing (LCD touchscreen + clickable/rotatable 3D parts).
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

function makeCuttingMatTexture() {
  const c = document.createElement('canvas');
  c.width = 2048;
  c.height = 2048;
  const g = c.getContext('2d');

  g.fillStyle = '#252930';
  g.fillRect(0, 0, 2048, 2048);

  g.fillStyle = 'rgba(255,255,255,0.014)';
  for (let i = 0; i < 14000; i++) {
    g.fillRect((Math.random() * 2048) | 0, (Math.random() * 2048) | 0, 2, 2);
  }

  const step = 64;
  g.strokeStyle = 'rgba(220, 230, 242, 0.14)';
  g.lineWidth = 1.5;
  g.beginPath();
  for (let x = 0; x <= 2048; x += step) {
    g.moveTo(x, 0); g.lineTo(x, 2048);
    g.moveTo(0, x); g.lineTo(2048, x);
  }
  g.stroke();

  g.strokeStyle = 'rgba(235, 242, 250, 0.28)';
  g.lineWidth = 2.4;
  g.beginPath();
  for (let x = 0; x <= 2048; x += step * 5) {
    g.moveTo(x, 0); g.lineTo(x, 2048);
    g.moveTo(0, x); g.lineTo(2048, x);
  }
  g.stroke();

  g.strokeStyle = 'rgba(220, 230, 242, 0.12)';
  g.lineWidth = 1.5;
  g.beginPath();
  g.moveTo(0, 2048); g.lineTo(2048, 0);
  g.moveTo(0, 1024); g.lineTo(1024, 0);
  g.moveTo(1024, 2048); g.lineTo(2048, 1024);
  g.stroke();

  g.fillStyle = 'rgba(225, 235, 245, 0.24)';
  g.font = '600 18px ui-monospace, monospace';
  for (let i = 1; i <= 6; i++) {
    g.fillText(String(i * 5), i * step * 5 + 6, 2048 - 14);
  }

  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(3, 3);
  tex.anisotropy = 8;
  return tex;
}

export class StudioScene {
  constructor(container, { onLcdPointer, onPartClick, onPartDrag, onViewChange } = {}) {
    this.container = container;
    this.onLcdPointer = onLcdPointer;
    this.onPartClick = onPartClick;
    this.onPartDrag = onPartDrag;
    this.onViewChange = onViewChange;

    this.board = null;
    this.peripherals = [];
    this.peripheralGroup = new THREE.Group();
    this.isoScale = 1.0;

    this.currentViewName = 'iso';
    this.presetLocked = true;
    this.camAnim = null;
    this.welcomeIntro = null;

    this.lcdPointerId = null;
    this.dragPart = null;

    this.raycaster = new THREE.Raycaster();
    this.ndc = new THREE.Vector2();

    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(container.clientWidth || window.innerWidth, container.clientHeight || window.innerHeight);
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.08;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color('#16191f');
    this.scene.fog = new THREE.Fog('#16191f', 42, 88);
    this.scene.add(this.peripheralGroup);

    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.72;

    const w = container.clientWidth || window.innerWidth;
    const h = container.clientHeight || window.innerHeight;
    this.camera = new THREE.PerspectiveCamera(34, w / Math.max(1, h), 0.1, 200);
    this.camera.position.set(4.8, 6.2, 16.2);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.075;
    this.controls.enablePan = false;
    this.controls.minDistance = 7.5;
    this.controls.maxDistance = 54;
    this.controls.maxPolarAngle = Math.PI * 0.495;
    this.controls.target.set(0, 4.45, 0);
    this.controls.update();

    this.controls.addEventListener('start', () => {
      this.camAnim = null;
      this.presetLocked = false;
    });

    this._buildStudio();
    this._bindEvents();
  }

  _buildStudio() {
    const keyLight = new THREE.DirectionalLight(0xfff7ee, 2.2);
    keyLight.position.set(10, 18, 12);
    keyLight.castShadow = true;
    keyLight.shadow.mapSize.set(2048, 2048);
    keyLight.shadow.camera.near = 2;
    keyLight.shadow.camera.far = 48;
    keyLight.shadow.camera.left = -16;
    keyLight.shadow.camera.right = 16;
    keyLight.shadow.camera.top = 16;
    keyLight.shadow.camera.bottom = -16;
    keyLight.shadow.bias = -0.0005;
    this.scene.add(keyLight);

    const rimLight = new THREE.DirectionalLight(0x88c8ff, 0.9);
    rimLight.position.set(-12, 10, -14);
    this.scene.add(rimLight);

    const pcbKeyLight = new THREE.DirectionalLight(0xeef6ff, 1.15);
    pcbKeyLight.position.set(-8, 10, -16);
    this.scene.add(pcbKeyLight);

    this.lcdGlowLight = new THREE.PointLight(0x70e2f4, 2.4, 16, 1.8);
    this.lcdGlowLight.position.set(0, 4.5, 2.2);
    this.scene.add(this.lcdGlowLight);

    const cuttingMat = new THREE.Mesh(
      new THREE.PlaneGeometry(96, 96),
      new THREE.MeshStandardMaterial({ map: makeCuttingMatTexture(), roughness: 0.86, metalness: 0.04 })
    );
    cuttingMat.rotation.x = -Math.PI / 2;
    cuttingMat.receiveShadow = true;
    this.scene.add(cuttingMat);
  }

  mountBoard(board) {
    if (this.board) {
      this.scene.remove(this.board.root);
    }
    this.board = board;
    if (board) {
      this.scene.add(board.root);
    }
  }

  mountPeripherals(peripherals = [], isoScale = 1.0) {
    while (this.peripheralGroup.children.length > 0) {
      this.peripheralGroup.remove(this.peripheralGroup.children[0]);
    }
    this.peripherals = peripherals;
    this.isoScale = isoScale;
    for (const p of peripherals) {
      if (p?.root) this.peripheralGroup.add(p.root);
    }
  }

  setGlowRgb(rgb) {
    if (!rgb) return;
    this.lcdGlowLight.color.setRGB(rgb[0], rgb[1], rgb[2]);
  }

  _getAllInteractables() {
    const out = [];
    if (this.board?.interactables?.length) out.push(...this.board.interactables);
    for (const p of this.peripherals) {
      if (p?.interactables?.length) out.push(...p.interactables);
    }
    return out;
  }

  _getFrontFitView() {
    const geom = this.board.getFrontFitGeometry();
    const vw = Math.max(1, this.container.clientWidth || window.innerWidth);
    const vh = Math.max(1, this.container.clientHeight || window.innerHeight);
    const aspect = vw / vh;

    const bottomEl = document.querySelector('.bottom-chrome');
    const botRect = bottomEl ? bottomEl.getBoundingClientRect() : null;
    const botReserve = botRect && botRect.height > 0 ? Math.max(54, (vh - botRect.top) + 10) : 68;
    const topReserve = aspect < 1.15 ? 46 : 14;
    const sideReserve = vw <= 540 ? 6 : 16;

    const usableW = Math.max(120, vw - sideReserve * 2);
    const usableH = Math.max(120, vh - topReserve - botReserve);
    const usableFracW = usableW / vw;
    const usableFracH = usableH / vh;

    const tanHalfFov = Math.tan(THREE.MathUtils.degToRad(this.camera.fov * 0.5));
    const dVert = geom.frameSize / (2 * tanHalfFov * usableFracH);
    const dHorz = geom.frameSize / (2 * tanHalfFov * aspect * usableFracW);
    const dist = Math.max(this.controls.minDistance, Math.min(this.controls.maxDistance, Math.max(dVert, dHorz)));

    const worldPerPx = (2 * dist * tanHalfFov) / vh;
    const shiftWorld = ((botReserve - topReserve) * 0.5) * worldPerPx;
    const target = geom.center.clone().addScaledVector(geom.up, -shiftWorld);
    const pos = target.clone().addScaledVector(geom.normal, dist);
    return { pos, target };
  }

  getResponsiveView(name) {
    if (!this.board) return { pos: new THREE.Vector3(6, 7, 18), target: new THREE.Vector3(0, 4, 0) };
    if (name === 'front') return this._getFrontFitView();
    const presets = this.board.getCameraPresets();
    const base = presets[name] || presets.iso;
    const vw = this.container.clientWidth || window.innerWidth;
    const vh = Math.max(1, this.container.clientHeight || window.innerHeight);
    const aspect = vw / vh;
    const rigScale = (name === 'iso') ? this.isoScale : 1.0;
    const distScale = (aspect < 1.05 ? (1.05 / Math.max(0.45, aspect)) : 1.0) * rigScale;
    const target = base.target.clone();
    if (name === 'iso' && this.isoScale > 1.05) {
      target.y -= 0.45; // Frame desk-level peripherals slightly better in wide rig view
    }
    const dir = base.pos.clone().sub(base.target).multiplyScalar(distScale);
    return {
      pos: target.clone().add(dir),
      target,
    };
  }

  setView(name, immediate = false) {
    if (!this.board) return;
    const presets = this.board.getCameraPresets();
    if (!presets[name]) return;
    this.currentViewName = name;
    this.presetLocked = true;
    const v = this.getResponsiveView(name);
    if (immediate) {
      this.camAnim = null;
      this.camera.position.copy(v.pos);
      this.controls.target.copy(v.target);
      this.controls.update();
    } else {
      this.camAnim = {
        t0: performance.now(),
        dur: 680,
        p0: this.camera.position.clone(),
        p1: v.pos.clone(),
        tStart: this.controls.target.clone(),
        tEnd: v.target.clone(),
      };
    }
    this.onViewChange?.(name);
  }

  startWelcomeIntro() {
    const targetView = this.getResponsiveView('iso');
    const tgt = targetView.target.clone();
    const endVec = targetView.pos.clone().sub(tgt);
    const endRadius = endVec.length();
    const endAz = Math.atan2(endVec.x, endVec.z);
    const endElev = Math.asin(Math.max(-0.9, Math.min(0.9, endVec.y / endRadius)));

    const startAz = endAz - Math.PI * 1.06;
    const holdAz  = endAz - Math.PI * 0.96;
    const startRadius = endRadius * 0.72;
    const holdRadius  = endRadius * 0.80;

    document.body.classList.add('intro-playing');
    this.controls.enabled = false;
    this.lcdGlowLight.intensity = 0.2;

    this.welcomeIntro = {
      t0: performance.now(),
      holdDur: 1100,
      spinDur: 1450,
      tgt, startAz, holdAz, endAz, startRadius, holdRadius, endRadius,
      startElev: 0.13, endElev,
    };

    const r0 = startRadius;
    this.camera.position.set(
      tgt.x + r0 * Math.cos(0.13) * Math.sin(startAz),
      tgt.y + r0 * Math.sin(0.13),
      tgt.z + r0 * Math.cos(0.13) * Math.cos(startAz)
    );
    this.controls.target.copy(tgt);
    this.camera.lookAt(tgt);
  }

  _finishWelcomeIntro() {
    if (!this.welcomeIntro) return;
    this.welcomeIntro = null;
    document.body.classList.remove('intro-playing');
    this.controls.enabled = true;
    this.lcdGlowLight.intensity = 2.4;
    this.setView('iso', true);
  }

  _stepWelcomeIntro(now) {
    const w = this.welcomeIntro;
    const el = now - w.t0;
    let az, rad, elev;
    if (el < w.holdDur) {
      const u = Math.max(0, Math.min(1, el / w.holdDur));
      const ease = 1 - Math.pow(1 - u, 2.2);
      az = w.startAz + (w.holdAz - w.startAz) * ease;
      rad = w.startRadius + (w.holdRadius - w.startRadius) * ease;
      elev = w.startElev;
      this.lcdGlowLight.intensity = 0.25;
    } else {
      const u = Math.max(0, Math.min(1, (el - w.holdDur) / w.spinDur));
      const ease = u < 0.5 ? 16 * u * u * u * u * u : 1 - Math.pow(-2 * u + 2, 5) / 2;
      az = w.holdAz + (w.endAz - w.holdAz) * ease;
      const arcBoost = Math.sin(u * Math.PI) * 1.6;
      rad = w.holdRadius + (w.endRadius - w.holdRadius) * ease + arcBoost;
      elev = w.startElev + (w.endElev - w.startElev) * ease;
      const glowU = Math.max(0, Math.min(1, (u - 0.35) / 0.55));
      this.lcdGlowLight.intensity = 0.25 + 2.15 * glowU;
      if (u >= 1) {
        this._finishWelcomeIntro();
        return;
      }
    }
    this.camera.position.set(
      w.tgt.x + rad * Math.cos(elev) * Math.sin(az),
      w.tgt.y + rad * Math.sin(elev),
      w.tgt.z + rad * Math.cos(elev) * Math.cos(az)
    );
    this.controls.target.copy(w.tgt);
    this.camera.lookAt(w.tgt);
  }

  _raycast(ev) {
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.ndc.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    this.ndc.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    this.raycaster.setFromCamera(this.ndc, this.camera);
  }

  _hitLcd(ev) {
    if (!this.board?.lcdMesh) return null;
    this._raycast(ev);
    const hits = this.raycaster.intersectObject(this.board.lcdMesh, false);
    if (hits.length === 0 || !hits[0].uv) return null;
    const uv = hits[0].uv;
    return [
      Math.max(0, Math.min(this.board.dispW - 1, Math.floor(uv.x * this.board.dispW))),
      Math.max(0, Math.min(this.board.dispH - 1, Math.floor((1 - uv.y) * this.board.dispH))),
    ];
  }

  _hitInteractable(ev) {
    const list = this._getAllInteractables();
    if (list.length === 0) return null;
    this._raycast(ev);
    const hits = this.raycaster.intersectObjects(list, false);
    return hits.length > 0 ? hits[0].object : null;
  }

  _bindEvents() {
    const dom = this.renderer.domElement;

    dom.addEventListener('pointerdown', (ev) => {
      if (this.welcomeIntro || this.lcdPointerId !== null || this.dragPart !== null) return;

      const pt = this._hitLcd(ev);
      if (pt) {
        this.lcdPointerId = ev.pointerId;
        this.controls.enabled = false;
        dom.setPointerCapture(ev.pointerId);
        this.onLcdPointer?.('down', pt[0], pt[1]);
        return;
      }

      const part = this._hitInteractable(ev);
      if (part) {
        ev.stopPropagation();
        if (part.userData?.kind === 'rotary') {
          this.dragPart = {
            pointerId: ev.pointerId,
            part,
            startX: ev.clientX,
            startY: ev.clientY,
            accum: 0,
            moved: false,
          };
          this.controls.enabled = false;
          dom.setPointerCapture(ev.pointerId);
          dom.style.cursor = 'ew-resize';
        } else {
          this.onPartClick?.(part.userData, part);
        }
      }
    }, true);

    dom.addEventListener('pointermove', (ev) => {
      if (this.lcdPointerId === ev.pointerId) {
        const pt = this._hitLcd(ev);
        if (pt) this.onLcdPointer?.('move', pt[0], pt[1]);
        return;
      }
      if (this.dragPart && this.dragPart.pointerId === ev.pointerId) {
        const dx = ev.clientX - this.dragPart.startX;
        const dy = ev.clientY - this.dragPart.startY;
        if (Math.abs(dx) + Math.abs(dy) > 5) this.dragPart.moved = true;
        this.dragPart.accum += (ev.movementX || 0);
        const STEP_PX = 14;
        while (Math.abs(this.dragPart.accum) >= STEP_PX) {
          const step = this.dragPart.accum > 0 ? 1 : -1;
          this.dragPart.accum -= step * STEP_PX;
          this.onPartDrag?.(this.dragPart.part.userData, step, this.dragPart.part);
        }
        return;
      }
      if (this.lcdPointerId === null && !this.welcomeIntro) {
        if (this._hitLcd(ev)) {
          dom.style.cursor = 'pointer';
          dom.title = '';
        } else {
          const part = this._hitInteractable(ev);
          dom.style.cursor = part ? (part.userData?.kind === 'rotary' ? 'ew-resize' : 'pointer') : 'grab';
          dom.title = part?.userData?.title || '';
        }
      }
    });

    const release = (ev) => {
      if (this.lcdPointerId === ev.pointerId) {
        this.lcdPointerId = null;
        this.controls.enabled = true;
        const pt = this._hitLcd(ev);
        const cx = this.board ? (this.board.dispW >> 1) : 240;
        const cy = this.board ? (this.board.dispH >> 1) : 240;
        this.onLcdPointer?.('up', pt ? pt[0] : cx, pt ? pt[1] : cy);
        return;
      }
      if (this.dragPart && this.dragPart.pointerId === ev.pointerId) {
        const dp = this.dragPart;
        this.dragPart = null;
        this.controls.enabled = true;
        dom.style.cursor = 'grab';
        if (!dp.moved) {
          this.onPartClick?.(dp.part.userData, dp.part);
        }
      }
    };
    dom.addEventListener('pointerup', release);
    dom.addEventListener('pointercancel', release);

    // Mouse wheel over 3D rotary encoder turns the knob directly!
    dom.addEventListener('wheel', (ev) => {
      if (this.welcomeIntro) return;
      const part = this._hitInteractable(ev);
      if (part && part.userData?.kind === 'rotary') {
        ev.preventDefault();
        ev.stopPropagation();
        const step = ev.deltaY < 0 ? 1 : -1;
        this.onPartDrag?.(part.userData, step, part);
      }
    }, { capture: true, passive: false });

    window.addEventListener('resize', () => {
      const w = this.container.clientWidth || window.innerWidth;
      const h = Math.max(1, this.container.clientHeight || window.innerHeight);
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
      this.renderer.setSize(w, h);
      if (this.presetLocked && !this.welcomeIntro) {
        this.setView(this.currentViewName, true);
      }
    });
  }

  renderFrame(now) {
    if (this.welcomeIntro) {
      this._stepWelcomeIntro(now);
    } else if (this.camAnim) {
      const u = Math.min(1, (now - this.camAnim.t0) / this.camAnim.dur);
      const ease = 1 - Math.pow(1 - u, 3);
      this.camera.position.lerpVectors(this.camAnim.p0, this.camAnim.p1, ease);
      this.controls.target.lerpVectors(this.camAnim.tStart, this.camAnim.tEnd, ease);
      if (u >= 1) this.camAnim = null;
    }
    if (!this.welcomeIntro) this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }
}
