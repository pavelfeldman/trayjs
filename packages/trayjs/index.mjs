import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { EventEmitter } from 'node:events';

const require = createRequire(import.meta.url);
const __dirname = dirname(fileURLToPath(import.meta.url));

const PLATFORMS = {
  'linux-x64':     '@trayjs/linux-x64',
  'linux-arm64':   '@trayjs/linux-arm64',
  'darwin-x64':    '@trayjs/darwin-x64',
  'darwin-arm64':  '@trayjs/darwin-arm64',
  'win32-x64':     '@trayjs/win32-x64',
  'win32-arm64':   '@trayjs/win32-arm64',
};

const BIN_NAME = process.platform === 'win32' ? 'tray.exe' : 'tray';

/**
 * Picks the right icon buffer for the current platform.
 * @param {{ png: Buffer, ico: Buffer }} icon
 * @returns {Buffer}
 */
function resolveIcon(icon) {
  return process.platform === 'win32' ? icon.ico : icon.png;
}

function getBinaryPath() {
  const key = `${process.platform}-${process.arch}`;
  const pkg = PLATFORMS[key];
  if (!pkg)
    throw new Error(`@trayjs/trayjs: unsupported platform ${key}`);
  try {
    // Published: resolve from installed optional dependency.
    const pkgJson = require.resolve(`${pkg}/package.json`);
    return join(dirname(pkgJson), 'bin', BIN_NAME);
  } catch {
    // Monorepo dev: binary is in sibling platform package.
    return join(__dirname, '..', key, 'bin', BIN_NAME);
  }
}

/**
 * @typedef {object} MenuItem
 * @property {string} id
 * @property {string} [title]
 * @property {string} [tooltip]
 * @property {boolean} [enabled]
 * @property {boolean} [checked]
 * @property {boolean} [separator]
 * @property {MenuItem[]} [items] - Nested submenu items
 */

/**
 * @typedef {{ png: Buffer, ico: Buffer }} Icon
 * PNG for macOS/Linux, ICO for Windows. Both are required.
 */

/**
 * @typedef {object} TrayOptions
 * @property {Icon} [icon] - Tray icon ({ png, ico })
 * @property {string} [tooltip]
 * @property {() => MenuItem[] | Promise<MenuItem[]>} [onMenuRequested]
 * @property {(id: string) => void} [onClicked]
 */

export class Tray extends EventEmitter {
  #proc;
  #rl;
  #menuRequestedCb;
  #clickedCb;
  #pendingIcon;

  /** @param {TrayOptions} opts */
  constructor({ icon, tooltip, onMenuRequested, onClicked } = {}) {
    super();
    this.#menuRequestedCb = onMenuRequested;
    this.#clickedCb = onClicked;
    this.#pendingIcon = icon;

    const bin = getBinaryPath();
    const args = [];
    if (tooltip) args.push('--tooltip', tooltip);

    this.#proc = spawn(bin, args, {
      stdio: ['pipe', 'pipe', 'inherit'],
    });

    this.#rl = createInterface({ input: this.#proc.stdout });
    this.#rl.on('line', (line) => this.#handle(JSON.parse(line)));
    this.#proc.on('close', (code) => this.emit('close', code));
  }

  #send(msg) {
    this.#proc.stdin.write(JSON.stringify(msg) + '\n');
  }

  async #handle(msg) {
    switch (msg.method) {
      case 'ready':
        if (this.#pendingIcon) {
          this.setIcon(this.#pendingIcon);
          this.#pendingIcon = null;
        }
        await this.#refreshMenu();
        this.emit('ready');
        break;
      case 'menuRequested':
        await this.#refreshMenu();
        break;
      case 'clicked':
        this.#clickedCb?.(msg.params.id);
        break;
    }
  }

  async #refreshMenu() {
    if (!this.#menuRequestedCb) return;
    const items = await this.#menuRequestedCb();
    this.#send({ method: 'setMenu', params: { items } });
  }

  /** @param {Icon} icon */
  setIcon(icon) {
    const buf = resolveIcon(icon);
    this.#send({
      method: 'setIcon',
      params: { base64: buf.toString('base64') },
    });
  }

  /** @param {MenuItem[]} items */
  setMenu(items) {
    this.#send({ method: 'setMenu', params: { items } });
  }

  /** @param {string} text */
  setTooltip(text) {
    this.#send({ method: 'setTooltip', params: { text } });
  }

  quit() {
    this.#send({ method: 'quit' });
  }
}
