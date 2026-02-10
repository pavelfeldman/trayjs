import { execFile } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Tray } from '../dist/index.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const icon = {
  png: readFileSync(join(__dirname, 'icon.png')),
  ico: readFileSync(join(__dirname, 'icon.ico')),
};

const tray = new Tray({
  tooltip: 'My App',
  icon,
  onClicked: (id) => {
    switch (id) {
      case 'terminal': openTerminal(); break;
      case 'files':    openFileManager(); break;
      case 'quit':     tray.quit(); break;
      default: console.log(`clicked: ${id}`); break;
    }
  },
  onMenuRequested: () => {
    console.log('menu requested');
    return [
      { id: 'terminal', title: 'Open Terminal' },
      { id: 'files',    title: 'File Manager' },
      { id: 'open-recent', title: 'Open Recent', items: [
        { id: 'recent-1', title: '~/Projects/app' },
        { id: 'recent-2', title: '~/Documents/notes.md' },
        { id: 'recent-3', title: '~/Downloads/archive.zip' },
        { separator: true },
        { id: 'clear-recent', title: 'Clear Recent' },
      ]},
      { separator: true },
      { id: 'help', title: 'Help', items: [
        { id: 'docs', title: 'Documentation' },
        { id: 'about', title: 'About' },
      ]},
      { id: 'quit', title: 'Quit' },
    ];
  }
});

tray.on('ready', () => {
});

function openTerminal() {
  switch (process.platform) {
    case 'darwin': execFile('open', ['-a', 'Terminal'], { detached: true }); break;
    case 'win32':  execFile('cmd', ['/c', 'start', 'cmd'], { detached: true }); break;
    default:       execFile('x-terminal-emulator', { detached: true }); break;
  }
}

function openFileManager() {
  switch (process.platform) {
    case 'darwin': execFile('open', ['.'], { detached: true }); break;
    case 'win32':  execFile('explorer', ['.'], { detached: true }); break;
    default:       execFile('xdg-open', ['.'], { detached: true }); break;
  }
}

process.on('SIGINT', () => tray.quit());
tray.on('close', () => process.exit(0));
