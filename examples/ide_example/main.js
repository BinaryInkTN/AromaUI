const { app, BrowserWindow, ipcMain, dialog, Menu, shell } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { exec } = require('child_process');

let mainWindow;

const EXAMPLES_DIR = path.join(__dirname, 'showcase');
const HOME = os.homedir();
const BUILD_DIR = path.join(__dirname, 'build_web');

const EXAMPLE_MAPPING = {
  default: { aroma: path.join(__dirname, 'default.aroma'), c: path.join(__dirname, 'ide_sandbox.c'), name: 'Settings UI' },
  widgets: { aroma: path.join(EXAMPLES_DIR, 'all_widgets.aroma'), c: path.join(__dirname, 'ide_sandbox.c'), name: 'All Widgets' },
  forms: { aroma: path.join(EXAMPLES_DIR, 'forms.aroma'), c: path.join(__dirname, 'ide_sandbox.c'), name: 'Forms' },
  buttons: { aroma: path.join(EXAMPLES_DIR, 'buttons.aroma'), c: path.join(__dirname, 'ide_sandbox.c'), name: 'Buttons' },
  layout: { aroma: path.join(EXAMPLES_DIR, 'layout.aroma'), c: path.join(__dirname, 'ide_sandbox.c'), name: 'Layout' },
};

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 850,
    minWidth: 1000,
    minHeight: 700,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      webgl: true,
      webaudio: true,
    },
    title: 'AromaUI IDE',
    autoHideMenuBar: true,
    darkTheme: true,
    backgroundColor: '#1e1e1e',
  });

  mainWindow.loadFile(path.join(BUILD_DIR, 'index.html'));

  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: 'deny' };
  });

  // Build application menu
  const menuTemplate = [
    {
      label: 'File',
      submenu: [
        {
          label: 'New Project',
          accelerator: 'CmdOrCtrl+Shift+N',
          click: () => mainWindow.webContents.send('menu-action', 'new-project'),
        },
        {
          label: 'Open Project',
          accelerator: 'CmdOrCtrl+O',
          click: () => mainWindow.webContents.send('menu-action', 'open-project'),
        },
        { type: 'separator' },
        {
          label: 'Save',
          accelerator: 'CmdOrCtrl+S',
          click: () => mainWindow.webContents.send('menu-action', 'save'),
        },
        {
          label: 'Save All',
          accelerator: 'CmdOrCtrl+Shift+S',
          click: () => mainWindow.webContents.send('menu-action', 'save-all'),
        },
        { type: 'separator' },
        {
          label: 'Back to Home',
          accelerator: 'CmdOrCtrl+Shift+H',
          click: () => mainWindow.webContents.send('menu-action', 'home'),
        },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    {
      label: 'Edit',
      submenu: [
        { role: 'undo' },
        { role: 'redo' },
        { type: 'separator' },
        { role: 'cut' },
        { role: 'copy' },
        { role: 'paste' },
        { role: 'selectAll' },
      ],
    },
    {
      label: 'View',
      submenu: [
        {
          label: 'Toggle Theme',
          accelerator: 'CmdOrCtrl+T',
          click: () => mainWindow.webContents.send('menu-action', 'toggle-theme'),
        },
        { type: 'separator' },
        { role: 'reload' },
        { role: 'toggleDevTools' },
        { type: 'separator' },
        { role: 'resetZoom' },
        { role: 'zoomIn' },
        { role: 'zoomOut' },
      ],
    },
    {
      label: 'Build',
      submenu: [
        {
          label: 'Build for Linux',
          accelerator: 'CmdOrCtrl+B',
          click: () => mainWindow.webContents.send('menu-action', 'build-linux'),
        },
        {
          label: 'Build for Web',
          accelerator: 'CmdOrCtrl+Shift+B',
          click: () => mainWindow.webContents.send('menu-action', 'build-web'),
        },
        { type: 'separator' },
        {
          label: 'Run',
          accelerator: 'F5',
          click: () => mainWindow.webContents.send('menu-action', 'run'),
        },
      ],
    },
  ];

  Menu.setApplicationMenu(Menu.buildFromTemplate(menuTemplate));
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

ipcMain.handle('load-example', async (event, key) => {
  const example = EXAMPLE_MAPPING[key];
  if (!example || !fs.existsSync(example.aroma)) {
    return { success: false, error: `Example '${key}' not found` };
  }
  try {
    const aromaContent = fs.readFileSync(example.aroma, 'utf8');
    let cContent = '';
    if (fs.existsSync(example.c)) {
      cContent = fs.readFileSync(example.c, 'utf8');
    }
    return { success: true, aroma: aromaContent, c: cContent, name: example.name };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('get-examples', async () => {
  return Object.entries(EXAMPLE_MAPPING).map(([key, val]) => ({
    key,
    name: val.name,
    hasAroma: fs.existsSync(val.aroma),
    hasC: fs.existsSync(val.c),
  }));
});

ipcMain.handle('open-folder', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    properties: ['openDirectory'],
    title: 'Select Project Folder',
  });
  if (result.canceled) return { canceled: true };
  const dir = result.filePaths[0];
  const files = scanProjectDir(dir);
  return { success: true, dir, name: path.basename(dir), files };
});

function scanProjectDir(dir) {
  const files = { folders: [], aroma: [], c: [], h: [], other: [], all: [] };
  try {
    for (const item of fs.readdirSync(dir, { withFileTypes: true })) {
      const itemPath = path.join(dir, item.name);
      if (item.isDirectory()) {
        // Skip build directories and hidden folders
        if (item.name.startsWith('.') || ['build', 'build_web', 'build_native', 'node_modules'].includes(item.name)) {
          continue;
        }
        files.folders.push(item.name);
      } else if (item.isFile()) {
        const ext = path.extname(item.name);
        const relPath = item.name;
        files.all.push(relPath);
        if (ext === '.aroma') files.aroma.push(relPath);
        else if (ext === '.c') files.c.push(relPath);
        else if (ext === '.h' || ext === '.hpp') files.h.push(relPath);
        else if (!['.cmake', '.o', '.log', '.tmp'].includes(ext)) files.other.push(relPath);
      }
    }
  } catch (e) {
    console.error('scanProjectDir error:', e);
  }
  return files;
}

function scanProjectTree(dir, relativePath = '') {
  const entries = [];
  try {
    const items = fs.readdirSync(dir, { withFileTypes: true });
    
    // Sort: folders first, then files alphabetically
    items.sort((a, b) => {
      if (a.isDirectory() && !b.isDirectory()) return -1;
      if (!a.isDirectory() && b.isDirectory()) return 1;
      return a.name.localeCompare(b.name);
    });

    for (const item of items) {
      // Skip build directories, hidden folders, and common ignore patterns
      if (item.name.startsWith('.') || 
          ['build', 'build_web', 'build_native', 'node_modules', 'dist', 'out', 'target'].includes(item.name)) {
        continue;
      }

      const itemPath = path.join(dir, item.name);
      const relPath = relativePath ? path.join(relativePath, item.name) : item.name;

      if (item.isDirectory()) {
        const children = scanProjectTree(itemPath, relPath);
        entries.push({
          name: item.name,
          path: relPath,
          fullPath: itemPath,
          type: 'folder',
          children,
          expanded: false
        });
      } else if (item.isFile()) {
        const ext = path.extname(item.name).toLowerCase();
        let fileType = 'file';
        if (ext === '.aroma') fileType = 'aroma';
        else if (ext === '.c') fileType = 'c';
        else if (ext === '.h' || ext === '.hpp') fileType = 'header';
        else if (ext === '.js') fileType = 'js';
        else if (ext === '.json') fileType = 'json';
        else if (ext === '.md') fileType = 'markdown';
        else if (ext === '.cmake' || ext === '.txt') fileType = 'text';
        else if (ext === '.wasm') fileType = 'wasm';
        else if (ext === '.html') fileType = 'html';
        else if (ext === '.css') fileType = 'css';

        entries.push({
          name: item.name,
          path: relPath,
          fullPath: itemPath,
          type: fileType,
          ext,
          size: fs.statSync(itemPath).size
        });
      }
    }
  } catch (e) {
    console.error('scanProjectTree error:', e);
  }
  return entries;
}

ipcMain.handle('scan-project-tree', async (event, dir) => {
  try {
    const tree = scanProjectTree(dir);
    return { success: true, tree };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('load-project-file', async (event, dir, filename) => {
  const filePath = path.join(dir, filename);
  if (!fs.existsSync(filePath)) {
    return { success: false, error: 'File not found' };
  }
  try {
    const content = fs.readFileSync(filePath, 'utf8');
    const ext = path.extname(filename);
    return { success: true, content, filename, ext };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('save-project-file', async (event, dir, filename, content) => {
  const filePath = path.join(dir, filename);
  try {
    fs.writeFileSync(filePath, content, 'utf8');
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('create-file', async (event, dir, filename, content) => {
  const filePath = path.join(dir, filename);
  try {
    if (fs.existsSync(filePath)) {
      return { success: false, error: 'File already exists' };
    }
    // Ensure parent directory exists
    const parentDir = path.dirname(filePath);
    if (!fs.existsSync(parentDir)) {
      fs.mkdirSync(parentDir, { recursive: true });
    }
    fs.writeFileSync(filePath, content || '', 'utf8');
    return { success: true, path: filePath };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('create-folder', async (event, dir, folderName) => {
  const folderPath = path.join(dir, folderName);
  try {
    if (fs.existsSync(folderPath)) {
      return { success: false, error: 'Folder already exists' };
    }
    fs.mkdirSync(folderPath, { recursive: true });
    return { success: true, path: folderPath };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('rename-file', async (event, dir, oldName, newName) => {
  const oldPath = path.join(dir, oldName);
  const newPath = path.join(dir, newName);
  try {
    if (!fs.existsSync(oldPath)) {
      return { success: false, error: 'File not found' };
    }
    if (fs.existsSync(newPath)) {
      return { success: false, error: 'Target already exists' };
    }
    fs.renameSync(oldPath, newPath);
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('delete-file', async (event, filePath) => {
  try {
    if (!fs.existsSync(filePath)) {
      return { success: false, error: 'File not found' };
    }
    const stat = fs.statSync(filePath);
    if (stat.isDirectory()) {
      fs.rmSync(filePath, { recursive: true, force: true });
    } else {
      fs.unlinkSync(filePath);
    }
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('show-confirm', async (event, title, message) => {
  const result = await dialog.showMessageBox(mainWindow, {
    type: 'warning',
    title: title || 'Confirm',
    message: message,
    buttons: ['OK', 'Cancel'],
    defaultId: 1,
    cancelId: 1,
  });
  return { confirmed: result.response === 0 };
});

ipcMain.handle('get-settings', async () => {
  const settingsPath = path.join(__dirname, 'settings.json');
  if (fs.existsSync(settingsPath)) {
    try {
      return JSON.parse(fs.readFileSync(settingsPath, 'utf8'));
    } catch (e) {}
  }
  return {
    theme: 'dark',
    fontSize: 14,
    fontFamily: 'Roboto Mono, Consolas, monospace',
    tabSize: 2,
    wordWrap: true,
    minimap: false,
    autoSave: false,
    showLineNumbers: true,
  };
});

ipcMain.handle('save-settings', async (event, settings) => {
  const settingsPath = path.join(__dirname, 'settings.json');
  try {
    fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2), 'utf8');
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
});

ipcMain.handle('create-project', async (event, dir, name) => {
  return new Promise((resolve) => {
    const projectPath = path.join(dir, name);
    if (fs.existsSync(projectPath)) {
      resolve({ success: false, error: 'Project already exists at ' + projectPath });
      return;
    }

    // Use the aroma CLI to create the project
    // Pipe empty lines for interactive prompts (accepts defaults)
    const cmd = `"${HOME}/.aroma/bin/aroma" create "${name}"`;
    const child = exec(cmd, {
      cwd: dir,
      maxBuffer: 1024 * 1024,
      input: '\n\n\n\ny\n',  // Accept defaults: package name, min SDK, target SDK, compile SDK, create project
    }, (error, stdout, stderr) => {
      if (fs.existsSync(projectPath)) {
        resolve({ success: true, path: projectPath, stdout, stderr });
      } else {
        resolve({
          success: false,
          error: error ? error.message : 'Project creation failed',
          stdout,
          stderr,
        });
      }
    });

    if (child.stdout) {
      child.stdout.on('data', (data) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('run-output', { type: 'stdout', data: data.toString() });
        }
      });
    }
    if (child.stderr) {
      child.stderr.on('data', (data) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('run-output', { type: 'stderr', data: data.toString() });
        }
      });
    }
  });
});

ipcMain.handle('build-project', async (event, dir, target) => {
  const aromaCli = path.join(HOME, '.aroma', 'bin', 'aroma');
  const buildTarget = target || 'linux';
  const cmd = `"${aromaCli}" build ${buildTarget}`;

  return new Promise((resolve) => {
    exec(cmd, {
      cwd: dir,
      maxBuffer: 1024 * 1024 * 10,
      stdio: ['ignore', 'pipe', 'pipe'],
    }, (error, stdout, stderr) => {
      if (error) {
        resolve({ success: false, error: error.message, stdout, stderr });
      } else {
        resolve({ success: true, stdout, stderr, target: buildTarget, dir });
      }
    });
  });
});

ipcMain.handle('build-and-run-project', async (event, dir, target) => {
  const aromaCli = path.join(HOME, '.aroma', 'bin', 'aroma');
  const buildTarget = target || 'linux';

  return new Promise((resolve) => {
    // First, build the project
    const buildCmd = `"${aromaCli}" build ${buildTarget}`;
    exec(buildCmd, { cwd: dir, maxBuffer: 1024 * 1024 * 10 }, (buildError, buildStdout, buildStderr) => {
      if (buildError) {
        resolve({ success: false, error: 'Build failed: ' + buildError.message, stage: 'build', stdout: buildStdout, stderr: buildStderr });
        return;
      }

      // For web target, serve the build_web directory
      if (buildTarget === 'web') {
        const buildWebDir = path.join(dir, 'build_web');
        if (fs.existsSync(buildWebDir)) {
          // Start a simple HTTP server
          const server = require('http').createServer((req, res) => {
            let filePath = path.join(buildWebDir, req.url === '/' ? 'index.html' : req.url);
            if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
              filePath = path.join(buildWebDir, 'index.html');
            }
            const ext = path.extname(filePath);
            const contentType = ext === '.html' ? 'text/html' :
                               ext === '.js' ? 'application/javascript' :
                               ext === '.wasm' ? 'application/wasm' :
                               ext === '.css' ? 'text/css' :
                               'application/octet-stream';
            res.setHeader('Content-Type', contentType);
            fs.createReadStream(filePath).pipe(res);
          });
          server.listen(0, '127.0.0.1', () => {
            const port = server.address().port;
            const url = `http://127.0.0.1:${port}/index.html`;
            if (mainWindow && !mainWindow.isDestroyed()) {
              mainWindow.webContents.send('run-output', { type: 'stdout', data: `Web server started at ${url}` });
              mainWindow.webContents.send('run-output', { type: 'started', pid: 0, url: url });
              // Open in browser
              shell.openExternal(url);
            }
            resolve({ success: true, stage: 'started', pid: 0, buildStdout, url });
          });
        } else {
          resolve({ success: false, error: 'build_web directory not found', stage: 'run', buildStdout });
        }
        return;
      }

      // For linux target, find the actual executable (not a directory)
      const buildDir = path.join(dir, 'build');
      let exePath = null;
      if (fs.existsSync(buildDir)) {
        const items = fs.readdirSync(buildDir);
        for (const item of items) {
          const itemPath = path.join(buildDir, item);
          const stat = fs.statSync(itemPath);
          // Must be a file, not a directory, and must be executable
          if (stat.isFile() && (stat.mode & 0o111)) {
            exePath = itemPath;
            break;
          }
        }
      }

      if (!exePath) {
        resolve({ success: false, error: 'No executable found in build directory. Build may have failed.', stage: 'run', buildStdout });
        return;
      }

      // Run the binary directly
      const child = exec(`"${exePath}"`, { cwd: dir, maxBuffer: 1024 * 1024 * 10 }, (runError, runStdout, runStderr) => {
        if (runError) {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('run-output', { type: 'stderr', data: 'Process exited with error: ' + runError.message });
          }
        } else {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('run-output', { type: 'stdout', data: 'Process exited successfully' });
          }
        }
      });

      // Stream output
      if (child.stdout) {
        child.stdout.on('data', (data) => {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('run-output', { type: 'stdout', data: data.toString() });
          }
        });
      }
      if (child.stderr) {
        child.stderr.on('data', (data) => {
          if (mainWindow && !mainWindow.isDestroyed()) {
            mainWindow.webContents.send('run-output', { type: 'stderr', data: data.toString() });
          }
        });
      }

      setImmediate(() => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('run-output', { type: 'started', pid: child.pid, exe: exePath });
        }
        resolve({ success: true, stage: 'started', pid: child.pid, buildStdout, exe: exePath });
      });
    });
  });
});

ipcMain.handle('run-binary', async (event, binaryPath) => {
  return new Promise((resolve) => {
    const child = exec(`"${binaryPath}"`, { maxBuffer: 1024 * 1024 * 10 }, (error, stdout, stderr) => {
      if (error) {
        resolve({ success: false, error: error.message, stdout, stderr });
      } else {
        resolve({ success: true, stdout, stderr });
      }
    });

    if (child.stdout) {
      child.stdout.on('data', (data) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('run-output', { type: 'stdout', data: data.toString() });
        }
      });
    }
    if (child.stderr) {
      child.stderr.on('data', (data) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('run-output', { type: 'stderr', data: data.toString() });
        }
      });
    }

    setTimeout(() => {
      resolve({ success: true, pid: child.pid });
    }, 100);
  });
});

ipcMain.handle('open-build-folder', async (event, dir) => {
  const buildDir = path.join(dir, 'build_native');
  if (fs.existsSync(buildDir)) {
    shell.openPath(buildDir);
    return { success: true, path: buildDir };
  }
  return { success: false, error: 'Build directory not found. Build the project first.' };
});

ipcMain.handle('find-binary', async (event, dir) => {
  const buildDir = path.join(dir, 'build_native');
  if (!fs.existsSync(buildDir)) {
    return { success: false, error: 'Build directory not found' };
  }
  // Find executable files in build dir
  const files = fs.readdirSync(buildDir, { withFileTypes: true });
  for (const file of files) {
    if (file.isFile()) {
      const filePath = path.join(buildDir, file.name);
      try {
        const stat = fs.statSync(filePath);
        // Check if executable (has execute bit)
        if (stat.mode & 0o111) {
          return { success: true, path: filePath };
        }
      } catch (e) {}
    }
  }
  return { success: false, error: 'No executable found' };
});

ipcMain.handle('build-web-preview', async (event, dir, projectName) => {
  const aromaCli = path.join(HOME, '.aroma', 'bin', 'aroma');
  const safeName = (projectName || path.basename(dir)).replace(/[^a-zA-Z0-9_-]/g, '_');
  const previewDir = path.join('/tmp', 'aromaui-preview', safeName);

  return new Promise((resolve) => {
    // Clean previous preview
    if (fs.existsSync(previewDir)) {
      try {
        fs.rmSync(previewDir, { recursive: true, force: true });
      } catch (e) {}
    }
    fs.mkdirSync(previewDir, { recursive: true });

    // Build for web
    const buildCmd = `"${aromaCli}" build web`;
    exec(buildCmd, { cwd: dir, maxBuffer: 1024 * 1024 * 20 }, (buildError, buildStdout, buildStderr) => {
      if (buildError) {
        resolve({ success: false, error: 'Build failed: ' + buildError.message, stdout: buildStdout, stderr: buildStderr });
        return;
      }

      // Copy build_web contents to preview directory
      const buildWebDir = path.join(dir, 'build_web');
      if (!fs.existsSync(buildWebDir)) {
        resolve({ success: false, error: 'build_web directory not found after build' });
        return;
      }

      try {
        copyRecursiveSync(buildWebDir, previewDir);
        const url = `file://${previewDir}/index.html`;
        resolve({ success: true, url, path: previewDir, buildStdout });
      } catch (e) {
        resolve({ success: false, error: 'Failed to copy files: ' + e.message });
      }
    });
  });
});

ipcMain.handle('open-web-preview', async (event, dir, projectName) => {
  const safeName = (projectName || path.basename(dir)).replace(/[^a-zA-Z0-9_-]/g, '_');
  const previewDir = path.join('/tmp', 'aromaui-preview', safeName);
  const indexFile = path.join(previewDir, 'index.html');

  if (!fs.existsSync(indexFile)) {
    return { success: false, error: 'Preview not built yet. Build the web preview first.' };
  }

  const url = `file://${indexFile}`;
  shell.openExternal(url);
  return { success: true, url, path: previewDir };
});

ipcMain.handle('get-preview-info', async (event, projectName) => {
  const safeName = (projectName || 'default').replace(/[^a-zA-Z0-9_-]/g, '_');
  const previewDir = path.join('/tmp', 'aromaui-preview', safeName);
  const exists = fs.existsSync(path.join(previewDir, 'index.html'));
  return {
    exists,
    path: previewDir,
    url: exists ? `file://${previewDir}/index.html` : null,
  };
});

ipcMain.handle('list-project-files', async (event, dir) => {
  if (!fs.existsSync(dir)) {
    return { success: false, error: 'Directory does not exist' };
  }
  const files = scanProjectDir(dir);
  return { success: true, files, dir };
});

ipcMain.handle('get-home-dir', async () => {
  return { path: HOME };
});

ipcMain.handle('get-basename', async (event, inputPath) => {
  return { name: path.basename(inputPath) };
});

ipcMain.handle('show-prompt', async (event, title, label, defaultValue) => {
  // Always return null - the renderer will use its own in-page prompt
  return { value: null };
});

ipcMain.handle('show-input', async (event, title, label, defaultValue) => {
  return { value: null };
});

ipcMain.handle('reveal-in-folder', async (event, filePath) => {
  shell.showItemInFolder(filePath);
  return { success: true };
});

function copyRecursiveSync(src, target) {
  const stat = fs.lstatSync(src);
  if (stat.isDirectory()) {
    if (!fs.existsSync(target)) {
      fs.mkdirSync(target, { recursive: true });
    }
    const entries = fs.readdirSync(src, { withFileTypes: true });
    for (const entry of entries) {
      const srcPath = path.join(src, entry.name);
      const targetPath = path.join(target, entry.name);
      if (entry.isDirectory()) {
        copyRecursiveSync(srcPath, targetPath);
      } else {
        fs.copyFileSync(srcPath, targetPath);
      }
    }
  } else {
    fs.copyFileSync(src, target);
  }
}
