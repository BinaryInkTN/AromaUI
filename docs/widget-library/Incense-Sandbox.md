<h1 id="incense-sandbox">Incense Sandbox</h1>
<p>The <strong>Incense Sandbox</strong> is an interactive, real-time playground for experimenting with AromaUI's declarative <a href="https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/docs/Incense-Format.md">Incense</a> markup. It pairs a <a href="https://microsoft.github.io/monaco-editor/">Monaco Editor</a> on the left with a live-rendering AromaUI canvas on the right, giving you instant visual feedback as you type.</p>
<h2 id="features">Features</h2>
<ul>
<li><strong>Real-time compilation</strong> — edits are debounced at 400ms and reloaded automatically</li>
<li><strong>Monaco Editor</strong> — syntax highlighting for all Incense widgets and properties</li>
<li><strong>Preset showcases</strong> — one-click examples for Buttons, Forms, Layout, and All Widgets</li>
<li><strong>Error reporting</strong> — parse and semantic errors are shown inline below the preview</li>
<li><strong>Emscripten/WASM</strong> — runs entirely in the browser with no server-side build step</li>
</ul>
<h2 id="supported-widgets">Supported Widgets</h2>
<p>The sandbox showcases the full widget library, including:</p>
<ul>
<li><strong>Basic:</strong> <code>Button</code>, <code>Label</code>, <code>Checkbox</code>, <code>Switch</code>, <code>Slider</code>, <code>Textbox</code>, <code>Progressbar</code>, <code>Divider</code></li>
<li><strong>Containers:</strong> <code>Card</code>, <code>Container</code> (flex row/column)</li>
<li><strong>Advanced:</strong> <code>Dropdown</code>, <code>Image</code>, <code>Gif</code>, <code>Loading</code>, <code>Gauge</code>, <code>Viewer3D</code>, <code>Map</code>, <code>Table</code>, <code>Tabs</code>, <code>Sidebar</code>, <code>Menu</code>, <code>Dialog</code>, <code>Snackbar</code>, <code>Tooltip</code>, <code>Chip</code></li>
</ul>
<h2 id="building">Building</h2>
<p>To build the sandbox for the web:</p>
<div class="codehilite"><pre><span></span><code>mkdir -p examples/incense_sandbox/build
cd examples/incense_sandbox/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j<span class="k">$(</span>nproc<span class="k">)</span>
</code></pre></div>
<p>This produces <code>incense_sandbox.html</code>, <code>incense_sandbox.js</code>, and <code>incense_sandbox.wasm</code>. Copy these files to <code>docs/widget-library/wasm/incense_sandbox/</code> to publish.</p>
<h2 id="usage">Usage</h2>
<ol>
<li>Open <code>docs/widget-library/wasm/incense_sandbox/index.html</code> in a browser (serve via HTTP for best results)</li>
<li>Edit the Incense markup in the Monaco editor on the left</li>
<li>The preview on the right updates automatically after you stop typing</li>
<li>Use the preset buttons at the top to load showcase examples</li>
<li>Check the status bar below the preview for errors</li>
</ol>
<h2 id="architecture">Architecture</h2>
<p>The sandbox is built on three layers:</p>
<div class="mermaid-wrapper">
<div class="mermaid-controls">
<button class="mermaid-export" onclick="exportMermaidAsSVG(this)" title="Export as SVG"><i data-lucide="download"></i></button>
<button class="mermaid-open" onclick="openMermaidInNewPage(this)" title="Open in new tab"><i data-lucide="external-link"></i></button>
</div>
<div class="mermaid">
flowchart TD
    subgraph subGraph2 ["Browser (JS)"]
        A["Monaco Editor"]
        B["Debounce (400ms)"]
        C["ccall / cwrap"]
    end
    subgraph subGraph1 ["WASM Boundary"]
        D["aroma_sandbox_reload"]
        E["IncenseLoadStringEx"]
        F["aroma_ui_create_window"]
    end
    subgraph subGraph0 ["AromaUI Core"]
        G["Scene Graph"]
        H["DrawList"]
        I["WebGL Renderer"]
    end
    A -->|"source"| B
    B -->|"string"| C
    C -->|"const char*"| D
    D -->|"parse"| E
    E -->|"build"| F
    F -->|"invalidate"| G
    G -->|"collect tasks"| H
    H -->|"flush"| I
    I -->|"present"| A
</div>
</div>

<p><strong>Sources:</strong></p>
<ul>
<li><a href="https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/examples/incense_sandbox/sandbox.c">examples/incense_sandbox/sandbox.c</a></li>
<li><a href="https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/include/aroma_incense_loader.h">include/aroma_incense_loader.h</a></li>
<li><a href="https://github.com/BinaryInkTN/AromaUI/blob/afd1c6b6/src/core/aroma_ui_impl.c">src/core/aroma_ui_impl.c</a></li>
</ul>
<hr />
<h2 id="extending">Extending</h2>
<p>To add your own preset examples, create a new <code>.aroma</code> file in <code>examples/incense_sandbox/showcase/</code> and add a button in <code>examples/incense_sandbox/index.html</code>. The preset content is embedded directly in the JavaScript for zero-load-time switching.</p>
