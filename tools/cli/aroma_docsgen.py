#!/usr/bin/env python3

import argparse
import hashlib
import html as _htmlesc
import json
import os
import re
import subprocess
import shutil
import tempfile
import urllib.parse
from datetime import datetime
from typing import Dict, List, Optional

import markdown
import yaml
from bs4 import BeautifulSoup
from markdown.extensions import Extension
from markdown.preprocessors import Preprocessor
from pygments.formatters import HtmlFormatter
from weasyprint import HTML

_DOT_RESERVED = frozenset(
    {
        "node",
        "edge",
        "graph",
        "digraph",
        "subgraph",
        "strict",
        "true",
        "false",
        "null",
    }
)


def _dot_id(raw: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]", "_", raw.strip())
    s = re.sub(r"_+", "_", s).strip("_") or "n"
    if s[0].isdigit():
        s = "n_" + s
    if s.lower() in _DOT_RESERVED:
        s = "n_" + s
    return s


def _dot_label(text: str) -> str:
    text = text.replace("\\", "\\\\")
    text = text.replace('"', '\\"')
    text = text.replace("\n", "\\n")
    text = text.replace("\r", "")
    return text


def _parse_node_decl(raw: str):
    raw = raw.strip()
    patterns = [
        (r"([A-Za-z0-9_]+)\(\[(.+?)\]\)", 'shape=rectangle style="rounded,filled"'),
        (r"([A-Za-z0-9_]+)\[\[(.+?)\]\]", "shape=rectangle"),
        (r"([A-Za-z0-9_]+)\[(.+?)\]", "shape=rectangle"),
        (r"([A-Za-z0-9_]+)\(\((.+?)\)\)", "shape=ellipse"),
        (r"([A-Za-z0-9_]+)\((.+?)\)", "shape=rectangle style=rounded"),
        (r"([A-Za-z0-9_]+)\{(.+?)\}", "shape=diamond"),
        (r"([A-Za-z0-9_]+)>(.+?)\]", "shape=trapezium"),
    ]
    for pattern, shape in patterns:
        m = re.match(pattern, raw)
        if m:
            return _dot_id(m.group(1)), m.group(2).strip(), shape
    m = re.match(r"^([A-Za-z0-9_]+)$", raw)
    if m:
        return _dot_id(m.group(1)), "", ""
    nid = _dot_id(raw)
    return nid, raw, ""


def _node_attr_str(label: str, shape: str, base: str) -> str:
    parts = [f'label="{_dot_label(label)}"']
    if shape:
        parts.append(shape)
    parts.append(base)
    return " ".join(p for p in parts if p)


def _parse_subgraph_header(line: str):
    rest = re.match(r"subgraph\s*(.*)", line, re.I).group(1).strip()
    m = re.match(r'^([A-Za-z0-9_]+)\["?([^"\]]+)"?\]\s*$', rest)
    if m:
        return _dot_id(m.group(1)), m.group(2).strip()
    m = re.match(r"^([A-Za-z0-9_]+)\[([^\]]+)\]\s*$", rest)
    if m:
        return _dot_id(m.group(1)), m.group(2).strip()
    if rest:
        return _dot_id(rest), rest.strip('"')
    return "sg", "subgraph"


def _mermaid_flowchart_to_dot(lines: List[str]) -> str:
    first = lines[0].strip()
    m = re.match(r"(?:graph|flowchart)\s+(\w+)", first, re.I)
    direction = "TB"
    if m:
        d = m.group(1).upper()
        direction = {"TD": "TB", "TB": "TB", "LR": "LR", "RL": "RL", "BT": "BT"}.get(
            d, "TB"
        )

    base_node = (
        'fontname="Google Sans" fontsize=13 style=filled '
        'fillcolor="#E6F4FF" color="#0468D7"'
    )
    base_edge = 'fontname="Google Sans" fontsize=11 color="#5C5C5C"'

    id_map: Dict[str, str] = {}
    node_attrs: Dict[str, str] = {}
    edges: List[str] = []
    sections: List[List[str]] = [[]]
    sg_depth = 0

    def current() -> List[str]:
        return sections[-1]

    def resolve_id(raw_token: str) -> str:
        bare = re.match(r"^([A-Za-z0-9_]+)", raw_token.strip())
        key = bare.group(1) if bare else raw_token.strip()
        if key not in id_map:
            id_map[key] = _dot_id(key)
        return id_map[key]

    def add_node(raw_token: str, label: str, shape: str) -> str:
        dot_id = resolve_id(raw_token)
        if dot_id not in node_attrs:
            lbl = label if label else dot_id
            node_attrs[dot_id] = _node_attr_str(lbl, shape, base_node)
        return dot_id

    def make_edge(sid: str, did: str, label: str = "", directed: bool = True) -> str:
        attrs = base_edge
        if label:
            attrs += f' label="{_dot_label(label)}"'
        if not directed:
            attrs += " dir=none"
        return f"    {sid} -> {did} [{attrs}]"

    def handle_node_token(raw_token: str) -> str:
        nid, nlbl, nshp = _parse_node_decl(raw_token)
        bare = re.match(r"^([A-Za-z0-9_]+)", raw_token.strip())
        key = bare.group(1) if bare else raw_token.strip()
        if key not in id_map:
            id_map[key] = nid
        dot_id = id_map[key]
        if dot_id not in node_attrs:
            lbl = nlbl if nlbl else dot_id
            node_attrs[dot_id] = _node_attr_str(lbl, nshp, base_node)
        return dot_id

    for raw_line in lines[1:]:
        line = raw_line.strip()
        if not line or line.startswith("%%") or line.startswith("%{"):
            continue
        lo = line.lower()
        if lo.startswith(("classdef ", "class ", "style ", "linkstyle ")):
            continue
        if lo.startswith("subgraph"):
            sg_depth += 1
            sg_id, sg_label = _parse_subgraph_header(line)
            cluster_id = f"cluster_{sg_id}_{sg_depth}"
            sections.append(
                [
                    f"subgraph {cluster_id} {{",
                    f'  label="{_dot_label(sg_label)}"',
                    f'  style=filled fillcolor="#F2F8FF" color="#A6C8FF"',
                ]
            )
            continue
        if lo == "end" and len(sections) > 1:
            finished = sections.pop()
            finished.append("}")
            indent = "  " * (len(sections))
            for sub_line in finished:
                sections[-1].append(indent + sub_line)
            continue

        m = re.match(r"(.+?)\s*-+>+\s*\|([^|]*)\|\s*(.+)", line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            lbl = m.group(2).strip()
            did = handle_node_token(m.group(3).strip())
            edges.append(make_edge(sid, did, lbl))
            continue
        m = re.match(r"(.+?)\s*--([^->|]+?)-->\s*(.+)", line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            lbl = m.group(2).strip()
            did = handle_node_token(m.group(3).strip())
            edges.append(make_edge(sid, did, lbl))
            continue
        m = re.match(r"(.+?)\s*-{2,}>+\s*(.+)", line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            did = handle_node_token(m.group(2).strip())
            edges.append(make_edge(sid, did))
            continue
        m = re.match(r"(.+?)\s*-{3,}\s*(.+)", line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            did = handle_node_token(m.group(2).strip())
            edges.append(make_edge(sid, did, directed=False))
            continue

        nid, nlbl, nshp = _parse_node_decl(line)
        if nlbl or (nid and len(sections) > 1):
            bare = re.match(r"^([A-Za-z0-9_]+)", line.strip())
            key = bare.group(1) if bare else line.strip()
            if key not in id_map:
                id_map[key] = nid
            dot_id = id_map[key]
            if dot_id not in node_attrs:
                lbl = nlbl if nlbl else dot_id
                node_attrs[dot_id] = _node_attr_str(lbl, nshp, base_node)
            if len(sections) > 1:
                current().append(f"  {dot_id}")

    while len(sections) > 1:
        finished = sections.pop()
        finished.append("}")
        indent = "  " * (len(sections))
        for sub_line in finished:
            sections[-1].append(indent + sub_line)

    parts = [
        "digraph G {",
        f"  rankdir={direction}",
        '  graph [fontname="Google Sans" bgcolor=white]',
        '  node  [fontname="Google Sans" fontsize=13 style=filled '
        'fillcolor="#E6F4FF" color="#0468D7"]',
        '  edge  [fontname="Google Sans" fontsize=11 color="#5C5C5C"]',
    ]
    for dot_id, attrs in node_attrs.items():
        parts.append(f"  {dot_id} [{attrs}]")
    parts.extend(f"  {l}" for l in sections[0])
    parts.extend(edges)
    parts.append("}")
    return "\n".join(parts)


def _mermaid_sequence_to_dot(lines: List[str]) -> str:
    actors: List[tuple] = []
    actor_ids: Dict[str, str] = {}
    messages: List[tuple] = []

    for line in lines[1:]:
        line = line.strip()
        if not line or line.startswith("%%"):
            continue
        lo = line.lower()
        if lo.startswith(
            (
                "note ",
                "loop",
                "alt",
                "opt",
                "else",
                "end",
                "activate",
                "deactivate",
                "rect",
                "par",
                "critical",
                "break",
                "autonumber",
            )
        ):
            continue
        if lo.startswith(("participant", "actor")):
            m = re.match(r"(?:participant|actor)\s+(\S+)(?:\s+as\s+(.+))?", line, re.I)
            if m:
                raw = m.group(1)
                display = (m.group(2) or raw).strip()
                nid = _dot_id(raw)
                if raw not in actor_ids:
                    actor_ids[raw] = nid
                    actors.append((nid, display))
            continue
        m = re.match(r"(\S+)\s*[-=]+[->xX)]+[+-]?\s*(\S+)\s*:\s*(.+)", line)
        if m:
            src_raw = m.group(1).rstrip(":")
            dst_raw = m.group(2).rstrip(":")
            msg = m.group(3).strip()
            for raw in (src_raw, dst_raw):
                if raw not in actor_ids:
                    nid = _dot_id(raw)
                    actor_ids[raw] = nid
                    actors.append((nid, raw))
            messages.append((actor_ids[src_raw], actor_ids[dst_raw], msg))

    parts = [
        "digraph G {",
        "  rankdir=LR",
        '  node [shape=box fontname="Google Sans" fontsize=13 style=filled '
        'fillcolor="#E6F4FF" color="#0468D7"]',
        '  edge [fontname="Google Sans" fontsize=11 color="#0468D7"]',
    ]
    for nid, display in actors:
        parts.append(f'  {nid} [label="{_dot_label(display)}"]')
    for src, dst, msg in messages:
        parts.append(f'  {src} -> {dst} [label="{_dot_label(msg)}"]')
    parts.append("}")
    return "\n".join(parts)


def _mermaid_to_dot(source: str) -> Optional[str]:
    lines = [l for l in source.strip().splitlines() if l.strip()]
    if not lines:
        return None
    first = lines[0].strip().lower()
    if re.match(r"(?:graph|flowchart)\b", first, re.I):
        return _mermaid_flowchart_to_dot(lines)
    if first.startswith("sequencediagram"):
        return _mermaid_sequence_to_dot(lines)
    return None


class MermaidRenderer:
    def __init__(self):
        try:
            import graphviz as _gv

            _gv.Source("digraph G {}").pipe(format="svg")
            self._gv = _gv
            self._available = True
        except Exception as e:
            self._gv = None
            self._available = False
            print(f"⚠  Mermaid PDF rendering disabled: graphviz not available ({e})")

    def render_one(self, source: str) -> Optional[str]:
        if not self._available:
            return None
        dot = _mermaid_to_dot(source)
        if dot is None:
            return None
        try:
            svg_bytes = self._gv.Source(dot).pipe(format="svg")
            svg = svg_bytes.decode("utf-8")
            svg = re.sub(r"<\?xml[^?]*\?>", "", svg)
            svg = re.sub(r"<!DOCTYPE[^>]*>", "", svg)
            return svg.strip()
        except Exception as e:
            print(f"  ⚠ diagram render failed: {e}")
            return None

    def render_all(self, diagrams: List[str]) -> List[Optional[str]]:
        return [self.render_one(src) for src in diagrams]


def _replace_mermaid_with_svg(html_content: str, renderer: MermaidRenderer) -> str:
    soup = BeautifulSoup(html_content, "html.parser")
    slots = soup.find_all("div", class_="mermaid")
    if not slots:
        return html_content

    sources = [slot.get_text() for slot in slots]
    svgs = renderer.render_all(sources)

    for slot, svg in zip(slots, svgs):
        target = slot.find_parent("div", class_="mermaid-wrapper") or slot
        if svg:
            replacement = BeautifulSoup(
                f'<div class="mermaid-pdf">{svg}</div>', "html.parser"
            ).find("div")
        else:
            src = slot.get_text().strip()
            diagram_type = src.splitlines()[0].strip() if src else "Diagram"
            replacement = BeautifulSoup(
                f'<div class="mermaid-fallback">'
                f'<p class="mermaid-fallback-label">⬡ {diagram_type}</p>'
                f"<pre><code>{src}</code></pre>"
                f"</div>",
                "html.parser",
            ).find("div")
        target.replace_with(replacement)

    return str(soup)


class MermaidPreprocessor(Preprocessor):
    def run(self, lines):
        new_lines = []
        i = 0
        while i < len(lines):
            line = lines[i]
            if line.strip() == "```mermaid":
                mermaid_content = []
                i += 1
                while i < len(lines) and lines[i].strip() != "```":
                    mermaid_content.append(lines[i])
                    i += 1
                if i < len(lines):
                    i += 1
                if mermaid_content:
                    while mermaid_content and not mermaid_content[0].strip():
                        mermaid_content.pop(0)
                    html = (
                        '<div class="mermaid-wrapper">\n'
                        '<div class="mermaid-controls">\n'
                        '<button class="mermaid-export" onclick="exportMermaidAsSVG(this)" title="Export as SVG">'
                        '<i data-lucide="download"></i></button>\n'
                        '<button class="mermaid-open" onclick="openMermaidInNewPage(this)" title="Open in new tab">'
                        '<i data-lucide="external-link"></i></button>\n'
                        "</div>\n"
                        '<div class="mermaid">\n'
                        + "\n".join(mermaid_content)
                        + "\n</div>\n</div>"
                    )
                    new_lines.append(html)
                continue
            new_lines.append(line)
            i += 1
        return new_lines


class SandboxPreprocessor(Preprocessor):
    def run(self, lines):
        new_lines = []
        i = 0
        while i < len(lines):
            line = lines[i]
            if line.strip() == "```c sandbox":
                sandbox_code = []
                i += 1
                while i < len(lines) and lines[i].strip() != "```":
                    sandbox_code.append(lines[i])
                    i += 1
                if i < len(lines):
                    i += 1
                if sandbox_code:
                    code_str = "\n".join(sandbox_code)
                    code_hash = hashlib.md5(code_str.encode('utf-8')).hexdigest()[:8]
                    
                    # Generate the HTML for the sandbox
                    html = (
                        '<div class="sandbox-wrapper" style="display:flex; flex-direction:row; gap:16px; margin:24px 0;">\n'
                        '  <div class="sandbox-code" style="flex:1; overflow:auto;">\n'
                        '    <pre><code class="language-c">' + code_str.replace('<', '&lt;').replace('>', '&gt;') + '</code></pre>\n'
                        '  </div>\n'
                        '  <div class="sandbox-preview" style="flex:1; border:1px solid var(--md-outline-variant); border-radius:8px; overflow:hidden;">\n'
                        f'    <iframe src="sandboxes/sandbox_{code_hash}.html" style="width:100%; height:100%; min-height:400px; border:none;"></iframe>\n'
                        '  </div>\n'
                        '</div>'
                    )
                    new_lines.append(html)
                    
                    # Compile the sandbox code
                    self._compile_sandbox(code_str, code_hash)
                continue
            new_lines.append(line)
            i += 1
        return new_lines

    def _compile_sandbox(self, code_str: str, code_hash: str):
        # Determine the project root
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        site_dir = os.path.join(project_root, "_site")
        sandbox_dir = os.path.join(site_dir, "sandboxes")
        os.makedirs(sandbox_dir, exist_ok=True)
        
        out_html = os.path.join(sandbox_dir, f"sandbox_{code_hash}.html")
        if os.path.exists(out_html):
            return # Already compiled
            
        with tempfile.TemporaryDirectory() as tmpdir:
            src_file = os.path.join(tmpdir, "main.c")
            
            # Wrap code if it doesn't contain main()
            if "int main(" not in code_str and "int main (" not in code_str:
                wrapped = (
                    '#include <aroma.h>\n'
                    '#ifdef __EMSCRIPTEN__\n'
                    '#include <emscripten/emscripten.h>\n'
                    '#endif\n\n'
                    'static void main_loop(void) {\n'
                    '    aroma_ui_run_frame();\n'
                    '}\n\n'
                    'int main(void) {\n'
                    '    aroma_ui_init();\n'
                    '    AromaWindow *window = aroma_ui_create_window("Aroma Sandbox", 400, 400);\n'
                    '    AromaNode *root = (AromaNode *)window;\n'
                    f'    {code_str}\n'
                    '#ifdef __EMSCRIPTEN__\n'
                    '    emscripten_set_main_loop(main_loop, 0, 1);\n'
                    '#else\n'
                    '    while (aroma_ui_is_running()) { aroma_ui_run_frame(); }\n'
                    '#endif\n'
                    '    return 0;\n'
                    '}\n'
                )
            else:
                wrapped = code_str
                
            with open(src_file, "w") as f:
                f.write(wrapped)
                
            include_dir = os.path.join(project_root, "include")
            lib_path = os.path.join(project_root, "examples", "smartwatch_example", "build_web", "aroma_root", "src", "libaroma.a")
            
            emcc_path = os.path.join(project_root, "vendors", "emscripten", "upstream", "emscripten", "emcc")
            if not os.path.exists(emcc_path):
                emcc_path = "emcc"
                
            cmd = [
                emcc_path, src_file, "-o", out_html,
                "-I" + include_dir,
                lib_path,
                "-s", "USE_FREETYPE=1",
                "-s", "USE_WEBGL2=1",
                "-s", "FULL_ES3=1",
                "-s", "ALLOW_MEMORY_GROWTH=1",
                "-s", "ASYNCIFY=1"
            ]
            
            env = os.environ.copy()
            env["EM_CACHE"] = os.path.join(sandbox_dir, ".emcache")
            env["EM_CONFIG"] = os.path.join(project_root, "vendors", "emscripten", ".emscripten")
            
            try:
                subprocess.run(cmd, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            except subprocess.CalledProcessError as e:
                print(f"Failed to compile sandbox {code_hash}: {e.stderr.decode('utf-8')}")
                # Create a fallback HTML
                with open(out_html, "w") as f:
                    f.write(f"<html><body><h3>Compilation Failed</h3><pre>{e.stderr.decode('utf-8')}</pre></body></html>")


class MermaidExtension(Extension):
    def extendMarkdown(self, md):
        md.preprocessors.register(MermaidPreprocessor(md), "mermaid", 175)

class SandboxExtension(Extension):
    def extendMarkdown(self, md):
        md.preprocessors.register(SandboxPreprocessor(md), "sandbox", 176)


class IncenseDemoPreprocessor(Preprocessor):
    """Turn ```incense-demo NAME blocks into live preview embeds.

    The demo code lives in sandbox.html's curated EXAMPLES map, so docs
    pages stay small and every demo is verified to load cleanly.
    """

    def run(self, lines):
        new_lines = []
        i = 0
        while i < len(lines):
            line = lines[i]
            m = re.match(r"```incense-demo\s+([A-Za-z0-9_-]+)\s*$", line.strip())
            if m:
                name = m.group(1)
                i += 1
                while i < len(lines) and lines[i].strip() != "```":
                    i += 1
                if i < len(lines):
                    i += 1
                html = (
                    '<div class="demo-frame">\n'
                    '  <div class="demo-head"><span>Live demo</span>'
                    f'<a href="sandbox.html?example={name}" target="_blank" rel="noopener">Open in sandbox</a></div>\n'
                    f'  <iframe src="sandbox.html?embed=1&example={name}" title="Live {name} demo" loading="lazy"></iframe>\n'
                    "</div>"
                )
                new_lines.append(html)
                continue
            new_lines.append(line)
            i += 1
        return new_lines


class IncenseDemoExtension(Extension):
    def extendMarkdown(self, md):
        md.preprocessors.register(IncenseDemoPreprocessor(md), "incense-demo", 174)

def _title_to_slug(title: str) -> str:
    slug = title.lower().strip()
    slug = re.sub(r'[^\w\s-]', '', slug)
    slug = re.sub(r'[\s_]+', '-', slug)
    slug = re.sub(r'-+', '-', slug).strip('-')
    if not slug:
        slug = 'page'
    return slug


class DocGenerator:
    def __init__(self):
        self.template = self._build_template()
        self.pdf_template = self._build_pdf_template()
        self._mermaid = MermaidRenderer()
        self.search_index = []

    def _pygments_css(self) -> str:
        light = HtmlFormatter(style="xcode", noclasses=False).get_style_defs(
            ".codehilite"
        )
        dark = HtmlFormatter(style="monokai", noclasses=False).get_style_defs(
            ".codehilite"
        )
        dark_prefixed = "\n".join(
            f"[data-theme='dark'] {line}"
            if line.strip() and not line.strip().startswith("/*")
            else line
            for line in dark.splitlines()
        )
        return f"{light}\n{dark_prefixed}\n"

    PLATFORM_ICONS = {
        "ios": "smartphone",
        "android": "smartphone",
        "web": "globe",
        "windows": "monitor",
        "macos": "monitor",
        "linux": "terminal",
        "docker": "box",
        "kubernetes": "layers",
        "aws": "cloud",
        "azure": "cloud",
        "gcp": "cloud",
        "python": "terminal",
        "javascript": "code-2",
        "typescript": "code-2",
        "react": "code-2",
        "vue": "code-2",
        "angular": "code-2",
        "node": "server",
        "go": "terminal",
        "rust": "terminal",
        "java": "coffee",
        "kotlin": "code-2",
        "swift": "smartphone",
        "flutter": "smartphone",
        "x11": "terminal",
        "wayland": "terminal",
        "espressif": "cpu",
        "embedded": "cpu",
        "esp32": "cpu",
    }

    PLATFORM_COLORS = {
        "android": "#3ddc84",
        "ios": "#007aff",
        "linux": "#e95420",
        "windows": "#0078d4",
        "macos": "#636366",
        "web": "#0071e3",
        "docker": "#2496ed",
        "kubernetes": "#326ce5",
        "aws": "#ff9900",
        "azure": "#0089d6",
        "gcp": "#4285f4",
        "python": "#3776ab",
        "javascript": "#f7df1e",
        "typescript": "#3178c6",
        "react": "#61dafb",
        "node": "#339933",
        "rust": "#ce422b",
        "go": "#00add8",
        "java": "#f89820",
        "kotlin": "#7f52ff",
        "swift": "#fa7343",
        "flutter": "#54c5f8",
        "x11": "#1f6fad",
        "wayland": "#ffb347",
        "espressif": "#e7352c",
        "embedded": "#6d6d6d",
    }

    def _normalize_platform(self, p) -> Optional[Dict]:
        if isinstance(p, str):
            name = p.strip()
            if not name:
                return None
            return {
                "name": name,
                "icon": self.PLATFORM_ICONS.get(name.lower(), "cpu"),
                "color": self.PLATFORM_COLORS.get(name.lower(), "#636366"),
            }
        if isinstance(p, dict):
            name = p.get("name") or p.get("title") or ""
            if not name:
                return None
            return {
                "name": name,
                "icon": p.get("icon") or self.PLATFORM_ICONS.get(name.lower(), "cpu"),
                "color": p.get("color")
                or self.PLATFORM_COLORS.get(name.lower(), "#636366"),
            }
        return None

    def _platform_badge_html(self, platform) -> str:
        p = self._normalize_platform(platform)
        if not p:
            return ""
        return (
            f'<span class="platform-badge" style="--badge-color:{p["color"]}" title="{p["name"]}">'
            f"<span>{p['name']}</span></span>"
        )

    def _welcome_page_html(
        self,
        project_name: str,
        description: str,
        hero: Optional[Dict] = None,
        version: str = "",
    ) -> str:
        hero = hero or {}
        hero_title = hero.get("title", project_name)
        hero_desc = hero.get("description", description)
        hero_bg = hero.get("background", "#0066CC")
        hero_actions = hero.get("actions", []) or []

        if hero_actions:
            buttons = "".join(
                f'<button class="{"m3-btn-filled" if a.get("primary") else "m3-btn-outlined"}"'
                f' onclick="{a.get("onclick", "")}">'
                f'<span>{a.get("text", "")}</span>'
                f"</button>"
                for a in hero_actions
            )
        else:
            buttons = (
                '<button class="m3-btn-filled" onclick="showFirstDoc()">'
                "<span>Start reading</span>"
                "</button>"
                '<button class="m3-btn-outlined" onclick="openSearch()">'
                "<span>Search docs</span>"
                "</button>"
            )

        features = [
            ("Cross-platform core", "One codebase for Linux, Android, Web and embedded.", "layers",
             "showPage(SLUG_TO_ID['architecture-overview'],'Overview',null)"),
            ("Incense UI language", "Describe interfaces in code, preview them instantly.", "code",
             "showPage(SLUG_TO_ID['incense-declarative-ui'],'Widget Library',null)"),
            ("Widget library", "Buttons, lists, maps, dialogs and more, ready to use.", "layout-grid",
             "showPage(SLUG_TO_ID['input-controls'],'Widget Library',null)"),
            ("CLI toolchain", "Scaffold, build and deploy from one command.", "terminal",
             "showPage(SLUG_TO_ID['building-deployment'],'Build & Deploy',null)"),
        ]
        tiles = (
            '<div class="metro-tiles">'
            + "".join(
                f'<div class="metro-tile" onclick="{go}">'
                f'<i data-lucide="{icon}" class="metro-icon"></i>'
                f'<span class="metro-label">{title}</span>'
                f'<span class="metro-desc">{desc}</span>'
                f"</div>"
                for title, desc, icon, go in features
            )
            + "</div>"
        )

        return f'''
        <div class="welcome-page">
            <div class="home-hero">
                <div class="hero-inner">
                  <div class="hero-copy">
                    <h1 class="home-title">{hero_title}</h1>
                    <p class="home-tagline">{hero_desc}</p>
                    <div class="home-hero-actions">
                        {buttons}
                    </div>
                    <div class="hero-release">
                        <a class="hero-dl-btn" id="heroDlBtn" href="https://github.com/BinaryInkTN/AromaUI/releases">
                            <span id="heroDlTxt">Download</span>
                        </a>
                        <span class="hero-dl-meta" id="heroDlMeta">Latest release</span>
                    </div>
                  </div>
                  <div class="hero-graphic" aria-hidden="true">
                    <svg viewBox="0 0 360 300" width="100%" role="img">
                      <circle cx="180" cy="150" r="128" fill="#EAF2FB"/>
                      <circle cx="64" cy="52" r="10" fill="#B6D1FF"/>
                      <circle cx="302" cy="248" r="14" fill="#B6D1FF"/>
                      <rect x="126" y="38" width="118" height="224" rx="16" fill="#FFFFFF" stroke="#4F8EF7" stroke-width="4"/>
                      <clipPath id="phoneClip"><rect x="126" y="38" width="118" height="224" rx="16"/></clipPath>
                      <g clip-path="url(#phoneClip)">
                        <rect x="126" y="38" width="118" height="34" fill="#4F8EF7"/>
                        <circle cx="185" cy="48" r="3" fill="#FFFFFF"/>
                        <rect x="140" y="88" width="90" height="12" rx="6" fill="#296AD4"/>
                        <rect x="140" y="108" width="64" height="8" rx="4" fill="#B6D1FF"/>
                        <rect x="140" y="132" width="90" height="34" rx="4" fill="#EAF2FB"/>
                        <rect x="140" y="174" width="90" height="34" rx="4" fill="#EAF2FB"/>
                        <rect x="140" y="216" width="58" height="20" rx="10" fill="#4F8EF7"/>
                      </g>
                      <rect x="30" y="96" width="76" height="60" rx="6" fill="#FFFFFF" stroke="#D7DBE4" stroke-width="2"/>
                      <text x="42" y="122" font-family="monospace" font-size="16" font-weight="bold" fill="#4F8EF7">&lt;/&gt;</text>
                      <text x="42" y="142" font-family="monospace" font-size="9" fill="#697177">aroma_ui</text>
                      <circle cx="286" cy="104" r="26" fill="#4F8EF7"/>
                      <path d="M279 92 L297 104 L279 116 Z" fill="#FFFFFF"/>
                      <rect x="262" y="190" width="68" height="44" rx="6" fill="#FFFFFF" stroke="#D7DBE4" stroke-width="2"/>
                      <rect x="272" y="202" width="48" height="8" rx="4" fill="#8CBF26"/>
                      <rect x="272" y="214" width="32" height="8" rx="4" fill="#B6D1FF"/>
                    </svg>
                  </div>
                </div>
            </div>
            {tiles}
            <div class="home-section sandbox-spotlight">
                <div class="sandbox-head">
                    <h2 class="section-heading" style="margin-bottom:0">Try it in your browser</h2>
                    <a class="sandbox-open" href="sandbox.html" target="_blank" rel="noopener">Open fullscreen</a>
                </div>
                <p class="sandbox-sub">Write Incense UI code and click Run. The preview renders instantly in your browser via WebAssembly.</p>
                <div class="sandbox-frame">
                    <iframe src="sandbox.html" title="Incense live sandbox" style="width:100%;height:800px;border:none;display:block"></iframe>
                </div>
            </div>
            <footer class="home-footer">
                <div class="home-footer-inner">
                    <h2>What are you building today? Share it with us</h2>
                    <p>Built an app, a widget, or an example with AromaUI? Show it off, ask for feedback, or report a snag. The project grows through community builds.</p>
                    <div class="home-footer-actions">
                        <a class="m3-btn-filled" href="https://github.com/BinaryInkTN/AromaUI/discussions" target="_blank" rel="noopener"><span>Start a discussion</span></a>
                        <a class="m3-btn-outlined" href="https://github.com/BinaryInkTN/AromaUI/issues" target="_blank" rel="noopener"><span>Report an issue</span></a>
                        <a class="m3-btn-outlined" href="https://github.com/BinaryInkTN/AromaUI" target="_blank" rel="noopener"><span>View on GitHub</span></a>
                    </div>
                </div>
            </footer>
        </div>
        '''

    def _category_page_html(
        self,
        category: Dict,
        subcategories: Dict[str, List],
        pages_dict: Dict,
        titles_dict: Dict,
    ) -> str:
        cat_name = category.get("name", "Category")
        cat_icon = category.get("icon", "folder")
        cat_desc = category.get("description", f"Documentation for {cat_name}")

        cards = []
        for sub_name, pages in subcategories.items():
            if not sub_name:
                continue

            preview_pages = pages[:3]
            page_count = len(pages)

            card = f"""
            <div class="subcategory-card" onclick="showSubcategory('{cat_name}', '{sub_name}')">
                <div class="card-header">
                    <div class="card-count">{page_count} document{"s" if page_count != 1 else ""}</div>
                </div>
                <h3 class="card-title">{sub_name}</h3>
                <p class="card-desc">Documentation for {sub_name}</p>
                <div class="card-preview">
                    {"".join(f'<span class="preview-tag">{titles_dict.get(p.get("id", ""), "Untitled")}</span>' for p in preview_pages)}
                    {f'<span class="preview-more">+{page_count - len(preview_pages)} more</span>' if page_count > len(preview_pages) else ""}
                </div>
            </div>
            """
            cards.append(card)

        return f'''
        <div class="category-page" data-category="{cat_name}">
            <div class="category-header">
                <h1 class="category-title">{cat_name}</h1>
                <p class="category-description">{cat_desc}</p>
            </div>
            <div class="subcategories-grid">
                {"".join(cards)}
            </div>
        </div>
        '''

    def _subcategory_page_html(
        self,
        category: str,
        subcategory: str,
        pages: List[Dict],
        titles_dict: Dict,
        pages_dict: Dict,
    ) -> str:
        cards = []
        for page in pages:
            page_id = page.get("id", "")
            title = page.get("title", "Untitled")
            if not title or title == "Untitled":
                title = titles_dict.get(page_id, "Untitled")

            desc = page.get("description", f"Documentation for {title}")
            icon = page.get("icon", "file-text")
            platforms = page.get("platforms", [])

            platform_badges = ""
            for p in platforms[:3]:
                norm = self._normalize_platform(p)
                if norm:
                    platform_badges += f'<span class="platform-tag" style="--tag-color:{norm["color"]}">{norm["name"]}</span>'

            card = f'''
            <div class="doc-card" onclick="showPage(\'{page_id}\', \'{category}\', \'{subcategory}\')">
                <div class="doc-card-icon">
                    <i data-lucide="{icon}"></i>
                </div>
                <div class="doc-card-content">
                    <h4 class="doc-card-title">{title}</h4>
                    <p class="doc-card-desc">{desc}</p>
                    <div class="doc-card-platforms">
                        {platform_badges}
                    </div>
                </div>
                <i data-lucide="chevron-right" class="doc-card-arrow"></i>
            </div>
            '''
            cards.append(card)

        return f'''
        <div class="subcategory-page" data-category="{category}" data-subcategory="{subcategory}">
            <div class="subcategory-header">
                <button class="back-button" onclick="showCategory(\'{category}\')">
                    <i data-lucide="arrow-left"></i> Back to {category}
                </button>
                <h1 class="subcategory-title">{subcategory}</h1>
                <p class="subcategory-description">Documentation for {subcategory}</p>
            </div>
            <div class="documents-grid">
                {"".join(cards)}
            </div>
        </div>
        '''

    def _process_markdown(self, content: str) -> str:
        exts = [
            "extra",
            "codehilite",
            "toc",
            "tables",
            "fenced_code",
            "attr_list",
            "def_list",
            "abbr",
            "footnotes",
            "md_in_html",
        ]
        md = markdown.Markdown(extensions=exts)
        md.registerExtensions([MermaidExtension(), SandboxExtension(), IncenseDemoExtension()], {})
        return md.convert(content)

    def load_markdown(self, path: str) -> str:
        try:
            if not os.path.exists(path):
                return f"<h1>File not found</h1><p>{path}</p>"
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
            if path.lower().endswith(".html"):
                return content
            return self._process_markdown(content)
        except Exception as e:
            return f"<h1>Error loading file</h1><p>{e}</p>"

    def _extract_text_from_html(self, html: str) -> str:
        soup = BeautifulSoup(html, "html.parser")
        for script in soup(["script", "style"]):
            script.decompose()
        return soup.get_text()

    def _build_pdf_template(self) -> str:
        return """<!DOCTYPE html><html><head><meta charset="UTF-8"><title>{{ title }}</title>
<style>
@page {
  size: A4;
  margin: 2.5cm 2cm;
  @top-center {
    content: "{{ title }}";
    font-family: 'Google Sans', 'Helvetica Neue', sans-serif;
    font-size: 9pt; color: #5C5C5C;
  }
  @bottom-center {
    content: "Page " counter(page) " of " counter(pages);
    font-family: 'Google Sans', 'Helvetica Neue', sans-serif;
    font-size: 9pt; color: #5C5C5C;
  }
}
body {
  font-family: 'Google Sans', 'Helvetica Neue', sans-serif;
  line-height: 1.7; color: #1C1B1F; font-size: 11pt;
}
h1 { font-size: 28pt; font-weight: 400; margin-top: 0; page-break-after: avoid; letter-spacing: -0.01em; }
h2 { font-size: 18pt; font-weight: 500; margin-top: 32pt; page-break-after: avoid; letter-spacing: -0.01em; }
h3 { font-size: 14pt; font-weight: 500; margin-top: 24pt; page-break-after: avoid; }
h4 { font-size: 12pt; font-weight: 500; margin-top: 18pt; color: #5C5C5C; page-break-after: avoid; }
p  { margin: 0 0 12pt; }
a  { color: #0468D7; text-decoration: none; border-bottom: 1pt solid #A6C8FF; }
pre, code {
  font-family: 'Roboto Mono', 'Menlo', monospace;
  background: #F8F9FA; border-radius: 4pt; font-size: 9.5pt;
}
pre {
  padding: 12pt 14pt; border: 1pt solid #DADCE0;
  page-break-inside: avoid; margin: 16pt 0;
}
pre code { background: none; border: none; padding: 0; }
code { padding: 2pt 5pt; border: 1pt solid #DADCE0; }
blockquote {
  margin: 16pt 0; padding: 12pt 16pt;
  border-left: 3pt solid #0468D7;
  background: #F2F8FF;
  border-radius: 0 4pt 4pt 0;
}
blockquote p { color: #1C1B1F; margin: 0; font-style: normal; }
table {
  width: 100%; border-collapse: collapse;
  margin: 18pt 0; page-break-inside: avoid;
  border: 1pt solid #DADCE0; border-radius: 8pt;
  font-size: 10.5pt;
}
th {
  padding: 10pt 12pt; background: #F8F9FA;
  font-weight: 500; text-align: left;
  border-bottom: 1pt solid #DADCE0;
}
td { padding: 8pt 12pt; border-bottom: 1pt solid #DADCE0; }
tr:last-child td { border-bottom: none; }
ul, ol { margin: 0 0 12pt 20pt; }
li     { margin: 5pt 0; }
img {
  max-width: 100%; border-radius: 8pt;
  border: 1pt solid #DADCE0; page-break-inside: avoid;
}
hr { border: none; border-top: 1pt solid #DADCE0; margin: 28pt 0; }
.mermaid-pdf {
  text-align: center;
  margin: 18pt 0;
  padding: 16pt;
  background: #F8F9FA;
  border: 1pt solid #DADCE0;
  border-radius: 8pt;
  page-break-inside: avoid;
}
.mermaid-pdf svg { max-width: 100%; height: auto; display: block; margin: 0 auto; }
.mermaid-fallback {
  margin: 18pt 0;
  border: 1pt solid #DADCE0;
  border-radius: 8pt;
  overflow: hidden;
  page-break-inside: avoid;
}
.mermaid-fallback-label {
  background: #F8F9FA;
  padding: 8pt 12pt;
  font-size: 10pt;
  font-weight: 500;
  color: #5C5C5C;
  margin: 0;
  border-bottom: 1pt solid #DADCE0;
}
.mermaid-fallback pre {
  margin: 0; border: none; border-radius: 0;
  background: #fff; padding: 12pt 14pt;
  font-size: 9pt; color: #1C1B1F;
}
.cover { text-align: center; margin-top: 100pt; page-break-after: always; }
.cover-title { font-size: 42pt; font-weight: 400; margin-bottom: 16pt; letter-spacing: -0.02em; }
.cover-sub { font-size: 18pt; color: #5C5C5C; font-weight: 400; margin-bottom: 40pt; }
.cover-meta { font-size: 11pt; color: #9AA0A6; margin-top: 56pt; line-height: 1.8; }
.cover-line { width: 48pt; height: 3pt; background: #0468D7; margin: 28pt auto; border-radius: 2pt; }
.toc-page { page-break-after: always; }
.toc-page h1 { border-bottom: 1pt solid #DADCE0; padding-bottom: 12pt; margin-bottom: 20pt; font-weight: 400; }
.toc-entry { display: flex; align-items: baseline; margin: 8pt 0; font-size: 11pt; }
.toc-title { flex: 1; font-weight: 400; }
.toc-dots { flex: 2; border-bottom: 1pt dotted #A6C8FF; margin: 0 10pt; height: 0.7em; }
.toc-page-num { color: #5C5C5C; font-size: 10pt; }
.section-break { page-break-before: always; }
.section-title-rule { border-bottom: 1pt solid #DADCE0; margin-bottom: 24pt; padding-bottom: 12pt; }
.footer { margin-top: 48pt; padding-top: 20pt; border-top: 1pt solid #DADCE0; font-size: 9pt; color: #9AA0A6; text-align: center; }
</style>
</head>
<body>
<div class="cover">
  <div class="cover-title">{{ title }}</div>
  <div class="cover-line"></div>
  <div class="cover-sub">{{ subtitle }}</div>
  <div class="cover-meta">
    Version {{ version }}<br>
    Generated {{ date }}<br>
    © {{ year }}{% if company %} {{ company }}{% endif %}
  </div>
</div>
<div class="toc-page">
  <h1>Contents</h1>
  {% for s in toc %}
  <div class="toc-entry">
    <span class="toc-title">{{ s.title }}</span>
    <span class="toc-dots"></span>
    <span class="toc-page-num">{{ s.page }}</span>
  </div>
  {% endfor %}
</div>
{% for s in sections %}
<div class="section {% if not loop.first %}section-break{% endif %}">
  <h1 class="section-title-rule">{{ s.title }}</h1>
  {{ s.content }}
</div>
{% endfor %}
<div class="footer"></div>
</body></html>"""

    def _build_template(self) -> str:
        return r"""<!DOCTYPE html>
<html lang="en" data-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{project_name} Documentation</title>
<meta name="description" content="{description}">
<link rel="icon" type="image/svg+xml" href="favicon.svg">
<style>
:root{{
  --md-primary: #4F8EF7;
  --md-on-primary: #FFFFFF;
  --md-primary-container: #EAF2FB;
  --md-on-primary-cont: #0B60EF;
  --md-secondary: #697177;
  --md-on-secondary: #FFFFFF;
  --md-secondary-cont: #F5F5F7;
  --md-on-secondary-cont: #333333;
  --md-background: #FFFFFF;
  --md-surface: #FFFFFF;
  --md-surface-variant: #F7F7F7;
  --md-on-surface: #333333;
  --md-on-surface-var: #697177;
  --md-on-surface-3: #999999;
  --md-outline: #DDDDDD;
  --md-outline-variant: #EEEEEE;
  --md-surf-1: #F7F7F7;
  --md-surf-2: #F0F3F8;
  --md-surf-3: #E8EBF0;
  --md-surf-4: #D7DBE4;
  --md-state-hover: rgba(0, 0, 0, 0.04);
  --md-state-focus: rgba(0, 102, 204, 0.12);
  --md-state-pressed: rgba(0, 0, 0, 0.08);
  --md-elev-1: 0 1px 2px rgba(0,0,0,0.08);
  --md-elev-2: 0 2px 8px rgba(0,0,0,0.10);
  --md-elev-3: 0 4px 16px rgba(0,0,0,0.12);
  --nav-drawer-w: 280px;
  --top-bar-h: 56px;
  --toc-w: 240px;
  --content-max: 720px;
  --fb: "Helvetica Neue", Helvetica, Arial, "Lucida Grande", sans-serif;
  --fd: "Helvetica Neue", Helvetica, Arial, "Lucida Grande", sans-serif;
  --fm: Monaco, Menlo, Consolas, "Courier New", monospace;
  --radius-sm: 4px;
  --radius-md: 6px;
  --radius-lg: 8px;
}}

[data-theme="dark"]{{
  --md-primary: #6EA8FE;
  --md-on-primary: #06233F;
  --md-primary-container: #14365E;
  --md-on-primary-cont: #B9D4FD;
  --md-secondary: #9AA0A6;
  --md-on-secondary: #1D1D1F;
  --md-secondary-cont: #2C2C2E;
  --md-on-secondary-cont: #E5E5EA;
  --md-background: #1D1D1F;
  --md-surface: #1D1D1F;
  --md-surface-variant: #262628;
  --md-on-surface: #E8EAED;
  --md-on-surface-var: #9AA0A6;
  --md-on-surface-3: #6E6E73;
  --md-outline: #48484A;
  --md-outline-variant: #2E2E30;
  --md-surf-1: #242426;
  --md-surf-2: #2C2C2E;
  --md-surf-3: #333336;
  --md-surf-4: #3A3A3C;
  --md-state-hover: rgba(255, 255, 255, 0.06);
  --md-state-focus: rgba(110, 168, 254, 0.18);
  --md-state-pressed: rgba(255, 255, 255, 0.10);
}}
[data-theme="dark"] .nav-dest{{color:#C7C7CC;}}
[data-theme="dark"] .nav-section-header{{color:#8E8E93;}}
[data-theme="dark"] .md code{{color:#8FBDF9;background-color:#2A2D33;}}
[data-theme="dark"] .md pre,[data-theme="dark"] .codehilite{{
  background-color:#232629!important;
  border-color:#3A3D42!important;
}}
[data-theme="dark"] .copy-btn{{background:#2C2C2E;border-color:#48484A;color:#E5E5EA;}}
[data-theme="dark"] .md th{{background-color:#26262A;}}
[data-theme="dark"] .md th,[data-theme="dark"] .md td{{border-color:#3A3A3C;}}
[data-theme="dark"] .md blockquote{{border-color:#3A3A3C;}}
[data-theme="dark"] .doc-back-btn,[data-theme="dark"] .dbtn{{
  background:transparent;border-color:#48484A;color:#E5E5EA;
}}
[data-theme="dark"] .doc-back-btn:hover,[data-theme="dark"] .dbtn:hover{{background:#2C2C2E;}}
[data-theme="dark"] .doc-pn-link{{border-color:#3A3A3C;}}

*,*::before,*::after{{box-sizing:border-box;margin:0;padding:0}}
html{{font-size:16px;-webkit-text-size-adjust:100%;scroll-behavior:smooth}}
::selection{{background:#B6D1FF;}}
:focus-visible{{outline:2px solid var(--md-primary);outline-offset:2px;border-radius:4px}}
body{{
  font-family:var(--fb);
  background:var(--md-background);
  color:var(--md-on-surface);
  height:100vh;overflow:hidden;
  -webkit-font-smoothing:antialiased;
  transition:background 200ms, color 200ms;
  font-weight:400;
  font-size:15px;
  line-height:1.42857;
}}

.top-app-bar{{
  position:fixed;top:0;left:0;right:0;
  height:var(--top-bar-h);
  background:#296ad4;
  background:linear-gradient(to right,#296ad4 0%,#4e8ef7 100%);
  display:flex;align-items:center;
  padding:0 16px;
  z-index:200;
  box-shadow:0 1px 0 rgba(0,0,0,0.08),0 4px 14px rgba(41,106,212,0.22);
}}
.top-app-bar.scrolled{{}}

.tab-logo-name{{
  font-family:var(--fd);
  font-size:19px;
  font-weight:500;
  color:#FFFFFF;
  letter-spacing:0;
  white-space:nowrap;
  cursor:pointer;
  margin-right:24px;
}}
.ionic-nav{{
  display:flex;align-items:center;gap:2px;
}}
.ionic-nav a{{
  color:#FFFFFF;
  opacity:.85;
  font-size:14px;
  text-decoration:none;
  padding:8px 14px;
  white-space:nowrap;
  cursor:pointer;
}}
.ionic-nav a:hover{{opacity:1;text-decoration:underline}}
.ionic-nav a i{{display:none}}

.top-bar-center{{
  flex:1;
  display:flex;
  align-items:center;
  justify-content:center;
  padding:0 24px;
  max-width:600px;
  margin:0 auto;
}}

.search-bar{{
  width:100%;
  max-width:420px;
  height:34px;
  border-radius:20px;
  background:rgba(255,255,255,0.3);
  border:none;
  display:flex;
  align-items:center;
  gap:8px;
  padding:0 8px 0 12px;
  transition:background 150ms;
  position:relative;
  z-index:210;
}}
.search-bar:hover{{
  background:rgba(255,255,255,0.4);
}}
.search-bar.open,.search-bar:focus-within{{
  background:#FFFFFF;
  border-radius:20px 20px 0 0;
  box-shadow:0 6px 18px rgba(20,40,80,0.25);
}}
.search-bar svg{{color:#FFFFFF;}}
.search-bar.open svg,.search-bar:focus-within svg{{color:#296AD4;}}
.search-bar-input{{color:#FFFFFF;}}
.search-bar-input::placeholder{{color:#FFFFFF;opacity:1;}}
.search-bar-input::-webkit-input-placeholder{{color:#FFFFFF;opacity:1;}}
.search-bar-input::-moz-placeholder{{color:#FFFFFF;opacity:1;}}
.search-bar-input:-ms-input-placeholder{{color:#FFFFFF;opacity:1;}}
.search-bar.open .search-bar-input,.search-bar:focus-within .search-bar-input{{color:#296AD4;}}
.search-bar.open .search-bar-input::placeholder,.search-bar:focus-within .search-bar-input::placeholder{{color:#697177;opacity:1;}}
.search-bar svg{{
  width:18px;height:18px;
  flex-shrink:0;
}}
.search-bar-input{{
  flex:1;min-width:0;
  height:100%;
  background:none;border:none;outline:none;
  font-family:var(--fb);font-size:14px;font-weight:400;
}}
.search-bar-kbd{{
  display:flex;gap:4px;align-items:center;
  font-size:11px;color:var(--md-on-surface-3);
  flex-shrink:0;
}}
.search-bar.open .search-bar-kbd{{display:none}}
.search-bar-kbd kbd{{
  padding:2px 6px;
  border-radius:4px;
  border:1px solid var(--md-outline-variant);
  background:var(--md-surf-2);
  font-size:10px;
  font-family:var(--fm);
}}
.search-bar-clear{{
  display:none;align-items:center;justify-content:center;
  width:28px;height:28px;border-radius:14px;flex-shrink:0;
  border:none;background:transparent;color:var(--md-on-surface-var);
  cursor:pointer;position:relative;overflow:hidden;
}}
.search-bar-clear::before{{
  content:'';position:absolute;inset:0;border-radius:inherit;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.search-bar-clear:hover::before{{opacity:.08}}
.search-bar-clear i{{width:16px;height:16px;position:relative;z-index:1}}
.search-bar.open .search-bar-clear{{display:flex}}

.search-dropdown{{
  display:none;position:absolute;top:100%;left:0;right:0;
  background:var(--md-surf-3);
  border-radius:0 0 16px 16px;
  overflow:hidden;
  box-shadow:var(--md-elev-2);
  font-family:var(--fb);
}}
.search-bar.open .search-dropdown{{display:block}}
.search-results{{max-height:420px;overflow-y:auto}}
.search-result-item{{
  display:flex;align-items:center;gap:12px;
  padding:14px 20px;cursor:pointer;
  border-top:1px solid var(--md-outline-variant);
  transition:background 150ms;position:relative;overflow:hidden;
}}
.search-result-item::before{{
  content:'';position:absolute;inset:0;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.search-result-item:hover::before,.search-result-item.selected::before{{opacity:.04}}
.search-result-icon{{
  width:36px;height:36px;border-radius:8px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;
  color:var(--md-on-primary-cont);flex-shrink:0;position:relative;z-index:1;
}}
.search-result-icon i{{width:18px;height:18px}}
.search-result-title{{font-size:15px;font-weight:500;color:var(--md-on-surface);position:relative;z-index:1}}
.search-result-path{{font-size:12px;color:var(--md-on-surface-var);margin-top:2px;position:relative;z-index:1}}
.search-dropdown-footer{{
  display:flex;gap:20px;padding:10px 20px;
  border-top:1px solid var(--md-outline-variant);
  background:var(--md-surf-4);
  font-size:11px;color:var(--md-on-surface-3);
}}
.search-dropdown-footer kbd{{
  padding:2px 6px;border-radius:4px;
  border:1px solid var(--md-outline-variant);font-size:10px;
}}

.top-bar-trailing{{
  display:flex;align-items:center;gap:8px;
  padding:0 4px;flex-shrink:0;
}}

.m3-icon-btn{{
  width:36px;height:36px;
  border-radius:4px;
  border:none;background:transparent;
  color:#FFFFFF;
  opacity:.85;
  cursor:pointer;
  display:flex;align-items:center;justify-content:center;
}}
.m3-icon-btn:hover{{opacity:1;background:rgba(255,255,255,0.12);}}
.m3-icon-btn i{{width:18px;height:18px;}}
.theme-toggle{{
  display:flex;align-items:center;
  background:rgba(255,255,255,0.18);
  border-radius:20px;
  padding:2px;
  gap:2px;
  flex-shrink:0;
}}
.theme-toggle button{{
  width:30px;height:30px;
  border-radius:15px;
  border:none;background:transparent;
  color:#FFFFFF;
  opacity:.7;
  cursor:pointer;
  display:flex;align-items:center;justify-content:center;
}}
.theme-toggle button:hover{{opacity:1;}}
.theme-toggle button.active{{
  background:#FFFFFF;
  color:#296AD4;
  opacity:1;
}}
.theme-toggle button i{{width:15px;height:15px;}}



.layout{{
  display:flex;
  height:100vh;
  padding-top:var(--top-bar-h);
  overflow:hidden;
}}
body.has-announce .top-app-bar{{top:36px}}
body.has-announce .layout{{padding-top:calc(var(--top-bar-h) + 36px)}}
.announce-bar{{
  position:fixed;top:0;left:0;right:0;height:36px;z-index:300;
  display:none;align-items:center;justify-content:center;gap:8px;
  padding:0 44px 0 16px;
  background:linear-gradient(to right,#0B2A5B 0%,#123A7D 100%);
  color:#FFFFFF;font-size:13px;white-space:nowrap;
  box-shadow:0 1px 0 rgba(0,0,0,0.08),0 4px 14px rgba(11,42,91,0.25);
}}
body.has-announce .announce-bar{{display:flex}}
.announce-bar span{{overflow:hidden;text-overflow:ellipsis}}
.announce-bar a{{color:#B9D4FD;text-decoration:none;font-weight:500}}
.announce-bar a:hover{{color:#FFFFFF;text-decoration:underline}}
.announce-close{{
  position:absolute;right:8px;top:50%;transform:translateY(-50%);
  width:28px;height:28px;border-radius:14px;
  border:none;background:transparent;color:#FFFFFF;opacity:.75;
  cursor:pointer;font-size:16px;line-height:1;
  display:flex;align-items:center;justify-content:center;
}}
.announce-close:hover{{opacity:1;background:rgba(255,255,255,0.12)}}

.nav-drawer{{
  width:var(--nav-drawer-w);
  flex-shrink:0;
  background:var(--md-surf-1);
  display:flex;flex-direction:column;
  overflow:hidden;
  border-right:1px solid var(--md-outline-variant);
  transition:transform 300ms cubic-bezier(0.2,0,0,1),background 200ms;
}}

.nav-drawer-content{{
  flex:1;overflow-y:auto;
  padding:8px 0 24px 20px;
  scrollbar-width:thin;
  scrollbar-color:var(--md-outline-variant) transparent;
}}
.nav-drawer-content::-webkit-scrollbar{{width:4px}}
.nav-drawer-content::-webkit-scrollbar-thumb{{
  background:var(--md-outline-variant);border-radius:4px;
}}

.nav-section-header{{
  padding:0;
  font-size:11px;
  font-weight:400;
  letter-spacing:2px;
  text-transform:uppercase;
  color:#AAAAAA;
  display:flex;align-items:center;gap:6px;
  cursor:pointer;user-select:none;
  margin:8px 0;
  line-height:20px;
}}
.nav-section-header:hover{{color:var(--md-on-surface-var)}}
.nav-sec-chev{{
  margin-left:auto;
  width:12px;height:12px;
  color:var(--md-on-surface-3);
  transition:transform 200ms cubic-bezier(0.2,0,0,1);
  flex-shrink:0;
}}
.nav-sec-chev.c{{transform:rotate(-90deg)}}
.sec-items.c{{display:none}}

.nav-dest{{
  display:flex;align-items:center;
  height:auto;
  font-size:12px;
  font-weight:400;
  color:#444444;
  cursor:pointer;
  position:relative;
  text-decoration:none;
  border-radius:0;
  margin:8px 0;
  padding:1px 0;
  border-right:2px solid transparent;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
.nav-dest:hover{{background:transparent;color:var(--md-on-surface);}}
.nav-dest.active{{
  color:var(--md-primary);
  background:rgba(79,142,247,0.08);
  font-weight:400;
  border-right:2px solid var(--md-primary);
}}
.nav-dest.active::before{{content:none}}
.nav-dest::before{{content:none}}
.nav-dest i{{display:none}}
.nav-dest.ph{{display:none!important}}
.nav-dest-text{{position:relative;z-index:1;}}

.nav-sub-header{{
  display:flex;align-items:center;
  padding:6px 0 2px 0;
  height:30px;
  font-size:12px;
  font-weight:400;
  color:var(--md-on-surface-var);
  cursor:pointer;user-select:none;
  gap:4px;
}}
.nav-sub-header:hover{{color:var(--md-on-surface)}}
.nav-sub-chev{{
  margin-left:auto;width:12px;height:12px;
  color:var(--md-on-surface-3);
  transition:transform 200ms cubic-bezier(0.2,0,0,1);
  flex-shrink:0;
}}
.nav-sub-chev.c{{transform:rotate(-90deg)}}
.sub-items.c{{display:none}}
.nav-dest.sub{{padding-left:12px;height:auto;font-size:12px}}

.nav-all{{
  display:block;padding:6px 0;
  font-size:12px;
  color:var(--md-primary);
  cursor:pointer;
  font-weight:400;
}}
.nav-all:hover{{text-decoration:underline}}
.nav-all.sub{{padding-left:12px}}

.main{{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0}}

.nav-crumbs{{
  display:flex;align-items:center;
  gap:8px;flex-shrink:1;min-width:0;
  margin-left:8px;
  font-size:13px;
  white-space:nowrap;overflow:hidden;
}}
.nav-crumbs .bc-seg{{
  color:rgba(255,255,255,.82);cursor:pointer;font-weight:400;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
.nav-crumbs .bc-seg:hover{{color:#FFFFFF;text-decoration:underline;}}
.nav-crumbs .bc-seg.cur{{color:#FFFFFF;cursor:default;font-weight:600}}
.nav-crumbs .bc-sep{{color:rgba(255,255,255,.6);font-size:.75rem}}
@media(max-width:1180px){{.nav-crumbs{{display:none}}}}

.c-layout{{flex:1;display:flex;overflow:hidden}}
.c-scroll{{
  flex:1;overflow-y:auto;
  scrollbar-width:thin;
  scrollbar-color:var(--md-outline-variant) transparent;
}}
.c-scroll::-webkit-scrollbar{{width:6px}}
.c-scroll::-webkit-scrollbar-thumb{{
  background:var(--md-outline-variant);border-radius:6px;
}}
.c-inner{{
  max-width:var(--content-max);
  margin:0 auto;
  padding:48px 48px 96px;
  animation:fl-fade-up 200ms cubic-bezier(0,0,0,1);
}}
.c-inner.wide{{
  max-width:none;
  padding:0 0 96px;
}}
.c-inner.wide .sandbox-spotlight{{
  padding:0 48px;
}}
@keyframes fl-fade-up{{
  from{{opacity:0;transform:translateY(8px)}}
  to{{opacity:1;transform:translateY(0)}}
}}

.toc-panel{{
  width:var(--toc-w);flex-shrink:0;
  background:var(--md-background);
  border-left:1px solid var(--md-outline-variant);
  overflow-y:auto;padding:24px 0 24px;
  display:none;
}}
.toc-panel.vis{{display:block}}
.toc-label{{
  padding:0 20px 12px;
  font-size:13px;
  font-weight:700;
  color:var(--md-on-surface);
}}
.toc-progress{{
  margin:0 20px 16px;
  height:2px;border-radius:2px;
  background:var(--md-outline-variant);
  overflow:hidden;
}}
.toc-fill{{
  height:100%;
  background:var(--md-primary);
  border-radius:2px;
  width:0%;transition:width .12s linear;
}}
.toc-item{{
  display:block;
  padding:6px 20px;
  font-size:13px;
  color:var(--md-on-surface-var);
  cursor:pointer;
  border-left:2px solid transparent;
  line-height:1.5;
  font-weight:400;
}}
.toc-item:hover{{color:var(--md-on-surface);background:var(--md-state-hover)}}
.toc-item.active{{
  color:var(--md-primary);
  border-left-color:var(--md-primary);
  font-weight:600;
}}

.section-heading{{
  font-family:var(--fd);
  font-size:20px;
  font-weight:600;color:var(--md-on-surface);
  margin-bottom:16px;letter-spacing:0;line-height:1.3;
}}
.metro-tiles{{
  display:flex;
  flex-wrap:wrap;
  justify-content:center;
  gap:8px;
  margin:0 auto 48px;
  max-width:780px;
}}
.metro-tile{{
  position:relative;
  width:180px;height:180px;flex:0 0 auto;
  border-radius:0;
  cursor:pointer;
  overflow:hidden;
  padding:14px 12px;
  background:#FFFFFF;
  border:1px solid #DDDDDD;
}}
.metro-tile:hover{{border-color:var(--md-primary);}}
.metro-icon{{
  position:absolute;top:14px;left:12px;
  width:26px;height:26px;
  color:var(--md-primary);
}}
.metro-label{{
  position:absolute;left:12px;bottom:34px;right:12px;
  color:var(--md-primary);
  font-size:16px;font-weight:600;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
.metro-desc{{
  position:absolute;left:12px;bottom:10px;right:12px;
  color:var(--md-on-surface-var);
  font-size:12px;font-weight:400;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
[data-theme="dark"] .metro-tile{{background:#242426;border-color:#38383A;}}
[data-theme="dark"] .metro-tile:hover{{border-color:#6EA8FE;}}
[data-theme="dark"] .metro-label,[data-theme="dark"] .metro-icon{{color:#8FBDF9;}}
[data-theme="dark"] .metro-desc{{color:#9AA0A6;}}
@media(max-width:760px){{.metro-tile{{width:44vw;height:44vw;max-width:200px;max-height:200px;}}}}
.sandbox-spotlight{{margin-bottom:64px}}
.home-footer{{margin:64px 0 0;border-top:1px solid var(--md-outline-variant);background:var(--md-surf-1);}}
.home-footer-inner{{max-width:960px;margin:0 auto;padding:40px 48px 48px;text-align:center;}}
.home-footer h2{{font-family:var(--fd);font-size:22px;font-weight:500;color:var(--md-on-surface);margin:0 0 10px;line-height:1.3;}}
.home-footer p{{font-size:14px;line-height:1.6;color:var(--md-on-surface-var);margin:0 auto 20px;max-width:640px;}}
.home-footer-actions{{display:flex;gap:12px;justify-content:center;flex-wrap:wrap;}}
.home-footer-actions a.m3-btn-filled,.home-footer-actions a.m3-btn-outlined{{text-decoration:none;}}
.sandbox-head{{
  display:flex;align-items:center;justify-content:space-between;
  gap:12px;flex-wrap:wrap;margin-bottom:12px;
}}
.sandbox-open{{
  font-size:13px;font-weight:400;color:var(--md-primary);
  text-decoration:none;white-space:nowrap;
}}
.sandbox-open:hover{{text-decoration:underline}}
.sandbox-sub{{
  color:var(--md-on-surface-var);margin:0 0 16px;font-size:14px;line-height:1.6;
  max-width:720px;
}}
.sandbox-frame{{
  border:1px solid var(--md-outline);border-radius:var(--radius-sm);overflow:hidden;
  background:var(--md-surface);
}}
.sandbox-frame iframe{{width:100%;height:800px;border:none;display:block;background:var(--md-surf-1);}}
.demo-frame{{border:1px solid var(--md-outline);border-radius:var(--radius-sm);overflow:hidden;background:var(--md-surface);margin:0 0 21px;}}
.demo-head{{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 14px;border-bottom:1px solid var(--md-outline-variant);font-size:13px;color:var(--md-on-surface-var);}}
.demo-head a{{font-size:13px;color:var(--md-primary);text-decoration:none;white-space:nowrap;}}
.demo-head a:hover{{text-decoration:underline}}
.demo-frame iframe{{width:100%;height:660px;border:none;display:block;background:var(--md-surf-1);}}
@media(max-width:760px){{.sandbox-frame iframe{{height:640px}}.hero-inner{{padding:52px 20px 44px;}}.home-title{{font-size:30px;}}}}

.welcome-page{{padding:8px 0 16px;}}
.home-hero{{
  margin:0 0 40px;
  padding:0;
  background:#FFFFFF;
  color:#333333;
  border-bottom:1px solid #EEEEEE;
}}
[data-theme="dark"] .home-hero{{background:#1D1D1F;border-bottom-color:#2E2E30;color:#E8EAED;}}
[data-theme="dark"] .home-title{{color:#E8EAED;}}
[data-theme="dark"] .home-tagline{{color:#9AA0A6;}}
[data-theme="dark"] .home-hero .m3-btn-outlined{{background:transparent;color:#E8EAED;border-color:#48484A;}}
[data-theme="dark"] .home-hero .m3-btn-outlined:hover{{background:#2C2C2E;}}
[data-theme="dark"] .hero-release{{border-top-color:#2E2E30;}}
[data-theme="dark"] .hero-dl-meta{{color:#9AA0A6;}}
.hero-inner{{
  margin:0 auto;
  max-width:960px;
  padding:52px 48px 44px;
  display:flex;
  align-items:center;
  gap:56px;
}}
.hero-copy{{
  flex:1 1 auto;
  min-width:0;
}}
.hero-graphic{{
  flex:0 0 340px;
  max-width:340px;
}}
.hero-graphic svg{{display:block;width:100%;height:auto;}}
@media(max-width:900px){{.hero-graphic{{display:none;}}.hero-inner{{gap:0;}}}}
.home-title{{
  font-family:var(--fd);
  font-size:36px;
  font-weight:500;color:#333333;
  line-height:1.2;margin:0 0 10px;
}}
.home-tagline{{
  font-size:15px;line-height:1.5;color:#697177;
  max-width:640px;margin:0 0 22px;
}}
.home-hero-actions{{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:0}}
.home-hero .m3-btn-filled,.home-hero .m3-btn-outlined{{
  height:36px;padding:0 20px;
  border-radius:2px;
  font-size:14px;font-weight:400;
}}
.home-hero .m3-btn-filled{{
  background:#4F8EF7;color:#FFFFFF;border:1px solid transparent;
}}
.home-hero .m3-btn-filled:hover{{
  background:#2875F5;
}}
.home-hero .m3-btn-outlined{{
  background:#FFFFFF;color:#333333;
  border:1px solid #CCCCCC;
}}
.home-hero .m3-btn-outlined:hover{{background:#EBEBEB;}}
.hero-release{{
  display:flex;align-items:center;gap:14px;flex-wrap:wrap;
  margin-top:24px;
  padding-top:20px;
  border-top:1px solid #EEEEEE;
  max-width:640px;
}}
.hero-dl-btn{{
  display:inline-block;
  padding:8px 18px;
  background:#4F8EF7;color:#FFFFFF;
  border-radius:2px;
  font-size:14px;font-weight:400;
  text-decoration:none;white-space:nowrap;
}}
.hero-dl-btn:hover{{background:#2875F5;text-decoration:none;color:#FFFFFF;}}
.hero-dl-meta{{
  font-size:13px;
  color:#697177;
}}
.m3-btn-filled,.m3-btn-outlined{{
  display:inline-block;
  padding:6px 14px;
  font-size:15px;font-weight:400;line-height:1.42857;
  text-align:center;vertical-align:middle;white-space:nowrap;
  border-radius:2px;
  border:1px solid transparent;
  cursor:pointer;
  transition:background-color .1s ease-in-out;
}}
.m3-btn-filled{{
  color:#FFFFFF;background-color:var(--md-primary);border-color:var(--md-primary);
}}
.m3-btn-filled:hover{{background-color:#2875f5;border-color:#2875f5;box-shadow:0 2px 6px rgba(40,117,245,0.35);}}
.m3-btn-outlined{{
  color:var(--md-on-surface);
  background-color:#FFFFFF;border-color:var(--md-outline);
}}
.m3-btn-outlined:hover{{background-color:#EBEBEB;}}
.m3-btn-filled i,.m3-btn-outlined i{{display:none}}
.m3-btn-filled span,.m3-btn-outlined span{{position:relative;z-index:1}}

.home-section{{margin-bottom:48px}}

.category-page{{padding:8px 0;}}
.category-header{{margin-bottom:24px}}
.category-title{{
  font-family:var(--fd);
  font-size:32px;
  font-weight:500;color:var(--md-on-surface);
  margin:21px 0 10.5px;line-height:1.1;
}}
.category-description{{font-size:14px;color:var(--md-on-surface-var);line-height:1.6;max-width:68ch}}
.subcategories-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(300px,1fr));
  gap:12px;margin-top:20px;
}}
.subcategory-card{{
  background:var(--md-surface);
  border:none;
  border-radius:0;
  padding:0 0 21px;cursor:pointer;
  position:relative;overflow:hidden;
}}
.subcategory-card:hover .card-title{{text-decoration:underline;}}
.card-header{{display:flex;align-items:center;justify-content:flex-end;margin-bottom:6px}}
.card-count{{
  font-size:12px;font-weight:400;
  color:var(--md-on-surface-3);
  padding:0;
  position:relative;z-index:1;
}}
.card-title{{font-size:22px;font-weight:500;color:var(--md-primary);margin:21px 0 10.5px;position:relative;z-index:1;}}
.subcategory-card:hover .card-title{{text-decoration:underline}}
.card-desc{{font-size:13px;color:var(--md-on-surface-var);margin-bottom:10px;line-height:1.5;position:relative;z-index:1;}}
.card-preview{{display:flex;flex-wrap:wrap;gap:6px;position:relative;z-index:1}}
.preview-tag{{
  font-size:14px;padding:0;
  background:transparent;border:none;
  border-radius:0;color:var(--md-primary);
}}
.preview-more{{font-size:14px;color:var(--md-on-surface-3);padding:0}}

.subcategory-page{{padding:8px 0;animation:fl-fade-up 200ms cubic-bezier(0,0,0,1)}}
.subcategory-header{{margin-bottom:32px}}
.back-button{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 14px;
  border-radius:var(--radius-sm);
  background:transparent;
  border:1px solid var(--md-outline-variant);
  font-size:13px;font-weight:400;
  color:var(--md-on-surface-var);cursor:pointer;margin-bottom:20px;
}}
.back-button:hover{{
  border-color:var(--md-outline);
  color:var(--md-on-surface);
}}
.back-button i{{width:15px;height:15px}}
.subcategory-title{{font-family:var(--fd);font-size:24px;font-weight:700;color:var(--md-on-surface);margin-bottom:8px;letter-spacing:0;line-height:1.25;}}
.subcategory-description{{font-size:14px;color:var(--md-on-surface-var);line-height:1.6;max-width:68ch}}
.documents-grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:12px}}
.doc-card{{
  display:flex;align-items:center;gap:14px;
  padding:10px 0;
  background:transparent;
  border:none;
  border-radius:0;cursor:pointer;
}}
.doc-card:hover .doc-card-title{{text-decoration:underline;}}
.doc-card-icon{{
  width:40px;height:40px;border-radius:8px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;flex-shrink:0;
}}
.doc-card-icon i{{width:20px;height:20px;color:var(--md-on-primary-cont)}}
.doc-card-content{{flex:1;min-width:0}}
.doc-card-title{{font-size:19px;font-weight:500;color:var(--md-primary);margin-bottom:4px}}
.doc-card-desc{{font-size:14px;color:var(--md-on-surface-var);line-height:1.4}}
.doc-card-platforms{{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}}
.platform-tag{{
  font-size:11px;padding:2px 8px;
  background:color-mix(in srgb,var(--tag-color) 8%,transparent);
  border:1px solid color-mix(in srgb,var(--tag-color) 20%,transparent);
  border-radius:12px;color:var(--tag-color);font-weight:500;
  text-transform:uppercase;letter-spacing:.02em;
}}
.doc-card-arrow{{
  flex-shrink:0;color:var(--md-on-surface-3);width:20px;height:20px;
  transition:transform 150ms,color 150ms;
}}
.doc-card:hover .doc-card-arrow{{transform:translateX(4px);color:var(--md-primary)}}

.platform-badges{{display:flex;gap:8px;flex-wrap:wrap;}}
.platform-badge{{
  display:inline-flex;align-items:center;padding:4px 12px;
  border-radius:16px;
  background:color-mix(in srgb,var(--badge-color) 8%,transparent);
  border:1px solid color-mix(in srgb,var(--badge-color) 20%,transparent);
  font-size:12px;font-weight:500;
  color:var(--badge-color);text-transform:uppercase;letter-spacing:.02em;
}}

.doc-nav-buttons{{margin-bottom:20px}}
.doc-back-btn{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 14px 0 10px;
  border-radius:2px;
  background:#FFFFFF;border:1px solid #CCCCCC;
  font-size:14px;font-weight:400;
  color:#333333;cursor:pointer;
}}
.doc-back-btn:hover{{
  background:#EBEBEB;
}}
.doc-back-btn i{{width:15px;height:15px}}

.doc-hdr{{
  margin-bottom:21px;
  padding-bottom:0;
  border-bottom:none;
}}
.doc-title-row{{display:block;margin-bottom:12px}}
.doc-title{{
  font-family:var(--fd);
  font-size:32px;
  font-weight:500;color:var(--md-on-surface);
  letter-spacing:0;line-height:1.1;
  margin:21px 0 10.5px;
}}
.doc-sub{{
  color:#888888;font-size:15px;margin:0;
}}
.doc-meta{{display:flex;align-items:center;gap:8px;margin-bottom:14px;flex-wrap:wrap}}
.doc-acts{{display:flex;gap:8px;margin-top:12px}}
.dbtn{{
  display:inline-flex;align-items:center;gap:6px;
  height:32px;padding:0 12px;
  border-radius:2px;background:transparent;
  border:1px solid #CCCCCC;
  font-family:var(--fb);font-size:13px;font-weight:400;
  color:#333333;cursor:pointer;
}}
.dbtn:hover{{background:#EBEBEB;}}
.dbtn.ok{{color:var(--md-primary);border-color:var(--md-primary)}}
.dbtn i{{width:15px;height:15px;}}
.dbtn span{{}}

.md{{
  color:#333333;
  line-height:1.7;font-size:15px;
  font-weight:400;
}}
.md h1,.md h2,.md h3,.md h4{{
  font-family:var(--fd);
  color:var(--md-on-surface);font-weight:500;
  letter-spacing:0;scroll-margin-top:24px;line-height:1.1;
}}
.md h1{{font-size:32px;margin:21px 0 10.5px;padding-bottom:0;border-bottom:none;}}
.md h2{{font-size:26px;margin:21px 0 10.5px;padding-bottom:0;border-bottom:none;}}
.md h3{{font-size:22px;margin:21px 0 10.5px;font-weight:500}}
.md h4{{font-size:19px;margin:10.5px 0 10.5px;color:var(--md-on-surface);font-weight:500}}
.md p{{margin:0 0 10.5px;color:var(--md-on-surface)}}
.md p strong,.md li strong{{color:var(--md-on-surface)}}
.md a{{color:var(--md-primary);text-decoration:none;}}
.md a:hover{{color:#0b60ef;text-decoration:underline}}
.md ul,.md ol{{margin:0 0 10.5px 0;padding-left:24px;}}
.subheading{{color:#888888;font-size:15px;}}
.md code{{
  font-family:var(--fm);
  margin-left:3px;margin-right:3px;
  padding:2px 4px;
  font-size:85%;
  color:#4D8CF4;
  background-color:whitesmoke;
  white-space:nowrap;
  border:none;border-radius:2px;
}}
.md pre{{
  display:block;
  margin:0 0 10.5px;border-radius:2px;
  border:none;
  border-left:4px solid #D7DBE4;
  overflow:hidden;background:#F0F3F8;position:relative;
  padding:10px;
}}
.md pre code{{
  display:block;padding:0;overflow-x:auto;
  line-height:1.42857;background:transparent;border:none;border-radius:0;
  font-size:14px;color:#333333;tab-size:4;
  white-space:pre-wrap;
}}
.codehilite{{
  background:#F0F3F8!important;
  border:none!important;
  border-left:4px solid #D7DBE4!important;
  border-radius:2px!important;
  overflow:hidden;margin:0 0 10.5px!important;padding:10px!important;
  box-shadow:0 1px 2px rgba(30,50,90,0.05);
}}
.codehilite pre{{margin:0!important;padding:0!important;background:transparent!important;border-radius:0!important;border:none!important;box-shadow:none!important}}
.copy-btn{{
  position:absolute;top:8px;right:10px;
  display:flex;align-items:center;gap:6px;
  height:28px;padding:0 10px;
  border-radius:2px;
  background:#FFFFFF;border:1px solid #CCCCCC;
  font-family:var(--fb);font-size:12px;font-weight:400;
  color:#333333;cursor:pointer;
  opacity:0;
  transition:opacity 150ms;
}}
.md pre:hover .codehilite:hover .copy-btn,.md pre:hover .copy-btn,.codehilite:hover .copy-btn{{opacity:1}}
.copy-btn:hover{{background:#EBEBEB;}}
.copy-btn.ok{{color:var(--md-primary);border-color:var(--md-primary);opacity:1}}
.copy-btn i{{width:13px;height:13px}}

.md table{{
  width:100%;margin:0 0 21px;border-collapse:collapse;
  font-size:14px;background:transparent;
}}
.md th{{
  padding:8px 12px;background:#F7F9FC;font-weight:bold;font-size:14px;
  text-align:left;color:var(--md-on-surface);
  border-top:1px solid #DDDDDD;
  border-bottom:2px solid #DDDDDD;
  white-space:nowrap;
  vertical-align:bottom;
}}
.md td{{padding:8px;border-top:1px solid #DDDDDD;color:var(--md-on-surface);vertical-align:top;}}
.md tr:last-child td{{border-bottom:none}}
.md tbody tr:hover td{{background-color:#F5F5F5;}}
.md ul,.md ol{{margin:0 0 20px 24px}}
.md li{{margin:8px 0;color:var(--md-on-surface-var)}}
.md li strong{{color:var(--md-on-surface)}}
.md blockquote{{
  margin:0 0 21px;padding:10.5px 21px;
  border-radius:0;
  border:none;
  border-left:5px solid #EEEEEE;
  background:transparent;
  position:relative;
}}
.md blockquote::before{{content:none}}
.md blockquote p{{color:var(--md-on-surface-var);margin:0 0 10.5px;font-size:14px;font-weight:400;line-height:1.6;}}
.md blockquote p:last-child{{margin-bottom:0}}
.md blockquote strong{{color:var(--md-on-surface)}}
.md hr{{border:none;border-top:1px solid var(--md-outline-variant);margin:48px 0}}
.md img{{max-width:100%;border-radius:var(--radius-md);border:1px solid var(--md-outline-variant);box-shadow:var(--md-elev-1)}}

.mermaid-wrapper{{
  background:var(--md-surf-1);
  border-radius:8px;
  border:1px solid var(--md-outline-variant);
  margin:24px 0;overflow:hidden;
}}
.mermaid-controls{{
  display:flex;justify-content:flex-end;gap:4px;
  padding:6px 8px;background:var(--md-surf-2);
  border-bottom:1px solid var(--md-outline-variant);
}}
.mermaid-export,.mermaid-open{{
  width:36px;height:36px;border-radius:18px;
  display:flex;align-items:center;justify-content:center;
  background:transparent;border:none;cursor:pointer;
  color:var(--md-on-surface-var);position:relative;overflow:hidden;
}}
.mermaid-export::before,.mermaid-open::before{{
  content:'';position:absolute;inset:0;border-radius:18px;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.mermaid-export:hover::before,.mermaid-open:hover::before{{opacity:.04}}
.mermaid-export i,.mermaid-open i{{width:18px;height:18px;position:relative;z-index:1}}
.mermaid{{padding:28px;text-align:center;min-height:120px;display:flex;align-items:center;justify-content:center}}
.mermaid svg{{max-width:100%;height:auto}}

.doc-pn-nav{{
  margin-top:56px;
  padding-top:28px;
  border-top:1px solid var(--md-outline-variant);
  display:grid;grid-template-columns:1fr 1fr;gap:16px;
}}
.doc-pn-nav:empty{{display:none;border-top:none;margin-top:0;padding-top:0}}
.doc-pn-link{{
  display:flex;align-items:center;gap:14px;
  padding:14px 18px;
  border:1px solid #DDDDDD;
  border-radius:2px;
  cursor:pointer;text-decoration:none;
  min-width:0;
}}
.doc-pn-link:hover{{border-color:var(--md-primary);}}
.doc-pn-link.next{{grid-column:2;flex-direction:row-reverse;text-align:right}}
.doc-pn-link i{{width:20px;height:20px;color:var(--md-on-surface-var);flex-shrink:0}}
.doc-pn-text{{min-width:0;display:flex;flex-direction:column;gap:4px}}
.doc-pn-label{{font-size:12px;font-weight:500;color:var(--md-on-surface-3);text-transform:uppercase;letter-spacing:.04em}}
.doc-pn-title{{
  font-size:15px;font-weight:500;color:var(--md-on-surface);
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
@media(max-width:600px){{
  .doc-pn-nav{{grid-template-columns:1fr}}
  .doc-pn-link.next{{grid-column:1}}
}}


.pf-wrap{{position:relative}}
.pf-chip{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 12px;
  border-radius:2px;
  background:transparent;
  border:1px solid var(--md-outline);
  font-family:var(--fb);font-size:13px;font-weight:500;
  color:var(--md-on-surface-var);cursor:pointer;
  position:relative;overflow:hidden;
  transition:background 150ms,border-color 150ms;
}}
.pf-chip::before{{
  content:'';position:absolute;inset:0;border-radius:inherit;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.pf-chip:hover::before{{opacity:.04}}
.pf-chip.open{{background:var(--md-secondary-cont);border-color:var(--md-secondary-cont);color:var(--md-on-secondary-cont);}}
.pf-chip i{{width:16px;height:16px;position:relative;z-index:1}}
.pf-chip span{{position:relative;z-index:1}}
.pf-chev{{width:16px;height:16px;position:relative;z-index:1;transition:transform 200ms cubic-bezier(0.2,0,0,1)}}
.pf-chip.open .pf-chev{{transform:rotate(180deg)}}
.pf-menu{{
  position:absolute;top:calc(100% + 4px);left:0;
  background:var(--md-surf-3);
  border-radius:8px;
  box-shadow:var(--md-elev-2);
  z-index:300;overflow:hidden;display:none;
  min-width:200px;
  animation:fl-menu-in 150ms;
}}
@keyframes fl-menu-in{{from{{opacity:0;transform:scaleY(.9);transform-origin:top}}to{{opacity:1;transform:scaleY(1)}}}}
.pf-menu.open{{display:block}}
.pf-menu-item{{
  display:flex;align-items:center;gap:12px;
  padding:12px 16px;font-size:14px;
  color:var(--md-on-surface);cursor:pointer;
  position:relative;overflow:hidden;
  transition:background 150ms;
}}
.pf-menu-item::before{{
  content:'';position:absolute;inset:0;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.pf-menu-item:hover::before{{opacity:.04}}
.pf-menu-item.active{{color:var(--md-primary)}}
.pf-menu-item-label{{flex:1;position:relative;z-index:1}}
.pf-menu-check{{
  width:18px;height:18px;color:var(--md-primary);
  opacity:0;position:relative;z-index:1;transition:opacity 100ms;
}}
.pf-menu-item.active .pf-menu-check{{opacity:1}}
.pf-menu-divider{{height:1px;background:var(--md-outline-variant);margin:6px 0}}

.sb-scrim{{display:none;position:fixed;inset:0;background:rgba(0,0,0,.32);z-index:40}}
.mob-btn{{
  display:none;align-items:center;justify-content:center;
  width:40px;height:40px;border-radius:20px;
  border:none;background:transparent;
  color:var(--md-on-surface-var);cursor:pointer;
  position:relative;overflow:hidden;
}}
.mob-btn::before{{
  content:'';position:absolute;inset:0;border-radius:20px;
  background:var(--md-on-surface);opacity:0;transition:opacity 150ms;
}}
.mob-btn:hover::before{{opacity:.04}}
.mob-btn i{{width:22px;height:22px;position:relative;z-index:1}}

@media(max-width:1160px){{.toc-panel{{display:none!important}}}}
@media(max-width:760px){{
  .nav-drawer{{
    position:fixed;left:0;top:var(--top-bar-h);bottom:0;
    transform:translateX(-100%);
    transition:transform 300ms cubic-bezier(0.2,0,0,1);
    z-index:50;box-shadow:var(--md-elev-3);
  }}
  body.has-announce .nav-drawer{{top:calc(var(--top-bar-h) + 36px)}}
  .nav-drawer.open{{transform:translateX(0)}}
  .sb-scrim.open{{display:block}}
  .mob-btn{{display:flex!important}}
  .c-inner{{padding:28px 20px 72px}}
  .c-inner.wide{{padding:0 0 72px}}
  .c-inner.wide .sandbox-spotlight{{padding:0 20px}}
  .home-hero{{margin:-28px -20px 28px;padding:48px 20px;}}
  .home-title{{font-size:32px;}}
  .top-bar-center{{justify-content:flex-start}}
}}

{pygments_styles}
</style>
</head>
<body>
{announce_html}
<header class="top-app-bar" id="topAppBar">
  <div class="tab-logo-name" onclick="showFirstPage()" title="{project_name} home">{project_name}</div>
  <nav class="ionic-nav">
    <a onclick="showFirstDoc()">Getting Started</a>
    <a href="sandbox.html" target="_blank" rel="noopener">Sandbox</a>
    <a onclick="window.open('https://github.com/BinaryInkTN/AromaUI','_blank')">GitHub</a>
  </nav>

  <div class="nav-crumbs" id="bcrumb">
    <span class="bc-seg" onclick="showFirstPage()">{project_name}</span>
    <span class="bc-sep" id="bcHomeSep" style="display:none">›</span>
    <span class="bc-seg" id="bcCategory" style="display:none" onclick="showCategoryFromBc()"></span>
    <span class="bc-sep" id="bcCatSep" style="display:none">›</span>
    <span class="bc-seg" id="bcSubcategory" style="display:none" onclick="showSubcategoryFromBc()"></span>
    <span class="bc-sep" id="bcSubSep" style="display:none">›</span>
    <span class="bc-seg cur" id="bcCur"></span>
  </div>

  <div class="top-bar-center">
    <button class="mob-btn" onclick="openDrawer()" style="margin-right:8px;flex-shrink:0">
      <i data-lucide="menu"></i>
    </button>
    <div class="search-bar" id="searchBar">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="11" cy="11" r="8"/><path d="m21 21-4.35-4.35"/>
      </svg>
      <input class="search-bar-input" id="searchInput" type="text"
             placeholder="Search documentation…"
             autocomplete="off"
             oninput="renderSearchResults(this.value)"
             onfocus="openSearch()"
             onkeydown="handleSearchKey(event)">
      <span class="search-bar-kbd" id="searchKbd">
        <kbd>⌘</kbd><kbd>K</kbd>
      </span>
      <button class="search-bar-clear" id="searchClear" onclick="closeSearch()" title="Close" aria-label="Close search">
        <i data-lucide="x"></i>
      </button>
      <div class="search-dropdown" id="searchDropdown">
        <div class="search-results" id="searchResults"></div>
        <div class="search-dropdown-footer">
          <span><kbd>↑↓</kbd> to navigate</span>
          <span><kbd>↵</kbd> to open</span>
          <span><kbd>Esc</kbd> to close</span>
        </div>
      </div>
    </div>
  </div>

  <div class="top-bar-trailing">
    <div class="pf-wrap" id="pfWrap">
      <button class="pf-chip" id="pfChip" onclick="togglePfMenu()">
        <i data-lucide="filter"></i>
        <span id="pfChipTxt">All platforms</span>
        <i data-lucide="chevron-down" class="pf-chev"></i>
      </button>
      <div class="pf-menu" id="pfMenu"></div>
    </div>

    <button class="m3-icon-btn" onclick="downloadPDF()" title="Download PDF">
      <i data-lucide="file-down"></i>
    </button>

    <div class="theme-toggle">
      <button id="lightBtn" onclick="setTheme('light')" title="Light theme">
        <i data-lucide="sun"></i>
      </button>
      <button id="darkBtn" onclick="setTheme('dark')" title="Dark theme">
        <i data-lucide="moon"></i>
      </button>
    </div>

  </div>
</header>

<div class="sb-scrim" id="sbScrim" onclick="closeDrawer()"></div>

<div class="layout">
  <nav class="nav-drawer" id="navDrawer">
    <div class="nav-drawer-content" id="navDrawerContent">
      {sidebar_content}
    </div>
  </nav>

  <div class="main">
    <div class="c-layout">
      <div class="c-scroll" id="cScroll">
        <div class="c-inner wide" id="cInner">
          <div id="welcomeView">{welcome_html}</div>
          <div id="categoryView" style="display:none"></div>
          <div id="subcategoryView" style="display:none"></div>
          <div id="docView" style="display:none">
            <div class="doc-nav-buttons">
              <button class="doc-back-btn" onclick="goBackFromDoc()">
                <i data-lucide="arrow-left"></i>
                <span>Back</span>
              </button>
            </div>
            <div class="doc-hdr">
              <div class="doc-title-row">
                <h1 class="doc-title" id="docTitle"></h1>
                <p class="subheading" id="docSub"></p>
              </div>
              <div class="doc-meta" id="docMeta"></div>
              <div class="doc-acts">
                <button class="dbtn" onclick="copyPageLink(this)">
                  <i data-lucide="link"></i>
                  <span>Copy link</span>
                </button>
              </div>
            </div>
            <div class="md" id="docContent"></div>
            <div class="doc-pn-nav" id="docPnNav"></div>
          </div>
        </div>
      </div>

      <div class="toc-panel" id="tocPanel">
        <div class="toc-label">Contents</div>
        <div class="toc-progress"><div class="toc-fill" id="tocFill"></div></div>
        <div id="tocList"></div>
      </div>
    </div>
  </div>
</div>

<script src="https://cdn.jsdelivr.net/npm/lucide@latest/dist/umd/lucide.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/marked/marked.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js"></script>
<script>
const PAGES = {pages_json};
const TITLES = {titles_json};
const CATS = {categories_json};
const SUBCATS = {subcategories_json};
const PAGE_PLATFORMS = {platforms_json};
const CATEGORY_PAGES = {category_pages_json};
const SUBCATEGORY_PAGES = {subcategory_pages_json};
const PDF_URL = '{pdf_url}';
const SEARCH_INDEX = {search_index_json};
const PAGE_ICONS = {page_icons_js};
const SLUG_TO_ID = {slug_to_id_json};
const ID_TO_SLUG = {id_to_slug_json};
const PAGE_ORDER = {page_order_json};
const FIRST_PAGE_ID = '{first_page_id}';

let currentId=null, currentCategory=null, currentSubcategory=null;
let tocSections=[], activePF='all';
let previousView={{type:'first',category:null,subcategory:null,id:null}};
let searchIdx=-1, searchResults=[];

const ic = () => typeof lucide!=='undefined' && lucide.createIcons();

function syncSandboxTheme(t){{
  if(t!=='light'&&t!=='dark') return;
  document.querySelectorAll('.sandbox-frame iframe, .demo-frame iframe').forEach(function(f){{
    try{{
      var d=f.contentDocument;
      if(d&&d.documentElement) d.documentElement.setAttribute('data-theme',t);
    }}catch(e){{}}
    try{{f.contentWindow.postMessage({{type:'aroma-theme',theme:t}},'*');}}catch(e){{}}
  }});
}}

function setTheme(t) {{
  document.documentElement.setAttribute('data-theme',t);
  localStorage.setItem('docs-theme',t);
  var l=document.getElementById('lightBtn'),d=document.getElementById('darkBtn');
  if(l) l.classList.toggle('active',t==='light');
  if(d) d.classList.toggle('active',t==='dark');
  syncSandboxTheme(t);
  setTimeout(()=>rerenderMermaid(t),80);
}}

function initTheme() {{
  const saved=localStorage.getItem('docs-theme')||'light';
  document.documentElement.setAttribute('data-theme',saved);
  var l=document.getElementById('lightBtn'),d=document.getElementById('darkBtn');
  if(l) l.classList.toggle('active',saved==='light');
  if(d) d.classList.toggle('active',saved==='dark');
  setTimeout(()=>syncSandboxTheme(saved),500);
}}

function updateBreadcrumbs() {{
  const elCat=document.getElementById('bcCategory'), elSub=document.getElementById('bcSubcategory'),
        sepCS=document.getElementById('bcCatSep'), sepSS=document.getElementById('bcSubSep'),
        bcCur=document.getElementById('bcCur'), bcHSep=document.getElementById('bcHomeSep');
  const show=(el,v)=>el.style.display=v?'inline':'none';
  if (currentId) {{
    show(bcHSep, currentCategory);
    elCat.textContent=currentCategory||''; show(elCat,currentCategory);
    elSub.textContent=currentSubcategory||''; show(elSub,currentSubcategory);
    show(sepCS,currentCategory); show(sepSS,currentSubcategory);
    bcCur.textContent=TITLES[currentId]||'Document'; bcCur.style.display='inline';
  }} else if (currentSubcategory) {{
    show(bcHSep,true); show(elCat,true); elCat.textContent=currentCategory;
    show(sepCS,true); show(elSub,true); elSub.textContent=currentSubcategory;
    show(sepSS,false); bcCur.style.display='none';
  }} else if (currentCategory) {{
    show(bcHSep,true); show(elCat,true); elCat.textContent=currentCategory;
    show(sepCS,false); show(elSub,false); show(sepSS,false); bcCur.style.display='none';
  }} else {{
    show(bcHSep,false); show(elCat,false); show(elSub,false); show(sepCS,false); show(sepSS,false);
    bcCur.textContent=''; bcCur.style.display='none';
  }}
}}

function showCategoryFromBc(){{if(currentCategory) showCategory(currentCategory);}}
function showSubcategoryFromBc(){{if(currentCategory&&currentSubcategory) showSubcategory(currentCategory,currentSubcategory);}}

function _hideAll(){{
  ['welcomeView','categoryView','subcategoryView','docView'].forEach(id=>
    document.getElementById(id).style.display='none');
}}

function initHeroRelease(){{
  if(window._heroReleaseDone) return;
  window._heroReleaseDone=true;
  fetch('https://api.github.com/repos/BinaryInkTN/AromaUI/releases?per_page=5',{{headers:{{'Accept':'application/vnd.github.v3+json'}}}})
  .then(function(r){{ if(!r.ok) throw 0; return r.json(); }})
  .then(function(rels){{
    if(!rels||!rels.length) return;
    var rel=null;
    for(var i=0;i<rels.length;i++){{ if(!rels[i].draft){{ rel=rels[i]; break; }} }}
    if(!rel) rel=rels[0];
    var assets=rel.assets||[];
    var btn=document.getElementById('heroDlBtn'),txt=document.getElementById('heroDlTxt'),meta=document.getElementById('heroDlMeta');
    if(!btn||!txt||!assets.length) return;
    var a=assets[0];
    btn.href=a.browser_download_url;
    txt.textContent='Download '+(rel.tag_name||rel.name||'latest');
    function fmt(n){{ if(!n) return ''; var mb=n/1048576; return mb>=1?mb.toFixed(1)+' MB':Math.round(n/1024)+' KB'; }}
    var bits=[];
    if(a.name) bits.push(a.name);
    if(a.size) bits.push(fmt(a.size));
    if(meta) meta.textContent=bits.join(' · ');
  }}).catch(function(){{}});
}}

function showWelcome(){{
  _hideAll();
  initHeroRelease();
  document.getElementById('cInner').classList.add('wide');
  document.getElementById('welcomeView').style.display='';
  document.getElementById('tocPanel').classList.remove('vis');
  currentCategory=null; currentSubcategory=null; currentId=null;
  previousView={{type:'first',category:null,subcategory:null,id:null}};
  updateBreadcrumbs(); setActiveNav(null);
  updateURL();
  document.getElementById('cScroll').scrollTop=0; closeDrawer(); setTimeout(ic,50);
}}

function showFirstPage() {{
  showWelcome();
}}

function showFirstDoc() {{
  if (FIRST_PAGE_ID && PAGES[FIRST_PAGE_ID]) {{
    showPage(FIRST_PAGE_ID, CATS[FIRST_PAGE_ID]||null, SUBCATS[FIRST_PAGE_ID]||null);
  }}
}}

function getBasePath() {{
  if (window.location.hostname.includes('github.io')) {{
    const parts = window.location.pathname.split('/').filter(Boolean);
    if (parts.length > 0) {{
      return '/' + parts[0];
    }}
  }}
  return '';
}}

function updateURL() {{
  const basePath = getBasePath();
  let hash = '';
  
  if (currentId) {{
    const slug = ID_TO_SLUG[currentId] || currentId;
    if (currentCategory && currentSubcategory) {{
      hash = '#/category/' + encodeURIComponent(currentCategory) + 
             '/subcategory/' + encodeURIComponent(currentSubcategory) + 
             '/page/' + slug;
    }} else if (currentCategory) {{
      hash = '#/category/' + encodeURIComponent(currentCategory) + '/page/' + slug;
    }} else {{
      hash = '#/page/' + slug;
    }}
  }} else if (currentSubcategory) {{
    hash = '#/category/' + encodeURIComponent(currentCategory) + 
           '/subcategory/' + encodeURIComponent(currentSubcategory);
  }} else if (currentCategory) {{
    hash = '#/category/' + encodeURIComponent(currentCategory);
  }} else {{
    hash = '#/';
  }}
  
  const newUrl = basePath + '/index.html' + hash;
  history.replaceState(null, '', newUrl);
}}

function parseHash() {{
  const hash = window.location.hash.substring(1);
  const parts = hash.split('/').filter(Boolean);
  
  if (parts.length === 0 || parts[0] === '') {{
    showFirstPage();
    return;
  }}
  
  if (parts[0] === 'category') {{
    if (parts.length >= 2) {{
      const category = decodeURIComponent(parts[1]);
      if (parts.length >= 4 && parts[2] === 'subcategory') {{
        const subcategory = decodeURIComponent(parts[3]);
        if (parts.length >= 6 && parts[4] === 'page') {{
          const slug = decodeURIComponent(parts[5]);
          if (SLUG_TO_ID[slug] && PAGES[SLUG_TO_ID[slug]]) {{
            showPage(SLUG_TO_ID[slug], category, subcategory);
            return;
          }}
        }}
        showSubcategory(category, subcategory);
        return;
      }} else if (parts.length >= 4 && parts[2] === 'page') {{
        const slug = decodeURIComponent(parts[3]);
        if (SLUG_TO_ID[slug] && PAGES[SLUG_TO_ID[slug]]) {{
          showPage(SLUG_TO_ID[slug], category, null);
          return;
        }}
      }}
      showCategory(category);
      return;
    }}
  }} else if (parts[0] === 'page') {{
    if (parts.length >= 2) {{
      const slug = decodeURIComponent(parts[1]);
      if (SLUG_TO_ID[slug] && PAGES[SLUG_TO_ID[slug]]) {{
        const pageId = SLUG_TO_ID[slug];
        const category = CATS[pageId] || null;
        const subcategory = SUBCATS[pageId] || null;
        showPage(pageId, category, subcategory);
        return;
      }}
    }}
  }} else {{
    const slug = decodeURIComponent(parts[0]);
    if (SLUG_TO_ID[slug] && PAGES[SLUG_TO_ID[slug]]) {{
      const pageId = SLUG_TO_ID[slug];
      const category = CATS[pageId] || null;
      const subcategory = SUBCATS[pageId] || null;
      showPage(pageId, category, subcategory);
      return;
    }}
  }}
  
  showFirstPage();
}}

function loadFromURL() {{
  const pathParts = window.location.pathname.split('/').filter(Boolean);
  const hash = window.location.hash.substring(1);
  
  if (hash) {{
    parseHash();
    return;
  }}
  
  const lastPart = pathParts[pathParts.length - 1] || '';
  const secondLastPart = pathParts.length >= 2 ? pathParts[pathParts.length - 2] : '';
  const thirdLastPart = pathParts.length >= 3 ? pathParts[pathParts.length - 3] : '';
  
  if (lastPart && SLUG_TO_ID[lastPart]) {{
    const pageId = SLUG_TO_ID[lastPart];
    if (PAGES[pageId]) {{
      let category = null;
      let subcategory = null;
      
      if (secondLastPart && CATS[pageId] === secondLastPart) {{
        category = secondLastPart;
        if (thirdLastPart && SUBCATS[pageId] === thirdLastPart) {{
          subcategory = thirdLastPart;
        }}
      }} else {{
        category = CATS[pageId] || null;
        subcategory = SUBCATS[pageId] || null;
      }}
      
      showPage(pageId, category, subcategory);
      return;
    }}
  }}
  
  if (lastPart && CATEGORY_PAGES[lastPart]) {{
    showCategory(lastPart);
    return;
  }}
  
  if (secondLastPart && SUBCATEGORY_PAGES[secondLastPart + '||' + lastPart]) {{
    showSubcategory(secondLastPart, lastPart);
    return;
  }}
  
  showFirstPage();
}}

function showCategory(category){{
  // Categories have no landing page: go to the first document instead.
  const firstId=PAGE_ORDER.find(id=>CATS[id]===category&&PAGES[id]);
  if(firstId){{showPage(firstId,category,SUBCATS[firstId]||null);return;}}
  showFirstPage();
}}

function showSubcategory(category,subcategory){{
  const key=category+'||'+subcategory;
  if (!SUBCATEGORY_PAGES[key]) return;
  _hideAll();
  document.getElementById('cInner').classList.remove('wide');
  document.getElementById('subcategoryView').style.display='';
  document.getElementById('subcategoryView').innerHTML=SUBCATEGORY_PAGES[key];
  document.getElementById('tocPanel').classList.remove('vis');
  currentCategory=category; currentSubcategory=subcategory; currentId=null;
  previousView={{type:'subcategory',category,subcategory,id:null}};
  updateBreadcrumbs(); setActiveNav(null);
  updateURL();
  document.getElementById('cScroll').scrollTop=0; closeDrawer(); setTimeout(ic,50);
}}

function showPage(id, category=null, subcategory=null){{
  if (!PAGES[id]) return;

  if (category && subcategory) {{
    previousView = {{type:'subcategory', category, subcategory, id:null}};
  }} else if (category) {{
    previousView = {{type:'category', category, subcategory:null, id:null}};
  }} else if (!currentId || currentId !== id) {{
    if (!previousView || previousView.type === 'first') {{
      previousView = {{type:'first', category:null, subcategory:null, id:null}};
    }}
  }}

  _hideAll();
  document.getElementById('cInner').classList.remove('wide');
  document.getElementById('docContent').innerHTML = PAGES[id];
  document.getElementById('docTitle').textContent = TITLES[id] || id;

  const meta = document.getElementById('docMeta');
  meta.innerHTML = '';
  (PAGE_PLATFORMS[id]||[]).forEach(p=>{{
    const b=document.createElement('span');
    b.className='platform-badge';
    b.style.setProperty('--badge-color', p.color||'#5C5C5C');
    b.innerHTML=`<span>${{p.name}}</span>`;
    meta.appendChild(b);
  }});

  document.getElementById('docView').style.display='';

  currentId = id;
  currentCategory = category || CATS[id] || null;
  currentSubcategory = subcategory || SUBCATS[id] || null;

  setActiveNav(id);
  updateURL();
  
  updateBreadcrumbs();
  renderPnNav(id);
  document.getElementById('cScroll').scrollTop=0;
  closeDrawer();
  setTimeout(()=>{{addCopyBtns();initMermaid(localStorage.getItem('docs-theme')||'light');ic();buildToc();}},60);
  if (id === 'f64fb834') {{ initDownloadsPage(); }}
}}

function initDownloadsPage(){{
  if (window._downloadsInitialized) return;
  window._downloadsInitialized = true;

  const GITHUB_REPO = 'BinaryInkTN/AromaUI';
  const GITHUB_API = `https://api.github.com/repos/${{GITHUB_REPO}}/releases/latest`;

  function formatDate(dateStr) {{
    const date = new Date(dateStr);
    return date.toLocaleDateString('en-US', {{
      year: 'numeric',
      month: 'long',
      day: 'numeric'
    }});
  }}

  function formatFileSize(bytes) {{
    if (!bytes) return '';
    const kb = bytes / 1024;
    const mb = kb / 1024;
    if (mb >= 1) return `${{mb.toFixed(1)}} MB`;
    return `${{kb.toFixed(0)}} KB`;
  }}

  function getPlatformIcon(platform) {{
    const icons = {{
      'linux': '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 2L2 7l10 5 10-5-10-5z"/><path d="M2 17l10 5 10-5"/><path d="M2 12l10 5 10-5"/></svg>',
      'windows': '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18"/><path d="M9 21V9"/></svg>',
      'android': '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="5" y="2" width="14" height="20" rx="2"/><path d="M12 18h.01"/></svg>',
      'web': '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M2 12h20"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>',
      'source': '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 0 0-.94-2.61c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0 0 20 4.77 5.07 5.07 0 0 0 19.91 1S18.73.65 16 2.48a13.38 13.38 0 0 0-7 0C6.27.65 5.09 1 5.09 1A5.07 5.07 0 0 0 5 4.77a5.44 5.44 0 0 0-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 0 0 9 18.13V22"/></svg>'
    }};
    return icons[platform] || icons['source'];
  }}

  function detectPlatform(filename) {{
    const lower = filename.toLowerCase();
    if (lower.includes('linux') || lower.includes('ubuntu') || lower.includes('debian')) return 'linux';
    if (lower.includes('windows') || lower.includes('win') || lower.endsWith('.exe') || lower.endsWith('.zip')) return 'windows';
    if (lower.includes('android') || lower.endsWith('.apk') || lower.endsWith('.aab')) return 'android';
    if (lower.includes('wasm') || lower.includes('web') || lower.includes('html')) return 'web';
    if (lower.endsWith('.tar.gz') || lower.endsWith('.tar.xz') || lower.includes('source')) return 'source';
    return 'source';
  }}

  function getPlatformName(platform) {{
    const names = {{
      'linux': 'Linux x64',
      'windows': 'Windows x64',
      'android': 'Android APK',
      'web': 'WebAssembly',
      'source': 'Source Code'
    }};
    return names[platform] || platform;
  }}

  function getPlatformDesc(platform) {{
    const descs = {{
      'linux': 'Native Linux binary with GLES3 backend',
      'windows': 'Windows native build with Vulkan support',
      'android': 'Universal APK for ARM and x86 devices',
      'web': 'Run in any modern browser via WASM',
      'source': 'Full source code for custom builds'
    }};
    return descs[platform] || 'Download archive';
  }}

  function loadReleaseData() {{
    const loadingEl = document.getElementById('downloads-loading');
    const errorEl = document.getElementById('downloads-error');
    const contentEl = document.getElementById('downloads-content');

    if (!loadingEl || !errorEl || !contentEl) return;

    loadingEl.style.display = 'block';
    errorEl.style.display = 'none';
    contentEl.style.display = 'none';

    function renderRelease(release) {{
      loadingEl.style.display = 'none';
      contentEl.style.display = 'block';

      document.getElementById('release-version').textContent = `v${{release.tag_name || release.name}}`;
      document.getElementById('release-date').textContent = formatDate(release.published_at);
      document.getElementById('release-author').textContent = `by ${{release.author ? release.author.login : 'BinaryInkTN'}}`;

      const notesEl = document.getElementById('release-notes');
      if (release.body) {{
        const cleaned = release.body.replace(/<img[^>]*>/g, '').replace(/!\[.*?\]\(.*?\)/g, '');
        notesEl.innerHTML = marked.parse(cleaned);
      }} else {{
        notesEl.innerHTML = '<p>No release notes available.</p>';
      }}

      const grid = document.getElementById('download-grid');
      grid.innerHTML = '';

      if (release.assets && release.assets.length > 0) {{
        const platformAssets = {{}};
        release.assets.forEach(asset => {{
          const platform = detectPlatform(asset.name);
          if (!platformAssets[platform]) platformAssets[platform] = [];
          platformAssets[platform].push(asset);
        }});

        Object.keys(platformAssets).forEach(platform => {{
          const assets = platformAssets[platform];
          const primaryAsset = assets[0];

          const card = document.createElement('a');
          card.className = 'download-card';
          if (platform === 'source') card.classList.add('source-code-card');
          card.href = primaryAsset.browser_download_url;
          card.target = '_blank';
          card.rel = 'noopener noreferrer';

          const icon = getPlatformIcon(platform);
          const name = getPlatformName(platform);
          const desc = getPlatformDesc(platform);
          const size = formatFileSize(primaryAsset.size);
          const downloadCount = assets.reduce((sum, a) => sum + (a.download_count || 0), 0);

          card.innerHTML = `
            <div class="platform-icon">${{icon}}</div>
            <div class="platform-name">${{name}}</div>
            <div class="platform-desc">${{desc}}</div>
            <span class="download-btn">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
              Download
            </span>
            <div class="file-size">${{size}} ${{downloadCount > 0 ? `(${{downloadCount.toLocaleString()}} downloads)` : ''}}</div>
          `;

          grid.appendChild(card);
        }});
      }} else {{
        grid.innerHTML = `
          <a href="${{release.html_url}}" target="_blank" rel="noopener noreferrer" class="download-card source-code-card">
            <div class="platform-icon">${{getPlatformIcon('source')}}</div>
            <div class="platform-name">Source Code</div>
            <div class="platform-desc">Download the source code archive</div>
            <span class="download-btn">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
              View on GitHub
            </span>
          </a>
        `;
      }}
    }}

    fetch(GITHUB_API, {{
      headers: {{
        'Accept': 'application/vnd.github.v3+json'
      }}
    }})
    .then(res => {{
      if (!res.ok) throw new Error(`HTTP ${{res.status}}`);
      return res.json();
    }})
    .then(release => renderRelease(release))
    .catch(err => {{
      console.warn('Latest release fetch failed, trying releases list:', err);
      fetch(`${{GITHUB_API.replace('/latest', '')}}?per_page=1`, {{
        headers: {{
          'Accept': 'application/vnd.github.v3+json'
        }}
      }})
      .then(res => {{
        if (!res.ok) throw new Error(`HTTP ${{res.status}}`);
        return res.json();
      }})
      .then(releases => {{
        if (releases && releases.length > 0) {{
          renderRelease(releases[0]);
        }} else {{
          throw new Error('No releases found');
        }}
      }})
      .catch(err2 => {{
        loadingEl.style.display = 'none';
        errorEl.style.display = 'block';
        console.error('Failed to load release data:', err2);
      }});
    }});
  }}

  loadReleaseData();
}}

function renderPnNav(id){{
  const nav = document.getElementById('docPnNav');
  if (!nav) return;
  const idx = PAGE_ORDER.indexOf(id);
  if (idx === -1) {{ nav.innerHTML = ''; return; }}

  const prevId = idx > 0 ? PAGE_ORDER[idx - 1] : null;
  const nextId = idx < PAGE_ORDER.length - 1 ? PAGE_ORDER[idx + 1] : null;

  const link = (pid, dir) => {{
    if (!pid || !PAGES[pid]) return '';
    const label = dir === 'prev' ? 'Previous' : 'Next';
    const icon = dir === 'prev' ? 'arrow-left' : 'arrow-right';
    const title = (TITLES[pid] || pid).replace(/"/g, '&quot;');
    return `<div class="doc-pn-link ${{dir}}" onclick="showPage('${{pid}}', '${{CATS[pid]||''}}', ${{SUBCATS[pid] ? `'${{SUBCATS[pid]}}'` : 'null'}})" title="${{title}}">
      <i data-lucide="${{icon}}"></i>
      <div class="doc-pn-text">
        <span class="doc-pn-label">${{label}}</span>
        <span class="doc-pn-title">${{TITLES[pid] || pid}}</span>
      </div>
    </div>`;
  }};

  nav.innerHTML = link(prevId, 'prev') + link(nextId, 'next');
  ic();
}}

function goBackFromDoc(){{
  if(previousView.type==='subcategory'&&previousView.category&&previousView.subcategory)
    showSubcategory(previousView.category,previousView.subcategory);
  else if(previousView.type==='category'&&previousView.category)
    showCategory(previousView.category);
  else showFirstPage();
}}

function setActiveNav(id){{
  document.querySelectorAll('.nav-dest').forEach(el=>el.classList.remove('active'));
  if (id) {{
    const t = document.querySelector(`.nav-dest[data-page="${{id}}"]`);
    if (t) t.classList.add('active');
  }}
}}

function buildToc(){{
  const hs=document.getElementById('docContent').querySelectorAll('h1,h2,h3');
  const list=document.getElementById('tocList');
  list.innerHTML=''; tocSections=[];
  hs.forEach((h,i)=>{{
    if(!h.id) h.id='h-'+i+'-'+h.textContent.toLowerCase().replace(/[^a-z0-9]+/g,'-');
    tocSections.push({{id:h.id,el:h,level:+h.tagName[1]}});
    const item=document.createElement('div');
    item.className='toc-item';
    item.style.paddingLeft=((+h.tagName[1]-1)*12+20)+'px';
    item.textContent=h.textContent;
    item.dataset.id=h.id;
    item.onclick=()=>document.getElementById('cScroll').scrollTo({{top:h.offsetTop-64,behavior:'smooth'}});
    list.appendChild(item);
  }});
  document.getElementById('tocPanel').classList.toggle('vis',tocSections.length>0);
  setTimeout(updateTocActive,60);
}}

document.getElementById('cScroll').addEventListener('scroll',function(){{
  const tot=this.scrollHeight-this.clientHeight;
  document.getElementById('tocFill').style.width=(tot>0?Math.min(100,Math.round(this.scrollTop/tot*100)):0)+'%';
  updateTocActive();
  document.getElementById('topAppBar').classList.toggle('scrolled',this.scrollTop>8);
}});

function updateTocActive(){{
  const scroller=document.getElementById('cScroll');
  if(!scroller||!tocSections.length) return;
  // Measure bottom-up: the active title is the last heading at or above
  // the viewport top (with a small margin), i.e. the title you are over
  // or the closest one you have scrolled past.
  const line=scroller.scrollTop+120;
  let active=null;
  for(let i=tocSections.length-1;i>=0;i--){{
    if(tocSections[i].el.offsetTop<=line){{active=tocSections[i].id;break;}}
  }}
  // Past the end: the last section stays active.
  if(scroller.scrollTop+scroller.clientHeight>=scroller.scrollHeight-4)
    active=tocSections[tocSections.length-1].id;
  // Above everything: you are over the first title.
  if(active===null) active=tocSections[0].id;
  document.querySelectorAll('.toc-item').forEach(el=>el.classList.toggle('active',el.dataset.id===active));
}}

function addCopyBtns(){{
  document.querySelectorAll('.md pre').forEach(pre=>{{
    if(pre.querySelector('.copy-btn')) return;
    const btn=document.createElement('button');
    btn.className='copy-btn';
    btn.innerHTML=`<i data-lucide="copy"></i> Copy`;
    btn.onclick=async()=>{{
      const code=pre.querySelector('code');
      if(!code) return;
      await navigator.clipboard.writeText(code.textContent);
      btn.classList.add('ok'); btn.innerHTML=`<i data-lucide="check"></i> Copied`; ic();
      setTimeout(()=>{{btn.classList.remove('ok');btn.innerHTML=`<i data-lucide="copy"></i> Copy`;ic();}},2000);
    }};
    pre.appendChild(btn);
  }});
  ic();
}}

function copyPageLink(btn){{
  navigator.clipboard.writeText(location.href);
  btn.classList.add('ok');
  const sp=btn.querySelector('span');
  if(sp) sp.textContent='Copied!';
  ic();
  setTimeout(()=>{{
    btn.classList.remove('ok');
    if(sp) sp.textContent='Copy link';
    ic();
  }},2000);
}}

function downloadPDF(){{if(PDF_URL) window.open(PDF_URL,'_blank');}}

function initPF(){{
  const all={{}};
  Object.values(PAGE_PLATFORMS).forEach(arr=>{{
    (arr||[]).forEach(p=>{{if(p&&p.name&&!all[p.name]) all[p.name]=p;}});
  }});
  const entries=Object.values(all).sort((a,b)=>a.name.localeCompare(b.name));
  const wrap=document.getElementById('pfWrap');
  if(!entries.length){{if(wrap) wrap.style.display='none'; return;}}
  const menu=document.getElementById('pfMenu');
  const allItem=document.createElement('div');
  allItem.className='pf-menu-item active'; allItem.dataset.p='all';
  allItem.onclick=e=>{{e.stopPropagation();filterPlatform('all');}};
  allItem.innerHTML=`<span class="pf-menu-item-label">All platforms</span><i data-lucide="check" class="pf-menu-check"></i>`;
  menu.appendChild(allItem);
  menu.appendChild(Object.assign(document.createElement('div'),{{className:'pf-menu-divider'}}));
  entries.forEach(p=>{{
    const opt=document.createElement('div');
    opt.className='pf-menu-item'; opt.dataset.p=p.name;
    opt.onclick=e=>{{e.stopPropagation();filterPlatform(p.name);}};
    opt.innerHTML=`<span class="pf-menu-item-label">${{p.name}}</span><i data-lucide="check" class="pf-menu-check"></i>`;
    menu.appendChild(opt);
  }});
  ic();
  document.addEventListener('click',e=>{{
    if(!document.getElementById('pfWrap')?.contains(e.target))
      document.getElementById('pfMenu').classList.remove('open');
  }});
}}

function togglePfMenu(){{
  const menu=document.getElementById('pfMenu'), chip=document.getElementById('pfChip');
  const open=menu.classList.toggle('open');
  chip.classList.toggle('open',open);
  if(open) ic();
}}

function filterPlatform(p){{
  activePF=p;
  document.querySelectorAll('.pf-menu-item').forEach(o=>o.classList.toggle('active',o.dataset.p===p));
  const txt=document.getElementById('pfChipTxt');
  if(txt) txt.textContent=p==='all'?'All platforms':p;
  document.querySelectorAll('.nav-dest[data-page]').forEach(el=>{{
    const pp=(PAGE_PLATFORMS[el.dataset.page]||[]).map(x=>x.name);
    el.classList.toggle('ph', p!=='all' && pp.length>0 && !pp.includes(p));
  }});
  document.getElementById('pfMenu').classList.remove('open');
  document.getElementById('pfChip').classList.remove('open');
  ic();
}}

function openSearch(){{
  document.getElementById('searchBar').classList.add('open');
  setTimeout(()=>document.getElementById('searchInput').focus(), 40);
  renderSearchResults(document.getElementById('searchInput').value||'');
}}

function closeSearch(){{
  document.getElementById('searchBar').classList.remove('open');
  document.getElementById('searchInput').value='';
  document.getElementById('searchInput').blur();
  searchIdx=-1;
}}

function renderSearchResults(q){{
  const query=q.toLowerCase().trim();
  searchResults = !query
    ? SEARCH_INDEX.slice(0,8)
    : SEARCH_INDEX.filter(item=>
        item.title.toLowerCase().includes(query)||
        item.category.toLowerCase().includes(query)||
        (item.subcategory&&item.subcategory.toLowerCase().includes(query))||
        (item.content&&item.content.toLowerCase().includes(query))
      ).slice(0,8);
  const c=document.getElementById('searchResults');
  if(!searchResults.length){{
    c.innerHTML='<div style="padding:32px;text-align:center;color:var(--md-on-surface-var);font-size:15px">No results found</div>';
    return;
  }}
  c.innerHTML=searchResults.map((item,i)=>`
    <div class="search-result-item" data-index="${{i}}" data-id="${{item.id}}"
         onclick="selectSearchResult('${{item.id}}')">
      <div class="search-result-icon"><i data-lucide="file-text"></i></div>
      <div class="search-result-text">
        <div class="search-result-title">${{item.title}}</div>
        <div class="search-result-path">${{[item.category,item.subcategory].filter(Boolean).join(' › ')}}</div>
      </div>
    </div>`).join('');
  ic(); searchIdx=-1;
}}

function selectSearchResult(id){{ closeSearch(); showPage(id); }}

function handleSearchKey(e){{
  const items=document.querySelectorAll('.search-result-item');
  if(e.key==='ArrowDown'){{e.preventDefault();searchIdx=Math.min(searchIdx+1,items.length-1);updateSearchSel(items);}}
  else if(e.key==='ArrowUp'){{e.preventDefault();searchIdx=Math.max(searchIdx-1,0);updateSearchSel(items);}}
  else if(e.key==='Enter'&&searchIdx>=0&&items[searchIdx]) selectSearchResult(items[searchIdx].dataset.id);
  else if(e.key==='Escape') closeSearch();
}}

function updateSearchSel(items){{
  items.forEach((el,i)=>{{
    el.classList.toggle('selected',i===searchIdx);
    if(i===searchIdx) el.scrollIntoView({{block:'nearest'}});
  }});
}}

function mCfg(t){{
  const d=t==='dark';
  return{{theme:'base',themeVariables:{{
    background: d?'#1C1B1F':'#FFFFFF',
    primaryColor: d?'#A6C8FF':'#0468D7',
    primaryTextColor: d?'#1C1B1F':'#FFFFFF',
    primaryBorderColor: d?'#5C5C5C':'#C4C7C5',
    lineColor: d?'#9AA0A6':'#5C5C5C',
    secondaryColor: d?'#2D2D2D':'#F2F2F2',
    tertiaryColor: d?'#4F378B':'#EADDFF',
    clusterBkg: d?'#252528':'#F8F9FA',
    nodeTextColor: d?'#1C1B1F':'#FFFFFF',
    edgeLabelBackground: d?'#1C1B1F':'#FFFFFF',
    fontFamily:"'Google Sans',system-ui,sans-serif",fontSize:'14px',
  }},startOnLoad:false,securityLevel:'loose',logLevel:'error',
  flowchart:{{useMaxWidth:true,htmlLabels:true,curve:'basis'}}}};
}}

function initMermaid(t){{
  if(typeof mermaid==='undefined'){{setTimeout(()=>initMermaid(t),150);return;}}
  try{{
    mermaid.initialize(mCfg(t));
    document.querySelectorAll('.mermaid').forEach(el=>{{if(!el.getAttribute('data-src')) el.setAttribute('data-src',el.innerHTML);}});
    mermaid.run({{querySelector:'.mermaid'}});
  }}catch(e){{console.warn('mermaid:',e);}}
}}

function rerenderMermaid(t){{
  if(typeof mermaid==='undefined') return;
  try{{
    mermaid.initialize(mCfg(t));
    const els=document.querySelectorAll('.mermaid');
    els.forEach(el=>{{const src=el.getAttribute('data-src');if(src){{el.innerHTML=src;el.removeAttribute('data-processed');}}}});
    if(els.length>0) mermaid.run({{querySelector:'.mermaid'}});
  }}catch(e){{console.warn('mermaid rerender:',e);}}
}}

function exportMermaidAsSVG(btn){{
  const svg=btn.closest('.mermaid-wrapper').querySelector('.mermaid svg');
  if(!svg) return;
  const clone=svg.cloneNode(true); clone.setAttribute('xmlns','http://www.w3.org/2000/svg');
  const blob=new Blob([new XMLSerializer().serializeToString(clone)],{{type:'image/svg+xml'}});
  const a=Object.assign(document.createElement('a'),{{href:URL.createObjectURL(blob),download:'diagram.svg'}});
  document.body.appendChild(a);a.click();document.body.removeChild(a);
}}

function openMermaidInNewPage(btn){{
  const svg=btn.closest('.mermaid-wrapper').querySelector('.mermaid svg');
  if(!svg) return;
  const clone=svg.cloneNode(true); clone.setAttribute('xmlns','http://www.w3.org/2000/svg');
  const win=window.open('','_blank');
  win.document.write(`<!DOCTYPE html><html><head><meta charset="UTF-8"><title>Diagram</title>
<style>body{{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#fff}}</style>
</head><body>${{new XMLSerializer().serializeToString(clone)}}</body></html>`);
  win.document.close();
}}

function dismissAnnounce(){{
  document.body.classList.remove('has-announce');
  try{{localStorage.setItem('docs-announce-dismissed','1');}}catch(e){{}}
}}

function toggleSec(id){{
  const el=document.getElementById('si-'+id);
  if(!el) return;
  const chev=el.previousElementSibling?.querySelector('.nav-sec-chev');
  const c=el.classList.toggle('c');
  if(chev){{chev.classList.toggle('c',c);chev.setAttribute('data-lucide',c?'chevron-right':'chevron-down');ic();}}
}}

function toggleSub(id){{
  const el=document.getElementById('ssi-'+id);
  if(!el) return;
  const hdr=document.querySelector(`[data-sg="${{id}}"]`);
  const chev=hdr?.querySelector('.nav-sub-chev');
  const c=el.classList.toggle('c');
  if(chev){{chev.classList.toggle('c',c);chev.setAttribute('data-lucide',c?'chevron-right':'chevron-down');ic();}}
}}

function openDrawer(){{
  document.getElementById('navDrawer').classList.add('open');
  document.getElementById('sbScrim').classList.add('open');
}}

function closeDrawer(){{
  document.getElementById('navDrawer').classList.remove('open');
  document.getElementById('sbScrim').classList.remove('open');
}}

document.addEventListener('keydown',e=>{{
  if((e.metaKey||e.ctrlKey)&&e.key==='k'){{e.preventDefault();openSearch();}}
  if(e.key==='Escape'){{
    if(document.getElementById('searchBar').classList.contains('open')) closeSearch();
  }}
}});

document.addEventListener('click',e=>{{
  if(!document.getElementById('searchBar')?.contains(e.target)) closeSearch();
}});

document.addEventListener('DOMContentLoaded',()=>{{
  initTheme();
  initPF();
  loadFromURL();
  setTimeout(ic, 100);
}});

window.addEventListener('hashchange', () => {{
  parseHash();
}});
</script>
</body>
</html>"""

    def generate_pdf(
        self, config, pages_dict, titles_dict, page_platforms, output_file
    ):
        from jinja2 import Template

        template = Template(self.pdf_template)
        sections, toc, pn = [], [], 3

        print("  Pre-rendering diagrams for PDF…")
        for sid, raw_content in pages_dict.items():
            content = _replace_mermaid_with_svg(raw_content, self._mermaid)
            sections.append({"title": titles_dict.get(sid, sid), "content": content})
            toc.append({"title": titles_dict.get(sid, sid), "page": pn})
            pn += 1

        html = template.render(
            title=config.get("name", "Documentation"),
            subtitle=config.get("description", ""),
            version=config.get("version", "1.0.0"),
            date=datetime.now().strftime("%B %d, %Y"),
            year=datetime.now().year,
            company=config.get("company", ""),
            toc=toc,
            sections=sections,
        )
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".html", encoding="utf-8", delete=False
        ) as f:
            f.write(html)
            tmp = f.name
        try:
            HTML(tmp).write_pdf(output_file)
            print(f"✓ PDF generated: {output_file}")
            return True
        except Exception as e:
            print(f"✗ PDF failed: {e}")
            return False
        finally:
            os.unlink(tmp)

    def generate(
        self, config_file: str, output_file: str, pdf_output: Optional[str] = None
    ):
        with open(config_file, "r", encoding="utf-8") as f:
            if config_file.endswith(".json"):
                config = json.load(f)
            elif config_file.endswith((".yml", ".yaml")):
                config = yaml.safe_load(f)
            else:
                raise ValueError("Config must be .json, .yml, or .yaml")

        project_name = config.get("name", "Documentation")
        project_version = config.get("version", "1.0.0")
        description = config.get("description", "Documentation")
        base_dir = os.path.dirname(os.path.abspath(config_file))
        sections = config.get("sections", [])
        categories = config.get("categories", [])

        if not categories:
            cat_names = sorted(set(s.get("category", "General") for s in sections))
            categories = [
                {"name": n, "icon": "folder", "description": f"Documentation for {n}"}
                for n in cat_names
            ]

        pages_dict: Dict[str, str] = {}
        titles_dict: Dict[str, str] = {}
        page_objects: Dict[str, Dict] = {}
        page_icon_map: Dict[str, str] = {}
        slug_to_id: Dict[str, str] = {}
        id_to_slug: Dict[str, str] = {}
        first_page_id: str = ""

        used_slugs: Dict[str, int] = {}

        for s in sections:
            title = s.get("title", "Untitled")
            sid = hashlib.md5(title.encode()).hexdigest()[:8]

            base_slug = _title_to_slug(title)
            if base_slug in used_slugs:
                used_slugs[base_slug] += 1
                slug = f"{base_slug}-{used_slugs[base_slug]}"
            else:
                used_slugs[base_slug] = 0
                slug = base_slug
            slug_to_id[slug] = sid
            id_to_slug[sid] = slug

            if not first_page_id:
                first_page_id = sid

            mdf = s.get("file", "")
            if mdf and not os.path.isabs(mdf):
                mdf = os.path.join(base_dir, mdf)

            pages_dict[sid] = (
                self.load_markdown(mdf)
                if mdf and os.path.exists(mdf)
                else f"<h1>{title}</h1><p>{s.get('description', '')}</p>"
            )
            titles_dict[sid] = title
            page_icon_map[sid] = s.get("icon", "file-text")

            page_objects[sid] = {
                "id": sid,
                "title": title,
                "description": s.get("description", f"Documentation for {title}"),
                "icon": s.get("icon", "file-text"),
                "category": s.get("category", "General"),
                "subcategory": s.get("subcategory", ""),
                "platforms": s.get("platforms", []),
            }

        # Map markdown basenames to (slug, category) so intra-doc links can
        # be rewritten to in-site hash routes instead of dead .md URLs.
        # Keyed by lowercase basename; docs slugs come from titles so the
        # source .md files can keep GitHub-friendly relative paths.
        file_to_route: Dict[str, str] = {}
        for s in sections:
            f = s.get("file", "")
            if f and f.lower().endswith(".md"):
                title = s.get("title", "Untitled")
                sid = hashlib.md5(title.encode()).hexdigest()[:8]
                slug = id_to_slug.get(sid)
                if slug:
                    cat = urllib.parse.quote(s.get("category", "General"), safe="")
                    key = os.path.basename(f).lower()
                    if key in file_to_route:
                        print(f"  ⚠ duplicate doc basename: {key} (keeping first)")
                        continue
                    file_to_route[key] = f"#/category/{cat}/page/{slug}"

        def _rewrite_doc_links(html: str) -> str:
            def _sub(m):
                url = m.group(1)
                if url.startswith(("http://", "https://", "mailto:", "#", "data:")):
                    return m.group(0)
                # Split off ?query and #fragment so they survive the rewrite.
                # parseHash() looks up the slug exactly, so a trailing
                # #anchor would break the lookup; keep the route clean and
                # drop in-page anchors (the SPA has no heading deep-links).
                query = ""
                base_url = url
                if "?" in base_url:
                    base_url, qs = base_url.split("?", 1)
                    qs = qs.split("#", 1)[0]
                    if qs:
                        query = "?" + qs
                base_url = base_url.split("#", 1)[0]
                if not base_url:
                    return m.group(0)
                base = os.path.basename(base_url).lower()
                if base.endswith(".md") and base in file_to_route:
                    return f'href="{file_to_route[base]}{query}"'
                if base.endswith(".md"):
                    print(f"  ⚠ unmapped .md link target: {url}")
                    return m.group(0)
                if base == "sandbox.html":
                    return f'href="sandbox.html{query}"'
                return m.group(0)
            return re.sub(r'href="([^"]+)"', _sub, html)

        for sid in pages_dict:
            pages_dict[sid] = _rewrite_doc_links(pages_dict[sid])

        sidebar_sections: Dict[str, Dict[str, List]] = {}
        page_categories: Dict[str, str] = {}
        page_subcats: Dict[str, str] = {}
        page_platforms: Dict[str, List] = {}

        for sid, page in page_objects.items():
            cat = page["category"]
            sub = page["subcategory"]
            sidebar_sections.setdefault(cat, {}).setdefault(sub, []).append(page)
            page_categories[sid] = cat
            page_subcats[sid] = sub
            raw_platforms = page.get("platforms", [])
            page_platforms[sid] = [
                n for n in (self._normalize_platform(p) for p in raw_platforms) if n
            ]

        search_index = []
        for sid, page in page_objects.items():
            content_text = self._extract_text_from_html(pages_dict.get(sid, ""))
            search_index.append({
                "id": sid,
                "title": page["title"],
                "category": page["category"],
                "subcategory": page["subcategory"],
                "content": content_text[:1000],
            })

        welcome_html = self._welcome_page_html(
            project_name,
            description,
            hero=config.get("hero", {}),
            version=project_version,
        )

        hero_cfg = config.get("hero", {}) or {}
        announce_text = (hero_cfg.get("announcement") or "").strip()
        changelog_url = (hero_cfg.get("changelog_url") or "").strip()
        if announce_text:
            announce_link = (
                f' <a href="{_htmlesc.escape(changelog_url, quote=True)}"'
                ' target="_blank" rel="noopener">read changelog</a>'
                if changelog_url
                else ""
            )
            announce_html = (
                '<div class="announce-bar" id="announceBar" role="note">'
                f"<span>{_htmlesc.escape(announce_text)}</span>{announce_link}"
                '<button class="announce-close" onclick="dismissAnnounce()"'
                ' aria-label="Dismiss announcement">&times;</button>'
                "</div>"
                "<script>(function(){try{if(localStorage.getItem("
                '"docs-announce-dismissed")!=="1")'
                "{document.body.classList.add(\"has-announce\");}}"
                'catch(e){document.body.classList.add("has-announce");}})();</script>'
            )
        else:
            announce_html = ""

        sb = []
        page_order: List[str] = []
        for cat in categories:
            cname = cat.get("name", "General")
            cid = re.sub(r"[^a-z0-9]+", "-", cname.lower())
            csects = sidebar_sections.get(cname, {})
            if not csects:
                continue

            sb.append(
                f"<div class='nav-section-header' onclick=\"toggleSec('{cid}')\">"
                f"{cname}"
                f"<i data-lucide='chevron-down' class='nav-sec-chev'></i>"
                f"</div>"
                f"<div class='sec-items' id='si-{cid}'>"
            )

            #sb.append(
            #    f"<div class='nav-all' onclick=\"showCategory('{cname}')\">Overview</div>"
            #)

            for page in csects.get("", []):
                sb.append(
                    f"<div class='nav-dest' data-page='{page['id']}' onclick=\"showPage('{page['id']}', '{cname}', null)\">"
                    f"<span class='nav-dest-text'>{page['title']}</span></div>"
                )
                page_order.append(page["id"])

            for sub_name, sub_items in csects.items():
                if not sub_name:
                    continue
                sub_id = f"{cid}--{re.sub(r'[^a-z0-9]+', '-', sub_name.lower())}"
                sb.append(
                    f"<div class='nav-sub-header' data-sg='{sub_id}' onclick=\"toggleSub('{sub_id}')\">"
                    f"{sub_name}"
                    f"<i data-lucide='chevron-down' class='nav-sub-chev'></i>"
                    f"</div>"
                    f"<div class='sub-items' id='ssi-{sub_id}'>"
                )
                # sb.append(
                #     f"<div class='nav-all sub' onclick=\"showSubcategory('{cname}', '{sub_name}')\">Overview</div>"
                # )
                for page in sub_items:
                    sb.append(
                        f"<div class='nav-dest sub' data-page='{page['id']}' onclick=\"showPage('{page['id']}', '{cname}', '{sub_name}')\">"
                        f"<span class='nav-dest-text'>{page['title']}</span></div>"
                    )
                    page_order.append(page["id"])
                sb.append("</div>")

            sb.append("</div>")

        # Categories with no subcategory structure at all never entered the
        # loop above (sidebar_sections only holds categories with pages), but
        # guard anyway so no page silently falls out of prev/next ordering.
        for sid in page_objects:
            if sid not in page_order:
                page_order.append(sid)

        category_pages: Dict[str, str] = {}
        for cat in categories:
            cname = cat.get("name", "General")
            if cname in sidebar_sections:
                category_pages[cname] = self._category_page_html(
                    cat, sidebar_sections[cname], page_objects, titles_dict
                )

        subcategory_pages: Dict[str, str] = {}
        for cat_name, subcats in sidebar_sections.items():
            for sub_name, pages in subcats.items():
                if sub_name:
                    key = f"{cat_name}||{sub_name}"
                    subcategory_pages[key] = self._subcategory_page_html(
                        cat_name, sub_name, pages, titles_dict, pages_dict
                    )

        pdf_url = os.path.basename(pdf_output) if pdf_output else ""
        page_icons_js = json.dumps(page_icon_map)

        def _esc(s: str) -> str:
            return s.replace("</script>", "<\\/script>")

        html = self._build_template().format(
            project_name=project_name,
            version=project_version,
            description=description,
            welcome_html=welcome_html,
            pygments_styles=self._pygments_css(),
            sidebar_content="\n".join(sb),
            pages_json=json.dumps({k: _esc(v) for k, v in pages_dict.items()}),
            titles_json=json.dumps({k: _esc(v) for k, v in titles_dict.items()}),
            categories_json=json.dumps({k: _esc(v) for k, v in page_categories.items()}),
            subcategories_json=json.dumps({k: _esc(v) for k, v in subcategory_pages.items()}),
            platforms_json=json.dumps(page_platforms),
            category_pages_json=json.dumps({k: _esc(v) for k, v in category_pages.items()}),
            subcategory_pages_json=json.dumps({k: _esc(v) for k, v in subcategory_pages.items()}),
            search_index_json=json.dumps([{k: _esc(v) if isinstance(v, str) else v for k, v in item.items()} for item in search_index]),
            slug_to_id_json=json.dumps(slug_to_id),
            id_to_slug_json=json.dumps(id_to_slug),
            page_order_json=json.dumps(page_order),
            first_page_id=first_page_id,
            year=datetime.now().year,
            last_updated=datetime.now().strftime("%B %d, %Y"),
            pdf_url=pdf_url,
            page_icons_js=page_icons_js,
            announce_html=announce_html,
        )

        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(html)
        print(f"✓ HTML generated: {output_file}")

        if pdf_output:
            try:
                self.generate_pdf(
                    config, pages_dict, titles_dict, page_platforms, pdf_output
                )
            except Exception as e:
                print(
                    f"✗ PDF failed: {e}\n  Install weasyprint: pip install weasyprint"
                )


def main():
    parser = argparse.ArgumentParser(
        description="Documentation Generator",
        epilog="Example:\n  %(prog)s -c docs.yaml -o output/index.html --pdf output/docs.pdf",
    )
    parser.add_argument(
        "-c", "--config", required=True, help="Configuration file (.json/.yml/.yaml)"
    )
    parser.add_argument(
        "-o", "--output", default="docs/index.html", help="Output HTML file"
    )
    parser.add_argument("--pdf", help="Generate PDF documentation")
    parser.add_argument("--version", action="version", version="v2.0")
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f"Error: Configuration file not found: {args.config}")
        return 1

    try:
        DocGenerator().generate(args.config, args.output, args.pdf)
        return 0
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    exit(main())