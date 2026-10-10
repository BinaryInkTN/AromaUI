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
        'fillcolor="#E6F4FF" color="#005A9E"'
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
                    f'  style=filled fillcolor="#F2F8FF" color="#C7E0F4"',
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
        'fillcolor="#E6F4FF" color="#005A9E"]',
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
        'fillcolor="#E6F4FF" color="#005A9E"]',
        '  edge [fontname="Google Sans" fontsize=11 color="#005A9E"]',
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


                    self._compile_sandbox(code_str, code_hash)
                continue
            new_lines.append(line)
            i += 1
        return new_lines

    def _compile_sandbox(self, code_str: str, code_hash: str):

        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        site_dir = os.path.join(project_root, "_site")
        sandbox_dir = os.path.join(site_dir, "sandboxes")
        os.makedirs(sandbox_dir, exist_ok=True)

        out_html = os.path.join(sandbox_dir, f"sandbox_{code_hash}.html")
        if os.path.exists(out_html):
            return

        with tempfile.TemporaryDirectory() as tmpdir:
            src_file = os.path.join(tmpdir, "main.c")


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
        browse_html: str = "",
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
            '<div class="learn-tiles-zone">'
            '<h2 class="section-heading" data-i18n="getStarted">Get started</h2>'
            '<p class="learn-tiles-sub">Follow a path from your first window to shipping on desktop, mobile, and embedded.</p>'
            '<div class="metro-tiles">'
            + "".join(
                f'<div class="metro-tile" onclick="{go}">'
                f'<i data-lucide="{icon}" class="metro-icon"></i>'
                f'<span class="metro-label">{title}</span>'
                f'<span class="metro-desc">{desc}</span>'
                f'<span class="metro-foot" data-i18n="learnMore">Learn more</span>'
                f"</div>"
                for title, desc, icon, go in features
            )
            + "</div></div>"
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
                        <a class="hero-dl-btn" href="aroma-demo.apk" download>
                            <span>Download Demo APK</span>
                        </a>
                        <span class="hero-dl-meta">Android · 20.2 MB</span>
                    </div>
                  </div>
                  <div class="hero-graphic" aria-hidden="true">
<svg viewBox="0 0 380 300" width="100%" role="img" aria-label="AromaUI runs on Android, Windows, Linux, and Web">
<ellipse cx="190" cy="150" rx="160" ry="112" transform="rotate(-10 190 150)" fill="none" class="hero-orbit" stroke="#C7E0F4" stroke-width="2.5" stroke-dasharray="2 8" stroke-linecap="round"/>
<g transform="translate(61.6,79.6) scale(3.2)"><path d="M18.4395 5.5586c-.675 1.1664-1.352 2.3318-2.0274 3.498-.0366-.0155-.0742-.0286-.1113-.043-1.8249-.6957-3.484-.8-4.42-.787-1.8551.0185-3.3544.4643-4.2597.8203-.084-.1494-1.7526-3.021-2.0215-3.4864a1.1451 1.1451 0 0 0-.1406-.1914c-.3312-.364-.9054-.4859-1.379-.203-.475.282-.7136.9361-.3886 1.5019 1.9466 3.3696-.0966-.2158 1.9473 3.3593.0172.031-.4946.2642-1.3926 1.0177C2.8987 12.176.452 14.772 0 18.9902h24c-.119-1.1108-.3686-2.099-.7461-3.0683-.7438-1.9118-1.8435-3.2928-2.7402-4.1836a12.1048 12.1048 0 0 0-2.1309-1.6875c.6594-1.122 1.312-2.2559 1.9649-3.3848.2077-.3615.1886-.7956-.0079-1.1191a1.1001 1.1001 0 0 0-.8515-.5332c-.5225-.0536-.9392.3128-1.0488.5449zm-.0391 8.461c.3944.5926.324 1.3306-.1563 1.6503-.4799.3197-1.188.0985-1.582-.4941-.3944-.5927-.324-1.3307.1563-1.6504.4727-.315 1.1812-.1086 1.582.4941zM7.207 13.5273c.4803.3197.5506 1.0577.1563 1.6504-.394.5926-1.1038.8138-1.584.4941-.48-.3197-.5503-1.0577-.1563-1.6504.4008-.6021 1.1087-.8106 1.584-.4941z" fill="#3DDC84"/></g>
<g fill="#0078D4"><rect x="245" y="37" width="24" height="24" rx="1.5"/><rect x="275" y="37" width="24" height="24" rx="1.5"/><rect x="245" y="67" width="24" height="24" rx="1.5"/><rect x="275" y="67" width="24" height="24" rx="1.5"/></g>
<g transform="translate(62.0,180.0) scale(3.0)"><path d="M12.504 0c-.155 0-.315.008-.48.021-4.226.333-3.105 4.807-3.17 6.298-.076 1.092-.3 1.953-1.05 3.02-.885 1.051-2.127 2.75-2.716 4.521-.278.832-.41 1.684-.287 2.489a.424.424 0 00-.11.135c-.26.268-.45.6-.663.839-.199.199-.485.267-.797.4-.313.136-.658.269-.864.68-.09.189-.136.394-.132.602 0 .199.027.4.055.536.058.399.116.728.04.97-.249.68-.28 1.145-.106 1.484.174.334.535.47.94.601.81.2 1.91.135 2.774.6.926.466 1.866.67 2.616.47.526-.116.97-.464 1.208-.946.587-.003 1.23-.269 2.26-.334.699-.058 1.574.267 2.577.2.025.134.063.198.114.333l.003.003c.391.778 1.113 1.132 1.884 1.071.771-.06 1.592-.536 2.257-1.306.631-.765 1.683-1.084 2.378-1.503.348-.199.629-.469.649-.853.023-.4-.2-.811-.714-1.376v-.097l-.003-.003c-.17-.2-.25-.535-.338-.926-.085-.401-.182-.786-.492-1.046h-.003c-.059-.054-.123-.067-.188-.135a.357.357 0 00-.19-.064c.431-1.278.264-2.55-.173-3.694-.533-1.41-1.465-2.638-2.175-3.483-.796-1.005-1.576-1.957-1.56-3.368.026-2.152.236-6.133-3.544-6.139zm.529 3.405h.013c.213 0 .396.062.584.198.19.135.33.332.438.533.105.259.158.459.166.724 0-.02.006-.04.006-.06v.105a.086.086 0 01-.004-.021l-.004-.024a1.807 1.807 0 01-.15.706.953.953 0 01-.213.335.71.71 0 00-.088-.042c-.104-.045-.198-.064-.284-.133a1.312 1.312 0 00-.22-.066c.05-.06.146-.133.183-.198.053-.128.082-.264.088-.402v-.02a1.21 1.21 0 00-.061-.4c-.045-.134-.101-.2-.183-.333-.084-.066-.167-.132-.267-.132h-.016c-.093 0-.176.03-.262.132a.8.8 0 00-.205.334 1.18 1.18 0 00-.09.4v.019c.002.089.008.179.02.267-.193-.067-.438-.135-.607-.202a1.635 1.635 0 01-.018-.2v-.02a1.772 1.772 0 01.15-.768c.082-.22.232-.406.43-.533a.985.985 0 01.594-.2zm-2.962.059h.036c.142 0 .27.048.399.135.146.129.264.288.344.465.09.199.14.4.153.667v.004c.007.134.006.2-.002.266v.08c-.03.007-.056.018-.083.024-.152.055-.274.135-.393.2.012-.09.013-.18.003-.267v-.015c-.012-.133-.04-.2-.082-.333a.613.613 0 00-.166-.267.248.248 0 00-.183-.064h-.021c-.071.006-.13.04-.186.132a.552.552 0 00-.12.27.944.944 0 00-.023.33v.015c.012.135.037.2.08.334.046.134.098.2.166.268.01.009.02.018.034.024-.07.057-.117.07-.176.136a.304.304 0 01-.131.068 2.62 2.62 0 01-.275-.402 1.772 1.772 0 01-.155-.667 1.759 1.759 0 01.08-.668 1.43 1.43 0 01.283-.535c.128-.133.26-.2.418-.2zm1.37 1.706c.332 0 .733.065 1.216.399.293.2.523.269 1.052.468h.003c.255.136.405.266.478.399v-.131a.571.571 0 01.016.47c-.123.31-.516.643-1.063.842v.002c-.268.135-.501.333-.775.465-.276.135-.588.292-1.012.267a1.139 1.139 0 01-.448-.067 3.566 3.566 0 01-.322-.198c-.195-.135-.363-.332-.612-.465v-.005h-.005c-.4-.246-.616-.512-.686-.71-.07-.268-.005-.47.193-.6.224-.135.38-.271.483-.336.104-.074.143-.102.176-.131h.002v-.003c.169-.202.436-.47.839-.601.139-.036.294-.065.466-.065zm2.8 2.142c.358 1.417 1.196 3.475 1.735 4.473.286.534.855 1.659 1.102 3.024.156-.005.33.018.513.064.646-1.671-.546-3.467-1.089-3.966-.22-.2-.232-.335-.123-.335.59.534 1.365 1.572 1.646 2.757.13.535.16 1.104.021 1.67.067.028.135.06.205.067 1.032.534 1.413.938 1.23 1.537v-.043c-.06-.003-.12 0-.18 0h-.016c.151-.467-.182-.825-1.065-1.224-.915-.4-1.646-.336-1.77.465-.008.043-.013.066-.018.135-.068.023-.139.053-.209.064-.43.268-.662.669-.793 1.187-.13.533-.17 1.156-.205 1.869v.003c-.02.334-.17.838-.319 1.35-1.5 1.072-3.58 1.538-5.348.334a2.645 2.645 0 00-.402-.533 1.45 1.45 0 00-.275-.333c.182 0 .338-.03.465-.067a.615.615 0 00.314-.334c.108-.267 0-.697-.345-1.163-.345-.467-.931-.995-1.788-1.521-.63-.4-.986-.87-1.15-1.396-.165-.534-.143-1.085-.015-1.645.245-1.07.873-2.11 1.274-2.763.107-.065.037.135-.408.974-.396.751-1.14 2.497-.122 3.854a8.123 8.123 0 01.647-2.876c.564-1.278 1.743-3.504 1.836-5.268.048.036.217.135.289.202.218.133.38.333.59.465.21.201.477.335.876.335.039.003.075.006.11.006.412 0 .73-.134.997-.268.29-.134.52-.334.74-.4h.005c.467-.135.835-.402 1.044-.7zm2.185 8.958c.037.6.343 1.245.882 1.377.588.134 1.434-.333 1.791-.765l.211-.01c.315-.007.577.01.847.268l.003.003c.208.199.305.53.391.876.085.4.154.78.409 1.066.486.527.645.906.636 1.14l.003-.007v.018l-.003-.012c-.015.262-.185.396-.498.595-.63.401-1.746.712-2.457 1.57-.618.737-1.37 1.14-2.036 1.191-.664.053-1.237-.2-1.574-.898l-.005-.003c-.21-.4-.12-1.025.056-1.69.176-.668.428-1.344.463-1.897.037-.714.076-1.335.195-1.814.12-.465.308-.797.641-.984l.045-.022zm-10.814.049h.01c.053 0 .105.005.157.014.376.055.706.333 1.023.752l.91 1.664.003.003c.243.533.754 1.064 1.189 1.637.434.598.77 1.131.729 1.57v.006c-.057.744-.48 1.148-1.125 1.294-.645.135-1.52.002-2.395-.464-.968-.536-2.118-.469-2.857-.602-.369-.066-.61-.2-.723-.4-.11-.2-.113-.602.123-1.23v-.004l.002-.003c.117-.334.03-.752-.027-1.118-.055-.401-.083-.71.043-.94.16-.334.396-.4.69-.533.294-.135.64-.202.915-.47h.002v-.002c.256-.268.445-.601.668-.838.19-.201.38-.336.663-.336zm7.159-9.074c-.435.201-.945.535-1.488.535-.542 0-.97-.267-1.28-.466-.154-.134-.28-.268-.373-.335-.164-.134-.144-.333-.074-.333.109.016.129.134.199.2.096.066.215.2.36.333.292.2.68.467 1.167.467.485 0 1.053-.267 1.398-.466.195-.135.445-.334.648-.467.156-.136.149-.267.279-.267.128.016.034.134-.147.332a8.097 8.097 0 01-.69.468zm-1.082-1.583V5.64c-.006-.02.013-.042.029-.05.074-.043.18-.027.26.004.063 0 .16.067.15.135-.006.049-.085.066-.135.066-.055 0-.092-.043-.141-.068-.052-.018-.146-.008-.163-.065zm-.551 0c-.02.058-.113.049-.166.066-.047.025-.086.068-.14.068-.05 0-.13-.02-.136-.068-.01-.066.088-.133.15-.133.08-.031.184-.047.259-.005.019.009.036.03.03.05v.02h.003z" fill="#FCC624"/></g>
<g transform="translate(243.2,173.2) scale(3.4)"><path d="M3.489 10.164c-.565.548-.885 1.172-.885 1.835 0 2.167 3.415 3.921 7.631 3.921 2.339 0 4.437-.484 5.837-1.335-1.533 1.426-4.265 2.43-7.385 2.43C3.89 17.015 0 14.769 0 11.999s3.89-5.014 8.689-5.014c3.131.002 5.872 1.009 7.398 2.444-1.399-.856-3.504-1.351-5.852-1.351-2.506 0-4.73.621-6.121 1.579l.785 3.395.971-3.481h.737l.971 3.481.805-3.481h.805L7.953 14.11h-.717l-.991-3.566L5.24 14.11h-.714zm19.839 3.48h-.162v.424h-.142v-.424h-.164v-.122h.468zm.064-.122h.209l.095.364.096-.364H24v.546h-.133v-.415h-.002l-.113.415h-.109l-.115-.415h-.003v.415h-.133zm-5.699.515c-.2.084-.399.126-.601.126-.319 0-.608-.055-.863-.166q-.3825-.1665-.645-.459c-.175-.195-.311-.424-.404-.688-.093-.263-.14-.547-.14-.851 0-.313.047-.601.14-.869.093-.269.226-.502.402-.699.175-.2.39-.355.645-.468s.541-.171.863-.171c.215 0 .421.034.621.098.199.064.381.16.543.284s.295.279.399.463.169.395.193.632h-.874c-.055-.233-.159-.408-.315-.525-.155-.118-.343-.176-.567-.176-.207 0-.382.04-.526.12s-.262.187-.35.322a1.41 1.41 0 0 0-.196.459c-.039.171-.062.348-.062.532 0 .175.02.346.062.512.04.167.107.315.196.448.088.133.206.24.35.32s.319.119.526.119c.303 0 .539-.077.705-.23s.262-.375.29-.668h-.922v-.689h1.75v2.255h-.584l-.093-.472c-.162.21-.344.357-.543.441m2.708-4.14v3.395h2.033v.774h-2.949V9.897zm-9.204 1.585c.109.151.191.337.251.557.053.21.08.452.08.716v.047H9.372c.011.41.164.876.807.876.45 0 .703-.344.719-.537l.002-.042h.592l-.007.051c-.008.075-.051.222-.135.377-.049.086-.104.166-.166.239a1.3 1.3 0 0 1-.248.218c-.071.046-.158.1-.287.139-.148.047-.326.069-.543.069-.415 0-.763-.151-1.007-.434a1.43 1.43 0 0 1-.266-.482c-.06-.182-.091-.386-.091-.601 0-.485.12-.896.348-1.186.125-.158.278-.28.457-.362.191-.086.41-.131.654-.131.218 0 .413.043.581.127.165.082.304.202.415.359m-1.064.047c-.402 0-.741.357-.765.785h1.543c-.046-.528-.302-.785-.778-.785m4.373.388c.058.182.086.381.084.588 0 .19-.022.385-.064.563-.049.206-.122.39-.22.545-.11.178-.252.318-.419.415-.186.109-.408.164-.654.164-.228 0-.426-.057-.585-.173a.89.89 0 0 1-.198-.193v.282h-.561V9.983h.59v1.393c.093-.111.202-.2.324-.262.146-.078.313-.118.497-.12.199 0 .383.04.547.118.158.075.295.184.408.324.109.135.193.297.251.481m-.53.67c0-.2-.013-.459-.111-.672-.12-.26-.335-.384-.654-.384-.3 0-.506.135-.628.417-.084.195-.126.452-.126.785 0 .696.412.942.765.942.244 0 .435-.102.565-.303.125-.191.189-.462.189-.785" fill="#990000"/></g>
</svg>
</div>
                </div>
            </div>
            {tiles}
            {browse_html}
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
a  { color: #005A9E; text-decoration: none; border-bottom: 1pt solid #C7E0F4; }
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
  border-left: 3pt solid #005A9E;
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
.cover-line { width: 48pt; height: 3pt; background: #005A9E; margin: 28pt auto; border-radius: 2pt; }
.toc-page { page-break-after: always; }
.toc-page h1 { border-bottom: 1pt solid #DADCE0; padding-bottom: 12pt; margin-bottom: 20pt; font-weight: 400; }
.toc-entry { display: flex; align-items: baseline; margin: 8pt 0; font-size: 11pt; }
.toc-title { flex: 1; font-weight: 400; }
.toc-dots { flex: 2; border-bottom: 1pt dotted #C7E0F4; margin: 0 10pt; height: 0.7em; }
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
  --ms-blue: #0078D4;
  --ms-blue-hover: #106EBE;
  --ms-blue-dark: #005A9E;
  --ms-link: #0067B8;
  --ms-link-hover: #005A9E;
  --ms-text: #323130;
  --ms-heading: #201F1E;
  --ms-secondary: #605E5C;
  --ms-tertiary: #8A8886;
  --ms-bg: #FFFFFF;
  --ms-surface: #FAF9F8;
  --ms-surface-2: #F3F2F1;
  --ms-border: #EDEBE9;
  --ms-border-dark: #C8C6C4;
  --ms-code-bg: #F3F2F1;
  --ms-note-bg: #EFF6FC;
  --ms-note-border: #0078D4;
  --ms-elev-1: 0 1.6px 3.6px rgba(0,0,0,.11), 0 .3px .9px rgba(0,0,0,.07);
  --ms-elev-2: 0 3.2px 7.2px rgba(0,0,0,.13), 0 .6px 1.8px rgba(0,0,0,.11);
  --md-primary: #0078D4;
  --md-on-primary: #FFFFFF;
  --md-primary-container: #EFF6FC;
  --md-on-primary-cont: #005A9E;
  --md-secondary: #605E5C;
  --md-on-secondary: #FFFFFF;
  --md-secondary-cont: #F3F2F1;
  --md-on-secondary-cont: #323130;
  --md-background: #FFFFFF;
  --md-surface: #FFFFFF;
  --md-surface-variant: #FAF9F8;
  --md-on-surface: #323130;
  --md-on-surface-var: #605E5C;
  --md-on-surface-3: #8A8886;
  --md-outline: #C8C6C4;
  --md-outline-variant: #EDEBE9;
  --md-surf-1: #FAF9F8;
  --md-surf-2: #F3F2F1;
  --md-surf-3: #FFFFFF;
  --md-surf-4: #EDEBE9;
  --md-state-hover: rgba(0, 0, 0, 0.04);
  --md-state-focus: rgba(0, 120, 212, 0.12);
  --md-state-pressed: rgba(0, 0, 0, 0.08);
  --md-elev-1: 0 1.6px 3.6px rgba(0,0,0,.11), 0 .3px .9px rgba(0,0,0,.07);
  --md-elev-2: 0 3.2px 7.2px rgba(0,0,0,.13), 0 .6px 1.8px rgba(0,0,0,.11);
  --md-elev-3: 0 6.4px 14.4px rgba(0,0,0,.18), 0 1.2px 3.6px rgba(0,0,0,.11);
  --nav-drawer-w: 296px;
  --top-bar-h: 96px;
  --toc-w: 264px;
  --content-max: 800px;
  --fb: "Segoe UI", -apple-system, BlinkMacSystemFont, Roboto, "Helvetica Neue", Arial, sans-serif;
  --fd: "Segoe UI", -apple-system, BlinkMacSystemFont, Roboto, "Helvetica Neue", Arial, sans-serif;
  --fm: Cascadia Code, Consolas, "Courier New", monospace;
  --radius-sm: 2px;
  --radius-md: 4px;
  --radius-lg: 6px;
}}

[data-theme="dark"]{{
  --ms-blue: #4CC2FF;
  --ms-blue-hover: #6CB8F0;
  --ms-link: #4CC2FF;
  --ms-link-hover: #8ED3FF;
  --ms-text: #F3F2F1;
  --ms-heading: #FFFFFF;
  --ms-secondary: #C8C6C4;
  --ms-bg: #1B1A19;
  --ms-surface: #252423;
  --ms-surface-2: #292828;
  --ms-border: #3B3A39;
  --ms-border-dark: #484644;
  --ms-code-bg: #292828;
  --ms-note-bg: #082338;
  --md-primary: #4CC2FF;
  --md-on-primary: #082338;
  --md-primary-container: #082338;
  --md-on-primary-cont: #8ED3FF;
  --md-secondary: #C8C6C4;
  --md-on-secondary: #1B1A19;
  --md-secondary-cont: #292828;
  --md-on-secondary-cont: #F3F2F1;
  --md-background: #1B1A19;
  --md-surface: #1B1A19;
  --md-surface-variant: #252423;
  --md-on-surface: #F3F2F1;
  --md-on-surface-var: #C8C6C4;
  --md-on-surface-3: #8A8886;
  --md-outline: #484644;
  --md-outline-variant: #3B3A39;
  --md-surf-1: #252423;
  --md-surf-2: #292828;
  --md-surf-3: #323130;
  --md-surf-4: #3B3A39;
  --md-state-hover: rgba(255, 255, 255, 0.06);
  --md-state-focus: rgba(76, 194, 255, 0.18);
  --md-state-pressed: rgba(255, 255, 255, 0.10);
}}

*,*::before,*::after{{box-sizing:border-box;margin:0;padding:0}}
html{{font-size:16px;-webkit-text-size-adjust:100%}}
::selection{{background:#B6D7FF;}}
:focus-visible{{outline:2px solid var(--ms-blue);outline-offset:2px;border-radius:2px}}
body{{
  font-family:var(--fb);
  background:var(--md-background);
  color:var(--ms-text);
  height:100vh;overflow:hidden;
  -webkit-font-smoothing:antialiased;
  transition:background 200ms, color 200ms;
  font-weight:400;
  font-size:16px;
  line-height:1.6;
}}

/* ===== Microsoft Learn header: white top bar + breadcrumb bar ===== */
.top-app-bar{{
  position:fixed;top:0;left:0;right:0;
  height:var(--top-bar-h);
  background:transparent;
  display:flex;flex-direction:column;
  z-index:200;
  box-shadow:none;
}}
.top-app-bar.scrolled .ms-header-top{{box-shadow:0 1.6px 3.6px rgba(0,0,0,.11);}}
[data-theme="dark"] .ms-header-top{{background:#1B1A19;border-bottom-color:#3B3A39;}}
[data-theme="dark"] .tab-logo-name,[data-theme="dark"] .ms-learn-brand{{color:#F3F2F1;}}
[data-theme="dark"] .ionic-nav a{{color:#F3F2F1;}}
.ms-header-top{{
  height:54px;flex-shrink:0;
  background:#FFFFFF;
  border-bottom:1px solid var(--ms-border);
  display:flex;align-items:center;
  padding:0 20px;
  gap:0;
}}
.ms-logo{{
  display:flex;align-items:center;flex-shrink:0;
  cursor:pointer;
  margin-right:20px;
}}
.ms-logo svg{{display:block;width:20px;height:20px;}}
.ms-logo img{{display:block;height:32px;width:auto;max-width:180px;object-fit:contain;}}
[data-theme="dark"] .ms-logo img{{filter:invert(1);}}
.ms-logo-sep{{
  width:1px;height:24px;background:var(--ms-border-dark);
  margin:0 16px;flex-shrink:0;
}}
.ms-learn-brand{{
  font-family:var(--fd);
  font-size:18px;font-weight:600;color:var(--ms-heading);
  letter-spacing:-.02em;white-space:nowrap;cursor:pointer;
  margin-right:6px;
}}
.ms-learn-brand:hover{{text-decoration:underline;}}
.tab-logo-name{{
  font-family:var(--fd);
  font-size:18px;font-weight:400;color:var(--ms-text);
  letter-spacing:0;white-space:nowrap;cursor:pointer;
  margin-right:24px;
  display:flex;align-items:center;
}}
.tab-logo-name:hover{{text-decoration:underline;}}
[data-theme="dark"] .tab-logo-name,[data-theme="dark"] .ms-learn-brand{{color:#F3F2F1;}}
.ionic-nav{{
  display:flex;align-items:center;gap:2px;
  margin-right:12px;
}}
.ionic-nav a{{
  color:var(--ms-text);
  font-size:14px;font-weight:400;
  text-decoration:none;
  padding:8px 12px;
  white-space:nowrap;cursor:pointer;
  border-radius:2px;
}}
.ionic-nav a:hover{{color:var(--ms-heading);background:var(--ms-surface-2);text-decoration:underline;text-underline-offset:4px;}}
.ionic-nav a i{{display:none}}
[data-theme="dark"] .ionic-nav a{{color:#F3F2F1;}}

/* Breadcrumb bar (Learn style: light, below header) */
.ms-breadcrumb-bar{{
  height:42px;flex-shrink:0;
  background:#FFFFFF;
  border-bottom:1px solid var(--ms-border);
  display:flex;align-items:center;
  padding:0 20px;
  gap:12px;
}}
[data-theme="dark"] .ms-breadcrumb-bar{{background:#252423;border-bottom-color:#3B3A39;}}
.nav-crumbs{{
  display:flex;align-items:center;
  gap:8px;flex:1;min-width:0;
  font-size:13px;
  white-space:nowrap;overflow:hidden;
}}
.nav-crumbs .bc-seg{{
  color:var(--ms-secondary);cursor:pointer;font-weight:400;
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
  font-size:13px;
}}
.nav-crumbs .bc-seg:hover{{color:var(--ms-link);text-decoration:underline;}}
.nav-crumbs .bc-seg.cur{{color:var(--ms-heading);cursor:default;font-weight:600}}
.nav-crumbs .bc-sep{{color:var(--ms-tertiary);font-size:13px}}
@media(max-width:1180px){{.nav-crumbs{{overflow-x:auto}}}}
@media(prefers-reduced-motion:reduce){{
  html{{scroll-behavior:auto}}
  .c-inner{{animation:none}}
  .toc-fill{{transition:none}}
}}

/* Search: Learn style bordered box */
.top-bar-center{{
  flex:1;
  display:flex;
  align-items:center;
  justify-content:flex-end;
  padding:0 12px;
  max-width:none;
  margin:0;
}}
.search-bar{{
  width:100%;
  max-width:300px;
  height:32px;
  border-radius:2px;
  background:#FFFFFF;
  border:1px solid var(--ms-tertiary);
  display:flex;
  align-items:center;
  gap:8px;
  padding:0 8px 0 10px;
  transition:border-color 150ms, box-shadow 150ms;
  position:relative;
  z-index:210;
}}
.search-bar:hover{{border-color:var(--ms-text);}}
.search-bar.open,.search-bar:focus-within{{
  background:#FFFFFF;
  border-color:var(--ms-blue);
  border-radius:2px 2px 0 0;
  box-shadow:0 0 0 1px var(--ms-blue);
}}
[data-theme="dark"] .search-bar{{background:#292828;border-color:#484644;}}
[data-theme="dark"] .search-bar-input{{color:#F3F2F1;}}
[data-theme="dark"] .search-bar.open,[data-theme="dark"] .search-bar:focus-within{{background:#292828;border-color:var(--ms-blue);}}
[data-theme="dark"] .search-dropdown{{background:#252423;}}
[data-theme="dark"] .search-dropdown-footer kbd{{background:#323130;border-color:#484644;color:#F3F2F1;}}
[data-theme="dark"] .search-bar-kbd kbd{{background:#323130;border-color:#484644;color:#C8C6C4;}}
.search-bar svg{{color:var(--ms-secondary);width:16px;height:16px;flex-shrink:0;}}
.search-bar.open svg,.search-bar:focus-within svg{{color:var(--ms-blue);}}
.search-bar-input{{color:var(--ms-text);}}
.search-bar-input::placeholder{{color:var(--ms-secondary);opacity:1;font-size:13px;}}
.search-bar-input::-webkit-input-placeholder{{color:var(--ms-secondary);opacity:1;}}
.search-bar-input::-moz-placeholder{{color:var(--ms-secondary);opacity:1;}}
.search-bar-input:-ms-input-placeholder{{color:var(--ms-secondary);opacity:1;}}
.search-bar.open .search-bar-input,.search-bar:focus-within .search-bar-input{{color:var(--ms-heading);}}
.search-bar svg{{
  width:16px;height:16px;
  flex-shrink:0;
}}
.search-bar-input{{
  flex:1;min-width:0;
  height:100%;
  background:none;border:none;outline:none;
  font-family:var(--fb);font-size:13px;font-weight:400;
}}
.search-bar-kbd{{
  display:flex;gap:4px;align-items:center;
  font-size:11px;color:var(--ms-tertiary);
  flex-shrink:0;
}}
.search-bar.open .search-bar-kbd{{display:none}}
.search-bar-kbd kbd{{
  padding:1px 5px;
  border-radius:2px;
  border:1px solid var(--ms-border);
  background:var(--ms-surface-2);
  font-size:10px;
  font-family:var(--fm);
}}
.search-bar-clear{{
  display:none;align-items:center;justify-content:center;
  width:24px;height:24px;border-radius:2px;flex-shrink:0;
  border:none;background:transparent;color:var(--ms-secondary);
  cursor:pointer;position:relative;overflow:hidden;
}}
.search-bar-clear:hover{{background:var(--ms-surface-2);color:var(--ms-heading);}}
.search-bar-clear i{{width:14px;height:14px;position:relative;z-index:1}}
.search-bar.open .search-bar-clear{{display:flex}}

.search-dropdown{{
  display:none;position:absolute;top:100%;left:-1px;right:-1px;
  background:#FFFFFF;
  border:1px solid var(--ms-blue);
  border-top:1px solid var(--ms-border);
  border-radius:0 0 2px 2px;
  overflow:hidden;
  box-shadow:var(--md-elev-2);
  font-family:var(--fb);
}}
.search-bar.open .search-dropdown{{display:block}}
.search-results{{max-height:420px;overflow-y:auto}}
.search-result-item{{
  display:flex;align-items:center;gap:12px;
  padding:10px 14px;cursor:pointer;
  border-top:1px solid var(--ms-border);
  transition:background 120ms;
}}
.search-result-item:first-child{{border-top:none;}}
.search-result-item:hover,.search-result-item.selected{{background:var(--ms-surface-2);}}
.search-result-icon{{
  width:32px;height:32px;border-radius:2px;
  background:var(--ms-note-bg);
  display:flex;align-items:center;justify-content:center;
  color:var(--ms-blue);flex-shrink:0;
}}
.search-result-icon i{{width:16px;height:16px}}
.search-result-title{{font-size:14px;font-weight:600;color:var(--ms-heading);}}
.search-result-path{{font-size:12px;color:var(--ms-secondary);margin-top:1px;}}
.search-dropdown-footer{{
  display:flex;gap:20px;padding:8px 14px;
  border-top:1px solid var(--ms-border);
  background:var(--ms-surface);
  font-size:11px;color:var(--ms-secondary);
}}
.search-dropdown-footer kbd{{
  padding:1px 5px;border-radius:2px;
  border:1px solid var(--ms-border);font-size:10px;background:#fff;
}}

.top-bar-trailing{{
  display:flex;align-items:center;gap:6px;
  padding:0 0 0 4px;flex-shrink:0;
}}
.m3-icon-btn{{
  width:32px;height:32px;
  border-radius:2px;
  border:none;background:transparent;
  color:var(--ms-text);
  cursor:pointer;
  display:flex;align-items:center;justify-content:center;
}}
.m3-icon-btn:hover{{background:var(--ms-surface-2);color:var(--ms-heading);}}
.m3-icon-btn i{{width:16px;height:16px;}}
.theme-toggle{{
  display:flex;align-items:center;
  background:transparent;
  border:1px solid var(--ms-border);
  border-radius:2px;
  padding:2px;
  gap:2px;
  flex-shrink:0;
}}
.theme-toggle button{{
  width:28px;height:28px;
  border-radius:2px;
  border:none;background:transparent;
  color:var(--ms-secondary);
  cursor:pointer;
  display:flex;align-items:center;justify-content:center;
}}
.theme-toggle button:hover{{background:var(--ms-surface-2);color:var(--ms-heading);}}
.theme-toggle button.active{{
  background:var(--ms-blue);
  color:#FFFFFF;
}}
.theme-toggle button i{{width:14px;height:14px;}}

.layout{{
  display:flex;
  height:100vh;
  padding-top:var(--top-bar-h);
  overflow:hidden;
}}
body.has-announce .top-app-bar{{top:38px}}
body.has-announce .layout{{padding-top:calc(var(--top-bar-h) + 38px)}}
.announce-bar{{
  position:fixed;top:0;left:0;right:0;height:38px;z-index:300;
  display:none;align-items:center;justify-content:center;gap:10px;
  padding:0 44px 0 16px;
  background:#0078D4;
  color:#FFFFFF;font-size:13px;white-space:nowrap;
  border-bottom:1px solid #005A9E;
}}
body.has-announce .announce-bar{{display:flex}}
.announce-bar span{{overflow:hidden;text-overflow:ellipsis}}
.announce-bar a{{color:#FFFFFF;text-decoration:underline;font-weight:600}}
.announce-bar a:hover{{color:#DEEFFF;}}
.announce-close{{
  position:absolute;right:8px;top:50%;transform:translateY(-50%);
  width:28px;height:28px;border-radius:2px;
  border:none;background:transparent;color:#FFFFFF;
  cursor:pointer;font-size:16px;line-height:1;
  display:flex;align-items:center;justify-content:center;
}}
.announce-close:hover{{background:rgba(255,255,255,0.2)}}
/* ===== Left TOC: Learn tree ===== */
.nav-drawer{{
  width:var(--nav-drawer-w);
  flex-shrink:0;
  background:#FFFFFF;
  display:flex;flex-direction:column;
  overflow:hidden;
  border-right:1px solid var(--ms-border);
  transition:transform 200ms ease,background 200ms;
}}
[data-theme="dark"] .nav-drawer{{background:#1B1A19;}}
[data-theme="dark"] .nav-dest{{color:#C8C6C4;}}
[data-theme="dark"] .nav-section-header{{color:#A19F9D;}}
.nav-drawer-content{{
  flex:1;overflow-y:auto;
  padding:16px 0 32px 0;
  scrollbar-width:thin;
  scrollbar-color:var(--ms-border-dark) transparent;
}}
.nav-drawer-content::-webkit-scrollbar{{width:6px}}
.nav-drawer-content::-webkit-scrollbar-thumb{{
  background:var(--ms-border);border-radius:3px;
}}
.nav-section-header{{
  padding:10px 16px 6px;
  font-size:15px;
  font-weight:600;
  letter-spacing:0;
  text-transform:none;
  color:var(--ms-heading);
  display:flex;align-items:center;gap:6px;
  cursor:pointer;user-select:none;
  margin:12px 0 0;
  line-height:1.4;
}}
.nav-section-header:first-of-type{{margin-top:0;}}
.nav-section-header:hover{{color:var(--ms-link);}}
.nav-sec-chev{{
  margin-left:auto;
  width:14px;height:14px;
  color:var(--ms-secondary);
  transition:transform 150ms;
  flex-shrink:0;
}}
.nav-sec-chev.c{{transform:rotate(-90deg)}}
.sec-items.c{{display:none}}

.nav-dest{{
  display:flex;align-items:center;
  font-size:14px;
  font-weight:400;
  color:var(--ms-text);
  cursor:pointer;
  text-decoration:none;
  border-radius:0;
  margin:0;
  padding:6px 16px 6px 16px;
  border-left:3px solid transparent;
  border-right:none;
  white-space:normal;line-height:1.45;
}}
.nav-dest:hover{{background:var(--ms-surface-2);color:var(--ms-heading);text-decoration:underline;text-underline-offset:3px;}}
.nav-dest.active{{
  color:var(--ms-heading);
  background:var(--ms-surface-2);
  font-weight:600;
  border-left:3px solid var(--ms-blue);
}}
.nav-dest.active::before{{content:none}}
.nav-dest::before{{content:none}}
.nav-dest i{{display:none}}
.nav-dest.ph{{display:none!important}}
.nav-dest-text{{position:relative;z-index:1;}}

.nav-sub-header{{
  display:flex;align-items:center;
  padding:6px 16px 6px 16px;
  font-size:14px;
  font-weight:600;
  color:var(--ms-heading);
  cursor:pointer;user-select:none;
  gap:6px;line-height:1.45;
}}
.nav-sub-header:hover{{color:var(--ms-link);background:var(--ms-surface-2);}}
.nav-sub-chev{{
  margin-left:auto;width:14px;height:14px;
  color:var(--ms-secondary);
  transition:transform 150ms;
  flex-shrink:0;
}}
.nav-sub-chev.c{{transform:rotate(-90deg)}}
.sub-items.c{{display:none}}
.nav-dest.sub{{padding-left:28px;font-size:13.5px;}}

.nav-all{{
  display:block;padding:6px 16px;
  font-size:13px;
  color:var(--ms-link);
  cursor:pointer;
  font-weight:400;
}}
.nav-all:hover{{text-decoration:underline}}
.nav-all.sub{{padding-left:28px}}

.main{{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0;background:var(--md-background);}}

.c-layout{{flex:1;display:flex;overflow:hidden}}
.c-scroll{{
  flex:1;overflow-y:auto;
  scrollbar-width:thin;
  scrollbar-color:var(--ms-border-dark) transparent;
  overscroll-behavior:contain;
  -webkit-overflow-scrolling:touch;
  background:var(--md-background);
}}
.c-scroll::-webkit-scrollbar{{width:10px}}
.c-scroll::-webkit-scrollbar-thumb{{
  background:var(--ms-border-dark);border-radius:5px;border:2px solid var(--md-background);
}}
.c-inner{{
  max-width:var(--content-max);
  margin:0 auto;
  padding:32px 40px 64px;
  animation:fl-fade-up 160ms ease-out;
}}
.c-inner.wide{{
  max-width:none;
  padding:0 0 0;
}}
@keyframes fl-fade-up{{
  from{{opacity:0}}
  to{{opacity:1}}
}}

/* ===== Right rail: "In this article" ===== */
.toc-panel{{
  width:var(--toc-w);flex-shrink:0;
  background:var(--md-background);
  border-left:none;
  overflow-y:auto;padding:32px 0 24px 24px;
  display:none;
}}
.toc-panel.vis{{display:block}}
.toc-label{{
  padding:0 20px 8px 0;
  font-size:15px;
  font-weight:600;
  color:var(--ms-heading);
}}
.toc-progress{{
  margin:0 20px 12px 0;
  height:2px;border-radius:1px;
  background:var(--ms-border);
  overflow:hidden;
}}
.toc-fill{{
  height:100%;
  background:var(--ms-blue);
  border-radius:1px;
  width:0%;transition:width .12s linear;
}}
.toc-item{{
  display:block;
  padding:4px 16px 4px 0;
  font-size:13px;
  color:var(--ms-secondary);
  cursor:pointer;
  border-left:2px solid transparent;
  line-height:1.5;
  font-weight:400;
}}
.toc-item:hover{{color:var(--ms-link);text-decoration:underline;}}
.toc-item.active{{
  color:var(--ms-heading);
  border-left-color:transparent;
  font-weight:600;
}}
.author-card{{
  margin:24px 20px 0 0;
  padding:16px;
  border:1px solid var(--ms-border);
  border-radius:4px;
  background:var(--ms-surface);
}}
.author-label{{
  font-size:11px;
  font-weight:600;
  letter-spacing:.06em;
  text-transform:uppercase;
  color:var(--ms-secondary);
  margin-bottom:10px;
}}
.author-row{{
  display:flex;
  align-items:center;
  gap:10px;
}}
.author-avatar{{
  width:36px;height:36px;flex-shrink:0;
  border-radius:50%;
  background:var(--ms-blue);
  color:#FFFFFF;
  display:flex;align-items:center;justify-content:center;
  font-size:14px;font-weight:600;
}}
.author-name{{
  font-size:14px;
  font-weight:600;
  color:var(--ms-heading);
  line-height:1.3;
}}
.author-role{{
  font-size:12px;
  color:var(--ms-secondary);
}}
/* ===== Learn home: hero + cards ===== */
.section-heading{{
  font-family:var(--fd);
  font-size:22px;
  font-weight:600;color:var(--ms-heading);
  margin-bottom:16px;letter-spacing:-.01em;line-height:1.3;
}}
.welcome-page{{padding:0;--rail:1200px;}}
.home-hero{{
  margin:0;
  padding:0;
  background:#FFFFFF;
  color:var(--ms-text);
  border-bottom:1px solid var(--ms-border);
}}
[data-theme="dark"] .home-hero{{background:#1B1A19;border-bottom-color:#3B3A39;}}
.hero-inner{{
  margin:0 auto;
  max-width:var(--rail);
  padding:80px 40px 72px;
  display:flex;
  align-items:center;
  gap:72px;
}}
.hero-copy{{flex:1 1 auto;min-width:0;max-width:660px;}}
.hero-graphic{{flex:0 0 380px;max-width:380px;}}
.hero-graphic svg,.hero-graphic img{{display:block;width:100%;height:auto;}}
.hero-plat-label{{fill:#323130;}}
[data-theme="dark"] .hero-plat-label{{fill:#F3F2F1;}}
.hero-dots{{fill:#C7E0F4;}}
[data-theme="dark"] .hero-dots{{fill:#3B3A39;}}
.hero-tux-body{{fill:#323130;}}
[data-theme="dark"] .hero-tux-body{{fill:#F3F2F1;}}
.hero-tux-belly{{fill:#FFFFFF;}}
[data-theme="dark"] .hero-tux-belly{{fill:#1B1A19;}}
.hero-tux-eye{{fill:#FFFFFF;}}
[data-theme="dark"] .hero-tux-eye{{fill:#1B1A19;}}
.hero-web{{stroke:#0078D4;}}
[data-theme="dark"] .hero-web{{stroke:#4CC2FF;}}
.hero-orbit{{stroke:#C7E0F4;}}
[data-theme="dark"] .hero-orbit{{stroke:#3B3A39;}}
@media(max-width:900px){{.hero-graphic{{display:none;}}.hero-inner{{gap:0;}}}}
.home-title{{
  font-family:var(--fd);
  font-size:44px;
  font-weight:600;color:var(--ms-heading);
  line-height:1.12;margin:0 0 16px;letter-spacing:-.025em;
}}
.home-tagline{{
  font-size:17px;line-height:1.65;color:var(--ms-secondary);
  max-width:60ch;margin:0 0 28px;
}}
.home-hero-actions{{display:flex;gap:12px;flex-wrap:wrap;margin:0;}}
.home-hero .m3-btn-filled,.home-hero .m3-btn-outlined{{
  height:40px;padding:0 24px;
  border-radius:2px;
  font-size:14px;font-weight:600;
}}
.home-hero .m3-btn-filled{{
  background:var(--ms-blue);color:#FFFFFF;border:1px solid var(--ms-blue);
}}
.home-hero .m3-btn-filled:hover{{
  background:var(--ms-blue-hover);border-color:var(--ms-blue-hover);
}}
.home-hero .m3-btn-outlined{{
  background:#FFFFFF;color:var(--ms-heading);
  border:1px solid var(--ms-border-dark);
}}
.home-hero .m3-btn-outlined:hover{{background:var(--ms-surface-2);}}
[data-theme="dark"] .home-hero .m3-btn-outlined{{background-color:transparent;color:#F3F2F1;border-color:#484644;}}
[data-theme="dark"] .home-hero .m3-btn-outlined:hover{{background-color:#323130;}}
.hero-release{{
  display:flex;align-items:center;gap:16px;flex-wrap:wrap;
  margin-top:32px;
  padding-top:24px;
  border-top:1px solid var(--ms-border);
  max-width:660px;
}}
.hero-dl-btn{{
  display:inline-flex;align-items:center;gap:8px;
  padding:9px 20px;
  background:var(--ms-blue);color:#FFFFFF;
  border:1px solid var(--ms-blue);
  border-radius:2px;
  font-size:14px;font-weight:600;
  text-decoration:none;white-space:nowrap;
}}
.hero-dl-btn:hover{{background:var(--ms-blue-hover);text-decoration:none;color:#FFFFFF;}}
.hero-dl-meta{{
  font-size:13px;
  color:var(--ms-secondary);
}}
.m3-btn-filled,.m3-btn-outlined{{
  display:inline-flex;align-items:center;justify-content:center;
  padding:8px 16px;
  font-size:14px;font-weight:600;line-height:1.4;
  text-align:center;vertical-align:middle;white-space:nowrap;
  border-radius:2px;
  border:1px solid transparent;
  cursor:pointer;
  transition:background-color .12s ease-in-out, border-color .12s;
  font-family:var(--fb);
}}
.m3-btn-filled{{
  color:#FFFFFF;background-color:var(--ms-blue);border-color:var(--ms-blue);
}}
.m3-btn-filled:hover{{background-color:var(--ms-blue-hover);border-color:var(--ms-blue-hover);}}
.m3-btn-outlined{{
  color:var(--ms-heading);
  background-color:#FFFFFF;border-color:var(--ms-border-dark);
}}
.m3-btn-outlined:hover{{background-color:var(--ms-surface-2);}}
[data-theme="dark"] .m3-btn-outlined{{background-color:transparent;color:#F3F2F1;border-color:#484644;}}
[data-theme="dark"] .m3-btn-outlined:hover{{background-color:#323130;}}
.m3-btn-filled i,.m3-btn-outlined i{{display:none}}
.m3-btn-filled span,.m3-btn-outlined span{{position:relative;z-index:1}}

.home-section{{margin:0;}}
.learn-tiles-zone{{
  max-width:var(--rail);margin:0 auto;padding:72px 40px 0;
}}
.learn-tiles-zone .section-heading{{margin:0 0 8px;}}
.learn-tiles-sub{{color:var(--ms-secondary);font-size:15px;margin:0 0 28px;max-width:72ch;line-height:1.6;}}
.learn-tiles-zone .metro-tiles{{padding:0;margin:0;max-width:none;}}
.metro-foot{{
  margin-top:12px;padding-top:0;
  color:var(--ms-link);font-size:13px;font-weight:600;
}}
.metro-foot::after{{content:" ›";}}
.metro-tile:hover .metro-foot{{text-decoration:underline;color:var(--ms-link-hover);}}
.learn-browse-zone{{
  max-width:var(--rail);margin:0 auto;padding:72px 40px 0;
}}
.learn-browse-zone .section-heading{{margin:0 0 20px;}}
.learn-browse-grid{{
  display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:32px 48px;
}}
.learn-browse-col h3{{
  font-size:16px;font-weight:600;color:var(--ms-heading);margin:0 0 10px;
  padding-bottom:10px;border-bottom:1px solid var(--ms-border);
}}
.learn-browse-col h3 a{{color:inherit;cursor:pointer;text-decoration:none;}}
.learn-browse-col h3 a:hover{{color:var(--ms-link);text-decoration:underline;}}
.learn-browse-col ul{{list-style:none;margin:0;padding:0;}}
.learn-browse-col li{{margin:0;padding:4px 0;font-size:14px;line-height:1.5;}}
.learn-browse-col li a{{color:var(--ms-link);cursor:pointer;text-decoration:none;}}
.learn-browse-col li a:hover{{text-decoration:underline;color:var(--ms-link-hover);}}
.learn-browse-sub{{
  list-style:none;
  font-size:11px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;
  color:var(--ms-secondary);
  padding:10px 0 2px!important;
}}
.learn-browse-sub:first-child{{padding-top:0!important;}}
/* Get-started cards: 4-up grid, tinted icon chips, bottom-aligned links */
.metro-tiles{{
  display:grid;
  grid-template-columns:repeat(4,minmax(0,1fr));
  gap:20px;
  margin:0;
  max-width:none;
  padding:0;
}}
@media(max-width:1100px){{.metro-tiles{{grid-template-columns:repeat(2,minmax(0,1fr));}}}}
@media(max-width:620px){{.metro-tiles{{grid-template-columns:minmax(0,1fr);}}}}
.metro-tile{{
  min-height:200px;
  border-radius:6px;
  cursor:pointer;
  overflow:hidden;
  padding:24px;
  background:#FFFFFF;
  border:1px solid var(--ms-border);
  box-shadow:var(--ms-elev-1);
  transition:box-shadow 150ms, border-color 150ms;
  display:flex;flex-direction:column;gap:10px;
}}
[data-theme="dark"] .metro-tile{{background:#252423;border-color:#3B3A39;}}
.metro-tile:hover{{border-color:var(--ms-blue);box-shadow:var(--ms-elev-2);}}
.metro-icon{{
  width:42px;height:42px;padding:10px;
  box-sizing:border-box;
  background:var(--ms-note-bg);
  border-radius:8px;
  color:var(--ms-blue);
  margin-bottom:8px;
}}
.metro-label{{
  color:var(--ms-link);
  font-size:17px;font-weight:600;line-height:1.35;
  white-space:normal;overflow:visible;text-overflow:clip;
}}
.metro-tile:hover .metro-label{{text-decoration:underline;color:var(--ms-link-hover);}}
.metro-desc{{
  color:var(--ms-secondary);
  font-size:14px;font-weight:400;line-height:1.6;flex:1;
  white-space:normal;overflow:visible;text-overflow:clip;
}}
.home-footer{{margin:64px 0 0;border-top:1px solid var(--ms-border);background:#FFFFFF;}}
[data-theme="dark"] .home-footer{{background:#1B1A19;}}
.home-footer-inner{{max-width:var(--rail);margin:0 auto;padding:72px 40px;text-align:left;}}
.home-footer h2{{font-family:var(--fd);font-size:28px;font-weight:600;color:var(--ms-heading);margin:0 0 12px;line-height:1.3;letter-spacing:-.01em;}}
.home-footer p{{font-size:15px;line-height:1.65;color:var(--ms-secondary);margin:0 0 28px;max-width:680px;}}
.home-footer-actions{{display:flex;gap:12px;flex-wrap:wrap;}}
.home-footer-actions a.m3-btn-filled,.home-footer-actions a.m3-btn-outlined{{text-decoration:none;}}
.demo-frame{{border:1px solid var(--ms-border);border-radius:4px;overflow:hidden;background:#FFFFFF;margin:0 0 24px;box-shadow:var(--ms-elev-1);}}
[data-theme="dark"] .demo-frame{{background:#252423;}}
.demo-head{{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 14px;border-bottom:1px solid var(--ms-border);font-size:13px;color:var(--ms-secondary);background:var(--ms-surface);}}
.demo-head a{{font-size:13px;color:var(--ms-link);text-decoration:none;white-space:nowrap;font-weight:600;}}
.demo-head a:hover{{text-decoration:underline}}
.demo-frame iframe{{width:100%;height:660px;border:none;display:block;background:var(--ms-surface);}}
@media(max-width:760px){{
  .hero-inner{{padding:56px 20px 48px;}}
  .home-title{{font-size:32px;}}
  .learn-tiles-zone,.learn-browse-zone{{padding:56px 20px 0;}}
  .home-footer-inner{{padding:56px 20px;}}
  .metro-tiles{{gap:16px;}}
}}

.category-page{{padding:32px 40px;max-width:1280px;margin:0 auto;}}
.category-header{{margin-bottom:24px}}
.category-title{{
  font-family:var(--fd);
  font-size:32px;
  font-weight:600;color:var(--ms-heading);
  margin:0 0 8px;line-height:1.2;letter-spacing:-.02em;
}}
.category-description{{font-size:15px;color:var(--ms-secondary);line-height:1.6;max-width:68ch}}
.subcategories-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(300px,1fr));
  gap:16px;margin-top:20px;
}}
.subcategory-card{{
  background:#FFFFFF;
  border:1px solid var(--ms-border);
  border-radius:4px;
  padding:20px;cursor:pointer;
  box-shadow:var(--ms-elev-1);
  transition:box-shadow 150ms, border-color 150ms;
}}
[data-theme="dark"] .subcategory-card{{background:#252423;border-color:#3B3A39;}}
.subcategory-card:hover{{border-color:var(--ms-blue);box-shadow:var(--ms-elev-2);}}
.subcategory-card:hover .card-title{{text-decoration:underline;}}
.card-header{{display:flex;align-items:center;justify-content:flex-end;margin-bottom:8px}}
.card-count{{
  font-size:12px;font-weight:400;
  color:var(--ms-secondary);
  background:var(--ms-surface-2);
  padding:2px 8px;border-radius:10px;
}}
.card-title{{font-size:18px;font-weight:600;color:var(--ms-link);margin:0 0 8px;}}
.subcategory-card:hover .card-title{{text-decoration:underline;color:var(--ms-link-hover);}}
.card-desc{{font-size:13px;color:var(--ms-secondary);margin-bottom:12px;line-height:1.55;}}
.card-preview{{display:flex;flex-wrap:wrap;gap:6px;}}
.preview-tag{{
  font-size:12px;padding:2px 8px;
  background:var(--ms-surface-2);border:1px solid var(--ms-border);
  border-radius:2px;color:var(--ms-text);
}}
.preview-more{{font-size:12px;color:var(--ms-secondary);padding:2px 4px;}}

.subcategory-page{{padding:32px 40px;max-width:1280px;margin:0 auto;animation:fl-fade-up 160ms ease-out;}}
.subcategory-header{{margin-bottom:24px}}
.back-button{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 14px;
  border-radius:2px;
  background:transparent;
  border:1px solid var(--ms-border-dark);
  font-size:13px;font-weight:400;
  color:var(--ms-text);cursor:pointer;margin-bottom:20px;
}}
.back-button:hover{{
  background:var(--ms-surface-2);
  color:var(--ms-heading);
}}
.back-button i{{width:14px;height:14px}}
.subcategory-title{{font-family:var(--fd);font-size:28px;font-weight:600;color:var(--ms-heading);margin-bottom:8px;letter-spacing:-.02em;line-height:1.25;}}
.subcategory-description{{font-size:15px;color:var(--ms-secondary);line-height:1.6;max-width:68ch}}
.documents-grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:16px}}
.doc-card{{
  display:flex;align-items:flex-start;gap:14px;
  padding:16px;
  background:#FFFFFF;
  border:1px solid var(--ms-border);
  border-radius:4px;cursor:pointer;
  box-shadow:var(--ms-elev-1);
  transition:box-shadow 150ms, border-color 150ms;
}}
[data-theme="dark"] .doc-card{{background:#252423;border-color:#3B3A39;}}
.doc-card:hover{{border-color:var(--ms-blue);box-shadow:var(--ms-elev-2);}}
.doc-card:hover .doc-card-title{{text-decoration:underline;color:var(--ms-link-hover);}}
.doc-card-icon{{
  width:40px;height:40px;border-radius:4px;
  background:var(--ms-note-bg);
  display:flex;align-items:center;justify-content:center;flex-shrink:0;
}}
.doc-card-icon i{{width:20px;height:20px;color:var(--ms-blue)}}
.doc-card-content{{flex:1;min-width:0}}
.doc-card-title{{font-size:15px;font-weight:600;color:var(--ms-link);margin-bottom:4px;line-height:1.4;}}
.doc-card-desc{{font-size:13px;color:var(--ms-secondary);line-height:1.5}}
.doc-card-platforms{{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}}
.platform-tag{{
  font-size:11px;padding:2px 8px;
  background:var(--ms-surface-2);
  border:1px solid var(--ms-border);
  border-radius:2px;color:var(--ms-secondary);font-weight:600;
  text-transform:uppercase;letter-spacing:.02em;
}}
.doc-card-arrow{{
  flex-shrink:0;color:var(--ms-tertiary);width:18px;height:18px;margin-top:2px;
  transition:transform 150ms,color 150ms;
}}
.doc-card:hover .doc-card-arrow{{transform:translateX(4px);color:var(--ms-blue)}}

.platform-badges{{display:flex;gap:8px;flex-wrap:wrap;}}
.platform-badge{{
  display:inline-flex;align-items:center;padding:3px 10px;
  border-radius:2px;
  background:var(--ms-surface-2);
  border:1px solid var(--ms-border);
  font-size:12px;font-weight:600;
  color:var(--ms-secondary);text-transform:uppercase;letter-spacing:.02em;
}}

.doc-nav-buttons{{margin-bottom:16px}}
.doc-back-btn{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 14px 0 10px;
  border-radius:2px;
  background:transparent;border:1px solid transparent;
  font-size:13px;font-weight:400;
  color:var(--ms-link);cursor:pointer;
}}
.doc-back-btn:hover{{
  background:var(--ms-surface-2);text-decoration:underline;
}}
.doc-back-btn i{{width:14px;height:14px}}
[data-theme="dark"] .doc-back-btn{{background:transparent;border-color:#484644;color:#F3F2F1;}}
[data-theme="dark"] .doc-back-btn:hover{{background:#323130;}}

/* ===== Article header: Learn metadata ===== */
.doc-hdr{{
  margin-bottom:16px;
  padding-bottom:16px;
  border-bottom:1px solid var(--ms-border);
}}
.doc-title-row{{display:block;margin-bottom:8px}}
.doc-title{{
  font-family:var(--fd);
  font-size:32px;
  font-weight:600;color:var(--ms-heading);
  letter-spacing:-.02em;line-height:1.2;
  margin:0 0 8px;
}}
.doc-sub{{
  color:var(--ms-secondary);font-size:15px;margin:0;line-height:1.6;
}}
.doc-meta{{display:flex;align-items:center;gap:8px;margin:12px 0 4px;flex-wrap:wrap;font-size:13px;color:var(--ms-secondary);}}
.doc-acts{{display:flex;gap:8px;margin-top:12px;flex-wrap:wrap;}}
.dbtn{{
  display:inline-flex;align-items:center;gap:6px;
  height:32px;padding:0 12px;
  border-radius:2px;background:#FFFFFF;
  border:1px solid var(--ms-border-dark);
  font-family:var(--fb);font-size:13px;font-weight:400;
  color:var(--ms-text);cursor:pointer;
}}
.dbtn:hover{{background:var(--ms-surface-2);color:var(--ms-heading);}}
.dbtn.ok{{color:var(--ms-blue);border-color:var(--ms-blue)}}
.dbtn i{{width:14px;height:14px;}}
[data-theme="dark"] .dbtn{{background:transparent;border-color:#484644;color:#F3F2F1;}}
[data-theme="dark"] .dbtn:hover{{background:#323130;}}

/* ===== Article body: Learn typography ===== */
.md{{
  color:var(--ms-text);
  line-height:1.6;font-size:15px;
  font-weight:400;
}}
.md h1,.md h2,.md h3,.md h4{{
  font-family:var(--fd);
  color:var(--ms-heading);font-weight:600;
  letter-spacing:-.01em;scroll-margin-top:110px;line-height:1.3;
}}
.md h1{{font-size:28px;margin:32px 0 12px;padding-bottom:8px;border-bottom:1px solid var(--ms-border);}}
.md h2{{font-size:22px;margin:28px 0 10px;padding-bottom:6px;border-bottom:1px solid var(--ms-border);}}
.md h3{{font-size:18px;margin:24px 0 8px;font-weight:600}}
.md h4{{font-size:16px;margin:20px 0 8px;color:var(--ms-heading);font-weight:600}}
.md p{{margin:0 0 14px;color:var(--ms-text);max-width:75ch;}}
.md p strong,.md li strong{{color:var(--ms-heading)}}
.md a{{color:var(--ms-link);text-decoration:none;}}
.md a:hover{{color:var(--ms-link-hover);text-decoration:underline;text-underline-offset:2px;}}
.md ul,.md ol{{margin:0 0 14px 0;padding-left:24px;}}
.subheading{{color:var(--ms-secondary);font-size:15px;}}
.md code{{
  font-family:var(--fm);
  margin:0 1px;
  padding:1px 5px;
  font-size:87.5%;
  color:#A4262C;
  background-color:var(--ms-code-bg);
  white-space:break-spaces;
  border:1px solid var(--ms-border);border-radius:2px;
}}
[data-theme="dark"] .md code{{color:#CE9178;background-color:#292828;border-color:#3B3A39;}}
.md pre{{
  display:block;
  margin:0 0 16px;border-radius:4px;
  border:1px solid var(--ms-border);
  overflow:hidden;background:#F8F8F8;position:relative;
  padding:14px 16px;
}}
[data-theme="dark"] .md pre{{background:#252423;background-color:#252423!important;border-color:#3B3A39!important;}}
.md pre code{{
  display:block;padding:0;overflow-x:auto;
  line-height:1.5;background:transparent;border:none;border-radius:0;
  font-size:13.5px;color:var(--ms-text);tab-size:4;
  white-space:pre;
}}
.codehilite{{
  background:#F8F8F8!important;
  border:1px solid var(--ms-border)!important;
  border-radius:4px!important;
  overflow:hidden;margin:0 0 16px!important;padding:14px 16px!important;
  box-shadow:none!important;
}}
[data-theme="dark"] .codehilite{{background:#252423!important;}}
.codehilite pre{{margin:0!important;padding:0!important;background:transparent!important;border-radius:0!important;border:none!important;box-shadow:none!important}}
.copy-btn{{
  position:absolute;top:8px;right:10px;
  display:flex;align-items:center;gap:6px;
  height:28px;padding:0 10px;
  border-radius:2px;
  background:#FFFFFF;border:1px solid var(--ms-border-dark);
  font-family:var(--fb);font-size:12px;font-weight:400;
  color:var(--ms-text);cursor:pointer;
  opacity:0;
  transition:opacity 150ms;
}}
.md pre:hover .codehilite:hover .copy-btn,.md pre:hover .copy-btn,.codehilite:hover .copy-btn{{opacity:1}}
.copy-btn:hover{{background:var(--ms-surface-2);}}
.copy-btn.ok{{color:var(--ms-blue);border-color:var(--ms-blue);opacity:1}}
.copy-btn i{{width:13px;height:13px}}
[data-theme="dark"] .copy-btn{{background:#323130;border-color:#484644;color:#F3F2F1;}}

.md table{{
  width:100%;margin:0 0 24px;border-collapse:collapse;
  font-size:14px;background:#FFFFFF;
  border:1px solid var(--ms-border);
}}
[data-theme="dark"] .md table{{background:#1B1A19;}}
.md th{{
  padding:10px 14px;background:var(--ms-surface);font-weight:600;font-size:13px;
  text-align:left;color:var(--ms-heading);
  border:1px solid var(--ms-border);
  white-space:nowrap;
  vertical-align:bottom;
}}
[data-theme="dark"] .md th{{background-color:#252423;}}
[data-theme="dark"] .md th,[data-theme="dark"] .md td{{border-color:#3B3A39;}}
.md td{{padding:10px 14px;border:1px solid var(--ms-border);color:var(--ms-text);vertical-align:top;}}
.md tbody tr:hover td{{background-color:var(--ms-surface);}}
.md ul,.md ol{{margin:0 0 16px 24px}}
.md li{{margin:6px 0;color:var(--ms-text)}}
.md li strong{{color:var(--ms-heading)}}
.md blockquote{{
  margin:0 0 20px;padding:14px 18px;
  border-radius:4px;
  border:1px solid var(--ms-border);
  border-left:4px solid var(--ms-note-border);
  background:var(--ms-note-bg);
  position:relative;
}}
[data-theme="dark"] .md blockquote{{background:#082338!important;border-color:#3B3A39!important;border-left-color:#4CC2FF!important;}}
.md blockquote::before{{content:none}}
.md blockquote p{{color:var(--ms-text);margin:0 0 8px;font-size:14px;font-weight:400;line-height:1.6;}}
.md blockquote p:last-child{{margin-bottom:0}}
.md blockquote strong{{color:var(--ms-heading)}}
.md hr{{border:none;border-top:1px solid var(--ms-border);margin:32px 0}}
.md img{{max-width:100%;border-radius:4px;border:1px solid var(--ms-border);box-shadow:var(--ms-elev-1)}}

.mermaid-wrapper{{
  background:#FFFFFF;
  border-radius:4px;
  border:1px solid var(--ms-border);
  margin:24px 0;overflow:hidden;
  box-shadow:var(--ms-elev-1);
}}
[data-theme="dark"] .mermaid-wrapper{{background:#252423;}}
.mermaid-controls{{
  display:flex;justify-content:flex-end;gap:4px;
  padding:6px 8px;background:var(--ms-surface);
  border-bottom:1px solid var(--ms-border);
}}
.mermaid-export,.mermaid-open{{
  width:32px;height:32px;border-radius:2px;
  display:flex;align-items:center;justify-content:center;
  background:transparent;border:none;cursor:pointer;
  color:var(--ms-secondary);
}}
.mermaid-export:hover,.mermaid-open:hover{{background:var(--ms-surface-2);color:var(--ms-heading);}}
.mermaid-export i,.mermaid-open i{{width:16px;height:16px;}}
.mermaid{{padding:28px;text-align:center;min-height:120px;display:flex;align-items:center;justify-content:center}}
.mermaid svg{{max-width:100%;height:auto}}
.mermaid .nodeLabel,.mermaid .node label,.mermaid span.nodeLabel{{color:#201F1E!important;}}
.mermaid .edgeLabel,.mermaid .edgeLabel span,.mermaid .label{{color:#201F1E!important;background-color:#FFFFFF!important;}}
.mermaid .cluster-label,.mermaid .cluster span{{color:#201F1E!important;}}
.mermaid text.actor{{fill:#201F1E!important;}}
[data-theme="dark"] .mermaid .nodeLabel,[data-theme="dark"] .mermaid .node label,[data-theme="dark"] .mermaid span.nodeLabel{{color:#F3F2F1!important;}}
[data-theme="dark"] .mermaid .edgeLabel,[data-theme="dark"] .mermaid .edgeLabel span,[data-theme="dark"] .mermaid .label{{color:#F3F2F1!important;background-color:#1B1A19!important;}}
[data-theme="dark"] .mermaid .cluster-label,[data-theme="dark"] .mermaid .cluster span{{color:#F3F2F1!important;}}
[data-theme="dark"] .mermaid text.actor{{fill:#F3F2F1!important;}}
.mermaid .actor text,.mermaid text.messageText,.mermaid .noteText,.mermaid text.noteText,.mermaid .loopText,.mermaid text.loopText,.mermaid .labelBox text{{fill:#201F1E!important;}}
[data-theme="dark"] .mermaid .actor text,[data-theme="dark"] .mermaid text.messageText,[data-theme="dark"] .mermaid .noteText,[data-theme="dark"] .mermaid text.noteText,[data-theme="dark"] .mermaid .loopText,[data-theme="dark"] .mermaid text.loopText,[data-theme="dark"] .mermaid .labelBox text{{fill:#F3F2F1!important;}}
@font-face{{
  font-family:'AromaMaterialIcons';
  src:url('material-icons.woff2') format('woff2');
  font-display:swap;
}}
.icon-search-wrap{{
  display:flex;align-items:center;gap:10px;
  border:1px solid var(--ms-border-dark);border-radius:2px;
  padding:0 12px;height:36px;background:#FFFFFF;
  margin:16px 0 8px;max-width:520px;
}}
[data-theme="dark"] .icon-search-wrap{{background:#292828;}}
.icon-search-wrap svg{{width:16px;height:16px;color:var(--ms-secondary);flex-shrink:0}}
.icon-search{{flex:1;min-width:0;height:100%;background:none;border:none;outline:none;font-family:var(--fb);font-size:14px;color:var(--ms-text);}}
.icon-search::placeholder{{color:var(--ms-secondary);}}
.icon-count{{font-size:12px;color:var(--ms-secondary);margin-bottom:4px;}}
.icon-grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:8px;margin:12px 0 32px;}}
.icon-tile{{
  border:1px solid var(--ms-border);border-radius:4px;
  padding:14px 6px 10px;cursor:pointer;text-align:center;
  background:#FFFFFF;transition:border-color 150ms, box-shadow 150ms;
}}
[data-theme="dark"] .icon-tile{{background:#252423;}}
.icon-tile:hover{{border-color:var(--ms-blue);box-shadow:var(--ms-elev-1);}}
.icon-tile.copied{{border-color:var(--ms-blue);background:var(--ms-note-bg);}}
.icon-glyph{{display:block;font-family:'AromaMaterialIcons';font-size:32px;line-height:1.2;color:var(--ms-heading);}}
.icon-name{{display:block;font-family:var(--fm);font-size:11px;color:var(--ms-secondary);margin-top:6px;word-break:break-word;}}
.icon-code{{display:block;font-family:var(--fm);font-size:10px;color:var(--ms-tertiary);margin-top:2px;}}
.icon-toast{{
  position:fixed;bottom:24px;left:50%;transform:translateX(-50%) translateY(8px);
  background:#323130;color:#FFFFFF;font-size:13px;
  padding:10px 18px;border-radius:2px;z-index:400;opacity:0;pointer-events:none;
  transition:opacity 150ms,transform 150ms;
  box-shadow:var(--ms-elev-2);
}}
.icon-toast.show{{opacity:1;transform:translateX(-50%) translateY(0);}}
.doc-ver{{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;padding:2px 10px;border-radius:2px;background:var(--ms-note-bg);color:var(--ms-blue-dark);border:1px solid var(--ms-border);white-space:nowrap;}}
[data-theme="dark"] .doc-ver{{color:#8ED3FF;}}
.doc-updated{{font-size:12px;color:var(--ms-secondary);white-space:nowrap;}}
.doc-status{{display:inline-flex;align-items:center;font-size:11px;font-weight:600;letter-spacing:.04em;text-transform:uppercase;padding:2px 10px;border-radius:2px;white-space:nowrap;border:1px solid var(--ms-border);}}
.doc-status.new{{background:#DFF6DD;color:#0B6A0B;border-color:#0B6A0B;}}
.doc-status.updated{{background:#EFF6FC;color:#005A9E;border-color:#0078D4;}}
.doc-status.beta{{background:#FFF4CE;color:#7A6200;border-color:#C19C00;}}
.doc-status.experimental{{background:#F2EBFA;color:#5C2D91;border-color:#5C2D91;}}
.doc-status.stable{{background:var(--ms-surface-2);color:var(--ms-secondary);}}
[data-theme="dark"] .doc-status.new{{background:#0B3D0B;color:#7FBA00;}}
[data-theme="dark"] .doc-status.updated{{background:#082338;color:#8ED3FF;}}
[data-theme="dark"] .doc-status.beta{{background:#3A2E0A;color:#FDD663;}}
[data-theme="dark"] .doc-status.experimental{{background:#2A2356;color:#B39DDB;}}
.top-ver{{font-size:11px;font-weight:600;color:var(--ms-blue-dark);background:var(--ms-note-bg);border:1px solid var(--ms-border);border-radius:10px;padding:2px 8px;white-space:nowrap;margin-left:8px;}}
[data-theme="dark"] .top-ver{{color:#8ED3FF;}}

.doc-pn-nav{{
  margin-top:40px;
  padding-top:24px;
  border-top:1px solid var(--ms-border);
  display:grid;grid-template-columns:1fr 1fr;gap:16px;
}}
.doc-pn-nav:empty{{display:none;border-top:none;margin-top:0;padding-top:0}}
.doc-pn-link{{
  display:flex;align-items:center;gap:14px;
  padding:14px 18px;
  border:1px solid var(--ms-border);
  border-radius:4px;
  cursor:pointer;text-decoration:none;
  min-width:0;background:#FFFFFF;
  box-shadow:var(--ms-elev-1);
}}
[data-theme="dark"] .doc-pn-link{{background:#252423;border-color:#3B3A39;}}
.doc-pn-link:hover{{border-color:var(--ms-blue);box-shadow:var(--ms-elev-2);}}
.doc-pn-link.next{{grid-column:2;flex-direction:row-reverse;text-align:right}}
.doc-pn-link i{{width:18px;height:18px;color:var(--ms-secondary);flex-shrink:0}}
.doc-pn-text{{min-width:0;display:flex;flex-direction:column;gap:4px}}
.doc-pn-label{{font-size:12px;font-weight:600;color:var(--ms-secondary);text-transform:uppercase;letter-spacing:.04em}}
.doc-pn-title{{
  font-size:14px;font-weight:600;color:var(--ms-link);
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
}}
@media(max-width:600px){{
  .doc-pn-nav{{grid-template-columns:1fr}}
  .doc-pn-link.next{{grid-column:1}}
}}

/* Footer + language picker (flagship) */
.ms-footer{{
  background:var(--ms-surface);
  border-top:1px solid var(--ms-border);
  margin-top:0;
}}
[data-theme="dark"] .ms-footer{{background:#252423;border-top-color:#3B3A39;}}
.ms-footer-inner{{
  max-width:1280px;margin:0 auto;padding:24px 40px 16px;
}}
.ms-footer-links{{
  display:flex;gap:24px;flex-wrap:wrap;
  font-size:13px;margin-bottom:12px;
}}
.ms-footer-links a{{color:var(--ms-secondary);text-decoration:none;cursor:pointer;}}
.ms-footer-links a:hover{{color:var(--ms-link);text-decoration:underline;}}
.ms-footer-bottom{{
  display:flex;align-items:center;gap:16px;flex-wrap:wrap;
  font-size:12px;color:var(--ms-secondary);
  padding-top:12px;border-top:1px solid var(--ms-border);
}}
.ms-footer-bottom .ms-globe{{display:inline-flex;align-items:center;gap:6px;}}
.lang-picker{{position:relative;}}
.lang-btn{{
  display:inline-flex;align-items:center;gap:6px;
  background:none;border:none;cursor:pointer;
  font-family:var(--fb);font-size:12px;color:var(--ms-secondary);
  padding:4px 6px;border-radius:2px;
}}
.lang-btn:hover{{color:var(--ms-link);background:var(--ms-surface-2);}}
.lang-btn i,.lang-btn svg{{width:14px;height:14px;}}
.lang-menu{{
  position:absolute;bottom:calc(100% + 6px);left:0;
  min-width:180px;max-height:280px;overflow-y:auto;
  background:#FFFFFF;
  border:1px solid var(--ms-border);
  border-radius:4px;
  box-shadow:var(--md-elev-2);
  z-index:300;display:none;
  padding:4px 0;
}}
[data-theme="dark"] .lang-menu{{background:#252423;}}
.lang-menu.open{{display:block}}
.lang-menu-item{{
  display:flex;align-items:center;gap:10px;
  padding:9px 14px;font-size:13px;
  color:var(--ms-text);cursor:pointer;
}}
.lang-menu-item:hover{{background:var(--ms-surface-2);}}
.lang-menu-item.active{{color:var(--ms-heading);font-weight:600;}}
.lang-menu-item span{{flex:1;white-space:nowrap;}}
.lang-menu-item svg{{width:15px;height:15px;color:var(--ms-blue);opacity:0;flex-shrink:0;}}
.lang-menu-item.active svg{{opacity:1;}}
.ms-copy{{margin-left:auto;}}

  flex:1;height:30px;border-radius:15px;border:1px solid var(--ms-border-dark);
  background:transparent;color:var(--ms-secondary);font-size:12px;font-weight:600;
  cursor:pointer;font-family:var(--fb);
}}
  height:30px;border:1px solid var(--ms-border-dark);border-radius:2px;
  background:var(--md-surface);color:var(--ms-text);font-size:12px;font-family:var(--fb);
  padding:0 6px;max-width:190px;
}}

.pf-wrap{{position:relative}}
.pf-chip{{
  display:inline-flex;align-items:center;gap:8px;
  height:32px;padding:0 12px;
  border-radius:2px;
  background:#FFFFFF;
  border:1px solid var(--ms-border-dark);
  font-family:var(--fb);font-size:13px;font-weight:400;
  color:var(--ms-text);cursor:pointer;
  transition:background 120ms,border-color 120ms;
}}
.pf-chip:hover{{background:var(--ms-surface-2);}}
.pf-chip.open{{background:var(--ms-surface-2);border-color:var(--ms-blue);color:var(--ms-heading);}}
[data-theme="dark"] .pf-chip{{background-color:#292828;}}
.pf-chip i{{width:14px;height:14px;}}
.pf-chev{{width:14px;height:14px;transition:transform 150ms}}
.pf-chip.open .pf-chev{{transform:rotate(180deg)}}
.pf-menu{{
  position:absolute;top:calc(100% + 4px);right:0;left:auto;
  background:#FFFFFF;
  border:1px solid var(--ms-border);
  border-radius:4px;
  box-shadow:var(--md-elev-2);
  z-index:300;overflow:hidden;display:none;
  min-width:200px;
}}
[data-theme="dark"] .pf-menu{{background:#252423;}}
.pf-menu.open{{display:block}}
.pf-menu-item{{
  display:flex;align-items:center;gap:12px;
  padding:10px 14px;font-size:13px;
  color:var(--ms-text);cursor:pointer;
  transition:background 120ms;
}}
.pf-menu-item:hover{{background:var(--ms-surface-2);}}
.pf-menu-item.active{{color:var(--ms-heading);font-weight:600;}}
.pf-menu-item-label{{flex:1}}
.pf-menu-check{{
  width:16px;height:16px;color:var(--ms-blue);
  opacity:0;transition:opacity 100ms;
}}
.pf-menu-item.active .pf-menu-check{{opacity:1}}
.pf-menu-divider{{height:1px;background:var(--ms-border);margin:6px 0}}

.sb-scrim{{display:none;position:fixed;inset:0;background:rgba(0,0,0,.32);z-index:40}}
.mob-btn{{
  display:none;align-items:center;justify-content:center;
  width:36px;height:36px;border-radius:2px;
  border:none;background:transparent;
  color:var(--ms-text);cursor:pointer;
}}
.mob-btn:hover{{background:var(--ms-surface-2);}}
.mob-btn i{{width:20px;height:20px;}}

@media(max-width:1160px){{.toc-panel{{display:none!important}}}}
@media(max-width:900px){{
  .ionic-nav{{display:none;}}
  .top-bar-center{{justify-content:flex-start;}}
  .search-bar{{max-width:none;}}
}}
@media(max-width:760px){{
  :root{{--top-bar-h: 96px;}}
  .nav-drawer{{
    position:fixed;left:0;top:var(--top-bar-h);bottom:0;
    transform:translateX(-100%);
    transition:transform 200ms ease;
    z-index:50;box-shadow:var(--md-elev-3);
    width:300px;
  }}
  body.has-announce .nav-drawer{{top:calc(var(--top-bar-h) + 38px)}}
  .nav-drawer.open{{transform:translateX(0)}}
  .sb-scrim.open{{display:block}}
  .mob-btn{{display:flex!important}}
  .c-inner{{padding:24px 20px 48px}}
  .c-inner.wide{{padding:0 0 0}}
  .home-hero{{margin:0;}}
  .home-title{{font-size:32px;}}
  .ms-header-top{{padding:0 12px;}}
  .ms-breadcrumb-bar{{padding:0 12px;}}
  .category-page,.subcategory-page{{padding:24px 20px;}}
  .ms-footer-inner{{padding:20px;}}
  .top-ver{{display:none;}}
}}
{pygments_styles}

</style>
</head>
<body>
{announce_html}
<header class="top-app-bar" id="topAppBar">
  <div class="ms-header-top">
    <button class="mob-btn" onclick="openDrawer()" style="margin-right:4px;flex-shrink:0" aria-label="Open navigation">
      <i data-lucide="menu"></i>
    </button>
    <div class="ms-logo" onclick="showFirstPage()" title="Home" aria-label="Home">
      <img src="logo.png" alt="Home">
    </div>
    <span class="ms-logo-sep" aria-hidden="true"></span>
    <span class="ms-learn-brand" onclick="showFirstPage()">Learn</span>
    <div class="tab-logo-name" onclick="showFirstPage()" title="{project_name} home">{project_name}<span class="top-ver" title="Documentation version">v{docs_version}</span></div>
    <nav class="ionic-nav" aria-label="Primary">
      <a href="sandbox.html" target="_blank" rel="noopener">Sandbox</a>
      <a onclick="window.open('https://github.com/BinaryInkTN/AromaUI','_blank')">GitHub</a>
    </nav>
    <div class="top-bar-center">
      <div class="search-bar" id="searchBar">
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="11" cy="11" r="8"/><path d="m21 21-4.35-4.35"/>
        </svg>
        <input class="search-bar-input" id="searchInput" type="text"
               placeholder="Search" data-i18n="searchPh"
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
            <span><kbd>↑↓</kbd> <span data-i18n="hintNav">to navigate</span></span>
            <span><kbd>↵</kbd> <span data-i18n="hintOpen">to open</span></span>
            <span><kbd>Esc</kbd> <span data-i18n="hintClose">to close</span></span>
          </div>
        </div>
      </div>
    </div>
    <div class="top-bar-trailing">
      <div class="pf-wrap" id="pfWrap">
        <button class="pf-chip" id="pfChip" onclick="togglePfMenu()" title="Filter by platform">
          <i data-lucide="filter"></i>
          <span id="pfChipTxt">All platforms</span>
          <i data-lucide="chevron-down" class="pf-chev"></i>
        </button>
        <div class="pf-menu" id="pfMenu"></div>
      </div>
      <div class="theme-toggle" role="group" aria-label="Theme">
        <button id="lightBtn" onclick="setTheme('light')" title="Light theme">
          <i data-lucide="sun"></i>
        </button>
        <button id="darkBtn" onclick="setTheme('dark')" title="Dark theme">
          <i data-lucide="moon"></i>
        </button>
      </div>
    </div>
  </div>
  <div class="ms-breadcrumb-bar">
    <div class="nav-crumbs" id="bcrumb" aria-label="Breadcrumb">
      <span class="bc-seg" onclick="showFirstPage()">{project_name}</span>
      <span class="bc-sep" id="bcHomeSep" style="display:none">/</span>
      <span class="bc-seg" id="bcCategory" style="display:none" onclick="showCategoryFromBc()"></span>
      <span class="bc-sep" id="bcCatSep" style="display:none">/</span>
      <span class="bc-seg" id="bcSubcategory" style="display:none" onclick="showSubcategoryFromBc()"></span>
      <span class="bc-sep" id="bcSubSep" style="display:none">/</span>
      <span class="bc-seg cur" id="bcCur"></span>
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
                <span data-i18n="back">Back</span>
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
                  <span data-i18n="copyLink">Copy link</span>
                </button>
              </div>
            </div>
            <div class="md" id="docContent"></div>
            <div class="doc-pn-nav" id="docPnNav"></div>
          </div>
        </div>
        <footer class="ms-footer" aria-label="Footer">
          <div class="ms-footer-inner">
            <div class="ms-footer-bottom">
              <span class="ms-copy">© 2026 {project_name}</span>
            </div>
          </div>
        </footer>
      </div>

      <div class="toc-panel" id="tocPanel">
        <div class="toc-label" data-i18n="toc">In this article</div>
        <div class="toc-progress"><div class="toc-fill" id="tocFill"></div></div>
        <div id="tocList"></div>
        {author_html}
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
const SEARCH_INDEX = {search_index_json};
const PAGE_ICONS = {page_icons_js};
const SLUG_TO_ID = {slug_to_id_json};
const ID_TO_SLUG = {id_to_slug_json};
const PAGE_ORDER = {page_order_json};
const FIRST_PAGE_ID = '{first_page_id}';
const DOCS_VERSION = '{docs_version}';
const PAGE_VERSIONS = {page_versions_json};
const PAGE_UPDATED = {page_updated_json};
const PAGE_STATUS = {page_status_json};
/* Docs AI assistant: retrieval over embedded pages + LLM. */
let currentId=null, currentCategory=null, currentSubcategory=null;
let tocSections=[], activePF='all';
let previousView={{type:'first',category:null,subcategory:null,id:null}};
let searchIdx=-1, searchResults=[];

const ic = () => typeof lucide!=='undefined' && lucide.createIcons();

function syncSandboxTheme(t){{
  if(t!=='light'&&t!=='dark') return;
  document.querySelectorAll('.home-live-demo iframe, .demo-frame iframe').forEach(function(f){{
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

function updateURL() {{
  const path = window.location.pathname;
  const dir = path.substring(0, path.lastIndexOf('/') + 1);
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

  const newUrl = dir + 'index.html' + hash;
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
  const _ver=(typeof PAGE_VERSIONS!=='undefined'&&PAGE_VERSIONS[id])||DOCS_VERSION||'';
  if(_ver){{
    const v=document.createElement('span');
    v.className='doc-ver';
    v.textContent='v'+String(_ver).replace(/^v/,'');
    v.title='Document version';
    meta.appendChild(v);
  }}
  const _st=(typeof PAGE_STATUS!=='undefined'&&PAGE_STATUS[id])||'';
  if(_st){{
    const s=document.createElement('span');
    s.className='doc-status '+String(_st).toLowerCase();
    s.textContent=_st;
    meta.appendChild(s);
  }}
  const _up=(typeof PAGE_UPDATED!=='undefined'&&PAGE_UPDATED[id])||'';
  if(_up){{
    const u=document.createElement('span');
    u.className='doc-updated';
    u.textContent='Updated '+_up;
    meta.appendChild(u);
  }}

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
  setTimeout(()=>{{addCopyBtns();initMermaid(localStorage.getItem('docs-theme')||'light');ic();buildToc();if(document.getElementById('iconGrid')) filterIcons('');}},60);
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
    item.onclick=()=>{{document.getElementById('cScroll').scrollTop=h.offsetTop-64;}};
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

function filterIcons(q){{
  const query=(q||'').toLowerCase().trim().split(/\s+/).filter(Boolean);
  const grid=document.getElementById('iconGrid');
  if(!grid) return;
  let shown=0;
  const tiles=grid.children;
  for(let i=0;i<tiles.length;i++){{
    const name=tiles[i].getAttribute('data-name')||'';
    const hit=query.every(tok=>name.indexOf(tok)>=0);
    tiles[i].style.display=hit?'':'none';
    if(hit) shown++;
  }}
  const c=document.getElementById('iconCount');
  if(c) c.textContent=shown+' of '+tiles.length+' icons';
}}

function copyIconName(name,el){{
  const done=()=>{{
    if(el){{el.classList.add('copied');setTimeout(()=>el.classList.remove('copied'),1200);}}
    let toast=document.getElementById('iconToast');
    if(!toast){{toast=document.createElement('div');toast.className='icon-toast';toast.id='iconToast';document.body.appendChild(toast);}}
    toast.textContent=name+' copied';
    toast.classList.add('show');
    clearTimeout(toast._t);
    toast._t=setTimeout(()=>toast.classList.remove('show'),1400);
  }};
  if(navigator.clipboard&&navigator.clipboard.writeText){{
    navigator.clipboard.writeText(name).then(done).catch(done);
  }}else{{
    const ta=document.createElement('textarea');
    ta.value=name;document.body.appendChild(ta);ta.select();
    try{{document.execCommand('copy');}}catch(e){{}}
    document.body.removeChild(ta);done();
  }}
}}

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
  // Fills are light in light mode / dark in dark mode so they contrast
  // with the docs text color (dark-on-light, light-on-dark). This also
  // matches the PDF graphviz palette (#E6F4FF fills). Do NOT use white
  // text in light mode: htmlLabels render as HTML and follow CSS text.
  const txt=d?'#E8EAED':'#1C1B1F';
  const bg=d?'#1D1D1F':'#FFFFFF';
  return{{theme:'base',themeVariables:{{
    background: bg,
    primaryColor: d?'#082338':'#E6F4FF',
    primaryTextColor: txt,
    primaryBorderColor: d?'#4CC2FF':'#005A9E',
    lineColor: d?'#9AA0A6':'#5C5C5C',
    secondaryColor: d?'#2C2C2E':'#F2F8FF',
    secondaryTextColor: txt,
    tertiaryColor: d?'#2C3A4A':'#EFF6FC',
    tertiaryTextColor: txt,
    textColor: txt,
    titleColor: txt,
    nodeTextColor: txt,
    edgeLabelBackground: bg,
    labelTextColor: txt,
    loopTextColor: txt,
    actorTextColor: txt,
    signalTextColor: txt,
    signalColor: d?'#E8EAED':'#1C1B1F',
    actorBorder: d?'#4CC2FF':'#005A9E',
    actorBkg: d?'#082338':'#E6F4FF',
    actorLineColor: d?'#9AA0A6':'#5C5C5C',
    activationBkgColor: d?'#1E3A5F':'#C7E0F4',
    activationBorderColor: d?'#4CC2FF':'#005A9E',
    labelBoxBkgColor: bg,
    labelBoxBorderColor: d?'#4CC2FF':'#005A9E',
    sequenceNumberColor: txt,
    labelBorder: d?'#4CC2FF':'#005A9E',
    labelBkg: bg,
    noteBkgColor: d?'#3A2E0A':'#FEF7E0',
    noteBorderColor: d?'#FDD663':'#B06000',
    noteTextColor: txt,
    taskTextColor: txt,
    clusterBkg: d?'#252528':'#F2F8FF',
    clusterBorder: d?'#48484A':'#C7E0F4',
    fontFamily:"'Segoe UI',system-ui,sans-serif",fontSize:'14px',
  }},startOnLoad:false,securityLevel:'loose',logLevel:'error',
  flowchart:{{useMaxWidth:true,htmlLabels:true,curve:'basis'}}}};
}}

// Initialize synchronously at parse time (not on page navigation): the
// mermaid bundle auto-renders on window `load` with whatever config is
// active, and on fast loads that beats the per-page init below. Seeding
// the themed config here guarantees every render - auto or manual -
// uses the docs palette instead of mermaid defaults.
try{{
  if(typeof mermaid!=='undefined') mermaid.initialize(mCfg(localStorage.getItem('docs-theme')||'light'));
}}catch(e){{}}

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
                "version": s.get("version", ""),
                "updated": s.get("updated", ""),
                "status": s.get("status", ""),
            }





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

        _browse_cols = []
        for _cat in categories:
            _cname = _cat.get("name", "General")
            _csects = sidebar_sections.get(_cname, {})
            _flat = []
            for _sub, _items in _csects.items():
                for _p in _items:
                    _flat.append((_p["id"], _p["title"], _sub))
            if not _flat:
                continue
            _lis = []
            _last_sub = None
            for _pid, _ptitle, _psub in _flat:
                if _psub and _psub != _last_sub:
                    _lis.append(
                        f"<li class=\"learn-browse-sub\">"
                        f"{_htmlesc.escape(_psub, quote=False)}</li>"
                    )
                    _last_sub = _psub
                _sub_arg = f"'{_htmlesc.escape(_psub, quote=True)}'" if _psub else "null"
                _lis.append(
                    f"<li><a onclick=\"showPage('{_pid}', "
                    f"'{_htmlesc.escape(_cname, quote=True)}', {_sub_arg})\">"
                    f"{_htmlesc.escape(_ptitle, quote=False)}</a></li>"
                )
            _browse_cols.append(
                f"<div class=\"learn-browse-col\">"
                f"<h3><a onclick=\"showCategory("
                f"'{_htmlesc.escape(_cname, quote=True)}')\">"
                f"{_htmlesc.escape(_cname, quote=False)}</a></h3>"
                f"<ul>{''.join(_lis)}</ul></div>"
            )
        browse_html = (
            "<div class=\"learn-browse-zone\">"
            "<h2 class=\"section-heading\">Browse the documentation</h2>"
            "<div class=\"learn-browse-grid\">" + "".join(_browse_cols) + "</div></div>"
            if _browse_cols else ""
        )

        welcome_html = self._welcome_page_html(
            project_name,
            description,
            hero=config.get("hero", {}),
            version=project_version,
            browse_html=browse_html,
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



                for page in sub_items:
                    sb.append(
                        f"<div class='nav-dest sub' data-page='{page['id']}' onclick=\"showPage('{page['id']}', '{cname}', '{sub_name}')\">"
                        f"<span class='nav-dest-text'>{page['title']}</span></div>"
                    )
                    page_order.append(page["id"])
                sb.append("</div>")

            sb.append("</div>")




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


        default_updated = datetime.now().strftime("%Y-%m-%d")
        page_versions = {
            sid: (page_objects[sid].get("version") or project_version or "")
            for sid in page_objects
        }
        page_updated = {
            sid: (page_objects[sid].get("updated") or default_updated)
            for sid in page_objects
        }
        page_status = {
            sid: (page_objects[sid].get("status") or "")
            for sid in page_objects
        }

        author = config.get("author", {}) or {}
        author_name = str(author.get("name", "Yassine Ahmed Ali"))
        author_role = str(author.get("role", "Maintainer"))
        author_initials = "".join(
            w[0] for w in author_name.split() if w
        )[:2].upper() or "A"
        author_html = (
            '<div class="author-card">'
            '<div class="author-label">Author</div>'
            '<div class="author-row">'
            f'<div class="author-avatar">{_htmlesc.escape(author_initials)}</div>'
            '<div>'
            f'<div class="author-name">{_htmlesc.escape(author_name)}</div>'
            f'<div class="author-role">{_htmlesc.escape(author_role)}</div>'
            "</div></div></div>"
        )

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
            author_html=author_html,
            docs_version=_htmlesc.escape(str(project_version)),
            page_versions_json=json.dumps(page_versions),
            page_updated_json=json.dumps(page_updated),
            page_status_json=json.dumps(page_status),
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