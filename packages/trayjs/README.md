# @trayjs/trayjs

Cross-platform system tray for Node.js. Works on Linux, macOS and Windows.

## Install

```
npm install @trayjs/trayjs
```

The correct native binary for your platform is installed automatically.

## Usage

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

## Supported platforms

| Platform | Package |
|----------|---------|
| Linux x64 | @trayjs/linux-x64 |
| Linux arm64 | @trayjs/linux-arm64 |
| macOS x64 | @trayjs/darwin-x64 |
| macOS arm64 | @trayjs/darwin-arm64 |
| Windows x64 | @trayjs/win32-x64 |
| Windows arm64 | @trayjs/win32-arm64 |
