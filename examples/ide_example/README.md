# AromaUI IDE

A desktop IDE for building AromaUI applications with the Incense markup language.

## Features

- **Home Page**: Create new projects or open existing ones
- **Project Management**: Uses the `aroma` CLI to scaffold new projects
- **Split Editor**: Edit both C code (`main.c`) and Incense markup (`.aroma`) in tabbed editors
- **Live Preview**: See your UI rendered in real-time with the WASM-based preview canvas
- **Build Integration**: Run `aroma build linux` or `aroma build web` directly from the IDE
- **Monaco Editor**: Full-featured code editor with syntax highlighting, autocomplete, and hover docs
- **Theme Support**: Dark and light themes

## Requirements

- Node.js 16+
- npm
- The `aroma` CLI installed at `~/.aroma/bin/aroma`
- AromaUI dependencies (for building)

## Installation

```bash
cd examples/ide_example
npm install
```

## Running

```bash
npm start
```

Or directly:

```bash
./run_ide.sh
```

## Usage

### Creating a New Project

1. Click "New Project" on the home page
2. Enter a project name
3. The IDE will run `aroma create <name>` to scaffold the project
4. Edit `main.c` and `app.aroma` in the tabbed editors
5. Click "Run" to build for the selected target (Linux or Web)

### Opening an Existing Project

1. Click "Open Project" on the home page
2. Select the project directory
3. The IDE will automatically open `.aroma` and `.c` files

### Building

- Select "Linux" or "Web" from the build target dropdown
- Click "Run" to execute `aroma build <target>`
- Build output appears in the console at the bottom of the IDE

## Architecture

- `main.js` - Electron main process, handles file I/O and build commands
- `preload.js` - Secure bridge between renderer and main process
- `web/index.html` - IDE UI (Monaco editor, preview, home page)
- `build_web/` - Built WASM/JS artifacts
- `showcase/` - Example `.aroma` files
