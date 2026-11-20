/**
 * Esp32WorkbenchElement.js — Portable <esp32-workbench> Custom Element
 *
 * Embeds a live interactive 3D ESP32-S3 hardware sandbox inside any HTML page:
 *   <esp32-workbench app="io_workbench" view="iso" intro="0"></esp32-workbench>
 */
import { Workbench } from './Workbench.js';

export class Esp32WorkbenchElement extends HTMLElement {
  static get observedAttributes() {
    return ['app', 'board', 'peripherals', 'view', 'jumper'];
  }

  constructor() {
    super();
    this.workbench = null;
    this._mounted = false;
  }

  connectedCallback() {
    if (this._mounted) return;
    this._mounted = true;

    this.style.display = this.style.display || 'block';
    this.style.position = this.style.position || 'relative';
    this.style.overflow = 'hidden';
    if (!this.style.minHeight) this.style.minHeight = '360px';

    const vp = document.createElement('div');
    vp.style.cssText = 'position:absolute;inset:0;width:100%;height:100%;touch-action:none;';
    this.appendChild(vp);

    this.workbench = new Workbench(vp, {
      onCartridgeLoaded: (cart, info) => {
        this.dispatchEvent(new CustomEvent('cartridge-loaded', { detail: { cart, info } }));
      },
      onRigChange: (rig) => {
        this.dispatchEvent(new CustomEvent('rig-change', { detail: rig }));
      },
    });

    const app = this.getAttribute('app') || 'crystal_garden';
    const board = this.getAttribute('board');
    const periphAttr = this.getAttribute('peripherals');
    const peripherals = periphAttr !== null
      ? (periphAttr.trim() ? periphAttr.split(',').map((s) => s.trim()).filter(Boolean) : [])
      : null;
    const jAttr = this.getAttribute('jumper');
    const jumpers = jAttr !== null ? { 0: Number(jAttr) ? 1 : 0 } : null;
    const startView = this.getAttribute('view') || null;
    const skipIntro = this.getAttribute('intro') === '0' || Boolean(startView);

    this.workbench.loadCartridge(app, {
      boardOverride: board || null,
      peripheralsOverride: peripherals,
      jumpersOverride: jumpers,
    }).then(() => {
      this.workbench.start({ startView, skipIntro });
      this.dispatchEvent(new CustomEvent('ready', { detail: this.workbench.getRigConfig() }));
    });
  }

  attributeChangedCallback(name, oldVal, newVal) {
    if (!this.workbench || oldVal === newVal) return;
    if (name === 'view' && newVal) this.workbench.setView(newVal);
    else if (name === 'board' && newVal) this.workbench.setBoard(newVal);
    else if (name === 'app' && newVal) this.workbench.loadCartridge(newVal);
  }
}

if (!customElements.get('esp32-workbench')) {
  customElements.define('esp32-workbench', Esp32WorkbenchElement);
}
