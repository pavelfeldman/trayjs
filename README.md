# trayjs

Cross-platform system tray for Node.js. Works on Linux, macOS and Windows.

## Packages

| Package | Description |
|---------|-------------|
| [@trayjs/trayjs](packages/trayjs) | Node.js API |
| @trayjs/linux-x64 | Native binary for Linux x64 |
| @trayjs/linux-arm64 | Native binary for Linux arm64 |
| @trayjs/darwin-x64 | Native binary for macOS x64 |
| @trayjs/darwin-arm64 | Native binary for macOS arm64 |
| @trayjs/win32-x64 | Native binary for Windows x64 |
| @trayjs/win32-arm64 | Native binary for Windows arm64 |

## Quick start

```
npm install @trayjs/trayjs
```

```js
import { readFileSync } from 'node:fs';
import { Tray } from '@trayjs/trayjs';

const tray = new Tray({
  tooltip: 'My App',
  icon: readFileSync('icon.png'),
  onMenuRequested: () => [
    { id: 'open', title: 'Open' },
    { separator: true },
    { id: 'quit', title: 'Quit' },
  ],
  onClicked: (id) => {
    if (id === 'quit') tray.quit();
  },
});

tray.on('close', () => process.exit(0));
```

## API

### `new Tray(options)`

| Option | Type | Description |
|--------|------|-------------|
| `icon` | `Buffer` | PNG icon buffer |
| `tooltip` | `string` | Tray tooltip text |
| `onMenuRequested` | `() => MenuItem[] \| Promise<MenuItem[]>` | Called every time the tray menu is opened |
| `onClicked` | `(id: string) => void` | Called when a menu item is clicked |

### `MenuItem`

| Field | Type | Description |
|-------|------|-------------|
| `id` | `string` | Unique identifier |
| `title` | `string` | Display text |
| `tooltip` | `string` | Hover tooltip |
| `enabled` | `boolean` | Clickable (default `true`) |
| `checked` | `boolean` | Show check mark |
| `separator` | `boolean` | Render as separator line |

### Methods

- `tray.setIcon(pngBuffer)` — update the icon at runtime
- `tray.setTooltip(text)` — update the tooltip at runtime
- `tray.quit()` — close the tray

### Events

- `'ready'` — tray is visible and accepting commands
- `'close'` — tray process exited

## Architecture

Each platform has a native tray binary (Objective-C on macOS, Win32 C on Windows, GTK/AppIndicator C on Linux).
The Node.js wrapper communicates with it over stdin/stdout using JSON-lines.

The correct platform-specific binary is installed automatically via npm optional dependencies.

## Development

```
git clone <repo>
cd trayjs
npm install
```

Native binaries are built by CI. Each GitHub Actions run produces downloadable artifacts
for all platforms, ready to publish to npm.
