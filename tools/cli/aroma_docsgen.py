#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import re
import subprocess
import shutil
import tempfile
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
            # Build using emcc directly, assuming libaroma.a is pre-built in examples/smartwatch_example/build_web/aroma_root/src/libaroma.a
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

    def _quick_links_html(self, config: Dict, slug_to_id: Optional[Dict[str, str]] = None) -> str:
        cards = config.get("quick_links", [])
        if not cards:
            return ""
        slug_to_id = slug_to_id or {}
        items = "".join(
            f'<div class="quick-link" onclick="showPage(\'{slug_to_id.get(c.get("page_id", ""), c.get("page_id", ""))}\')">'
            f'<div class="ql-icon"><i data-lucide="{c.get("icon", "link")}"></i></div>'
            f'<div class="ql-body">'
            f'<div class="ql-title">{c.get("title", "")}</div>'
            f'<div class="ql-desc">{c.get("description", "")}</div>'
            f"</div>"
            f'<i data-lucide="chevron-right" class="ql-arrow"></i>'
            f"</div>"
            for c in cards
        )
        return (
            '<div class="quick-links-section">'
            '<h2 class="section-heading">Quick Links</h2>'
            f'<div class="quick-links">{items}</div>'
            "</div>"
        )

    def _welcome_page_html(
        self,
        project_name: str,
        description: str,
        quick_links_html: str,
        categories: List[Dict],
        sidebar_sections: Dict[str, Dict[str, List]],
    ) -> str:
        cat_cards = []
        for cat in categories:
            cname = cat.get("name", "General")
            subcats = sidebar_sections.get(cname)
            if not subcats:
                continue
            page_count = sum(len(pages) for pages in subcats.values())
            cicon = cat.get("icon", "folder")
            cdesc = cat.get("description", f"Documentation for {cname}")
            cat_cards.append(
                f'<div class="home-cat-card" onclick="showCategory(\'{cname}\')">'
                f'<div class="home-cat-icon"><i data-lucide="{cicon}"></i></div>'
                f'<h3 class="home-cat-title">{cname}</h3>'
                f'<p class="home-cat-desc">{cdesc}</p>'
                f'<span class="home-cat-count">{page_count} document{"s" if page_count != 1 else ""}</span>'
                f'</div>'
            )

        browse_section = (
            (
                '<div class="home-section">'
                '<h2 class="section-heading">Browse the Docs</h2>'
                f'<div class="home-cat-grid">{"".join(cat_cards)}</div>'
                '</div>'
            )
            if cat_cards
            else ""
        )

        return f'''
        <div class="welcome-page">
            <div class="home-hero">
                <h1 class="home-title">{project_name}</h1>
                <p class="home-tagline">{description}</p>
                <div class="home-hero-actions">
                    <button class="m3-btn-filled" onclick="showFirstDoc()">
                        <i data-lucide="book-open"></i>
                        <span>Start reading</span>
                    </button>
                    <button class="m3-btn-outlined" onclick="openSearch()">
                        <i data-lucide="search"></i>
                        <span>Search docs</span>
                    </button>
                </div>
            </div>
            {quick_links_html}
            <div class="home-section">
                <h2 class="section-heading">Try Incense</h2>
                <p style="color:var(--md-on-surface-var);margin:8px 0 16px;font-size:15px">Interactive Incense language playground. Edit the code and click Run.</p>
                <div style="border:1px solid var(--md-outline);border-radius:12px;overflow:hidden;background:var(--md-surface)">
                    <iframe src="sandbox.html" style="width:100%;height:720px;border:none;display:block"></iframe>
                </div>
            </div>
            {browse_section}
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
                <div class="cat-icon-wrap">
                    <i data-lucide="{cat_icon}"></i>
                </div>
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
        md.registerExtensions([MermaidExtension(), SandboxExtension()], {})
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
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@400;500&family=Google+Sans+Display:wght@400&family=Roboto+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
:root{{
  --md-primary: #02569B;
  --md-on-primary: #FFFFFF;
  --md-primary-container: #E1F5FE;
  --md-on-primary-cont: #01579B;
  --md-secondary: #0175C2;
  --md-on-secondary: #FFFFFF;
  --md-secondary-cont: #E1F5FE;
  --md-on-secondary-cont: #01579B;
  --md-background: #FFFFFF;
  --md-surface: #FFFFFF;
  --md-surface-variant: #F5F7F9;
  --md-on-surface: #202124;
  --md-on-surface-var: #5F6368;
  --md-on-surface-3: #80868B;
  --md-outline: #DADCE0;
  --md-outline-variant: #E8EAED;
  --md-surf-1: #F8F9FA;
  --md-surf-2: #F1F3F4;
  --md-surf-3: #EAEDEE;
  --md-surf-4: #E3E7E8;
  --md-state-hover: rgba(1, 117, 194, 0.08);
  --md-state-focus: rgba(1, 117, 194, 0.12);
  --md-state-pressed: rgba(1, 117, 194, 0.16);
  --md-elev-1: 0 1px 2px 0 rgba(60,64,67,0.3), 0 1px 3px 1px rgba(60,64,67,0.15);
  --md-elev-2: 0 1px 2px 0 rgba(60,64,67,0.3), 0 2px 6px 2px rgba(60,64,67,0.15);
  --md-elev-3: 0 1px 3px 0 rgba(60,64,67,0.3), 0 4px 8px 3px rgba(60,64,67,0.15);
  --nav-drawer-w: 300px;
  --top-bar-h: 64px;
  --toc-w: 260px;
  --content-max: 840px;
  --fb: 'Google Sans', 'Roboto', system-ui, sans-serif;
  --fd: 'Google Sans Display', 'Google Sans', sans-serif;
  --fm: 'Roboto Mono', monospace;
}}

[data-theme="dark"]{{
  --md-primary: #40CAFF;
  --md-on-primary: #003355;
  --md-primary-container: #004C80;
  --md-on-primary-cont: #B3E5FC;
  --md-secondary: #40CAFF;
  --md-on-secondary: #003355;
  --md-secondary-cont: #004C80;
  --md-on-secondary-cont: #B3E5FC;
  --md-background: #202124;
  --md-surface: #202124;
  --md-surface-variant: #303134;
  --md-on-surface: #E8EAED;
  --md-on-surface-var: #9AA0A6;
  --md-on-surface-3: #80868B;
  --md-outline: #5F6368;
  --md-outline-variant: #3C4043;
  --md-surf-1: #292A2D;
  --md-surf-2: #303134;
  --md-surf-3: #37383B;
  --md-surf-4: #3E3F42;
  --md-state-hover: rgba(64, 202, 255, 0.08);
  --md-state-focus: rgba(64, 202, 255, 0.12);
  --md-state-pressed: rgba(64, 202, 255, 0.16);
}}
*,*::before,*::after{{box-sizing:border-box;margin:0;padding:0}}
html{{font-size:16px;-webkit-text-size-adjust:100%;scroll-behavior:smooth}}
body{{
  font-family:var(--fb);
  background:var(--md-background);
  color:var(--md-on-surface);
  height:100vh;overflow:hidden;
  -webkit-font-smoothing:antialiased;
  transition:background 200ms, color 200ms;
  font-weight:400;
}}

.top-app-bar{{
  position:fixed;top:0;left:0;right:0;
  height:var(--top-bar-h);
  background:var(--md-surface);
  display:flex;align-items:center;
  padding:0 16px 0 8px;
  z-index:200;
  border-bottom:1px solid var(--md-outline-variant);
  transition:background 200ms,border-color 200ms;
}}
.top-app-bar.scrolled{{
  box-shadow:var(--md-elev-2);
  border-bottom-color:transparent;
}}

.tab-logo-name{{
  font-family:var(--fd);
  font-size:22px;
  font-weight:400;
  margin-left:8px;
  color:var(--md-on-surface);
  letter-spacing:-0.01em;
  white-space:nowrap;
}}

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
  max-width:480px;
  height:40px;
  border-radius:20px;
  background:var(--md-surface-variant);
  border:none;
  display:flex;
  align-items:center;
  gap:10px;
  padding:0 8px 0 16px;
  transition:background 150ms,box-shadow 150ms,border-radius 150ms;
  position:relative;
  z-index:210;
}}
.search-bar:hover{{
  background:var(--md-surf-2);
}}
.search-bar.open{{
  background:var(--md-surf-3);
  box-shadow:var(--md-elev-2);
  border-radius:20px 20px 0 0;
}}
.search-bar svg{{
  width:18px;height:18px;
  color:var(--md-on-surface-var);
  flex-shrink:0;
}}
.search-bar-input{{
  flex:1;min-width:0;
  height:100%;
  background:none;border:none;outline:none;
  font-family:var(--fb);font-size:14px;font-weight:400;
  color:var(--md-on-surface);
}}
.search-bar-input::placeholder{{color:var(--md-on-surface-var)}}
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
  width:40px;height:40px;
  border-radius:20px;
  border:none;background:transparent;
  color:var(--md-on-surface-var);
  cursor:pointer;
  display:flex;align-items:center;justify-content:center;
  position:relative;
  overflow:hidden;
}}
.m3-icon-btn::before{{
  content:'';
  position:absolute;inset:0;
  border-radius:20px;
  background:var(--md-on-surface);
  opacity:0;
  transition:opacity 150ms;
}}
.m3-icon-btn:hover::before{{opacity:.04}}
.m3-icon-btn i{{width:20px;height:20px;position:relative;z-index:1}}

.m3-segmented{{
  display:flex;
  border:1px solid var(--md-outline);
  border-radius:20px;
  overflow:hidden;
  height:36px;
}}
.m3-seg-opt{{
  display:flex;align-items:center;justify-content:center;gap:6px;
  padding:0 12px;
  font-family:var(--fb);
  font-size:13px;
  font-weight:500;
  color:var(--md-on-surface);
  border:none;background:transparent;cursor:pointer;
  transition:background 150ms,color 150ms;
}}
.m3-seg-opt + .m3-seg-opt{{border-left:1px solid var(--md-outline)}}
.m3-seg-opt i{{width:16px;height:16px}}
.m3-seg-opt.active{{
  background:var(--md-secondary-cont);
  color:var(--md-on-secondary-cont);
}}

.version-chip{{
  display:inline-flex;align-items:center;
  height:28px;padding:0 10px;
  border-radius:6px;
  background:var(--md-secondary-cont);
  color:var(--md-on-secondary-cont);
  font-size:11px;
  font-weight:500;font-family:var(--fm);
  letter-spacing:.02em;
  border:none;
  white-space:nowrap;
}}

.layout{{
  display:flex;
  height:100vh;
  padding-top:var(--top-bar-h);
  overflow:hidden;
}}

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
  padding:16px 0 24px;
  scrollbar-width:thin;
  scrollbar-color:var(--md-outline-variant) transparent;
}}
.nav-drawer-content::-webkit-scrollbar{{width:4px}}
.nav-drawer-content::-webkit-scrollbar-thumb{{
  background:var(--md-outline-variant);border-radius:4px;
}}

.nav-section-header{{
  padding:20px 24px 6px;
  font-size:13px;
  font-weight:500;
  letter-spacing:.02em;
  color:var(--md-on-surface-var);
  display:flex;align-items:center;gap:6px;
  cursor:pointer;user-select:none;
}}
.nav-section-header:hover{{color:var(--md-on-surface)}}
.nav-sec-chev{{
  margin-left:auto;
  width:16px;height:16px;
  color:var(--md-on-surface-3);
  transition:transform 200ms cubic-bezier(0.2,0,0,1);
  flex-shrink:0;
}}
.nav-sec-chev.c{{transform:rotate(-90deg)}}
.sec-items.c{{display:none}}

.nav-dest{{
  display:flex;align-items:center;
  padding:0 24px;
  height:40px;
  font-size:14px;
  font-weight:400;
  color:var(--md-on-surface-var);
  cursor:pointer;
  position:relative;
  text-decoration:none;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
.nav-dest::before{{
  content:'';
  position:absolute;inset:2px 6px;
  border-radius:20px;
  background:transparent;
  transition:background 150ms;
}}
.nav-dest:hover::before{{background:var(--md-state-hover)}}
.nav-dest.active{{
  color:var(--md-primary);
  background:transparent;
}}
.nav-dest.active::before{{
  background:var(--md-primary-container);
}}
.nav-dest i{{display:none}}
.nav-dest.ph{{display:none!important}}
.nav-dest-text{{position:relative;z-index:1;}}

.nav-sub-header{{
  display:flex;align-items:center;
  padding:6px 24px 2px 32px;
  height:36px;
  font-size:13px;
  font-weight:500;
  color:var(--md-on-surface-var);
  cursor:pointer;user-select:none;
  gap:4px;
}}
.nav-sub-header:hover{{color:var(--md-on-surface)}}
.nav-sub-chev{{
  margin-left:auto;width:14px;height:14px;
  color:var(--md-on-surface-3);
  transition:transform 200ms cubic-bezier(0.2,0,0,1);
  flex-shrink:0;
}}
.nav-sub-chev.c{{transform:rotate(-90deg)}}
.sub-items.c{{display:none}}
.nav-dest.sub{{padding-left:40px;height:36px;font-size:13px}}

.nav-all{{
  display:block;padding:6px 24px;
  font-size:13px;
  color:var(--md-primary);
  cursor:pointer;
  font-weight:500;
}}
.nav-all:hover{{text-decoration:underline}}
.nav-all.sub{{padding-left:40px}}

.main{{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0}}

.breadcrumb-strip{{
  display:flex;align-items:center;
  padding:0 32px;height:40px;
  background:var(--md-surf-1);
  border-bottom:1px solid var(--md-outline-variant);
  font-size:12px;
  color:var(--md-on-surface-var);
  gap:8px;flex-shrink:0;
}}
.bc-seg{{
  color:var(--md-on-surface-var);cursor:pointer;
}}
.bc-seg:hover{{color:var(--md-primary)}}
.bc-seg.cur{{color:var(--md-on-surface);cursor:default;font-weight:500}}
.bc-sep{{color:var(--md-on-surface-3);font-size:.7rem}}

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
@keyframes fl-fade-up{{
  from{{opacity:0;transform:translateY(8px)}}
  to{{opacity:1;transform:translateY(0)}}
}}

.toc-panel{{
  width:var(--toc-w);flex-shrink:0;
  background:var(--md-surf-1);
  border-left:1px solid var(--md-outline-variant);
  overflow-y:auto;padding:28px 0 24px;
  display:none;
}}
.toc-panel.vis{{display:block}}
.toc-label{{
  padding:0 20px 12px;
  font-size:13px;
  font-weight:500;letter-spacing:.01em;
  color:var(--md-on-surface-var);
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
  font-weight:500;
}}

.quick-links-section{{margin-top:8px}}
.section-heading{{
  font-family:var(--fd);
  font-size:28px;
  font-weight:400;color:var(--md-on-surface);
  margin-bottom:24px;letter-spacing:-.01em;
}}
.quick-links{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(280px,1fr));
  gap:16px;margin-bottom:56px;
}}
.quick-link{{
  display:flex;align-items:center;gap:16px;
  padding:20px;
  background:var(--md-surface);
  border:1px solid var(--md-outline-variant);
  border-radius:8px;
  cursor:pointer;
  position:relative;overflow:hidden;
  transition:box-shadow 150ms,border-color 150ms;
}}
.quick-link:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.ql-icon{{
  width:44px;height:44px;border-radius:8px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;
  color:var(--md-on-primary-cont);flex-shrink:0;
  position:relative;z-index:1;
}}
.ql-icon i{{width:22px;height:22px}}
.ql-body{{flex:1;min-width:0;position:relative;z-index:1}}
.ql-title{{font-size:15px;font-weight:500;color:var(--md-on-surface);margin-bottom:4px}}
.ql-desc{{font-size:13px;color:var(--md-on-surface-var);line-height:1.5}}
.ql-arrow{{
  color:var(--md-on-surface-var);flex-shrink:0;
  width:20px;height:20px;position:relative;z-index:1;
  transition:transform 150ms,color 150ms;
}}
.quick-link:hover .ql-arrow{{transform:translateX(4px);color:var(--md-primary)}}

.welcome-page{{padding:8px 0 16px;animation:fl-fade-up 200ms cubic-bezier(0,0,0,1)}}
.home-hero{{
  padding:24px 0 48px;
  border-bottom:1px solid var(--md-outline-variant);
  margin-bottom:48px;
}}
.home-title{{
  font-family:var(--fd);
  font-size:clamp(40px,6vw,56px);
  font-weight:400;color:var(--md-on-surface);
  letter-spacing:-.02em;line-height:1.1;margin-bottom:16px;
}}
.home-tagline{{
  font-size:18px;line-height:1.6;color:var(--md-on-surface-var);
  max-width:640px;margin-bottom:32px;
}}
.home-hero-actions{{display:flex;gap:12px;flex-wrap:wrap}}
.m3-btn-filled,.m3-btn-outlined{{
  display:inline-flex;align-items:center;gap:10px;
  height:44px;padding:0 24px;
  border-radius:22px;
  font-family:var(--fb);font-size:14px;font-weight:500;
  cursor:pointer;position:relative;overflow:hidden;
  transition:box-shadow 150ms,border-color 150ms;
}}
.m3-btn-filled{{
  background:var(--md-primary);color:var(--md-on-primary);border:none;
}}
.m3-btn-filled::before{{
  content:'';position:absolute;inset:0;border-radius:inherit;
  background:var(--md-on-primary);opacity:0;transition:opacity 150ms;
}}
.m3-btn-filled:hover{{box-shadow:var(--md-elev-1)}}
.m3-btn-filled:hover::before{{opacity:.08}}
.m3-btn-outlined{{
  background:transparent;color:var(--md-primary);
  border:1px solid var(--md-outline);
}}
.m3-btn-outlined::before{{
  content:'';position:absolute;inset:0;border-radius:inherit;
  background:var(--md-primary);opacity:0;transition:opacity 150ms;
}}
.m3-btn-outlined:hover{{border-color:var(--md-primary)}}
.m3-btn-outlined:hover::before{{opacity:.08}}
.m3-btn-filled i,.m3-btn-outlined i{{width:18px;height:18px;position:relative;z-index:1}}
.m3-btn-filled span,.m3-btn-outlined span{{position:relative;z-index:1}}

.home-section{{margin-bottom:56px}}
.home-cat-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(260px,1fr));
  gap:16px;
}}
.home-cat-card{{
  background:var(--md-surface);
  border:1px solid var(--md-outline-variant);
  border-radius:12px;
  padding:24px;cursor:pointer;
  transition:box-shadow 150ms,border-color 150ms;
}}
.home-cat-card:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.home-cat-icon{{
  width:48px;height:48px;border-radius:10px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;
  color:var(--md-on-primary-cont);margin-bottom:16px;
}}
.home-cat-icon i{{width:24px;height:24px}}
.home-cat-title{{font-size:17px;font-weight:500;color:var(--md-on-surface);margin-bottom:6px}}
.home-cat-desc{{font-size:13px;color:var(--md-on-surface-var);line-height:1.5;margin-bottom:16px}}
.home-cat-count{{font-size:12px;font-weight:500;color:var(--md-on-surface-3)}}

.category-page{{padding:8px 0;animation:fl-fade-up 200ms cubic-bezier(0,0,0,1)}}
.category-header{{margin-bottom:40px}}
.cat-icon-wrap{{
  width:56px;height:56px;border-radius:12px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;margin-bottom:20px;
}}
.cat-icon-wrap i{{width:28px;height:28px;color:var(--md-on-primary-cont)}}
.category-title{{
  font-family:var(--fd);
  font-size:36px;
  font-weight:400;color:var(--md-on-surface);
  margin-bottom:12px;letter-spacing:-.01em;
}}
.category-description{{font-size:16px;color:var(--md-on-surface-var);line-height:1.6}}
.subcategories-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(300px,1fr));
  gap:16px;margin-top:24px;
}}
.subcategory-card{{
  background:var(--md-surface);
  border:1px solid var(--md-outline-variant);
  border-radius:8px;
  padding:24px;cursor:pointer;
  position:relative;overflow:hidden;
  transition:box-shadow 150ms,border-color 150ms;
}}
.subcategory-card:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.card-header{{display:flex;align-items:center;justify-content:flex-end;margin-bottom:12px}}
.card-count{{
  font-size:11px;font-weight:500;
  background:var(--md-secondary-cont);
  color:var(--md-on-secondary-cont);
  padding:4px 10px;border-radius:16px;
  position:relative;z-index:1;
}}
.card-title{{font-size:18px;font-weight:500;color:var(--md-on-surface);margin-bottom:8px;position:relative;z-index:1;}}
.card-desc{{font-size:13px;color:var(--md-on-surface-var);margin-bottom:16px;line-height:1.5;position:relative;z-index:1;}}
.card-preview{{display:flex;flex-wrap:wrap;gap:6px;position:relative;z-index:1}}
.preview-tag{{
  font-size:12px;padding:4px 10px;
  background:var(--md-surf-2);border:1px solid var(--md-outline-variant);
  border-radius:16px;color:var(--md-on-surface-var);
}}
.preview-more{{font-size:12px;color:var(--md-on-surface-3);padding:4px}}

.subcategory-page{{padding:8px 0;animation:fl-fade-up 200ms cubic-bezier(0,0,0,1)}}
.subcategory-header{{margin-bottom:32px}}
.back-button{{
  display:inline-flex;align-items:center;gap:8px;
  height:36px;padding:0 16px;
  border-radius:18px;
  background:transparent;
  border:1px solid var(--md-outline-variant);
  font-size:14px;font-weight:500;
  color:var(--md-on-surface-var);cursor:pointer;margin-bottom:20px;
  transition:border-color 150ms,box-shadow 150ms;
}}
.back-button:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.back-button i{{width:18px;height:18px}}
.subcategory-title{{font-family:var(--fd);font-size:32px;font-weight:400;color:var(--md-on-surface);margin-bottom:8px;letter-spacing:-.01em;}}
.subcategory-description{{font-size:16px;color:var(--md-on-surface-var)}}
.documents-grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:12px}}
.doc-card{{
  display:flex;align-items:center;gap:16px;
  padding:16px 20px;
  background:var(--md-surface);
  border:1px solid var(--md-outline-variant);
  border-radius:8px;cursor:pointer;
  transition:box-shadow 150ms,border-color 150ms;
}}
.doc-card:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.doc-card-icon{{
  width:40px;height:40px;border-radius:8px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;flex-shrink:0;
}}
.doc-card-icon i{{width:20px;height:20px;color:var(--md-on-primary-cont)}}
.doc-card-content{{flex:1;min-width:0}}
.doc-card-title{{font-size:15px;font-weight:500;color:var(--md-on-surface);margin-bottom:4px}}
.doc-card-desc{{font-size:13px;color:var(--md-on-surface-var);line-height:1.4}}
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
  height:36px;padding:0 16px 0 12px;
  border-radius:18px;
  background:transparent;border:1px solid var(--md-outline-variant);
  font-size:14px;font-weight:500;
  color:var(--md-on-surface-var);cursor:pointer;
  transition:border-color 150ms,box-shadow 150ms;
}}
.doc-back-btn:hover{{
  box-shadow:var(--md-elev-1);
  border-color:var(--md-primary);
}}
.doc-back-btn i{{width:18px;height:18px}}

.doc-hdr{{
  margin-bottom:32px;
  padding-bottom:24px;
  border-bottom:1px solid var(--md-outline-variant);
}}
.doc-title-row{{display:flex;align-items:flex-start;gap:20px;margin-bottom:16px}}
.doc-title-icon{{
  width:52px;height:52px;
  border-radius:12px;
  background:var(--md-primary-container);
  display:flex;align-items:center;justify-content:center;
  flex-shrink:0;margin-top:4px;
}}
.doc-title-icon i{{width:26px;height:26px;color:var(--md-on-primary-cont)}}
.doc-title{{
  font-family:var(--fd);
  font-size:clamp(28px,4vw,40px);
  font-weight:400;color:var(--md-on-surface);
  letter-spacing:-.02em;line-height:1.2;
}}
.doc-meta{{display:flex;align-items:center;gap:8px;margin-bottom:16px;flex-wrap:wrap}}
.doc-acts{{display:flex;gap:12px;margin-top:12px}}
.dbtn{{
  display:inline-flex;align-items:center;gap:8px;
  height:36px;padding:0 16px;
  border-radius:18px;background:transparent;border:none;
  font-family:var(--fb);font-size:14px;font-weight:500;
  color:var(--md-primary);cursor:pointer;
  position:relative;overflow:hidden;
}}
.dbtn::before{{
  content:'';position:absolute;inset:0;border-radius:inherit;
  background:var(--md-primary);opacity:0;transition:opacity 150ms;
}}
.dbtn:hover::before{{opacity:.04}}
.dbtn.ok{{color:var(--md-on-secondary-cont)}}
.dbtn.ok::before{{background:var(--md-secondary-cont);opacity:1}}
.dbtn i{{width:18px;height:18px;position:relative;z-index:1}}
.dbtn span{{position:relative;z-index:1}}

.md{{
  color:var(--md-on-surface);
  line-height:1.8;font-size:16px;
  font-weight:400;
}}
.md h1,.md h2,.md h3,.md h4{{
  font-family:var(--fd);
  color:var(--md-on-surface);font-weight:400;
  letter-spacing:-.01em;scroll-margin-top:24px;line-height:1.3;
}}
.md h1{{font-size:32px;margin:0 0 24px;padding-bottom:16px;border-bottom:1px solid var(--md-outline-variant);}}
.md h2{{font-size:24px;margin:48px 0 20px}}
.md h3{{font-size:18px;margin:32px 0 16px;font-weight:500}}
.md h4{{font-size:16px;margin:24px 0 12px;color:var(--md-on-surface-var);font-weight:500}}
.md p{{margin:0 0 20px}}
.md a{{color:var(--md-primary);text-decoration:none;border-bottom:1px solid color-mix(in srgb,var(--md-primary) 30%,transparent);font-weight:500;}}
.md a:hover{{border-bottom-color:var(--md-primary)}}
.md code{{
  font-family:var(--fm);font-size:.875em;
  padding:2px 6px;
  background:var(--md-surf-2);
  border:1px solid var(--md-outline-variant);
  border-radius:4px;color:var(--md-primary);
}}
.md pre{{
  margin:24px 0;border-radius:8px;
  border:1px solid var(--md-outline-variant);
  overflow:hidden;background:var(--md-surf-1);position:relative;
}}
.md pre code{{
  display:block;padding:20px 24px;overflow-x:auto;
  line-height:1.7;background:transparent;border:none;border-radius:0;
  font-size:.8125rem;color:var(--md-on-surface);tab-size:2;
}}
.codehilite{{background:var(--md-surf-1)!important;margin:0!important;padding:0!important}}
.codehilite pre{{margin:0!important;padding:20px 24px!important;background:var(--md-surf-1)!important;border-radius:0!important;border:none!important}}
.copy-btn{{
  position:absolute;top:10px;right:12px;
  display:flex;align-items:center;gap:6px;
  height:32px;padding:0 12px;
  border-radius:16px;
  background:var(--md-surf-3);border:1px solid var(--md-outline-variant);
  font-family:var(--fb);font-size:12px;font-weight:500;
  color:var(--md-on-surface-var);cursor:pointer;
  opacity:0;
  transition:opacity 150ms,box-shadow 150ms;
}}
.md pre:hover .copy-btn{{opacity:1}}
.copy-btn:hover{{box-shadow:var(--md-elev-1)}}
.copy-btn.ok{{background:var(--md-secondary-cont);color:var(--md-on-secondary-cont);opacity:1;border-color:transparent}}
.copy-btn i{{width:14px;height:14px}}

.md table{{width:100%;margin:24px 0;border-collapse:collapse;border:1px solid var(--md-outline-variant);border-radius:8px;overflow:hidden;font-size:14px;}}
.md th{{padding:12px 16px;background:var(--md-surf-2);font-weight:500;font-size:13px;text-align:left;color:var(--md-on-surface-var);border-bottom:1px solid var(--md-outline-variant);}}
.md td{{padding:12px 16px;border-bottom:1px solid var(--md-outline-variant);color:var(--md-on-surface-var)}}
.md tr:last-child td{{border-bottom:none}}
.md tr:hover td{{background:var(--md-surf-1)}}
.md ul,.md ol{{margin:0 0 20px 24px}}
.md li{{margin:8px 0;color:var(--md-on-surface-var)}}
.md li strong{{color:var(--md-on-surface)}}
.md blockquote{{
  margin:24px 0;padding:20px 24px;
  border-radius:8px;
  border:none;
  background:var(--md-primary-container);
  position:relative;
}}
.md blockquote::before{{
  content:'';position:absolute;left:0;top:10px;bottom:10px;
  width:4px;border-radius:4px;background:var(--md-primary);
}}
.md blockquote p{{color:var(--md-on-primary-cont);margin:0;font-size:15px}}
.md hr{{border:none;border-top:1px solid var(--md-outline-variant);margin:48px 0}}
.md img{{max-width:100%;border-radius:8px;border:1px solid var(--md-outline-variant)}}

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
  padding:16px 20px;
  border:1px solid var(--md-outline-variant);
  border-radius:16px;
  cursor:pointer;text-decoration:none;
  transition:border-color 150ms,box-shadow 150ms;
  min-width:0;
}}
.doc-pn-link:hover{{border-color:var(--md-primary);box-shadow:var(--md-elev-1)}}
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

.pg-footer{{
  margin-top:64px;padding-top:24px;
  border-top:1px solid var(--md-outline-variant);
  display:flex;align-items:center;justify-content:space-between;
  font-size:13px;color:var(--md-on-surface-3);
}}

.pf-wrap{{position:relative}}
.pf-chip{{
  display:inline-flex;align-items:center;gap:8px;
  height:36px;padding:0 14px;
  border-radius:18px;
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
  .nav-drawer.open{{transform:translateX(0)}}
  .sb-scrim.open{{display:block}}
  .mob-btn{{display:flex!important}}
  .c-inner{{padding:28px 20px 72px}}
  .top-bar-center{{justify-content:flex-start}}
}}

{pygments_styles}
</style>
</head>
<body>

<header class="top-app-bar" id="topAppBar">
  <span class="tab-logo-name">{project_name}</span>

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

    <div class="version-chip">v{version}</div>

    <div class="m3-segmented">
      <button class="m3-seg-opt active" id="lightBtn" onclick="setTheme('light')" title="Light theme">
        <i data-lucide="sun"></i>
      </button>
      <button class="m3-seg-opt" id="darkBtn" onclick="setTheme('dark')" title="Dark theme">
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
    <div class="breadcrumb-strip" id="bcrumb">
      <span class="bc-seg" onclick="showFirstPage()">{project_name}</span>
      <span class="bc-sep" id="bcHomeSep" style="display:none">›</span>
      <span class="bc-seg" id="bcCategory" style="display:none" onclick="showCategoryFromBc()"></span>
      <span class="bc-sep" id="bcCatSep" style="display:none">›</span>
      <span class="bc-seg" id="bcSubcategory" style="display:none" onclick="showSubcategoryFromBc()"></span>
      <span class="bc-sep" id="bcSubSep" style="display:none">›</span>
      <span class="bc-seg cur" id="bcCur"></span>
    </div>

    <div class="c-layout">
      <div class="c-scroll" id="cScroll">
        <div class="c-inner">
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
                <div class="doc-title-icon" id="docTitleIcon">
                  <i data-lucide="file-text" id="docTitleIconInner"></i>
                </div>
                <h1 class="doc-title" id="docTitle"></h1>
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
            <div class="pg-footer">
              <span>© {year} {project_name}</span>
              <span>v{version} · {last_updated}</span>
            </div>
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

function setTheme(t) {{
  document.documentElement.setAttribute('data-theme',t);
  localStorage.setItem('docs-theme',t);
  document.getElementById('lightBtn').classList.toggle('active',t==='light');
  document.getElementById('darkBtn').classList.toggle('active',t==='dark');
  setTimeout(()=>rerenderMermaid(t),80);
}}

function initTheme() {{
  const saved=localStorage.getItem('docs-theme')||'light';
  document.documentElement.setAttribute('data-theme',saved);
  document.getElementById('lightBtn').classList.toggle('active',saved==='light');
  document.getElementById('darkBtn').classList.toggle('active',saved==='dark');
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

function showWelcome(){{
  _hideAll();
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
  if (!CATEGORY_PAGES[category]) return;
  _hideAll();
  document.getElementById('categoryView').style.display='';
  document.getElementById('categoryView').innerHTML=CATEGORY_PAGES[category];
  document.getElementById('tocPanel').classList.remove('vis');
  currentCategory=category; currentSubcategory=null; currentId=null;
  previousView={{type:'category',category,subcategory:null,id:null}};
  updateBreadcrumbs(); setActiveNav(null);
  updateURL();
  document.getElementById('cScroll').scrollTop=0; closeDrawer(); setTimeout(ic,50);
}}

function showSubcategory(category,subcategory){{
  const key=category+'||'+subcategory;
  if (!SUBCATEGORY_PAGES[key]) return;
  _hideAll();
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
  document.getElementById('docContent').innerHTML = PAGES[id];
  document.getElementById('docTitle').textContent = TITLES[id] || id;

  const iconEl = document.getElementById('docTitleIconInner');
  if (iconEl) iconEl.setAttribute('data-lucide', PAGE_ICONS[id] || 'file-text');

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
}}

document.getElementById('cScroll').addEventListener('scroll',function(){{
  const tot=this.scrollHeight-this.clientHeight;
  document.getElementById('tocFill').style.width=(tot>0?Math.min(100,Math.round(this.scrollTop/tot*100)):0)+'%';
  let active=null; const top=this.scrollTop+88;
  tocSections.forEach(s=>{{if(s.el.offsetTop<=top) active=s.id;}});
  document.querySelectorAll('.toc-item').forEach(el=>el.classList.toggle('active',el.dataset.id===active));
  document.getElementById('topAppBar').classList.toggle('scrolled',this.scrollTop>8);
}});

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
    primaryTextColor: d?'#E8EAED':'#1C1B1F',
    primaryBorderColor: d?'#5C5C5C':'#C4C7C5',
    lineColor: d?'#9AA0A6':'#5C5C5C',
    secondaryColor: d?'#2D2D2D':'#F2F2F2',
    tertiaryColor: d?'#4F378B':'#EADDFF',
    clusterBkg: d?'#252528':'#F8F9FA',
    nodeTextColor: d?'#E8EAED':'#1C1B1F',
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

        quick_links_html = self._quick_links_html(config, slug_to_id)
        welcome_html = self._welcome_page_html(
            project_name, description, quick_links_html, categories, sidebar_sections
        )

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