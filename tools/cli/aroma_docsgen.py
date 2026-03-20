#!/usr/bin/env python3

import argparse
import json
import os
import re
import tempfile
from datetime import datetime
from typing import Dict, List, Optional
import hashlib

from bs4 import BeautifulSoup
import markdown
import yaml
from markdown.extensions import Extension
from markdown.preprocessors import Preprocessor
from pygments.formatters import HtmlFormatter
from weasyprint import HTML


_DOT_RESERVED = frozenset({
    'node', 'edge', 'graph', 'digraph', 'subgraph', 'strict',
    'true', 'false', 'null',
})


def _dot_id(raw: str) -> str:
    s = re.sub(r'[^A-Za-z0-9_]', '_', raw.strip())
    s = re.sub(r'_+', '_', s).strip('_') or 'n'
    if s[0].isdigit():
        s = 'n_' + s
    if s.lower() in _DOT_RESERVED:
        s = 'n_' + s
    return s


def _dot_label(text: str) -> str:
    text = text.replace('\\', '\\\\')
    text = text.replace('"',  '\\"')
    text = text.replace('\n', '\\n')
    text = text.replace('\r', '')
    return text


def _parse_node_decl(raw: str):
    raw = raw.strip()
    patterns = [
        (r'([A-Za-z0-9_]+)\(\[(.+?)\]\)',  'shape=rectangle style="rounded,filled"'),
        (r'([A-Za-z0-9_]+)\[\[(.+?)\]\]',  'shape=rectangle'),
        (r'([A-Za-z0-9_]+)\[(.+?)\]',      'shape=rectangle'),
        (r'([A-Za-z0-9_]+)\(\((.+?)\)\)',   'shape=ellipse'),
        (r'([A-Za-z0-9_]+)\((.+?)\)',       'shape=rectangle style=rounded'),
        (r'([A-Za-z0-9_]+)\{(.+?)\}',      'shape=diamond'),
        (r'([A-Za-z0-9_]+)>(.+?)\]',       'shape=trapezium'),
    ]
    for pattern, shape in patterns:
        m = re.match(pattern, raw)
        if m:
            return _dot_id(m.group(1)), m.group(2).strip(), shape
    m = re.match(r'^([A-Za-z0-9_]+)$', raw)
    if m:
        return _dot_id(m.group(1)), '', ''
    nid = _dot_id(raw)
    return nid, raw, ''


def _node_attr_str(label: str, shape: str, base: str) -> str:
    parts = [f'label="{_dot_label(label)}"']
    if shape:
        parts.append(shape)
    parts.append(base)
    return ' '.join(p for p in parts if p)


def _parse_subgraph_header(line: str):
    rest = re.match(r'subgraph\s*(.*)', line, re.I).group(1).strip()
    m = re.match(r'^([A-Za-z0-9_]+)\["?([^"\]]+)"?\]\s*$', rest)
    if m:
        return _dot_id(m.group(1)), m.group(2).strip()
    m = re.match(r'^([A-Za-z0-9_]+)\[([^\]]+)\]\s*$', rest)
    if m:
        return _dot_id(m.group(1)), m.group(2).strip()
    if rest:
        return _dot_id(rest), rest.strip('"')
    return 'sg', 'subgraph'


def _mermaid_flowchart_to_dot(lines: List[str]) -> str:
    first = lines[0].strip()
    m = re.match(r'(?:graph|flowchart)\s+(\w+)', first, re.I)
    direction = 'TB'
    if m:
        d = m.group(1).upper()
        direction = {'TD': 'TB', 'TB': 'TB', 'LR': 'LR', 'RL': 'RL', 'BT': 'BT'}.get(d, 'TB')

    base_node = ('fontname="Helvetica" fontsize=12 style=filled '
                 'fillcolor="#f0f4ff" color="#4a6fa5"')
    base_edge = 'fontname="Helvetica" fontsize=10 color="#555555"'

    id_map: Dict[str, str] = {}
    node_attrs: Dict[str, str] = {}
    edges: List[str] = []
    sections: List[List[str]] = [[]]
    sg_depth = 0

    def current() -> List[str]:
        return sections[-1]

    def resolve_id(raw_token: str) -> str:
        bare = re.match(r'^([A-Za-z0-9_]+)', raw_token.strip())
        key  = bare.group(1) if bare else raw_token.strip()
        if key not in id_map:
            id_map[key] = _dot_id(key)
        return id_map[key]

    def add_node(raw_token: str, label: str, shape: str) -> str:
        dot_id = resolve_id(raw_token)
        if dot_id not in node_attrs:
            lbl = label if label else dot_id
            node_attrs[dot_id] = _node_attr_str(lbl, shape, base_node)
        return dot_id

    def make_edge(sid: str, did: str, label: str = '', directed: bool = True) -> str:
        attrs = base_edge
        if label:
            attrs += f' label="{_dot_label(label)}"'
        if not directed:
            attrs += ' dir=none'
        return f'    {sid} -> {did} [{attrs}]'

    def handle_node_token(raw_token: str) -> str:
        nid, nlbl, nshp = _parse_node_decl(raw_token)
        bare = re.match(r'^([A-Za-z0-9_]+)', raw_token.strip())
        key  = bare.group(1) if bare else raw_token.strip()
        if key not in id_map:
            id_map[key] = nid
        dot_id = id_map[key]
        if dot_id not in node_attrs:
            lbl = nlbl if nlbl else dot_id
            node_attrs[dot_id] = _node_attr_str(lbl, nshp, base_node)
        return dot_id

    for raw_line in lines[1:]:
        line = raw_line.strip()
        if not line or line.startswith('%%') or line.startswith('%{'):
            continue
        lo = line.lower()
        if lo.startswith(('classdef ', 'class ', 'style ', 'linkstyle ')):
            continue
        if lo.startswith('subgraph'):
            sg_depth += 1
            sg_id, sg_label = _parse_subgraph_header(line)
            cluster_id = f'cluster_{sg_id}_{sg_depth}'
            sections.append([
                f'subgraph {cluster_id} {{',
                f'  label="{_dot_label(sg_label)}"',
                f'  style=filled fillcolor="#f8f9ff" color="#aab4cc"',
            ])
            continue
        if lo == 'end' and len(sections) > 1:
            finished = sections.pop()
            finished.append('}')
            indent = '  ' * (len(sections))
            for sub_line in finished:
                sections[-1].append(indent + sub_line)
            continue

        m = re.match(r'(.+?)\s*-+>+\s*\|([^|]*)\|\s*(.+)', line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            lbl = m.group(2).strip()
            did = handle_node_token(m.group(3).strip())
            edges.append(make_edge(sid, did, lbl))
            continue
        m = re.match(r'(.+?)\s*--([^->|]+?)-->\s*(.+)', line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            lbl = m.group(2).strip()
            did = handle_node_token(m.group(3).strip())
            edges.append(make_edge(sid, did, lbl))
            continue
        m = re.match(r'(.+?)\s*-{2,}>+\s*(.+)', line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            did = handle_node_token(m.group(2).strip())
            edges.append(make_edge(sid, did))
            continue
        m = re.match(r'(.+?)\s*-{3,}\s*(.+)', line)
        if m:
            sid = handle_node_token(m.group(1).strip())
            did = handle_node_token(m.group(2).strip())
            edges.append(make_edge(sid, did, directed=False))
            continue

        nid, nlbl, nshp = _parse_node_decl(line)
        if nlbl or (nid and len(sections) > 1):
            bare = re.match(r'^([A-Za-z0-9_]+)', line.strip())
            key  = bare.group(1) if bare else line.strip()
            if key not in id_map:
                id_map[key] = nid
            dot_id = id_map[key]
            if dot_id not in node_attrs:
                lbl = nlbl if nlbl else dot_id
                node_attrs[dot_id] = _node_attr_str(lbl, nshp, base_node)
            if len(sections) > 1:
                current().append(f'  {dot_id}')

    while len(sections) > 1:
        finished = sections.pop()
        finished.append('}')
        indent = '  ' * (len(sections))
        for sub_line in finished:
            sections[-1].append(indent + sub_line)

    parts = [
        'digraph G {',
        f'  rankdir={direction}',
        '  graph [fontname="Helvetica" bgcolor=white]',
        '  node  [fontname="Helvetica" fontsize=12 style=filled '
        'fillcolor="#f0f4ff" color="#4a6fa5"]',
        '  edge  [fontname="Helvetica" fontsize=10 color="#555555"]',
    ]
    for dot_id, attrs in node_attrs.items():
        parts.append(f'  {dot_id} [{attrs}]')
    parts.extend(f'  {l}' for l in sections[0])
    parts.extend(edges)
    parts.append('}')
    return '\n'.join(parts)


def _mermaid_sequence_to_dot(lines: List[str]) -> str:
    actors: List[tuple] = []
    actor_ids: Dict[str, str] = {}
    messages: List[tuple] = []

    for line in lines[1:]:
        line = line.strip()
        if not line or line.startswith('%%'):
            continue
        lo = line.lower()
        if lo.startswith(('note ', 'loop', 'alt', 'opt', 'else', 'end',
                           'activate', 'deactivate', 'rect', 'par', 'critical',
                           'break', 'autonumber')):
            continue
        if lo.startswith(('participant', 'actor')):
            m = re.match(r'(?:participant|actor)\s+(\S+)(?:\s+as\s+(.+))?', line, re.I)
            if m:
                raw     = m.group(1)
                display = (m.group(2) or raw).strip()
                nid     = _dot_id(raw)
                if raw not in actor_ids:
                    actor_ids[raw] = nid
                    actors.append((nid, display))
            continue
        m = re.match(r'(\S+)\s*[-=]+[->xX)]+[+-]?\s*(\S+)\s*:\s*(.+)', line)
        if m:
            src_raw = m.group(1).rstrip(':')
            dst_raw = m.group(2).rstrip(':')
            msg     = m.group(3).strip()
            for raw in (src_raw, dst_raw):
                if raw not in actor_ids:
                    nid = _dot_id(raw)
                    actor_ids[raw] = nid
                    actors.append((nid, raw))
            messages.append((actor_ids[src_raw], actor_ids[dst_raw], msg))

    parts = [
        'digraph G {',
        '  rankdir=LR',
        '  node [shape=box fontname="Helvetica" fontsize=12 style=filled '
        'fillcolor="#dce8f5" color="#3a6fa5"]',
        '  edge [fontname="Helvetica" fontsize=10 color="#3a6fa5"]',
    ]
    for nid, display in actors:
        parts.append(f'  {nid} [label="{_dot_label(display)}"]')
    for src, dst, msg in messages:
        parts.append(f'  {src} -> {dst} [label="{_dot_label(msg)}"]')
    parts.append('}')
    return '\n'.join(parts)


def _mermaid_to_dot(source: str) -> Optional[str]:
    lines = [l for l in source.strip().splitlines() if l.strip()]
    if not lines:
        return None
    first = lines[0].strip().lower()
    if re.match(r'(?:graph|flowchart)\b', first, re.I):
        return _mermaid_flowchart_to_dot(lines)
    if first.startswith('sequencediagram'):
        return _mermaid_sequence_to_dot(lines)
    return None


class MermaidRenderer:
    def __init__(self):
        try:
            import graphviz as _gv
            _gv.Source('digraph G {}').pipe(format='svg')
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
            svg_bytes = self._gv.Source(dot).pipe(format='svg')
            svg = svg_bytes.decode('utf-8')
            svg = re.sub(r'<\?xml[^?]*\?>', '', svg)
            svg = re.sub(r'<!DOCTYPE[^>]*>', '', svg)
            return svg.strip()
        except Exception as e:
            print(f"  ⚠ diagram render failed: {e}")
            print("  ── generated DOT ──")
            for i, ln in enumerate(dot.splitlines(), 1):
                print(f"  {i:3}: {ln}")
            print("  ── mermaid source ──")
            for ln in source.strip().splitlines():
                print(f"       {ln}")
            return None

    def render_all(self, diagrams: List[str]) -> List[Optional[str]]:
        return [self.render_one(src) for src in diagrams]


def _replace_mermaid_with_svg(html_content: str, renderer: MermaidRenderer) -> str:
    soup = BeautifulSoup(html_content, "html.parser")
    slots = soup.find_all("div", class_="mermaid")
    if not slots:
        return html_content

    sources = [slot.get_text() for slot in slots]
    svgs    = renderer.render_all(sources)

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
                f'<pre><code>{src}</code></pre>'
                f'</div>',
                "html.parser"
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
                        '</div>\n'
                        '<div class="mermaid">\n'
                        + "\n".join(mermaid_content)
                        + '\n</div>\n</div>'
                    )
                    new_lines.append(html)
                continue
            new_lines.append(line)
            i += 1
        return new_lines


class MermaidExtension(Extension):
    def extendMarkdown(self, md):
        md.preprocessors.register(MermaidPreprocessor(md), "mermaid", 175)


class DocGenerator:
    def __init__(self):
        self.template      = self._build_template()
        self.pdf_template  = self._build_pdf_template()
        self._mermaid      = MermaidRenderer()
        self.search_index = []

    def _pygments_css(self) -> str:
        light = HtmlFormatter(style="xcode",   noclasses=False).get_style_defs(".codehilite")
        dark  = HtmlFormatter(style="monokai", noclasses=False).get_style_defs(".codehilite")
        dark_prefixed = "\n".join(
            f"[data-theme='dark'] {line}" if line.strip() and not line.strip().startswith("/*") else line
            for line in dark.splitlines()
        )
        return f"{light}\n{dark_prefixed}\n"

    PLATFORM_ICONS = {
        "ios": "smartphone", "android": "smartphone", "web": "globe",
        "windows": "monitor", "macos": "monitor", "linux": "terminal",
        "docker": "box", "kubernetes": "layers",
        "aws": "cloud", "azure": "cloud", "gcp": "cloud",
        "python": "terminal", "javascript": "code-2", "typescript": "code-2",
        "react": "code-2", "vue": "code-2", "angular": "code-2",
        "node": "server", "go": "terminal", "rust": "terminal",
        "java": "coffee", "kotlin": "code-2", "swift": "smartphone",
        "flutter": "smartphone", "x11": "terminal", "wayland": "terminal",
        "espressif": "cpu", "embedded": "cpu", "esp32": "cpu",
    }

    PLATFORM_COLORS = {
        "android":    "#3ddc84",
        "ios":        "#007aff",
        "linux":      "#e95420",
        "windows":    "#0078d4",
        "macos":      "#636366",
        "web":        "#0071e3",
        "docker":     "#2496ed",
        "kubernetes": "#326ce5",
        "aws":        "#ff9900",
        "azure":      "#0089d6",
        "gcp":        "#4285f4",
        "python":     "#3776ab",
        "javascript": "#f7df1e",
        "typescript": "#3178c6",
        "react":      "#61dafb",
        "node":       "#339933",
        "rust":       "#ce422b",
        "go":         "#00add8",
        "java":       "#f89820",
        "kotlin":     "#7f52ff",
        "swift":      "#fa7343",
        "flutter":    "#54c5f8",
        "x11":        "#1f6fad",
        "wayland":    "#ffb347",
        "espressif":  "#e7352c",
        "embedded":   "#6d6d6d",
    }

    def _normalize_platform(self, p) -> Optional[Dict]:
        if isinstance(p, str):
            name = p.strip()
            if not name:
                return None
            return {
                "name":  name,
                "icon":  self.PLATFORM_ICONS.get(name.lower(), "cpu"),
                "color": self.PLATFORM_COLORS.get(name.lower(), "#636366"),
            }
        if isinstance(p, dict):
            name = p.get("name") or p.get("title") or ""
            if not name:
                return None
            return {
                "name":  name,
                "icon":  p.get("icon") or self.PLATFORM_ICONS.get(name.lower(), "cpu"),
                "color": p.get("color") or self.PLATFORM_COLORS.get(name.lower(), "#636366"),
            }
        return None

    def _platform_badge_html(self, platform) -> str:
        p = self._normalize_platform(platform)
        if not p:
            return ""
        return (
            f'<span class="platform-badge" style="--badge-color:{p["color"]}" title="{p["name"]}">'
            f'<span>{p["name"]}</span></span>'
        )

    def _hero_html(self, config: Dict) -> str:
        h = config.get("hero", {})
        title       = h.get("title", config.get("name", "Docs"))
        subtitle    = h.get("subtitle", "")
        description = h.get("description", "")
        eyebrow     = h.get("eyebrow", "")

        badges_html = "".join(
            self._platform_badge_html(p)
            for p in (h.get("platformIcons") or h.get("platforms") or [])
        )

        actions_html = ""
        for a in h.get("actions", []):
            cls    = "btn-primary" if a.get("primary") else "btn-ghost"
            icon   = f'<i data-lucide="{a["icon"]}"></i>' if a.get("icon") else ""
            href   = a.get("url", "#")
            onclick = f' onclick="{a["onclick"]}"' if a.get("onclick") else ""
            actions_html += (
                f'<a href="{href}" class="hero-btn {cls}"{onclick}>'
                f'{icon}{a.get("text","")}</a>'
            )

        stats_html = ""
        if h.get("stats"):
            stats_html = '<div class="hero-stats">' + "".join(
                f'<div class="stat-item">'
                f'<div class="stat-value">{s.get("value","")}</div>'
                f'<div class="stat-label">{s.get("label","")}</div>'
                f'</div>'
                for s in h["stats"]
            ) + '</div>'

        return (
            '<section class="hero">'
            + (f'<div class="hero-eyebrow"><span>{eyebrow}</span></div>' if eyebrow else "")
            + f'<img src="./images/aroma.png" width="64" height="64" style="margin-bottom: 2rem;"/>'
            + f'<h1 class="hero-title">{title}</h1>'
            + (f'<p class="hero-subtitle">{subtitle}</p>' if subtitle else "")
            + (f'<p class="hero-desc">{description}</p>' if description else "")
            + stats_html
            + (f'<div class="hero-actions">{actions_html}</div>' if actions_html else "")
            + (f'<div class="platform-badges">{badges_html}</div>' if badges_html else "")
            + '</section>'
            + (f'<div class="hero-image-container"><img src="./images/hero-image.png" class="hero-image"/></div>')
        )

    def _quick_links_html(self, config: Dict) -> str:
        cards = config.get("quick_links", [])
        if not cards:
            return ""
        items = "".join(
            f'<div class="quick-link" onclick="showPage(\'{c.get("page_id","")}\')">'
            f'<div class="ql-icon"><i data-lucide="{c.get("icon","link")}"></i></div>'
            f'<div class="ql-body">'
            f'<div class="ql-title">{c.get("title","")}</div>'
            f'<div class="ql-desc">{c.get("description","")}</div>'
            f'</div>'
            f'<i data-lucide="chevron-right" class="ql-arrow"></i>'
            f'</div>'
            for c in cards
        )
        return (
            '<div class="quick-links-section">'
            '<h2 class="section-heading">Quick Links</h2>'
            f'<div class="quick-links">{items}</div>'
            '</div>'
        )

    def _category_page_html(self, category: Dict, subcategories: Dict[str, List], pages_dict: Dict, titles_dict: Dict) -> str:
        cat_name = category.get("name", "Category")
        cat_icon = category.get("icon", "folder")
        cat_desc = category.get("description", f"Documentation for {cat_name}")
        
        cards = []
        for sub_name, pages in subcategories.items():
            if not sub_name:
                continue
                
            preview_pages = pages[:3]
            page_count = len(pages)
            
            card = f'''
            <div class="subcategory-card" onclick="showSubcategory('{cat_name}', '{sub_name}')">
                <div class="card-header">
                    <div class="card-icon">
                        <i data-lucide="folder-open"></i>
                    </div>
                    <div class="card-count">{page_count} document{"s" if page_count != 1 else ""}</div>
                </div>
                <h3 class="card-title">{sub_name}</h3>
                <p class="card-desc">Documentation for {sub_name}</p>
                <div class="card-preview">
                    {"".join(f'<span class="preview-tag">{titles_dict.get(p.get("id", ""), "Untitled")}</span>' for p in preview_pages)}
                    {f'<span class="preview-more">+{page_count - len(preview_pages)} more</span>' if page_count > len(preview_pages) else ''}
                </div>
                <div class="card-arrow">
                    <i data-lucide="arrow-right-circle"></i>
                </div>
            </div>
            '''
            cards.append(card)
        
        return f'''
        <div class="category-page" data-category="{cat_name}">
            <div class="category-header">
                <div class="category-header-icon">
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

    def _subcategory_page_html(self, category: str, subcategory: str, pages: List[Dict], titles_dict: Dict, pages_dict: Dict) -> str:
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
        exts = ["extra","codehilite","toc","tables","fenced_code",
                "attr_list","def_list","abbr","footnotes","md_in_html"]
        md = markdown.Markdown(extensions=exts)
        md.registerExtensions([MermaidExtension()], {})
        return md.convert(content)

    def load_markdown(self, path: str) -> str:
        try:
            if not os.path.exists(path):
                return f"<h1>File not found</h1><p>{path}</p>"
            with open(path, "r", encoding="utf-8") as f:
                return self._process_markdown(f.read())
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
    font-family: 'Helvetica Neue', Helvetica, sans-serif;
    font-size: 9pt; color: #6e6e73;
  }
  @bottom-center {
    content: "Page " counter(page) " of " counter(pages);
    font-family: 'Helvetica Neue', Helvetica, sans-serif;
    font-size: 9pt; color: #6e6e73;
  }
}
body {
  font-family: 'Helvetica Neue', Helvetica, sans-serif;
  line-height: 1.65; color: #1d1d1f; font-size: 11pt;
}
h1 { font-size: 22pt; font-weight: 700; margin-top: 0; page-break-after: avoid; letter-spacing: -0.02em; }
h2 { font-size: 16pt; font-weight: 600; margin-top: 28pt; page-break-after: avoid; letter-spacing: -0.015em; }
h3 { font-size: 13pt; font-weight: 600; margin-top: 18pt; page-break-after: avoid; }
h4 { font-size: 11pt; font-weight: 600; margin-top: 14pt; color: #6e6e73; page-break-after: avoid; }
p  { margin: 0 0 10pt; }
a  { color: #0071e3; text-decoration: none; }
pre, code {
  font-family: 'Menlo', 'Monaco', monospace;
  background: #f5f5f7; border-radius: 5pt; font-size: 9pt;
}
pre {
  padding: 10pt 12pt; border: 1pt solid #e5e5ea;
  page-break-inside: avoid; margin: 12pt 0;
}
pre code { background: none; border: none; padding: 0; }
code { padding: 1pt 4pt; border: 1pt solid #e5e5ea; }
blockquote {
  margin: 12pt 0; padding: 10pt 14pt;
  border-left: 3pt solid #0071e3;
  background: rgba(0,113,227,.05);
  border-radius: 0 5pt 5pt 0;
}
blockquote p { color: #6e6e73; margin: 0; font-style: italic; }
table {
  width: 100%; border-collapse: collapse;
  margin: 14pt 0; page-break-inside: avoid;
  border: 1pt solid #e5e5ea; border-radius: 6pt;
  font-size: 10pt;
}
th {
  padding: 8pt 10pt; background: #f5f5f7;
  font-weight: 600; text-align: left;
  border-bottom: 1pt solid #e5e5ea;
}
td { padding: 7pt 10pt; border-bottom: 1pt solid #e5e5ea; }
tr:last-child td { border-bottom: none; }
ul, ol { margin: 0 0 10pt 18pt; }
li     { margin: 4pt 0; }
img {
  max-width: 100%; border-radius: 6pt;
  border: 1pt solid #e5e5ea; page-break-inside: avoid;
}
hr { border: none; border-top: 1pt solid #e5e5ea; margin: 24pt 0; }
.mermaid-pdf {
  text-align: center;
  margin: 14pt 0;
  padding: 14pt;
  background: #fafafa;
  border: 1pt solid #e5e5ea;
  border-radius: 8pt;
  page-break-inside: avoid;
}
.mermaid-pdf svg { max-width: 100%; height: auto; display: block; margin: 0 auto; }
.mermaid-pdf svg[style*="max-width"] { width: 100% !important; max-width: 100% !important; }
.mermaid-fallback {
  margin: 14pt 0;
  border: 1pt solid #e5e5ea;
  border-radius: 6pt;
  overflow: hidden;
  page-break-inside: avoid;
}
.mermaid-fallback-label {
  background: #f5f5f7;
  padding: 6pt 10pt;
  font-size: 9pt;
  font-weight: 600;
  color: #6e6e73;
  margin: 0;
  border-bottom: 1pt solid #e5e5ea;
}
.mermaid-fallback pre {
  margin: 0; border: none; border-radius: 0;
  background: #fff; padding: 10pt 12pt;
  font-size: 8.5pt; color: #1d1d1f;
}
.cover { text-align: center; margin-top: 90pt; page-break-after: always; }
.cover-title { font-size: 34pt; font-weight: 700; margin-bottom: 16pt; letter-spacing: -0.025em; }
.cover-sub { font-size: 16pt; color: #6e6e73; margin-bottom: 36pt; }
.cover-meta { font-size: 11pt; color: #aeaeb2; margin-top: 48pt; line-height: 1.8; }
.cover-line { width: 40pt; height: 3pt; background: #0071e3; margin: 24pt auto; border-radius: 2pt; }
.toc-page { page-break-after: always; }
.toc-page h1 { border-bottom: 1pt solid #e5e5ea; padding-bottom: 10pt; margin-bottom: 18pt; }
.toc-entry { display: flex; align-items: baseline; margin: 7pt 0; font-size: 11pt; }
.toc-title { flex: 1; }
.toc-dots { flex: 2; border-bottom: 1pt dotted #d1d1d6; margin: 0 8pt; height: 0.7em; }
.toc-page-num { color: #6e6e73; font-size: 10pt; }
.section-break { page-break-before: always; }
.section-title-rule { border-top: 2pt solid #e5e5ea; margin-bottom: 20pt; padding-top: 0; }
.footer { margin-top: 36pt; padding-top: 16pt; border-top: 1pt solid #e5e5ea; font-size: 9pt; color: #aeaeb2; text-align: center; }
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
  <h1>Table of Contents</h1>
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
<div class="footer">Copyright © 2026 BinaryInkTN</div>
</body></html>"""

    def _build_template(self) -> str:
      return r"""<!DOCTYPE html>
<html lang="en" data-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{project_name} - Docs</title>
<meta name="description" content="{description}">
<style>
*,*::before,*::after{{box-sizing:border-box;margin:0;padding:0}}
:root{{
  --bg:#ffffff;
  --bg2:#f5f5f7;
  --bg3:#fafafa;
  --surf:rgba(255,255,255,.92);
  --surf2:rgba(255,255,255,.98);
  --bdr:rgba(0,0,0,.08);
  --bdr2:rgba(0,0,0,.12);
  --t1:#1d1d1f;
  --t2:#6e6e73;
  --t3:#aeaeb2;
  --acc:#0071e3;
  --acc2:#0077ed;
  --accl:rgba(0,113,227,.07);
  --accm:rgba(0,113,227,.14);
  --r1:5px;
  --r2:8px;
  --r3:12px;
  --r4:16px;
  --r5:20px;
  --sw:260px;
  --hh:48px;
  --ease:cubic-bezier(0.4, 0, 0.2, 1);
  --tr:.2s var(--ease);
  --tr-slow:.3s var(--ease);
  --sh1:0 1px 2px rgba(0,0,0,.05),0 1px 3px rgba(0,0,0,.04);
  --sh2:0 2px 8px rgba(0,0,0,.07),0 1px 3px rgba(0,0,0,.04);
  --sh3:0 8px 24px rgba(0,0,0,.08),0 2px 8px rgba(0,0,0,.05);
  --sh4:0 24px 48px rgba(0,0,0,.10),0 6px 16px rgba(0,0,0,.06);
  --fb:-apple-system,BlinkMacSystemFont,'Helvetica Neue',Helvetica,sans-serif;
  --fm:'Menlo','Monaco','Courier New',monospace;
  --code-bg:#f5f5f7;
  --code-fg:#1d1d1f;
}}
[data-theme="dark"]{{
  --bg:#000000;
  --bg2:#111111;
  --bg3:#1c1c1e;
  --surf:rgba(28,28,30,.96);
  --surf2:rgba(44,44,46,.98);
  --bdr:rgba(255,255,255,.09);
  --bdr2:rgba(255,255,255,.15);
  --t1:#f5f5f7;
  --t2:#98989d;
  --t3:#636366;
  --acc:#2997ff;
  --acc2:#409cff;
  --accl:rgba(41,151,255,.09);
  --accm:rgba(41,151,255,.16);
  --sh1:0 1px 2px rgba(0,0,0,.25),0 1px 3px rgba(0,0,0,.18);
  --sh2:0 2px 8px rgba(0,0,0,.35),0 1px 3px rgba(0,0,0,.20);
  --sh3:0 8px 24px rgba(0,0,0,.45),0 2px 8px rgba(0,0,0,.28);
  --sh4:0 24px 48px rgba(0,0,0,.55),0 6px 16px rgba(0,0,0,.35);
  --code-bg:#1c1c1e;
  --code-fg:#f5f5f7;
}}
html{{font-size:15px;-webkit-text-size-adjust:100%}}
body{{
  font-family:var(--fb);
  background:var(--bg);
  color:var(--t1);
  height:100vh;
  overflow:hidden;
  -webkit-font-smoothing:antialiased;
  -moz-osx-font-smoothing:grayscale;
  transition:background var(--tr),color var(--tr);
}}
.layout{{display:flex;height:100vh;overflow:hidden}}

.announcement-bar {{
  background: linear-gradient(135deg, #2997ff 0%, #0071e3 100%);
  color: white;
  padding: 10px 20px;
  text-align: center;
  font-size: 14px;
  font-weight: 500;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  position: relative;
  z-index: 100;
  max-height: 44px;
  transition: max-height var(--tr-slow), opacity var(--tr-slow), margin var(--tr-slow), padding var(--tr-slow);
  overflow: hidden;
}}
.announcement-bar.hidden {{
  max-height: 0;
  opacity: 0;
  padding-top: 0;
  padding-bottom: 0;
  margin-bottom: -1px;
  pointer-events: none;
}}
.announcement-bar .announcement-text {{
  color: white;
  letter-spacing: -0.01em;
}}
.announcement-bar .announcement-link {{
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: rgba(255,255,255,0.15);
  color: white;
  padding: 4px 12px;
  border-radius: 20px;
  text-decoration: none;
  font-size: 13px;
  font-weight: 600;
  border: 1px solid rgba(255,255,255,0.3);
  transition: all var(--tr);
}}
.announcement-bar .announcement-link:hover {{
  background: rgba(255,255,255,0.25);
  border-color: rgba(255,255,255,0.5);
  transform: translateY(-1px);
}}
.announcement-bar .announcement-link i {{
  width: 14px;
  height: 14px;
  transition: transform var(--tr);
}}
.announcement-bar .announcement-link:hover i {{
  transform: translateX(2px);
}}
.announcement-bar .announcement-close {{
  position: absolute;
  right: 12px;
  background: rgba(255,255,255,0.15);
  border: 1px solid rgba(255,255,255,0.3);
  color: white;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all var(--tr);
}}
.announcement-bar .announcement-close:hover {{
  background: rgba(255,255,255,0.25);
  transform: scale(1.1);
}}
.announcement-bar .announcement-close i {{
  width: 14px;
  height: 14px;
}}
[data-theme="dark"] .announcement-bar {{
  background: linear-gradient(135deg, #1a5fb4 0%, #0b3b8c 100%);
}}

.sidebar{{
  width:var(--sw);
  background:var(--bg2);
  border-right:1px solid var(--bdr);
  display:flex;
  flex-direction:column;
  overflow:hidden;
  flex-shrink:0;
  z-index:50;
  transition:transform var(--tr-slow), background var(--tr), border-color var(--tr);
}}
.sb-hdr{{
  padding:12px 12px 8px;
  border-bottom:1px solid var(--bdr);
  flex-shrink:0;
  transition:border-color var(--tr);
}}
.logo-row{{
  display:flex;
  align-items:center;
  gap:8px;
  cursor:pointer;
  padding:5px 6px;
  border-radius:var(--r2);
  margin:-5px -6px 8px;
  transition:background var(--tr);
}}
.logo-row:hover{{background:var(--bdr)}}
.logo-icon{{
  width:28px;
  height:28px;
  border-radius:6px;
  background:var(--acc);
  display:flex;
  align-items:center;
  justify-content:center;
  flex-shrink:0;
  transition:background var(--tr);
}}
.logo-icon svg{{width:14px;height:14px;fill:none;stroke:#fff;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}}
.logo-name{{
  font-size:.875rem;
  font-weight:600;
  color:var(--t1);
  overflow:hidden;
  text-overflow:ellipsis;
  white-space:nowrap;
  letter-spacing:-.01em;
  transition:color var(--tr);
}}
.logo-version{{
  font-size:.625rem;
  color:var(--t3);
  font-family:var(--fm);
  margin-left:auto;
  flex-shrink:0;
  letter-spacing:.02em;
  transition:color var(--tr);
}}
.search-container {{
  position: relative;
  width: 90%;
  max-width: 400px;
  margin: 8px auto;
}}
.sb-search{{
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  background: var(--bg);
  border: 1px solid var(--bdr);
  border-radius: 24px;
  padding: 8px 12px;
  cursor: pointer;
  transition: all var(--tr);
  width: 100%;
}}
.sb-search:hover{{
  border-color: var(--bdr2);
  transform: translateY(-1px);
  box-shadow: var(--sh1);
}}
.sb-search i{{color:var(--t3);width:16px;height:16px;flex-shrink:0; transition:color var(--tr);}}
.sb-search-ph{{flex:1;font-size:.875rem;color:var(--t3);user-select:none; transition:color var(--tr); text-align:left;}}
.sb-search-kbd{{
  font-size:.6875rem;
  color:var(--t3);
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:4px;
  padding:2px 6px;
  font-family:var(--fm);
  flex-shrink:0;
  transition:all var(--tr);
}}
.search-dropdown {{
  position: absolute;
  top: calc(100% + 4px);
  left: 0;
  right: 0;
  background: var(--surf2);
  border: 1px solid var(--bdr);
  border-radius: 16px;
  box-shadow: var(--sh3);
  z-index: 200;
  overflow: hidden;
  display: none;
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  opacity: 0;
  transform: translateY(-10px);
  transition: opacity var(--tr), transform var(--tr);
}}
.search-dropdown.open {{
  display: block;
  opacity: 1;
  transform: translateY(0);
}}
.search-header {{
  padding: 12px 16px;
  border-bottom: 1px solid var(--bdr);
  display: flex;
  align-items: center;
  gap: 8px;
}}
.search-header i {{
  width: 16px;
  height: 16px;
  color: var(--t3);
}}
.search-header input {{
  flex: 1;
  background: none;
  border: none;
  outline: none;
  font-family: var(--fb);
  font-size: .9375rem;
  color: var(--t1);
}}
.search-header input::placeholder {{
  color: var(--t3);
}}
.search-results {{
  max-height: 360px;
  overflow-y: auto;
}}
.search-item {{
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 16px;
  cursor: pointer;
  border-bottom: 1px solid var(--bdr);
  transition: all var(--tr);
}}
.search-item:last-child {{
  border-bottom: none;
}}
.search-item:hover,
.search-item.selected {{
  background: var(--bg2);
  transform: translateX(2px);
}}
.search-item-icon {{
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: var(--accl);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--acc);
  flex-shrink: 0;
  transition: all var(--tr);
}}
.search-item:hover .search-item-icon {{
  background: var(--acc);
  color: white;
  transform: scale(1.05);
}}
.search-item-icon i {{
  width: 14px;
  height: 14px;
}}
.search-item-content {{
  flex: 1;
  min-width: 0;
}}
.search-item-title {{
  font-size: .875rem;
  font-weight: 500;
  color: var(--t1);
  margin-bottom: 1px;
}}
.search-item-path {{
  font-size: .75rem;
  color: var(--t3);
}}
.search-empty {{
  padding: 24px;
  text-align: center;
  color: var(--t3);
  font-size: .875rem;
}}
.search-footer {{
  padding: 8px 16px;
  border-top: 1px solid var(--bdr);
  display: flex;
  gap: 16px;
  font-size: .6875rem;
  color: var(--t3);
  background: var(--bg2);
}}
.search-footer kbd {{
  display: inline-flex;
  align-items: center;
  padding: 2px 5px;
  background: var(--bg);
  border: 1px solid var(--bdr);
  border-radius: 3px;
  font-family: var(--fm);
  font-size: .5625rem;
  margin-right: 2px;
}}
.pf-wrap{{margin-bottom:4px;position:relative}}
.pf-btn{{
  display:flex;
  align-items:center;
  gap:7px;
  width:100%;
  padding:6px 9px;
  background:var(--bg);
  border:1px solid var(--bdr);
  border-radius:var(--r2);
  cursor:pointer;
  transition:all var(--tr);
  font-family:var(--fb);
  -webkit-font-smoothing:antialiased;
}}
.pf-btn:hover{{border-color:var(--bdr2); transform: translateY(-1px); box-shadow: var(--sh1);}}
.pf-btn.open{{border-color:var(--acc);background:var(--accl)}}
.pf-btn-icon{{
  width:18px;height:18px;border-radius:4px;
  display:flex;align-items:center;justify-content:center;
  background:var(--bg2);flex-shrink:0;
  transition:all var(--tr);
}}
.pf-btn-icon i{{width:10px;height:10px;color:var(--t2); transition:color var(--tr);}}
.pf-btn.open .pf-btn-icon{{background:var(--acc)}}
.pf-btn.open .pf-btn-icon i{{color:#fff}}
.pf-btn-txt{{flex:1;font-size:.75rem;font-weight:500;color:var(--t1);text-align:left;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap; transition:color var(--tr);}}
.pf-btn-chev{{width:12px;height:12px;color:var(--t3);flex-shrink:0;transition:transform var(--tr), color var(--tr);}}
.pf-btn.open .pf-btn-chev{{transform:rotate(180deg); color:var(--acc);}}
.pf-dd{{
  position:absolute;
  top:calc(100% + 4px);
  left:0;right:0;
  background:var(--surf2);
  border:1px solid var(--bdr);
  border-radius:var(--r3);
  box-shadow:var(--sh3);
  z-index:200;
  overflow:hidden;
  display:none;
  backdrop-filter:blur(20px);
  -webkit-backdrop-filter:blur(20px);
  opacity:0;
  transform: translateY(-10px);
  transition: opacity var(--tr), transform var(--tr);
}}
.pf-dd.open{{display:block; opacity:1; transform: translateY(0);}}
.pf-opt{{
  display:flex;
  align-items:center;
  gap:8px;
  padding:7px 10px;
  cursor:pointer;
  transition:all var(--tr);
  font-size:.8125rem;
  color:var(--t1);
}}
.pf-opt:hover{{background:var(--bg2); transform: translateX(2px);}}
.pf-opt.active{{color:var(--acc);background:var(--accl)}}
.pf-opt-icon{{
  width:22px;height:22px;border-radius:5px;
  display:flex;align-items:center;justify-content:center;
  flex-shrink:0;
}}
.pf-opt-icon i{{width:11px;height:11px}}
.pf-opt-name{{flex:1;font-weight:500}}
.pf-opt-count{{font-size:.6875rem;color:var(--t3);font-family:var(--fm); transition:color var(--tr);}}
.pf-opt-chk{{width:13px;height:13px;color:var(--acc);opacity:0;flex-shrink:0; transition:opacity var(--tr);}}
.pf-opt.active .pf-opt-chk{{opacity:1}}
.pf-divider{{height:1px;background:var(--bdr);margin:3px 0; transition:background var(--tr);}}
.sb-nav{{
  flex:1;
  overflow-y:auto;
  padding:4px 0 16px;
  scrollbar-width:thin;
  scrollbar-color:var(--bdr2) transparent;
}}
.sb-nav::-webkit-scrollbar{{width:3px}}
.sb-nav::-webkit-scrollbar-thumb{{background:var(--bdr2);border-radius:3px; transition:background var(--tr);}}
.sec-label{{
  display:flex;
  align-items:center;
  gap:5px;
  padding:10px 12px 3px;
  font-size:.625rem;
  font-weight:600;
  letter-spacing:.07em;
  text-transform:uppercase;
  color:var(--t3);
  cursor:pointer;
  user-select:none;
  transition:all var(--tr);
}}
.sec-label:hover{{color:var(--t2); transform: translateX(2px);}}
.sec-chev{{margin-left:auto;transition:transform var(--tr), color var(--tr);width:12px;height:12px;color:var(--t3)}}
.sec-chev.c{{transform:rotate(-90deg)}}
.sec-items{{overflow:hidden; transition:max-height var(--tr-slow);}}
.sec-items.c{{display:none}}
.nav-item{{
  display:flex;
  align-items:center;
  gap:7px;
  padding:5px 12px 5px 16px;
  font-size:.8125rem;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  position:relative;
  line-height:1.35;
  border-radius:0;
}}
.nav-item:hover{{color:var(--t1);background:var(--bdr); transform: translateX(2px);}}
.nav-item.active{{
  color:var(--acc);
  background:var(--accl);
  font-weight:500;
}}
.nav-item.active::before{{
  content:'';
  position:absolute;
  left:0;
  top:4px;
  bottom:4px;
  width:2px;
  background:var(--acc);
  border-radius:0 2px 2px 0;
  animation: slideIn 0.2s var(--ease);
}}
@keyframes slideIn {{
  from {{ transform: scaleY(0); }}
  to {{ transform: scaleY(1); }}
}}
.nav-item i{{flex-shrink:0;width:14px;height:14px;opacity:.5; transition:opacity var(--tr);}}
.nav-item.active i{{opacity:1}}
.nav-item.ph{{display:none!important}}
.sub-label{{
  padding:8px 12px 8px 20px;
  font-size:.75rem;
  font-weight:500;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  user-select:none;
  display:flex;
  align-items:center;
}}
.sub-label:hover{{color:var(--t1); transform: translateX(2px);}}
.sub-chev{{margin-left:auto;transition:transform var(--tr), color var(--tr);width:11px;height:11px}}
.sub-chev.c{{transform:rotate(-90deg)}}
.sub-items{{overflow:hidden; transition:max-height var(--tr-slow);}}
.sub-items.c{{display:none}}
.nav-item.sub{{padding-left:28px;font-size:.75rem}}
.sec-empty{{opacity:.3;pointer-events:none}}

.main{{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0}}
.hdr{{
  height:var(--hh);
  background:var(--surf);
  backdrop-filter:saturate(180%) blur(20px);
  -webkit-backdrop-filter:saturate(180%) blur(20px);
  border-bottom:1px solid var(--bdr);
  display:flex;
  align-items:center;
  padding:0 16px;
  gap:8px;
  flex-shrink:0;
  position:relative;
  z-index:10;
  transition:background var(--tr), border-color var(--tr);
}}
.bc{{
  display:flex;
  align-items:center;
  gap:4px;
  font-size:.8125rem;
  color:var(--t2);
  flex:1;
  min-width:0;
  flex-wrap:wrap;
  transition:color var(--tr);
}}
.bc-home{{
  display:flex;
  align-items:center;
  gap:4px;
  color:var(--t1);
  cursor:pointer;
  font-weight:500;
  transition:all var(--tr);
  white-space:nowrap;
  flex-shrink:0;
}}
.bc-home:hover{{color:var(--acc); transform: translateX(-1px);}}
.bc-home i{{width:12px;height:12px; transition:transform var(--tr);}}
.bc-home:hover i{{transform: translateX(-2px);}}
.bc-sep{{color:var(--t3);font-size:.6875rem;margin:0 2px; transition:color var(--tr);}}
.bc-category{{
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  white-space:nowrap;
}}
.bc-category:hover{{color:var(--acc); transform: translateX(-1px);}}
.bc-subcategory{{
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  white-space:nowrap;
}}
.bc-subcategory:hover{{color:var(--acc); transform: translateX(-1px);}}
.bc-cur{{
  color:var(--t2);
  overflow:hidden;
  text-overflow:ellipsis;
  white-space:nowrap;
  max-width:200px;
  transition:color var(--tr);
}}
.hdr-r{{display:flex;align-items:center;gap:6px;flex-shrink:0}}
.hbtn{{
  display:flex;
  align-items:center;
  gap:4px;
  padding:5px 10px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:var(--r2);
  font-family:var(--fb);
  font-size:.75rem;
  font-weight:500;
  color:var(--t1);
  cursor:pointer;
  transition:all var(--tr);
  white-space:nowrap;
  -webkit-font-smoothing:antialiased;
}}
.hbtn:hover{{background:var(--bg);border-color:var(--bdr2); transform: translateY(-1px); box-shadow: var(--sh1);}}
.hbtn i{{width:13px;height:13px; transition:transform var(--tr);}}
.hbtn:hover i{{transform: scale(1.1);}}
.hbtn kbd{{
  background:var(--bg);
  border:1px solid var(--bdr);
  border-radius:3px;
  padding:1px 4px;
  font-size:.5625rem;
  font-family:var(--fm);
  color:var(--t3);
  transition:all var(--tr);
}}
.pdf-download-btn {{
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  background: var(--bg2);
  border: 1px solid var(--bdr);
  border-radius: var(--r2);
  color: var(--t2);
  cursor: pointer;
  transition: all var(--tr);
}}
.pdf-download-btn:hover {{
  background: var(--bg);
  border-color: var(--acc);
  color: var(--acc);
  transform: translateY(-1px);
}}
.pdf-download-btn i {{
  width: 16px;
  height: 16px;
}}
.theme-toggle{{
  position:relative;
  display:inline-block;
  width:48px;
  height:24px;
}}
.theme-toggle input{{
  opacity:0;
  width:0;
  height:0;
}}
.theme-slider{{
  position:absolute;
  cursor:pointer;
  top:0;
  left:0;
  right:0;
  bottom:0;
  background-color:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:34px;
  transition:.15s;
  display:flex;
  align-items:center;
  padding:2px;
}}
.theme-slider:before{{
  position:absolute;
  content:"";
  height:18px;
  width:18px;
  left:2px;
  bottom:2px;
  background-color:var(--acc);
  border-radius:50%;
  transition:.15s;
  z-index:2;
}}
.theme-slider .sun-icon,
.theme-slider .moon-icon{{
  position:absolute;
  width:14px;
  height:14px;
  z-index:1;
  transition:all var(--tr);
}}
.theme-slider .sun-icon{{
  left:6px;
  color:var(--t2);
}}
.theme-slider .moon-icon{{
  right:6px;
  color:var(--t2);
}}
input:checked + .theme-slider:before{{
  transform:translateX(24px);
}}
input:checked + .theme-slider .sun-icon{{
  color:var(--t2);
}}
input:checked + .theme-slider .moon-icon{{
  color:var(--acc);
}}
.c-layout{{flex:1;display:flex;overflow:hidden}}
.c-scroll{{
  flex:1;
  overflow-y:auto;
  scrollbar-width:thin;
  scrollbar-color:var(--bdr2) transparent;
}}
.c-scroll::-webkit-scrollbar{{width:4px}}
.c-scroll::-webkit-scrollbar-thumb{{background:var(--bdr2);border-radius:3px; transition:background var(--tr);}}
.c-inner{{max-width:900px;margin:0 auto;padding:40px 32px 72px; animation: fadeIn 0.3s var(--ease);}}
@keyframes fadeIn {{
  from {{ opacity: 0; transform: translateY(10px); }}
  to {{ opacity: 1; transform: translateY(0); }}
}}

.toc-p{{
  width:200px;
  flex-shrink:0;
  border-left:1px solid var(--bdr);
  overflow-y:auto;
  padding:32px 0 16px;
  display:none;
  transition:border-color var(--tr);
}}
.toc-p.vis{{display:block; animation: slideInRight 0.2s var(--ease);}}
@keyframes slideInRight {{
  from {{ opacity: 0; transform: translateX(10px); }}
  to {{ opacity: 1; transform: translateX(0); }}
}}
.toc-ttl{{
  padding:0 12px 8px;
  font-size:.625rem;
  font-weight:600;
  letter-spacing:.07em;
  text-transform:uppercase;
  color:var(--t3);
  transition:color var(--tr);
}}
.toc-prog{{margin:0 12px 10px;height:2px;background:var(--bdr);border-radius:2px;overflow:hidden; transition:background var(--tr);}}
.toc-fill{{height:100%;background:var(--acc);border-radius:2px;width:0%;transition:width .1s ease;}}
.toc-item{{
  display:block;
  padding:3px 12px;
  font-size:.75rem;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  border-left:2px solid transparent;
  line-height:1.4;
}}
.toc-item:hover{{color:var(--t1); transform: translateX(2px);}}
.toc-item.active{{color:var(--acc);border-left-color:var(--acc);background:var(--accl);font-weight:500}}

.hero{{text-align:center;padding:8px 0 44px;max-width:600px;margin:0 auto; animation: fadeIn 0.4s var(--ease);}}
.hero-eyebrow{{margin-bottom:16px;display:flex;justify-content:center;}}
.hero-eyebrow span{{
  display:inline-flex;
  align-items:center;
  padding:3px 10px;
  background:var(--accl);
  color:var(--acc);
  font-size:.75rem;
  font-weight:600;
  border-radius:100px;
  letter-spacing:.01em;
  border:1px solid var(--accm);
  transition:all var(--tr);
  animation: slideIn 0.3s var(--ease);
}}
.hero-eyebrow span:hover{{
  transform: translateY(-1px);
  box-shadow: var(--sh1);
}}
.hero-title{{
  font-size:clamp(1.75rem,4vw,2.875rem);
  font-weight:700;
  line-height:1.1;
  letter-spacing:-.03em;
  color:var(--t1);
  margin-bottom:12px;
  transition:color var(--tr);
}}
.hero-subtitle{{
  font-size:1.125rem;
  font-weight:400;
  color:var(--t2);
  margin-bottom:10px;
  letter-spacing:-.01em;
  transition:color var(--tr);
}}
.hero-desc{{
  font-size:1rem;
  color:var(--t2);
  line-height:1.65;
  max-width:560px;
  margin:0 auto 28px auto;
  transition:color var(--tr);
}}
.hero-stats{{
  display:flex;
  gap:28px;
  margin-bottom:28px;
  padding-bottom:28px;
  border-bottom:1px solid var(--bdr);
  justify-content:center;
  transition:border-color var(--tr);
}}
.stat-value{{
  font-size:1.625rem;
  font-weight:700;
  color:var(--acc);
  letter-spacing:-.03em;
  line-height:1.1;
  transition:color var(--tr);
}}
.stat-label{{
  font-size:.6875rem;
  color:var(--t3);
  margin-top:2px;
  text-transform:uppercase;
  letter-spacing:.05em;
  font-weight:500;
  transition:color var(--tr);
}}
.hero-actions{{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:22px;justify-content:center;}}
.hero-btn{{
  display:inline-flex;
  align-items:center;
  gap:6px;
  padding:9px 17px;
  border-radius:var(--r3);
  font-size:.9375rem;
  font-weight:500;
  text-decoration:none;
  cursor:pointer;
  border:none;
  font-family:var(--fb);
  transition:all var(--tr);
  letter-spacing:-.01em;
  -webkit-font-smoothing:antialiased;
}}
.btn-primary{{
  background:var(--acc);
  color:#fff;
  box-shadow:0 1px 3px rgba(0,113,227,.25),0 2px 8px rgba(0,113,227,.15);
}}
.btn-primary:hover{{
  background:var(--acc2);
  transform:translateY(-2px);
  box-shadow:0 2px 6px rgba(0,113,227,.3),0 6px 16px rgba(0,113,227,.2);
}}
.btn-ghost{{
  background:var(--bg2);
  color:var(--t1);
  border:1px solid var(--bdr2);
}}
.btn-ghost:hover{{background:var(--bg);border-color:var(--acc);color:var(--acc); transform:translateY(-2px);}}
.platform-badges{{display:flex;gap:5px;flex-wrap:wrap;justify-content:center;}}
.platform-badge{{
  display:inline-flex;
  align-items:center;
  gap:4px;
  padding:2px 7px;
  background:color-mix(in srgb,var(--badge-color) 9%,transparent);
  border:1px solid color-mix(in srgb,var(--badge-color) 22%,transparent);
  border-radius:100px;
  font-size:.625rem;
  font-weight:600;
  color:var(--badge-color);
  text-transform:uppercase;
  letter-spacing:.04em;
  transition:all var(--tr);
}}
.platform-badge:hover{{
  transform: translateY(-1px);
  box-shadow: var(--sh1);
}}
.platform-badge i{{width:10px;height:10px; transition:transform var(--tr);}}
.platform-badge:hover i{{transform: rotate(360deg);}}

.hero-image-container{{
  width:100%;
  display:flex;
  justify-content:center;
  align-items:center;
  margin:32px 0 16px 0;
  overflow:hidden;
}}
.hero-image{{
  max-width:100%;
  height:auto;
  display:block;
  border-radius:var(--r4);
  box-shadow:var(--sh3);
  transition:transform var(--tr-slow), box-shadow var(--tr);
}}
.hero-image:hover{{
  transform: scale(1.02);
  box-shadow: var(--sh4);
}}
@media (max-width: 768px) {{
  .hero-image{{
    max-width:95%;
  }}
}}

.category-page{{
  padding:20px 0;
  animation: fadeIn 0.3s var(--ease);
}}
.category-header{{
  text-align:center;
  margin-bottom:48px;
}}
.category-header-icon{{
  width:64px;
  height:64px;
  border-radius:20px;
  background:var(--accl);
  display:flex;
  align-items:center;
  justify-content:center;
  margin:0 auto 16px;
  transition:all var(--tr);
}}
.category-header-icon:hover{{
  transform: scale(1.05) rotate(5deg);
  background:var(--accm);
}}
.category-header-icon i{{
  width:32px;
  height:32px;
  color:var(--acc);
  transition:transform var(--tr);
}}
.category-header-icon:hover i{{
  transform: scale(1.1);
}}
.category-title{{
  font-size:2.5rem;
  font-weight:700;
  color:var(--t1);
  margin-bottom:8px;
  letter-spacing:-.02em;
  transition:color var(--tr);
}}
.category-description{{
  font-size:1.125rem;
  color:var(--t2);
  max-width:600px;
  margin:0 auto;
  transition:color var(--tr);
}}

.subcategories-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(300px,1fr));
  gap:20px;
  margin-top:32px;
}}
.subcategory-card{{
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:20px;
  padding:24px;
  cursor:pointer;
  transition:all var(--tr-slow);
  position:relative;
  overflow:hidden;
}}
.subcategory-card:hover{{
  transform:translateY(-6px);
  border-color:var(--acc);
  box-shadow:var(--sh3);
  background:var(--bg);
}}
.subcategory-card:hover .card-arrow{{
  transform:translateX(6px);
  color:var(--acc);
}}
.card-header{{
  display:flex;
  align-items:center;
  justify-content:space-between;
  margin-bottom:16px;
}}
.card-icon{{
  width:40px;
  height:40px;
  border-radius:12px;
  background:var(--accl);
  display:flex;
  align-items:center;
  justify-content:center;
  transition:all var(--tr);
}}
.subcategory-card:hover .card-icon{{
  background:var(--acc);
}}
.subcategory-card:hover .card-icon i{{
  color:white;
}}
.card-icon i{{
  width:20px;
  height:20px;
  color:var(--acc);
  transition:color var(--tr);
}}
.card-count{{
  font-size:.75rem;
  font-weight:500;
  color:var(--t3);
  background:var(--bg3);
  padding:4px 8px;
  border-radius:20px;
  transition:all var(--tr);
}}
.subcategory-card:hover .card-count{{
  background:var(--accl);
  color:var(--acc);
}}
.card-title{{
  font-size:1.25rem;
  font-weight:600;
  color:var(--t1);
  margin-bottom:8px;
  letter-spacing:-.01em;
  transition:color var(--tr);
}}
.card-desc{{
  font-size:.875rem;
  color:var(--t2);
  margin-bottom:16px;
  line-height:1.5;
  transition:color var(--tr);
}}
.card-preview{{
  display:flex;
  flex-wrap:wrap;
  gap:4px;
  margin-bottom:16px;
}}
.preview-tag{{
  font-size:.6875rem;
  padding:2px 8px;
  background:var(--bg3);
  border:1px solid var(--bdr);
  border-radius:12px;
  color:var(--t2);
  transition:all var(--tr);
}}
.subcategory-card:hover .preview-tag{{
  background:var(--bg);
  border-color:var(--acc);
  color:var(--acc);
}}
.preview-more{{
  font-size:.6875rem;
  color:var(--t3);
  padding:2px 4px;
  transition:color var(--tr);
}}
.card-arrow{{
  position:absolute;
  bottom:24px;
  right:24px;
  transition:transform var(--tr-slow),color var(--tr);
}}
.card-arrow i{{
  width:20px;
  height:20px;
  color:var(--t3);
}}

.subcategory-page{{
  padding:20px 0;
  animation: fadeIn 0.3s var(--ease);
}}
.subcategory-header{{
  margin-bottom:32px;
}}
.back-button{{
  display:inline-flex;
  align-items:center;
  gap:6px;
  padding:8px 12px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:100px;
  font-size:.875rem;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
  margin-bottom:16px;
}}
.back-button:hover{{
  background:var(--bg);
  border-color:var(--acc);
  color:var(--acc);
  transform: translateX(-4px);
}}
.back-button i{{
  width:16px;
  height:16px;
  transition:transform var(--tr);
}}
.back-button:hover i{{
  transform: translateX(-2px);
}}
.subcategory-title{{
  font-size:2rem;
  font-weight:700;
  color:var(--t1);
  margin-bottom:8px;
  letter-spacing:-.02em;
  transition:color var(--tr);
}}
.subcategory-description{{
  font-size:1rem;
  color:var(--t2);
  transition:color var(--tr);
}}

.documents-grid{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(350px,1fr));
  gap:16px;
}}
.doc-card{{
  display:flex;
  align-items:center;
  gap:16px;
  padding:16px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:16px;
  cursor:pointer;
  transition:all var(--tr-slow);
}}
.doc-card:hover{{
  transform:translateY(-4px);
  border-color:var(--acc);
  box-shadow:var(--sh3);
  background:var(--bg);
}}
.doc-card:hover .doc-card-arrow{{
  transform:translateX(6px);
  color:var(--acc);
}}
.doc-card-icon{{
  width:40px;
  height:40px;
  border-radius:10px;
  background:var(--accl);
  display:flex;
  align-items:center;
  justify-content:center;
  flex-shrink:0;
  transition:all var(--tr);
}}
.doc-card:hover .doc-card-icon{{
  background:var(--acc);
}}
.doc-card:hover .doc-card-icon i{{
  color:white;
}}
.doc-card-icon i{{
  width:20px;
  height:20px;
  color:var(--acc);
  transition:color var(--tr);
}}
.doc-card-content{{
  flex:1;
  min-width:0;
}}
.doc-card-title{{
  font-size:1rem;
  font-weight:600;
  color:var(--t1);
  margin-bottom:4px;
  transition:color var(--tr);
}}
.doc-card-desc{{
  font-size:.8125rem;
  color:var(--t2);
  margin-bottom:6px;
  line-height:1.4;
  transition:color var(--tr);
}}
.doc-card-platforms{{
  display:flex;
  flex-wrap:wrap;
  gap:4px;
}}
.platform-tag{{
  font-size:.625rem;
  padding:2px 6px;
  background:color-mix(in srgb,var(--tag-color) 9%,transparent);
  border:1px solid color-mix(in srgb,var(--tag-color) 22%,transparent);
  border-radius:12px;
  color:var(--tag-color);
  font-weight:500;
  transition:all var(--tr);
}}
.doc-card:hover .platform-tag{{
  transform: translateY(-1px);
  box-shadow: var(--sh1);
}}
.doc-card-arrow{{
  flex-shrink:0;
  color:var(--t3);
  width:16px;
  height:16px;
  transition:transform var(--tr-slow),color var(--tr);
}}

.quick-links-section{{margin-top:6px}}
.section-heading{{
  font-size:1.125rem;
  font-weight:600;
  color:var(--t1);
  margin-bottom:12px;
  letter-spacing:-.02em;
  transition:color var(--tr);
}}
.quick-links{{
  display:grid;
  grid-template-columns:repeat(auto-fill,minmax(260px,1fr));
  gap:8px;
  margin-bottom:44px;
}}
.quick-link{{
  display:flex;
  align-items:center;
  gap:11px;
  padding:12px 14px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:var(--r3);
  cursor:pointer;
  transition:all var(--tr-slow);
}}
.quick-link:hover{{
  background:var(--bg);
  border-color:var(--acc);
  transform:translateY(-2px);
  box-shadow:var(--sh2);
}}
.ql-icon{{
  width:32px;
  height:32px;
  border-radius:var(--r2);
  background:var(--accl);
  display:flex;
  align-items:center;
  justify-content:center;
  color:var(--acc);
  flex-shrink:0;
  transition:all var(--tr);
}}
.quick-link:hover .ql-icon{{
  background:var(--acc);
  color:white;
}}
.ql-icon i{{width:16px;height:16px; transition:transform var(--tr);}}
.quick-link:hover .ql-icon i{{transform: rotate(360deg);}}
.ql-body{{flex:1;min-width:0}}
.ql-title{{font-size:.875rem;font-weight:600;color:var(--t1);margin-bottom:1px;letter-spacing:-.01em; transition:color var(--tr);}}
.ql-desc{{font-size:.75rem;color:var(--t2);line-height:1.4; transition:color var(--tr);}}
.ql-arrow{{
  color:var(--t3);
  flex-shrink:0;
  width:13px;
  height:13px;
  transition:transform var(--tr-slow),color var(--tr);
}}
.quick-link:hover .ql-arrow{{transform:translateX(4px);color:var(--acc)}}

.doc-hdr{{margin-bottom:24px;padding-bottom:18px;border-bottom:1px solid var(--bdr); transition:border-color var(--tr);}}
.doc-meta{{display:flex;align-items:center;gap:6px;margin-bottom:8px;flex-wrap:wrap}}
.doc-title{{
  font-size:clamp(1.5rem,3.5vw,1.9375rem);
  font-weight:700;
  color:var(--t1);
  letter-spacing:-.03em;
  line-height:1.2;
  margin-bottom:8px;
  transition:color var(--tr);
}}
.doc-acts{{display:flex;gap:6px;margin-top:10px}}
.doc-nav-buttons{{
  display:flex;
  gap:8px;
  margin-bottom:16px;
}}
.doc-back-btn{{
  display:inline-flex;
  align-items:center;
  gap:6px;
  padding:6px 12px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:100px;
  font-size:.8125rem;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
}}
.doc-back-btn:hover{{
  background:var(--bg);
  border-color:var(--acc);
  color:var(--acc);
  transform: translateX(-4px);
}}
.doc-back-btn i{{
  width:14px;
  height:14px;
  transition:transform var(--tr);
}}
.doc-back-btn:hover i{{
  transform: translateX(-2px);
}}
.dbtn{{
  display:flex;
  align-items:center;
  gap:4px;
  padding:5px 10px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:var(--r2);
  font-family:var(--fb);
  font-size:.75rem;
  font-weight:500;
  color:var(--t2);
  cursor:pointer;
  transition:all var(--tr);
}}
.dbtn:hover{{background:var(--bg);border-color:var(--bdr2);color:var(--t1); transform: translateY(-1px); box-shadow: var(--sh1);}}
.dbtn.ok{{background:var(--accl);border-color:var(--acc);color:var(--acc)}}
.dbtn i{{width:12px;height:12px; transition:transform var(--tr);}}
.dbtn:hover i{{transform: scale(1.1);}}

.md{{color:var(--t1);line-height:1.75;font-size:.9375rem; transition:color var(--tr);}}
.md h1,.md h2,.md h3,.md h4{{
  color:var(--t1);
  font-weight:700;
  letter-spacing:-.025em;
  scroll-margin-top:20px;
  line-height:1.25;
  transition:color var(--tr);
}}
.md h1{{
  font-size:1.75rem;
  margin:0 0 16px;
  padding-bottom:12px;
  border-bottom:1px solid var(--bdr);
  transition:border-color var(--tr);
}}
.md h2{{font-size:1.3125rem;margin:40px 0 12px;font-weight:600}}
.md h3{{font-size:1.0625rem;margin:28px 0 9px;font-weight:600}}
.md h4{{font-size:.9375rem;margin:20px 0 7px;color:var(--t2);font-weight:600}}
.md p{{margin:0 0 14px}}
.md p:last-child{{margin-bottom:0}}
.md a{{color:var(--acc);text-decoration:none;font-weight:500; transition:all var(--tr);}}
.md a:hover{{text-decoration:underline; opacity:0.8;}}
.md code{{
  font-family:var(--fm);
  font-size:.82em;
  padding:2px 5px;
  background:var(--bg2);
  border-radius:var(--r1);
  border:1px solid var(--bdr);
  color:var(--t1);
  transition:all var(--tr);
}}
.md pre{{
  margin:18px 0;
  border-radius:var(--r3);
  border:1px solid var(--bdr);
  overflow:hidden;
  background:var(--code-bg);
  position:relative;
  box-shadow:var(--sh1);
  transition:all var(--tr);
}}
.md pre:hover{{
  box-shadow: var(--sh2);
}}
.md pre code{{
  display:block;
  padding:16px 18px;
  overflow-x:auto;
  line-height:1.65;
  background:transparent;
  border:none;
  border-radius:0;
  font-size:.8125rem;
  tab-size:2;
}}
.codehilite{{background:var(--code-bg)!important;margin:0!important;padding:0!important}}
.codehilite pre{{
  margin:0!important;
  padding:16px 18px!important;
  background:var(--code-bg)!important;
  border-radius:0!important;
  border:none!important;
  box-shadow:none!important;
}}
.md pre .codehilite{{border-radius:0;border:none;margin:0;padding:0}}
.copy-btn{{
  position:absolute;
  top:8px;
  right:8px;
  display:flex;
  align-items:center;
  gap:4px;
  padding:3px 8px;
  background:var(--surf);
  border:1px solid var(--bdr);
  border-radius:var(--r2);
  font-family:var(--fb);
  font-size:.6875rem;
  font-weight:500;
  color:var(--t2);
  cursor:pointer;
  opacity:0;
  transition:opacity var(--tr),background var(--tr), transform var(--tr);
  backdrop-filter:blur(8px);
  transform: translateY(-2px);
}}
.md pre:hover .copy-btn{{opacity:1}}
.copy-btn:hover{{background:var(--bg);border-color:var(--bdr2);color:var(--t1); transform: translateY(-3px);}}
.copy-btn.ok{{background:var(--accl);border-color:var(--acc);color:var(--acc);opacity:1}}
.copy-btn i{{width:10px;height:10px; transition:transform var(--tr);}}
.copy-btn:hover i{{transform: scale(1.1);}}
.md table{{
  width:100%;
  margin:18px 0;
  border-collapse:collapse;
  border:1px solid var(--bdr);
  border-radius:var(--r3);
  overflow:hidden;
  font-size:.8125rem;
  box-shadow:var(--sh1);
  transition:all var(--tr);
}}
.md table:hover{{
  box-shadow: var(--sh2);
}}
.md th{{
  padding:9px 13px;
  background:var(--bg2);
  font-weight:600;
  text-align:left;
  font-size:.75rem;
  color:var(--t1);
  border-bottom:1px solid var(--bdr);
  transition:all var(--tr);
}}
.md td{{padding:8px 13px;border-bottom:1px solid var(--bdr);color:var(--t2); transition:all var(--tr);}}
.md tr:last-child td{{border-bottom:none}}
.md tr:hover td{{background:var(--bg2)}}
.md ul,.md ol{{margin:0 0 14px 18px}}
.md li{{margin:4px 0;color:var(--t2); transition:color var(--tr);}}
.md li strong{{color:var(--t1)}}
.md blockquote{{
  margin:18px 0;
  padding:12px 16px;
  border-left:3px solid var(--acc);
  background:var(--accl);
  border-radius:0 var(--r2) var(--r2) 0;
  transition:all var(--tr);
}}
.md blockquote:hover{{
  transform: translateX(2px);
  box-shadow: var(--sh1);
}}
.md blockquote p{{color:var(--t2);margin:0;font-size:.9375rem;font-style:italic}}
.md hr{{border:none;border-top:1px solid var(--bdr);margin:32px 0; transition:border-color var(--tr);}}
.md img{{max-width:100%;border-radius:var(--r3);border:1px solid var(--bdr);box-shadow:var(--sh2); transition:all var(--tr);}}
.md img:hover{{
  transform: scale(1.01);
  box-shadow: var(--sh3);
}}

.mermaid-wrapper{{
  background:var(--bg2);
  border-radius:var(--r3);
  border:1px solid var(--bdr);
  margin:18px 0;
  overflow:hidden;
  box-shadow:var(--sh1);
  transition:all var(--tr-slow);
}}
.mermaid-wrapper:hover{{
  box-shadow: var(--sh2);
}}
.mermaid-wrapper.resized{{
  position:fixed;
  top:50%;
  left:50%;
  transform:translate(-50%,-50%);
  width:88vw;
  height:88vh;
  z-index:1000;
  box-shadow:var(--sh4);
  animation: zoomIn 0.2s var(--ease);
}}
@keyframes zoomIn {{
  from {{ opacity: 0; transform: translate(-50%,-50%) scale(0.9); }}
  to {{ opacity: 1; transform: translate(-50%,-50%) scale(1); }}
}}
.mermaid-wrapper.resized .mermaid{{height:calc(100% - 44px);overflow:auto;padding:18px}}
.mermaid-wrapper.fullscreen{{
  position:fixed;
  inset:0;
  z-index:1000;
  border-radius:0;
  box-shadow:none;
  animation: fadeIn 0.2s var(--ease);
}}
.mermaid-wrapper.fullscreen .mermaid{{height:calc(100% - 44px);overflow:auto;padding:24px}}
.mermaid-controls{{
  display:flex;
  justify-content:flex-end;
  gap:4px;
  padding:7px 9px;
  background:var(--bg);
  border-bottom:1px solid var(--bdr);
  transition:all var(--tr);
}}
.mermaid-export,.mermaid-open{{
  display:flex;
  align-items:center;
  justify-content:center;
  padding:4px;
  width:26px;
  height:26px;
  background:var(--bg2);
  border:1px solid var(--bdr);
  border-radius:var(--r1);
  cursor:pointer;
  color:var(--t2);
  transition:all var(--tr);
}}
.mermaid-export:hover,.mermaid-open:hover{{
  background:var(--accl);
  border-color:var(--acc);
  color:var(--acc);
  transform: scale(1.05);
}}
.mermaid-export i,.mermaid-open i{{width:11px;height:11px; transition:transform var(--tr);}}
.mermaid-export:hover i,.mermaid-open:hover i{{transform: scale(1.1);}}
.mermaid{{
  padding:18px;
  text-align:center;
  min-height:140px;
  display:flex;
  align-items:center;
  justify-content:center;
}}
.mermaid svg{{max-width:100%;height:auto}}
.mermaid-overlay{{
  position:fixed;
  inset:0;
  background:rgba(0,0,0,.45);
  backdrop-filter:blur(8px);
  z-index:999;
  display:none;
  animation: fadeIn 0.2s var(--ease);
}}
.mermaid-overlay.active{{display:block}}

.pg-footer{{
  margin-top:48px;
  padding-top:18px;
  border-top:1px solid var(--bdr);
  display:flex;
  align-items:center;
  justify-content:space-between;
  font-size:.75rem;
  color:var(--t3);
  transition:all var(--tr);
}}
.sb-overlay{{
  display:none;
  position:fixed;
  inset:0;
  background:rgba(0,0,0,.25);
  backdrop-filter:blur(4px);
  z-index:40;
  animation: fadeIn 0.2s var(--ease);
}}
.mob-btn{{
  display:none;
  align-items:center;
  justify-content:center;
  width:32px;
  height:32px;
  background:none;
  border:1px solid var(--bdr);
  border-radius:var(--r2);
  cursor:pointer;
  color:var(--t1);
  transition:all var(--tr);
}}
.mob-btn:hover{{
  background:var(--bg2);
  border-color:var(--acc);
  color:var(--acc);
  transform: scale(1.05);
}}
@media(max-width:1100px){{.toc-p{{display:none!important}}}}
@media(max-width:700px){{
  .sidebar{{
    position:fixed;
    left:0;
    top:0;
    bottom:0;
    transform:translateX(-100%);
    transition:transform var(--tr-slow), background var(--tr);
    z-index:50;
  }}
  .sidebar.open{{transform:translateX(0)}}
  .sb-overlay.open{{display:block}}
  .mob-btn{{display:flex!important}}
  .c-inner{{padding:20px 14px 52px}}
}}
{pygments_styles}
</style>
</head>
<body>
<div class="announcement-bar" id="announcementBar">
  <span class="announcement-text">AromaSDK is built on GLPS, a lightweight platform abstraction layer</span>
  <a href="https://binaryinktn.github.io/GLPS/" target="_blank" class="announcement-link">
    <i data-lucide="github"></i> Check it out
  </a>
  <div class="announcement-close" onclick="dismissAnnouncement()">
    <i data-lucide="x"></i>
  </div>
</div>
<div class="sb-overlay" id="sbOverlay" onclick="closeSidebar()"></div>
<div class="mermaid-overlay" id="mermaidOverlay" onclick="closeMermaidFullscreen()"></div>
<div class="layout">
  <nav class="sidebar" id="sidebar">
    <div class="sb-hdr">
      <div class="logo-row" onclick="showHome()">
        <span class="logo-name">{project_name}</span>
        <span class="logo-version">v{version}</span>
      </div>
      <div class="search-container" id="searchContainer">
        <div class="sb-search" onclick="toggleSearchDropdown()">
          <i data-lucide="search"></i>
          <span class="sb-search-ph">Search docs…</span>
          <span class="sb-search-kbd">⌘K</span>
        </div>
        <div class="search-dropdown" id="searchDropdown">
          <div class="search-header">
            <i data-lucide="search"></i>
            <input type="text" id="searchInput" placeholder="Search documentation..."
                   oninput="handleSearch(this.value)" onkeydown="handleSearchKey(event)" autocomplete="off">
          </div>
          <div class="search-results" id="searchResults"></div>
          <div class="search-footer">
            <span><kbd>↑</kbd><kbd>↓</kbd> navigate</span>
            <span><kbd>↵</kbd> open</span>
            <span><kbd>Esc</kbd> close</span>
          </div>
        </div>
      </div>
      <div class="pf-wrap" id="pfWrap">
        <div class="pf-btn" id="pfBtn" onclick="togglePfDd()">
          <span class="pf-btn-txt" id="pfBtnTxt">All platforms</span>
          <i data-lucide="chevron-down" class="pf-btn-chev"></i>
        </div>
        <div class="pf-dd" id="pfDd"></div>
      </div>
    </div>
    <div class="sb-nav" id="sbNav">
      <div class="nav-item active" data-page="__home__" onclick="showHome()">
        <i data-lucide="home"></i> Home
      </div>
      {sidebar_content}
    </div>
  </nav>
  <div class="main">
    <header class="hdr">
      <button class="mob-btn" onclick="openSidebar()">
        <i data-lucide="menu" style="width:16px;height:16px"></i>
      </button>
      <div class="bc" id="breadcrumbs">
        <span class="bc-home" onclick="showHome()">
         {project_name}
        </span>
        <span class="bc-sep" id="bcHomeSep">/</span>
        <span class="bc-category" id="bcCategory" style="display:none" onclick="showCategoryFromBc()"></span>
        <span class="bc-sep" id="bcCatSep" style="display:none">/</span>
        <span class="bc-subcategory" id="bcSubcategory" style="display:none" onclick="showSubcategoryFromBc()"></span>
        <span class="bc-sep" id="bcSubSep" style="display:none">/</span>
        <span class="bc-cur" id="bcCur">Home</span>
      </div>
      <div class="hdr-r">
     
        <button class="pdf-download-btn" onclick="downloadPDF()" title="Download PDF">
          <i data-lucide="file-text"></i>
        </button>
        <label class="theme-toggle">
          <input type="checkbox" id="themeToggle" onchange="toggleTheme()">
          <span class="theme-slider">
            <i data-lucide="sun" class="sun-icon"></i>
            <i data-lucide="moon" class="moon-icon"></i>
          </span>
        </label>
      </div>
    </header>
    <div class="c-layout">
      <div class="c-scroll" id="cScroll">
        <div class="c-inner">
          <div id="homeView">
            {hero_section}
            {action_cards}
            <div class="pg-footer">
              <span>© {year} {project_name}</span>
              <span>v{version} · Updated {last_updated}</span>
            </div>
          </div>
          <div id="categoryView" style="display:none"></div>
          <div id="subcategoryView" style="display:none"></div>
          <div id="docView" style="display:none">
            <div class="doc-nav-buttons">
              <button class="doc-back-btn" id="docBackBtn" onclick="goBackFromDoc()">
                <i data-lucide="arrow-left"></i> Back
              </button>
            </div>
            <div class="doc-hdr">
              <div class="doc-meta" id="docMeta"></div>
              <h1 class="doc-title" id="docTitle"></h1>
              <div class="doc-acts">
                <button class="dbtn" onclick="copyPageLink(this)">
                  <i data-lucide="link"></i> Copy link
                </button>
              </div>
            </div>
            <div class="md" id="docContent"></div>
            <div class="pg-footer">
              <span>© {year} {project_name}</span>
              <span>v{version} · Updated {last_updated}</span>
            </div>
          </div>
        </div>
      </div>
      <div class="toc-p" id="tocPanel">
        <div class="toc-ttl">On this page</div>
        <div class="toc-prog"><div class="toc-fill" id="tocFill"></div></div>
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

let currentId = null, currentCategory = null, currentSubcategory = null, tocSections = [], srchIdx = -1, activePF = 'all';
let previousView = {{ type: 'home', category: null, subcategory: null, id: null }};
let searchSelectedIndex = -1;
let searchResults = [];

const ic = () => typeof lucide !== 'undefined' && lucide.createIcons();

function dismissAnnouncement() {{
  const bar = document.getElementById('announcementBar');
  bar.classList.add('hidden');
  localStorage.setItem('announcement-dismissed', 'true');
  setTimeout(() => {{
    window.dispatchEvent(new Event('resize'));
  }}, 300);
}}

function setTheme(t) {{
  document.documentElement.setAttribute('data-theme', t);
  localStorage.setItem('docs-theme', t);
  const toggle = document.getElementById('themeToggle');
  if (toggle) toggle.checked = t === 'dark';
  setTimeout(() => rerenderMermaid(t), 100);
}}

function toggleTheme() {{
  const toggle = document.getElementById('themeToggle');
  const newTheme = toggle.checked ? 'dark' : 'light';
  setTheme(newTheme);
  ic();
}}

function initTheme() {{
  const saved = localStorage.getItem('docs-theme') || 'light';
  document.documentElement.setAttribute('data-theme', saved);
  const toggle = document.getElementById('themeToggle');
  if (toggle) toggle.checked = saved === 'dark';
  ic();
}}

function updateBreadcrumbs() {{
  const bcCategory = document.getElementById('bcCategory');
  const bcSubcategory = document.getElementById('bcSubcategory');
  const bcCatSep = document.getElementById('bcCatSep');
  const bcSubSep = document.getElementById('bcSubSep');
  const bcCur = document.getElementById('bcCur');

  if (currentId) {{
    bcCategory.style.display = currentCategory ? 'inline-block' : 'none';
    bcCategory.textContent = currentCategory || '';
    bcSubcategory.style.display = currentSubcategory ? 'inline-block' : 'none';
    bcSubcategory.textContent = currentSubcategory || '';
    bcCatSep.style.display = currentCategory ? 'inline-block' : 'none';
    bcSubSep.style.display = currentSubcategory ? 'inline-block' : 'none';
    bcCur.style.display = 'inline-block';
    bcCur.textContent = TITLES[currentId] || 'Document';
  }} else if (currentSubcategory) {{
    bcCategory.style.display = 'inline-block';
    bcCategory.textContent = currentCategory;
    bcSubcategory.style.display = 'inline-block';
    bcSubcategory.textContent = currentSubcategory;
    bcCatSep.style.display = 'inline-block';
    bcSubSep.style.display = 'inline-block';
    bcCur.style.display = 'none';
  }} else if (currentCategory) {{
    bcCategory.style.display = 'inline-block';
    bcCategory.textContent = currentCategory;
    bcSubcategory.style.display = 'none';
    bcCatSep.style.display = 'inline-block';
    bcSubSep.style.display = 'none';
    bcCur.style.display = 'none';
  }} else {{
    bcCategory.style.display = 'none';
    bcSubcategory.style.display = 'none';
    bcCatSep.style.display = 'none';
    bcSubSep.style.display = 'none';
    bcCur.style.display = 'inline-block';
    bcCur.textContent = 'Home';
  }}
}}

function showCategoryFromBc() {{
  if (currentCategory) showCategory(currentCategory);
}}

function showSubcategoryFromBc() {{
  if (currentCategory && currentSubcategory) showSubcategory(currentCategory, currentSubcategory);
}}

function showCategory(category) {{
  if (!CATEGORY_PAGES[category]) return;
  previousView = {{ type: 'category', category: category, subcategory: null, id: null }};
  document.getElementById('homeView').style.display = 'none';
  document.getElementById('categoryView').style.display = '';
  document.getElementById('subcategoryView').style.display = 'none';
  document.getElementById('docView').style.display = 'none';
  document.getElementById('categoryView').innerHTML = CATEGORY_PAGES[category];
  document.getElementById('tocPanel').classList.remove('vis');
  currentCategory = category;
  currentSubcategory = null;
  currentId = null;
  updateBreadcrumbs();
  setActiveNav(null);
  history.replaceState(null, '', '#' + category);
  document.getElementById('cScroll').scrollTop = 0;
  closeSidebar();
  setTimeout(() => {{
    ic();
  }}, 50);
}}

function showSubcategory(category, subcategory) {{
  const key = category + '||' + subcategory;
  if (!SUBCATEGORY_PAGES[key]) return;
  previousView = {{ type: 'subcategory', category: category, subcategory: subcategory, id: null }};
  document.getElementById('homeView').style.display = 'none';
  document.getElementById('categoryView').style.display = 'none';
  document.getElementById('subcategoryView').style.display = '';
  document.getElementById('docView').style.display = 'none';
  document.getElementById('subcategoryView').innerHTML = SUBCATEGORY_PAGES[key];
  document.getElementById('tocPanel').classList.remove('vis');
  currentCategory = category;
  currentSubcategory = subcategory;
  currentId = null;
  updateBreadcrumbs();
  setActiveNav(null);
  history.replaceState(null, '', '#' + category + '/' + subcategory);
  document.getElementById('cScroll').scrollTop = 0;
  closeSidebar();
  setTimeout(() => {{
    ic();
  }}, 50);
}}

function showHome() {{
  previousView = {{ type: 'home', category: null, subcategory: null, id: null }};
  document.getElementById('homeView').style.display = '';
  document.getElementById('categoryView').style.display = 'none';
  document.getElementById('subcategoryView').style.display = 'none';
  document.getElementById('docView').style.display = 'none';
  currentCategory = null;
  currentSubcategory = null;
  currentId = null;
  updateBreadcrumbs();
  setActiveNav(null);
  history.replaceState(null, '', location.pathname);
  document.getElementById('tocPanel').classList.remove('vis');
  closeSidebar();
  document.getElementById('cScroll').scrollTop = 0;
}}

function showPage(id, category = null, subcategory = null) {{
  if (!PAGES[id]) return;
  if (category && subcategory) {{
    previousView = {{
      type: 'subcategory',
      category: category,
      subcategory: subcategory,
      id: null
    }};
  }} else if (category) {{
    previousView = {{
      type: 'category',
      category: category,
      subcategory: null,
      id: null
    }};
  }} else {{
    previousView = {{
      type: 'home',
      category: null,
      subcategory: null,
      id: null
    }};
  }}
  document.getElementById('homeView').style.display = 'none';
  document.getElementById('categoryView').style.display = 'none';
  document.getElementById('subcategoryView').style.display = 'none';
  document.getElementById('docView').style.display = '';
  document.getElementById('docContent').innerHTML = PAGES[id];
  document.getElementById('docTitle').textContent = TITLES[id] || id;
  const meta = document.getElementById('docMeta');
  meta.innerHTML = '';
  (PAGE_PLATFORMS[id] || []).forEach(p => {{
    const b = document.createElement('span');
    b.className = 'platform-badge';
    b.style.setProperty('--badge-color', p.color || '#636366');
    b.innerHTML = `<span>${{p.name}}</span>`;
    meta.appendChild(b);
  }});
  setActiveNav(id);
  history.replaceState(null, '', '#' + id);
  currentId = id;
  currentCategory = CATS[id] || null;
  currentSubcategory = SUBCATS[id] || null;
  updateBreadcrumbs();
  document.getElementById('cScroll').scrollTop = 0;
  closeSidebar();
  setTimeout(() => {{
    addCopyBtns();
    initMermaid(localStorage.getItem('docs-theme') || 'light');
    ic();
    buildToc();
  }}, 60);
}}

function goBackFromDoc() {{
  if (previousView.type === 'subcategory' && previousView.category && previousView.subcategory) {{
    showSubcategory(previousView.category, previousView.subcategory);
  }} else if (previousView.type === 'category' && previousView.category) {{
    showCategory(previousView.category);
  }} else {{
    showHome();
  }}
}}

function setActiveNav(id) {{
  document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
  const t = id
    ? document.querySelector(`.nav-item[data-page="${{id}}"]`)
    : document.querySelector('.nav-item[data-page="__home__"]');
  if (t) t.classList.add('active');
}}

function buildToc() {{
  const headings = document.getElementById('docContent').querySelectorAll('h1,h2,h3');
  const list = document.getElementById('tocList');
  list.innerHTML = '';
  tocSections = [];
  headings.forEach((h, i) => {{
    if (!h.id) h.id = 'h-' + i + '-' + h.textContent.toLowerCase().replace(/[^a-z0-9]+/g, '-');
    tocSections.push({{ id: h.id, el: h, level: +h.tagName[1] }});
    const item = document.createElement('div');
    item.className = 'toc-item';
    item.style.paddingLeft = ((+h.tagName[1] - 1) * 9 + 12) + 'px';
    item.textContent = h.textContent;
    item.dataset.id = h.id;
    item.onclick = () => {{
      document.getElementById('cScroll').scrollTo({{ top: h.offsetTop - 64, behavior: 'smooth' }});
    }};
    list.appendChild(item);
  }});
  document.getElementById('tocPanel').classList.toggle('vis', tocSections.length > 0);
}}

document.getElementById('cScroll').addEventListener('scroll', function() {{
  const tot = this.scrollHeight - this.clientHeight;
  const pct = tot > 0 ? Math.min(100, Math.round((this.scrollTop / tot) * 100)) : 0;
  document.getElementById('tocFill').style.width = pct + '%';
  let active = null;
  const top = this.scrollTop + 100;
  tocSections.forEach(s => {{ if (s.el.offsetTop <= top) active = s.id; }});
  document.querySelectorAll('.toc-item').forEach(el => el.classList.toggle('active', el.dataset.id === active));
}});

function addCopyBtns() {{
  document.querySelectorAll('.md pre').forEach(pre => {{
    if (pre.querySelector('.copy-btn')) return;
    const btn = document.createElement('button');
    btn.className = 'copy-btn';
    btn.innerHTML = `<i data-lucide="copy"></i> Copy`;
    btn.onclick = async () => {{
      const code = pre.querySelector('code');
      if (!code) return;
      await navigator.clipboard.writeText(code.textContent);
      btn.classList.add('ok');
      btn.innerHTML = `<i data-lucide="check"></i> Copied`;
      ic();
      setTimeout(() => {{
        btn.classList.remove('ok');
        btn.innerHTML = `<i data-lucide="copy"></i> Copy`;
        ic();
      }}, 2000);
    }};
    pre.appendChild(btn);
  }});
  ic();
}}

function copyPageLink(btn) {{
  navigator.clipboard.writeText(location.href);
  btn.classList.add('ok');
  btn.innerHTML = `<i data-lucide="check"></i> Copied`;
  ic();
  setTimeout(() => {{
    btn.classList.remove('ok');
    btn.innerHTML = `<i data-lucide="link"></i> Copy link`;
    ic();
  }}, 2000);
}}

function downloadPDF() {{
  if (PDF_URL) window.open(PDF_URL, '_blank');
}}

function exportMermaidAsSVG(btn) {{
  const wrapper = btn.closest('.mermaid-wrapper');
  const mermaidEl = wrapper.querySelector('.mermaid');
  const svg = mermaidEl.querySelector('svg');
  if (!svg) return;
  const svgClone = svg.cloneNode(true);
  svgClone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  svgClone.setAttribute('xmlns:xlink', 'http://www.w3.org/1999/xlink');
  const svgString = new XMLSerializer().serializeToString(svgClone);
  const blob = new Blob([svgString], {{ type: 'image/svg+xml' }});
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'diagram.svg';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}}

function openMermaidInNewPage(btn) {{
  const wrapper = btn.closest('.mermaid-wrapper');
  const mermaidEl = wrapper.querySelector('.mermaid');
  const svg = mermaidEl.querySelector('svg');
  if (!svg) return;
  const svgClone = svg.cloneNode(true);
  svgClone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  svgClone.setAttribute('xmlns:xlink', 'http://www.w3.org/1999/xlink');
  const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Mermaid Diagram</title>
  <style>
    body {{
      margin: 0;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      font-family: -apple-system, BlinkMacSystemFont, 'Helvetica Neue', sans-serif;
    }}

    @media (prefers-color-scheme: dark) {{
      body {{ background: #1c1c1e; }}
    }}
  </style>
</head>
<body>
    ${{new XMLSerializer().serializeToString(svgClone)}}

</body>
</html>`;
  const win = window.open('', '_blank');
  win.document.write(html);
  win.document.close();
}}

function toggleSearchDropdown() {{
  const dropdown = document.getElementById('searchDropdown');
  const isOpen = dropdown.classList.contains('open');
  if (isOpen) {{
    closeSearchDropdown();
  }} else {{
    openSearchDropdown();
  }}
}}

function openSearchDropdown() {{
  const dropdown = document.getElementById('searchDropdown');
  dropdown.classList.add('open');
  setTimeout(() => {{
    document.getElementById('searchInput').focus();
  }}, 50);
  handleSearch('');
}}

function closeSearchDropdown() {{
  const dropdown = document.getElementById('searchDropdown');
  dropdown.classList.remove('open');
  document.getElementById('searchInput').value = '';
  searchSelectedIndex = -1;
}}

function handleSearch(query) {{
  const q = query.toLowerCase().trim();
  if (!q) {{
    searchResults = Object.keys(TITLES).slice(0, 8).map(id => ({{
      id: id,
      title: TITLES[id],
      category: CATS[id] || '',
      subcategory: SUBCATS[id] || ''
    }}));
  }} else {{
    searchResults = SEARCH_INDEX
      .filter(item => 
        item.title.toLowerCase().includes(q) ||
        item.category.toLowerCase().includes(q) ||
        (item.subcategory && item.subcategory.toLowerCase().includes(q))
      )
      .slice(0, 8);
  }}
  renderSearchResults();
}}

function renderSearchResults() {{
  const container = document.getElementById('searchResults');
  if (!searchResults.length) {{
    container.innerHTML = '<div class="search-empty">No results found</div>';
    return;
  }}
  container.innerHTML = searchResults.map((item, index) => `
    <div class="search-item" data-index="${{index}}" data-id="${{item.id}}"
         onclick="selectSearchResult('${{item.id}}')">
      <div class="search-item-icon">
        <i data-lucide="file-text"></i>
      </div>
      <div class="search-item-content">
        <div class="search-item-title">${{item.title}}</div>
        <div class="search-item-path">
          ${{[item.category, item.subcategory].filter(Boolean).join(' › ')}}
        </div>
      </div>
    </div>
  `).join('');
  lucide.createIcons();
  searchSelectedIndex = -1;
}}

function selectSearchResult(id) {{
  closeSearchDropdown();
  showPage(id);
}}

function handleSearchKey(e) {{
  const items = document.querySelectorAll('.search-item');
  switch(e.key) {{
    case 'ArrowDown':
      e.preventDefault();
      if (items.length) {{
        searchSelectedIndex = Math.min(searchSelectedIndex + 1, items.length - 1);
        updateSearchSelection(items);
      }}
      break;
    case 'ArrowUp':
      e.preventDefault();
      if (items.length) {{
        searchSelectedIndex = Math.max(searchSelectedIndex - 1, 0);
        updateSearchSelection(items);
      }}
      break;
    case 'Enter':
      e.preventDefault();
      if (searchSelectedIndex >= 0 && items[searchSelectedIndex]) {{
        const id = items[searchSelectedIndex].dataset.id;
        selectSearchResult(id);
      }}
      break;
    case 'Escape':
      e.preventDefault();
      closeSearchDropdown();
      break;
  }}
}}

function updateSearchSelection(items) {{
  items.forEach((el, i) => {{
    el.classList.toggle('selected', i === searchSelectedIndex);
    if (i === searchSelectedIndex) {{
      el.scrollIntoView({{ block: 'nearest' }});
    }}
  }});
}}

document.addEventListener('click', function(e) {{
  const container = document.getElementById('searchContainer');
  const dropdown = document.getElementById('searchDropdown');
  if (!container.contains(e.target) && dropdown.classList.contains('open')) {{
    closeSearchDropdown();
  }}
}});

const PICONS = {{
  ios:'smartphone', android:'smartphone', web:'globe', windows:'monitor',
  macos:'monitor', linux:'terminal', docker:'box', kubernetes:'layers',
  aws:'cloud', azure:'cloud', gcp:'cloud', python:'terminal',
  javascript:'code-2', typescript:'code-2', react:'code-2', vue:'code-2',
  angular:'code-2', node:'server', go:'terminal', rust:'terminal',
  java:'coffee', kotlin:'code-2', swift:'smartphone', flutter:'smartphone',
}};

function initPF() {{
  const all = {{}};
  Object.values(PAGE_PLATFORMS).forEach(arr => {{
    (arr || []).forEach(p => {{
      if (p && p.name && !all[p.name]) all[p.name] = p;
    }});
  }});
  const entries = Object.values(all).sort((a, b) => a.name.localeCompare(b.name));
  if (!entries.length) return;
  const dd = document.getElementById('pfDd');
  const allOpt = document.createElement('div');
  allOpt.className = 'pf-opt active';
  allOpt.dataset.p = 'all';
  allOpt.onclick = function(e) {{ e.stopPropagation(); filterPlatform('all'); }};
  allOpt.innerHTML = `
    <span class="pf-opt-name">All platforms</span>
    <i data-lucide="check" class="pf-opt-chk"></i>`;
  dd.appendChild(allOpt);
  const divider = document.createElement('div');
  divider.className = 'pf-divider';
  dd.appendChild(divider);
  entries.forEach(p => {{
    const opt = document.createElement('div');
    opt.className = 'pf-opt';
    opt.dataset.p = p.name;
    opt.onclick = function(e) {{ e.stopPropagation(); filterPlatform(p.name); }};
    opt.innerHTML = `
      <span class="pf-opt-name">${{p.name}}</span>
      <i data-lucide="check" class="pf-opt-chk"></i>`;
    dd.appendChild(opt);
  }});
  ic();
  document.addEventListener('click', function(e) {{
    const pfWrap = document.getElementById('pfWrap');
    const pfDd = document.getElementById('pfDd');
    if (!pfWrap.contains(e.target)) {{
      pfDd.classList.remove('open');
      document.getElementById('pfBtn').classList.remove('open');
    }}
  }});
}}

function togglePfDd() {{
  const btn = document.getElementById('pfBtn');
  const dd  = document.getElementById('pfDd');
  const open = dd.classList.toggle('open');
  btn.classList.toggle('open', open);
}}

function filterPlatform(p) {{
  activePF = p;
  document.querySelectorAll('.pf-opt').forEach(o => {{
    o.classList.toggle('active', o.dataset.p === p);
  }});
  const txtEl = document.getElementById('pfBtnTxt');
  if (p === 'all') {{
    txtEl.textContent = 'All platforms';
  }} else {{
    txtEl.textContent = p;
  }}
  document.querySelectorAll('.nav-item[data-page]').forEach(el => {{
    if (el.dataset.page === '__home__') return;
    if (p === 'all') {{
      el.classList.remove('ph');
    }} else {{
      const pp = (PAGE_PLATFORMS[el.dataset.page] || []).map(x => x.name);
      el.classList.toggle('ph', pp.length > 0 && !pp.includes(p));
    }}
  }});
  document.querySelectorAll('.sec-items,.sub-items').forEach(grp => {{
    const vis = [...grp.querySelectorAll('.nav-item[data-page]')].some(e => !e.classList.contains('ph'));
    const label = grp.previousElementSibling;
    if (label && label.classList.contains('sec-label')) {{
      label.classList.toggle('sec-empty', !vis);
    }}
  }});
  closePfDd();
  ic();
}}

function closePfDd() {{
  document.getElementById('pfDd').classList.remove('open');
  document.getElementById('pfBtn').classList.remove('open');
}}

function mCfg(t) {{
  const d = t === 'dark';
  return {{
    theme: 'base',
    themeVariables: {{
      background:          d ? '#111111' : '#ffffff',
      primaryColor:        d ? '#2997ff' : '#0071e3',
      primaryTextColor:    d ? '#f5f5f7' : '#1d1d1f',
      primaryBorderColor:  d ? 'rgba(255,255,255,.12)' : 'rgba(0,0,0,.10)',
      lineColor:           d ? '#636366' : '#aeaeb2',
      secondaryColor:      d ? '#1c1c1e' : '#f5f5f7',
      tertiaryColor:       d ? '#111111' : '#ffffff',
      clusterBkg:          d ? '#1c1c1e' : '#f5f5f7',
      nodeTextColor:       d ? '#000000' : '#1d1d1f',
      edgeLabelBackground: d ? '#1c1c1e' : '#ffffff',
      fontFamily:          "-apple-system, 'Helvetica Neue', sans-serif",
      fontSize:            '13px',
    }},
    startOnLoad: false,
    securityLevel: 'loose',
    logLevel: 'error',
    flowchart: {{ useMaxWidth: true, htmlLabels: true, curve: 'basis' }},
  }};
}}

let _mermaidInited = false;

function initMermaid(t) {{
  if (typeof mermaid === 'undefined') {{
    setTimeout(() => initMermaid(t), 150);
    return;
  }}
  try {{
    mermaid.initialize(mCfg(t));
    _mermaidInited = true;
    document.querySelectorAll('.mermaid').forEach(el => {{
      if (!el.getAttribute('data-src')) el.setAttribute('data-src', el.innerHTML);
    }});
    mermaid.run({{ querySelector: '.mermaid' }});
  }} catch(e) {{
    console.warn('mermaid init error:', e);
  }}
}}

function rerenderMermaid(t) {{
  if (typeof mermaid === 'undefined') return;
  try {{
    mermaid.initialize(mCfg(t));
    const els = document.querySelectorAll('.mermaid');
    els.forEach(el => {{
      const src = el.getAttribute('data-src');
      if (src) {{
        el.innerHTML = src;
        el.removeAttribute('data-processed');
      }}
    }});
    if (els.length > 0) {{
      mermaid.run({{ querySelector: '.mermaid' }});
    }}
  }} catch(e) {{
    console.warn('mermaid rerender error:', e);
  }}
}}

function closeMermaidFullscreen() {{
  const w = document.querySelector('.mermaid-wrapper.fullscreen');
  if (!w) return;
  w.classList.remove('fullscreen');
  document.getElementById('mermaidOverlay').classList.remove('active');
  const b = w.querySelector('.mermaid-fullscreen i');
  if (b) {{ b.setAttribute('data-lucide', 'fullscreen'); ic(); }}
}}

function openSidebar() {{
  document.getElementById('sidebar').classList.add('open');
  document.getElementById('sbOverlay').classList.add('open');
}}
function closeSidebar() {{
  document.getElementById('sidebar').classList.remove('open');
  document.getElementById('sbOverlay').classList.remove('open');
}}

function toggleSec(id) {{
  const el = document.getElementById('si-' + id);
  if (!el) return;
  const hdr = el.previousElementSibling;
  const chev = hdr?.querySelector('.sec-chev');
  const c = el.classList.toggle('c');
  if (chev) {{
    chev.classList.toggle('c', c);
    chev.setAttribute('data-lucide', c ? 'chevron-right' : 'chevron-down');
    ic();
  }}
}}

function toggleSub(id) {{
  const el = document.getElementById('ssi-' + id);
  if (!el) return;
  const hdr = document.querySelector(`[data-sg="${{id}}"]`);
  const chev = hdr?.querySelector('.sub-chev');
  const c = el.classList.toggle('c');
  if (chev) {{
    chev.classList.toggle('c', c);
    chev.setAttribute('data-lucide', c ? 'chevron-right' : 'chevron-down');
    ic();
  }}
}}

document.addEventListener('keydown', e => {{
  if ((e.metaKey || e.ctrlKey) && e.key === 'k') {{
    e.preventDefault();
    openSearchDropdown();
  }}
  if (e.key === '/' && !['INPUT', 'TEXTAREA'].includes(document.activeElement.tagName)) {{
    e.preventDefault();
    openSearchDropdown();
  }}
  if (e.key === 'Escape') {{
    if (document.getElementById('searchDropdown').classList.contains('open')) {{
      closeSearchDropdown();
    }}
    if (document.querySelector('.mermaid-wrapper.fullscreen')) {{
      closeMermaidFullscreen();
    }}
  }}
}});

document.addEventListener('DOMContentLoaded', () => {{
  initTheme();
  initPF();
  const dismissed = localStorage.getItem('announcement-dismissed');
  if (dismissed === 'true') {{
    document.getElementById('announcementBar').classList.add('hidden');
  }}
  const hash = window.location.hash.slice(1);
  if (hash) {{
    if (hash.includes('/')) {{
      const [category, subcategory] = hash.split('/');
      if (SUBCATEGORY_PAGES[category + '||' + subcategory]) {{
        showSubcategory(category, subcategory);
        return;
      }}
    }} else if (CATEGORY_PAGES[hash]) {{
      showCategory(hash);
      return;
    }} else if (PAGES[hash]) {{
      showPage(hash);
      return;
    }}
  }}
  addCopyBtns();
  initMermaid(localStorage.getItem('docs-theme') || 'light');
  ic();
}});
</script>
</body>
</html>"""
    def generate_pdf(self, config, pages_dict, titles_dict, page_platforms, output_file):
        from jinja2 import Template
        template = Template(self.pdf_template)
        sections, toc, pn = [], [], 3

        print("  Pre-rendering Mermaid diagrams for PDF…")
        for sid, raw_content in pages_dict.items():
            content = _replace_mermaid_with_svg(raw_content, self._mermaid)
            sections.append({'title': titles_dict.get(sid, sid), 'content': content})
            toc.append({'title': titles_dict.get(sid, sid), 'page': pn})
            pn += 1

        html = template.render(
            title    = config.get('name', 'Documentation'),
            subtitle = config.get('description', ''),
            version  = config.get('version', '1.0.0'),
            date     = datetime.now().strftime('%B %d, %Y'),
            year     = datetime.now().year,
            company  = config.get('company', ''),
            toc      = toc,
            sections = sections,
        )
        with tempfile.NamedTemporaryFile(mode='w', suffix='.html', encoding='utf-8', delete=False) as f:
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

    def generate(self, config_file: str, output_file: str, pdf_output: Optional[str] = None):
        with open(config_file, "r", encoding="utf-8") as f:
            if config_file.endswith(".json"):
                config = json.load(f)
            elif config_file.endswith((".yml", ".yaml")):
                config = yaml.safe_load(f)
            else:
                raise ValueError("Config must be .json, .yml, or .yaml")

        project_name    = config.get("name", "Docs")
        project_version = config.get("version", "1.0.0")
        description     = config.get("description", "Documentation")
        base_dir        = os.path.dirname(os.path.abspath(config_file))
        sections        = config.get("sections", [])
        categories      = config.get("categories", [])

        if not categories:
            cat_names = sorted(set(s.get("category", "General") for s in sections))
            categories = [{"name": n, "icon": "folder", "description": f"Documentation for {n}"} for n in cat_names]

        pages_dict: Dict[str, str] = {}
        titles_dict: Dict[str, str] = {}
        page_objects: Dict[str, Dict] = {}
        
        for s in sections:
            title = s.get("title", "Untitled")
            sid = hashlib.md5(title.encode()).hexdigest()[:8]
            
            mdf = s.get("file", "")
            if mdf and not os.path.isabs(mdf):
                mdf = os.path.join(base_dir, mdf)
            
            pages_dict[sid] = self.load_markdown(mdf) if mdf and os.path.exists(mdf) else f"<h1>{title}</h1><p>{s.get('description','')}</p>"
            titles_dict[sid] = title
            
            page_objects[sid] = {
                "id": sid,
                "title": title,
                "description": s.get("description", f"Documentation for {title}"),
                "icon": s.get("icon", "file-text"),
                "category": s.get("category", "General"),
                "subcategory": s.get("subcategory", ""),
                "platforms": s.get("platforms", [])
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
                "content": content_text[:1000]
            })

        hero_html = self._hero_html(config)
        quick_links_html = self._quick_links_html(config)

        sb = []
        for cat in categories:
            cname = cat.get("name", "General")
            cid = re.sub(r"[^a-z0-9]+", "-", cname.lower())
            csects = sidebar_sections.get(cname, {})
            if not csects:
                continue

            sb.append(
                f'<div>'
                f'<div class="sec-label" onclick="toggleSec(\'{cid}\')">'
                f'<i data-lucide="{cat.get("icon","folder")}" style="width:11px;height:11px"></i>'
                f'{cname}'
                f'<i data-lucide="chevron-down" class="sec-chev"></i>'
                f'</div>'
                f'<div class="sec-items" id="si-{cid}">'
            )

            sb.append(
                f'<div class="nav-item" onclick="showCategory(\'{cname}\')">'
                f'<i data-lucide="layout-grid"></i>All {cname}</div>'
            )

            for page in csects.get("", []):
                sb.append(
                    f'<div class="nav-item" data-page="{page["id"]}" onclick="showPage(\'{page["id"]}\', \'{cname}\', null)">'
                    f'<i data-lucide="{page["icon"]}"></i>{page["title"]}</div>'
                )

            for sub_name, sub_items in csects.items():
                if not sub_name:
                    continue
                sub_id = f"{cid}--{re.sub(r'[^a-z0-9]+', '-', sub_name.lower())}"
                sb.append(
                    f'<div class="sub-label" data-sg="{sub_id}" onclick="toggleSub(\'{sub_id}\')">'
                    f'{sub_name}'
                    f'<i data-lucide="chevron-down" class="sub-chev"></i>'
                    f'</div>'
                    f'<div class="sub-items" id="ssi-{sub_id}">'
                )
                
                sb.append(
                    f'<div class="nav-item sub" onclick="showSubcategory(\'{cname}\', \'{sub_name}\')">'
                    f'<i data-lucide="layers"></i>All {sub_name}</div>'
                )
                
                for page in sub_items:
                    sb.append(
                        f'<div class="nav-item sub" data-page="{page["id"]}" onclick="showPage(\'{page["id"]}\', \'{cname}\', \'{sub_name}\')">'
                        f'<i data-lucide="{page["icon"]}"></i>{page["title"]}</div>'
                    )
                sb.append('</div>')

            sb.append('</div></div>')

        category_pages: Dict[str, str] = {}
        for cat in categories:
            cname = cat.get("name", "General")
            if cname in sidebar_sections:
                category_pages[cname] = self._category_page_html(cat, sidebar_sections[cname], page_objects, titles_dict)

        subcategory_pages: Dict[str, str] = {}
        for cat_name, subcats in sidebar_sections.items():
            for sub_name, pages in subcats.items():
                if sub_name:
                    key = f"{cat_name}||{sub_name}"
                    subcategory_pages[key] = self._subcategory_page_html(cat_name, sub_name, pages, titles_dict, pages_dict)

        pdf_url = os.path.basename(pdf_output) if pdf_output else ""

        html = self._build_template().format(
            project_name       = project_name,
            version            = project_version,
            description        = description,
            hero_section       = hero_html,
            action_cards       = quick_links_html,
            pygments_styles    = self._pygments_css(),
            sidebar_content    = "\n".join(sb),
            pages_json         = json.dumps(pages_dict),
            titles_json        = json.dumps(titles_dict),
            categories_json    = json.dumps(page_categories),
            subcategories_json = json.dumps(page_subcats),
            platforms_json     = json.dumps(page_platforms),
            category_pages_json = json.dumps(category_pages),
            subcategory_pages_json = json.dumps(subcategory_pages),
            search_index_json  = json.dumps(search_index),
            year               = datetime.now().year,
            last_updated       = datetime.now().strftime("%b %d, %Y"),
            pdf_url            = pdf_url,
        )

        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(html)
        print(f"✓ HTML generated: {output_file}")

        if pdf_output:
            try:
                self.generate_pdf(config, pages_dict, titles_dict, page_platforms, pdf_output)
            except Exception as e:
                print(f"✗ PDF failed: {e}\n  Install weasyprint: pip install weasyprint")


def main():
    parser = argparse.ArgumentParser(
        description="Aroma Documentation Generator",
        epilog="Example:\n  %(prog)s -c docs.yaml -o output/index.html --pdf output/docs.pdf"
    )
    parser.add_argument("-c", "--config",  required=True, help="Config file (.json/.yml/.yaml)")
    parser.add_argument("-o", "--output",  default="docs/index.html", help="Output HTML file")
    parser.add_argument("--pdf",           help="Generate PDF (requires weasyprint)")
    parser.add_argument("--version",       action="version", version="v2.0")
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f"Error: config not found: {args.config}")
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