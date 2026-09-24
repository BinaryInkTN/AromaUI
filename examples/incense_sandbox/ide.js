

(function() {
  const state = {
    theme: 'dark',
    currentProject: null,
    currentFile: null,
    files: {},
    editorInstance: null,
    canvasConfig: { width: 320, height: 480 }
  };


  const elems = {
    themeBtn: document.getElementById('themeBtn'),
    buildBtn: document.getElementById('buildBtn'),
    runBtn: document.getElementById('runBtn'),
    runNativeBtn: document.getElementById('runNativeBtn'),
    nativeTarget: document.getElementById('nativeTarget'),
    fileTree: document.getElementById('file-tree'),
    editorTabs: document.getElementById('editor-tabs'),
    consoleOutput: document.getElementById('console-output'),
    statusMsg: document.getElementById('status-msg'),
    currentProjectLabel: document.getElementById('current-project'),
    canvasWrap: document.getElementById('canvas-wrap'),
    canvas: document.getElementById('canvas'),
    phoneFrame: document.getElementById('phone-frame'),
    resizeHandle: document.getElementById('resizeHandle'),
    homeScreen: document.getElementById('home-screen'),
    homeProjectsList: document.getElementById('home-projects-list'),
    newProjName: document.getElementById('newProjName')
  };

  function setStatus(msg) {
    elems.statusMsg.textContent = msg;
  }

  function logConsole(msg, type = 'info') {
    const d = new Date();
    const timeStr = `${d.getHours().toString().padStart(2,'0')}:${d.getMinutes().toString().padStart(2,'0')}:${d.getSeconds().toString().padStart(2,'0')}`;
    const span = document.createElement('div');
    span.className = `log-${type}`;
    span.textContent = `[${timeStr}] ${msg}`;
    elems.consoleOutput.appendChild(span);
    elems.consoleOutput.scrollTop = elems.consoleOutput.scrollHeight;
  }


  async function api(endpoint, payload = {}) {
    try {
      const res = await fetch(endpoint, {
        method: Object.keys(payload).length > 0 ? 'POST' : 'GET',
        headers: { 'Content-Type': 'application/json' },
        body: Object.keys(payload).length > 0 ? JSON.stringify(payload) : undefined
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Server error');
      return data;
    } catch (e) {
      logConsole(e.message, 'error');
      throw e;
    }
  }

  async function loadProjectsList() {
    try {
      const data = await api('/api/projects');
      elems.homeProjectsList.innerHTML = '';
      if (!data.projects || data.projects.length === 0) {
        elems.homeProjectsList.innerHTML = '<div style="color:var(--ide-text-muted)">No projects found. Create one below.</div>';
        return;
      }
      data.projects.forEach(p => {
        const div = document.createElement('div');
        div.className = 'home-item';
        div.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path></svg> ${p}`;
        div.onclick = () => openProject(p);
        elems.homeProjectsList.appendChild(div);
      });
    } catch (e) {
      elems.homeProjectsList.innerHTML = '<div style="color:var(--ide-error)">Failed to load projects. Is ide_server.py running?</div>';
    }
  }

  window.createNewProject = async function() {
    const name = elems.newProjName.value.trim();
    if (!name) return alert('Enter a project name');
    const btn = elems.newProjName.nextElementSibling;
    btn.disabled = true;
    btn.textContent = 'Creating (may take ~20s)...';
    try {
      logConsole(`Creating project: ${name}...`, 'info');
      await api('/api/project/create', { name });


      const defaultAroma =
`Window {
  title: "New App";
  width: 320;
  height: 480;
  layout: flex;
  direction: column;

  Label { text: "Hello from ${name}"; }
}`;
      await api('/api/project/save', { name, files: { 'src/ui.aroma': defaultAroma } });

      logConsole(`Project ${name} created!`, 'success');
      elems.newProjName.value = '';
      await openProject(name);
    } catch (e) {
    } finally {
      btn.disabled = false;
      btn.textContent = 'Create';
    }
  };

  async function openProject(name) {
    try {
      logConsole(`Opening project ${name}...`, 'info');
      const data = await api('/api/project/open', { name });
      state.currentProject = name;
      state.files = data.files || {};


      if (!state.files['src/ui.aroma']) state.files['src/ui.aroma'] = 'Window { width: 320; height: 480; }\\n';

      elems.currentProjectLabel.textContent = name;
      elems.homeScreen.classList.add('hidden');

      renderFileTree();
      openFile('src/ui.aroma');
      logConsole(`Project ${name} loaded.`, 'success');
    } catch (e) {}
  }

  window.showHomeScreen = function() {
    elems.homeScreen.classList.remove('hidden');
    loadProjectsList();
  };

  window.saveState = async function() {
    if (!state.currentProject || !state.currentFile) return;
    state.files[state.currentFile] = state.editorInstance.getValue();
    try {
      await api('/api/project/save', { name: state.currentProject, files: state.files });
      logConsole('Project saved.', 'success');
      setStatus('Saved');
    } catch (e) {}
  };


  function renderFileTree() {
    elems.fileTree.innerHTML = '';
    const keys = Object.keys(state.files).sort();
    keys.forEach(file => {
      const div = document.createElement('div');
      div.className = 'file-tree-item' + (file === state.currentFile ? ' selected' : '');
      div.onclick = () => openFile(file);
      div.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path><polyline points="13 2 13 9 20 9"></polyline></svg>${file}`;
      elems.fileTree.appendChild(div);
    });
  }

  function renderTabs() {
    elems.editorTabs.innerHTML = '';
    if (!state.currentFile) return;
    const div = document.createElement('div');
    div.className = 'editor-tab active';
    div.innerHTML = `<span>${state.currentFile}</span>`;
    elems.editorTabs.appendChild(div);
  }

  function openFile(file) {
    if (state.currentFile && state.editorInstance) {
      state.files[state.currentFile] = state.editorInstance.getValue();
    }
    state.currentFile = file;
    renderFileTree();
    renderTabs();

    let lang = 'incense';
    if (file.endsWith('.c') || file.endsWith('.h')) lang = 'c';
    else if (file.endsWith('.json')) lang = 'json';

    if (state.editorInstance) {
      monaco.editor.setModelLanguage(state.editorInstance.getModel(), lang);
      state.editorInstance.setValue(state.files[file] || '');
    }


    if (file === 'src/ui.aroma') {
      parseAromaDimensions();
    }
  }

  function parseAromaDimensions() {
    const code = state.files['src/ui.aroma'];
    if (!code) return;
    const wMatch = code.match(/width\s*:\s*(\d+)/);
    const hMatch = code.match(/height\s*:\s*(\d+)/);
    if (wMatch) state.canvasConfig.width = parseInt(wMatch[1], 10);
    if (hMatch) state.canvasConfig.height = parseInt(hMatch[1], 10);
    applyCanvasSize();
  }

  function applyCanvasSize() {
    elems.phoneFrame.style.width = state.canvasConfig.width + 'px';
    elems.phoneFrame.style.height = state.canvasConfig.height + 'px';
    elems.canvas.width = state.canvasConfig.width;
    elems.canvas.height = state.canvasConfig.height;
  }


  let isResizing = false;
  elems.resizeHandle.addEventListener('mousedown', (e) => {
    isResizing = true;
    e.preventDefault();
  });
  window.addEventListener('mousemove', (e) => {
    if (!isResizing) return;
    const rect = elems.canvasWrap.getBoundingClientRect();
    const frameRect = elems.phoneFrame.getBoundingClientRect();

    let newW = e.clientX - frameRect.left;
    let newH = e.clientY - frameRect.top;
    newW = Math.max(100, Math.min(newW, 2000));
    newH = Math.max(100, Math.min(newH, 2000));

    state.canvasConfig.width = Math.round(newW);
    state.canvasConfig.height = Math.round(newH);
    applyCanvasSize();
  });
  window.addEventListener('mouseup', () => {
    if (isResizing) {
      isResizing = false;
      syncCanvasSizeToCode();
      window.saveState();
      compilePreview();
    }
  });

  function syncCanvasSizeToCode() {
    if (state.currentFile !== 'src/ui.aroma') return;
    let code = state.editorInstance.getValue();
    code = code.replace(/(width\s*:\s*)(\d+)/, `$1${state.canvasConfig.width}`);
    code = code.replace(/(height\s*:\s*)(\d+)/, `$1${state.canvasConfig.height}`);
    state.editorInstance.setValue(code);
    state.files['src/ui.aroma'] = code;
  }


  function compilePreview() {
    window.saveState();
    const code = state.files['src/ui.aroma'] || '';

    if (window.Module && Module._aroma_sandbox_reload && Module._malloc) {
      applyCanvasSize();
      logConsole('Hot-reloading Emscripten Preview...', 'info');

      try {
        const n = Module.lengthBytesUTF8(code) + 1;
        const ptr = Module._malloc(n);
        Module.stringToUTF8(code, ptr, n);
        Module._aroma_sandbox_reload(ptr);
        Module._free(ptr);

        if (Module._aroma_sandbox_has_error) {
          if (Module._aroma_sandbox_has_error()) {
            const msg = Module.UTF8ToString(Module._aroma_sandbox_get_last_error());
            logConsole('Preview error: ' + msg, 'error');
            setStatus('Preview error');
            return;
          }
        }
      } catch (e) {
        logConsole('Preview reload failed: ' + (e && e.message ? e.message : e), 'error');
        setStatus('Preview error');
        return;
      }

      logConsole('Preview running.', 'success');
    } else {
      logConsole('Emscripten module not ready.', 'error');
    }
  }

  async function runNative() {
    if (!state.currentProject) return;
    window.saveState();
    const target = elems.nativeTarget.value;
    logConsole(`Building project for ${target}...`, 'info');
    setStatus('Building...');
    try {
      const data = await api('/api/project/build', { name: state.currentProject, target });
      logConsole(data.log || 'Build finished', 'success');

      if (data.success) {
        logConsole(`Running project on ${target}...`, 'info');
        setStatus('Running...');
        const runData = await api('/api/project/run', { name: state.currentProject, target });
        logConsole(runData.log || 'Run command dispatched', 'info');
      }

      setStatus('Ready');
    } catch (e) {
      setStatus('Build Error');
    }
  }


  window.initIDE = function(editor) {
    state.editorInstance = editor;

    elems.themeBtn.onclick = () => {
      state.theme = state.theme === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', state.theme);
      monaco.editor.setTheme(state.theme === 'dark' ? 'incense-dark' : 'incense-light');
    };

    elems.buildBtn.onclick = compilePreview;
    elems.runBtn.onclick = compilePreview;
    elems.runNativeBtn.onclick = runNative;

    loadProjectsList();
    logConsole('IDE Ready. Select or create a project.', 'info');
  };


  window.aroma_sandbox_get_width = function() { return state.canvasConfig.width; };
  window.aroma_sandbox_get_height = function() { return state.canvasConfig.height; };

})();
