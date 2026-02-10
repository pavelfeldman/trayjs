import { spawn, ChildProcess } from 'node:child_process';
import { createInterface, Interface } from 'node:readline';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { EventEmitter } from 'node:events';

const require = createRequire(import.meta.url);
const __dirname = dirname(fileURLToPath(import.meta.url));

const PLATFORMS: Record<string, string> = {
  'linux-x64':     '@trayjs/linux-x64',
  'linux-arm64':   '@trayjs/linux-arm64',
  'darwin-x64':    '@trayjs/darwin-x64',
  'darwin-arm64':  '@trayjs/darwin-arm64',
  'win32-x64':     '@trayjs/win32-x64',
  'win32-arm64':   '@trayjs/win32-arm64',
};

const BIN_NAME = process.platform === 'win32' ? 'tray.exe' : 'tray';

export interface MenuItem {
  id: string;
  title?: string;
  tooltip?: string;
  enabled?: boolean;
  checked?: boolean;
  separator?: boolean;
  items?: MenuItem[];
}

export interface Icon {
  png: Buffer;
  ico: Buffer;
}

export interface TrayOptions {
  icon?: Icon;
  tooltip?: string;
  onMenuRequested?: () => MenuItem[] | Promise<MenuItem[]>;
  onClicked?: (id: string) => void;
}

function resolveIcon(icon: Icon): Buffer {
  return process.platform === 'win32' ? icon.ico : icon.png;
}

function getBinaryPath(): string {
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

export class Tray extends EventEmitter {
  #proc: ChildProcess;
  #rl: Interface;
  #menuRequestedCb?: () => MenuItem[] | Promise<MenuItem[]>;
  #clickedCb?: (id: string) => void;
  #pendingIcon?: Icon | null;

  constructor({ icon, tooltip, onMenuRequested, onClicked }: TrayOptions = {}) {
    super();
    this.#menuRequestedCb = onMenuRequested;
    this.#clickedCb = onClicked;
    this.#pendingIcon = icon;

    const bin = getBinaryPath();
    const args: string[] = [];
    if (tooltip) args.push('--tooltip', tooltip);

    this.#proc = spawn(bin, args, {
      stdio: ['pipe', 'pipe', 'inherit'],
    });

    this.#rl = createInterface({ input: this.#proc.stdout! });
    this.#rl.on('line', (line: string) => this.#handle(JSON.parse(line)));
    this.#proc.on('close', (code: number | null) => this.emit('close', code));
  }

  #send(msg: Record<string, unknown>): void {
    this.#proc.stdin!.write(JSON.stringify(msg) + '\n');
  }

  async #handle(msg: { method: string; params?: Record<string, unknown> }): Promise<void> {
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
        this.#clickedCb?.((msg.params as { id: string }).id);
        break;
    }
  }

  async #refreshMenu(): Promise<void> {
    if (!this.#menuRequestedCb) return;
    const items = await this.#menuRequestedCb();
    this.#send({ method: 'setMenu', params: { items } });
  }

  setIcon(icon: Icon): void {
    const buf = resolveIcon(icon);
    this.#send({
      method: 'setIcon',
      params: { base64: buf.toString('base64') },
    });
  }

  setMenu(items: MenuItem[]): void {
    this.#send({ method: 'setMenu', params: { items } });
  }

  setTooltip(text: string): void {
    this.#send({ method: 'setTooltip', params: { text } });
  }

  quit(): void {
    this.#send({ method: 'quit' });
  }
}
