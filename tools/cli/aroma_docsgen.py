#!/usr/bin/env python3

import argparse
import json
import os
import re
from datetime import datetime
from typing import Dict, List

import markdown
import yaml
from markdown.extensions import Extension
from markdown.preprocessors import Preprocessor
from pygments.formatters import HtmlFormatter


class MermaidPreprocessor(Preprocessor):
    def run(self, lines):
        new_lines = []
        i = 0

        while i < len(lines):
            line = lines[i]

            if line.strip() == "```mermaid":
                mermaid_content = []
                i += 1

                while i < len(lines) and not lines[i].strip() == "```":
                    mermaid_content.append(lines[i])
                    i += 1

                if i < len(lines):
                    i += 1

                if mermaid_content:
                    while mermaid_content and not mermaid_content[0].strip():
                        mermaid_content.pop(0)

                    mermaid_html = '<div class="mermaid-wrapper">\n'
                    mermaid_html += '<div class="mermaid-controls">\n'
                    mermaid_html += '<button class="mermaid-resize" onclick="toggleMermaidSize(this)"><i data-lucide="maximize-2"></i></button>\n'
                    mermaid_html += '<button class="mermaid-fullscreen" onclick="fullscreenMermaid(this)"><i data-lucide="fullscreen"></i></button>\n'
                    mermaid_html += "</div>\n"
                    mermaid_html += '<div class="mermaid">\n'
                    mermaid_html += "\n".join(mermaid_content)
                    mermaid_html += "\n</div>\n"
                    mermaid_html += "</div>"
                    new_lines.append(mermaid_html)
                continue

            new_lines.append(line)
            i += 1

        return new_lines


class MermaidExtension(Extension):
    def extendMarkdown(self, md):
        md.preprocessors.register(MermaidPreprocessor(md), "mermaid", 175)


class DocGenerator:
    def __init__(self):
        self.template = self._get_template()

    def _get_pygments_styles(self) -> str:
        # Use built-in Pygments styles
        light_formatter = HtmlFormatter(style="default", noclasses=False)
        dark_formatter = HtmlFormatter(style="monokai", noclasses=False)

        light_styles = light_formatter.get_style_defs(".codehilite")
        dark_styles = dark_formatter.get_style_defs(".codehilite")

        return f"""
        {light_styles}
        [data-theme="dark"] .codehilite {{ {dark_styles} }}
        [data-theme="sepia"] .codehilite {{ {light_styles} }}
        """

    def _get_hero_section(self, config: Dict) -> str:
        hero_config = config.get("hero", {})
        hero_title = hero_config.get("title", config.get("name", "Docs"))
        description = hero_config.get("description", "Comprehensive documentation")

        platform_icons_html = ""
        for platform in hero_config.get("platformIcons", []):
            platform_icons_html += f'<div class="hero-badge" title="{platform.get("title", "")}">{platform.get("title", "")}</div>'

        actions_html = ""
        for action in hero_config.get("actions", []):
            btn_class = "btn-primary" if action.get("primary") else "btn-outline"
            onclick = action.get("onclick", "")
            actions_html += f'<a href="#" class="hero-btn {btn_class}" onclick="{onclick}; return false;">{action.get("text", "")}</a>'

        return f"""
        <section class="hero">
            <h1 class="hero-title">{hero_title}</h1>
            <p class="hero-desc">{description}</p>
            <div class="hero-actions">{actions_html}</div>
            <div class="hero-badges">{platform_icons_html}</div>
            <img src="images/hero-image.png" alt="Hero Image" class="hero-image" style="max-width: 90rem; width: auto;">
        </section>
        """

    def _get_action_cards(self) -> str:
        return """
                <h1>Some links you might like:</h1>
<br>
        <div class="quick-links">
            <div class="quick-link" onclick="showPage('sdk-installation')">
                <div class="ql-icon">
                    <i data-lucide="play"></i>
                </div>
                <div class="ql-body">
                    <div class="ql-title">Get Started</div>
                    <div class="ql-desc">Quick start &amp; installation</div>
                </div>
                <i data-lucide="arrow-right" class="ql-arrow"></i>
            </div>
            <div class="quick-link" onclick="showPage('architecture-overview')">
                <div class="ql-icon">
                    <i data-lucide="layers"></i>
                </div>
                <div class="ql-body">
                    <div class="ql-title">Architecture Overview</div>
                    <div class="ql-desc">Learn about AromaUI's architecture</div>
                </div>
                <i data-lucide="arrow-right" class="ql-arrow"></i>
            </div>
            <div class="quick-link" onclick="showPage('theming-system')">
                <div class="ql-icon">
                    <i data-lucide="droplet"></i>
                </div>
                <div class="ql-body">
                    <div class="ql-title">Theming Guide</div>
                    <div class="ql-desc">Customize the look and feel</div>
                </div>
                <i data-lucide="arrow-right" class="ql-arrow"></i>
                </div>
            
        </div>
        """

    def _process_markdown(self, content: str) -> str:
        extensions = [
            "extra",
            "codehilite",
            "toc",
            "tables",
            "fenced_code",
            "attr_list",
            "def_list",
            "abbr",
            "footnotes",
        ]

        md = markdown.Markdown(extensions=extensions)
        mermaid_ext = MermaidExtension()
        md.registerExtensions([mermaid_ext], {})

        html = md.convert(content)
        return html

    def load_markdown(self, file_path: str) -> str:
        try:
            if not os.path.exists(file_path):
                return f"<h1>File not found</h1><p>{file_path}</p>"
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()
            return self._process_markdown(content)
        except Exception as e:
            return f"<h1>Error</h1><p>{e}</p>"

    def _get_icon_name(self, icon_name: str) -> str:
        lucide_name = icon_name
        return f'<i data-lucide="{lucide_name}"></i>'

    def _get_template(self) -> str:
        return r"""<!DOCTYPE html>
<html lang="en" data-theme="light">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{project_name} – Documentation</title>
    <meta name="description" content="{description}">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,400;0,14..32,500;0,14..32,600;1,14..32,400&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <style>
/* ─── Material You Design Tokens ────────────────── */
:root {{
  --md-ref-palette-primary0: #000000;
  --md-ref-palette-primary10: #001b3f;
  --md-ref-palette-primary20: #002f66;
  --md-ref-palette-primary30: #1f4480;
  --md-ref-palette-primary40: #3b5c9a;
  --md-ref-palette-primary50: #5575b5;
  --md-ref-palette-primary60: #6f8fd1;
  --md-ref-palette-primary70: #8aa9ed;
  --md-ref-palette-primary80: #abc4ff;
  --md-ref-palette-primary90: #d6e2ff;
  --md-ref-palette-primary95: #ecf0ff;
  --md-ref-palette-primary99: #fefbff;
  --md-ref-palette-primary100: #ffffff;

  --md-sys-color-primary: var(--md-ref-palette-primary40);
  --md-sys-color-on-primary: var(--md-ref-palette-primary100);
  --md-sys-color-primary-container: var(--md-ref-palette-primary90);
  --md-sys-color-on-primary-container: var(--md-ref-palette-primary10);
  --md-sys-color-secondary: #526070;
  --md-sys-color-on-secondary: #ffffff;
  --md-sys-color-secondary-container: #d6e4f7;
  --md-sys-color-on-secondary-container: #0f1d2b;
  --md-sys-color-surface: #fdfcff;
  --md-sys-color-on-surface: #1a1c1e;
  --md-sys-color-surface-variant: #dfe3eb;
  --md-sys-color-on-surface-variant: #43474e;
  --md-sys-color-outline: #73778f;
  --md-sys-color-outline-variant: #c3c7cf;
  --md-sys-color-background: #fdfcff;
  --md-sys-color-on-background: #1a1c1e;
  --md-sys-color-surface-container: #f0f0f4;
  --md-sys-color-surface-container-high: #e6eaf0;
  --md-sys-color-surface-container-highest: #dde1e8;
  --md-elevation-1: 0 1px 3px 1px rgba(0,0,0,0.15), 0 1px 2px 0 rgba(0,0,0,0.30);
  --md-elevation-2: 0 2px 6px 2px rgba(0,0,0,0.15), 0 1px 2px 0 rgba(0,0,0,0.30);
  --md-elevation-3: 0 4px 8px 3px rgba(0,0,0,0.15), 0 1px 3px 0 rgba(0,0,0,0.30);
  --md-elevation-4: 0 6px 12px 4px rgba(0,0,0,0.15), 0 2px 4px 0 rgba(0,0,0,0.30);
  --md-radius-extra-small: 4px;
  --md-radius-small: 8px;
  --md-radius-medium: 12px;
  --md-radius-large: 16px;
  --md-radius-extra-large: 28px;
  --md-transition: 200ms cubic-bezier(0.2, 0, 0, 1);
}}

[data-theme="dark"] {{
  --md-sys-color-primary: #abc4ff;
  --md-sys-color-on-primary: #002f66;
  --md-sys-color-primary-container: #1f4480;
  --md-sys-color-on-primary-container: #d6e2ff;
  --md-sys-color-secondary: #bac8db;
  --md-sys-color-on-secondary: #243240;
  --md-sys-color-secondary-container: #3a4858;
  --md-sys-color-on-secondary-container: #d6e4f7;
  --md-sys-color-surface: #111315;
  --md-sys-color-on-surface: #e1e2e5;
  --md-sys-color-surface-variant: #43474e;
  --md-sys-color-on-surface-variant: #c3c7cf;
  --md-sys-color-outline: #8d919c;
  --md-sys-color-outline-variant: #43474e;
  --md-sys-color-background: #111315;
  --md-sys-color-on-background: #e1e2e5;
  --md-sys-color-surface-container: #1e2024;
  --md-sys-color-surface-container-high: #292b30;
  --md-sys-color-surface-container-highest: #34363c;
  --md-elevation-1: 0 1px 3px 1px rgba(0,0,0,0.5), 0 1px 2px 0 rgba(0,0,0,0.3);
  --md-elevation-2: 0 2px 6px 2px rgba(0,0,0,0.5), 0 1px 2px 0 rgba(0,0,0,0.3);
  --md-elevation-3: 0 4px 8px 3px rgba(0,0,0,0.5), 0 1px 3px 0 rgba(0,0,0,0.3);
  --md-elevation-4: 0 6px 12px 4px rgba(0,0,0,0.5), 0 2px 4px 0 rgba(0,0,0,0.3);
}}

[data-theme="sepia"] {{
  --md-sys-color-primary: #8b5e2a;
  --md-sys-color-on-primary: #ffffff;
  --md-sys-color-primary-container: #ffe2cc;
  --md-sys-color-on-primary-container: #2f1a08;
  --md-sys-color-secondary: #6f5c4b;
  --md-sys-color-on-secondary: #ffffff;
  --md-sys-color-secondary-container: #fbdfcd;
  --md-sys-color-on-secondary-container: #281a0e;
  --md-sys-color-surface: #faf1e4;
  --md-sys-color-on-surface: #2c2416;
  --md-sys-color-surface-variant: #f2e0cf;
  --md-sys-color-on-surface-variant: #504538;
  --md-sys-color-outline: #837568;
  --md-sys-color-outline-variant: #d5c4b4;
  --md-sys-color-background: #faf1e4;
  --md-sys-color-on-background: #2c2416;
  --md-sys-color-surface-container: #f0e4d6;
  --md-sys-color-surface-container-high: #e8daca;
  --md-sys-color-surface-container-highest: #e0d0be;
  --md-elevation-1: 0 1px 3px 1px rgba(0,0,0,0.1), 0 1px 2px 0 rgba(0,0,0,0.06);
  --md-elevation-2: 0 2px 6px 2px rgba(0,0,0,0.1), 0 1px 2px 0 rgba(0,0,0,0.06);
  --md-elevation-3: 0 4px 8px 3px rgba(0,0,0,0.1), 0 1px 3px 0 rgba(0,0,0,0.06);
  --md-elevation-4: 0 6px 12px 4px rgba(0,0,0,0.1), 0 2px 4px 0 rgba(0,0,0,0.06);
}}

/* ─── Reset ──────────────────────────────────────── */
*, *::before, *::after {{ box-sizing: border-box; margin: 0; padding: 0; }}
html {{ font-size: 16px; }}

/* ─── Base ───────────────────────────────────────── */
body {{
  font-family: 'Inter', sans-serif;
  background: var(--md-sys-color-background);
  color: var(--md-sys-color-on-background);
  height: 100vh;
  overflow: hidden;
  -webkit-font-smoothing: antialiased;
  transition: background var(--md-transition), color var(--md-transition);
  font-size: 1rem;
  line-height: 1.6;
}}

/* ─── Layout ─────────────────────────────────────── */
.layout {{
  display: flex;
  height: 100vh;
  overflow: hidden;
}}

/* ─── Sidebar ────────────────────────────────────── */
.sidebar {{
  width: 320px;
  background: var(--md-sys-color-surface-container);
  border-right: 1px solid var(--md-sys-color-outline-variant);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  flex-shrink: 0;
  z-index: 50;
  transition: background var(--md-transition), border-color var(--md-transition);
}}

.sidebar-top {{
  padding: 0;
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
  flex-shrink: 0;
}}

.sidebar-logo {{
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 24px 24px 16px;
  text-decoration: none;
  cursor: pointer;
}}

.logo-name {{
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--md-sys-color-on-surface);
  letter-spacing: -0.01em;
}}

.logo-version {{
  font-size: 0.875rem;
  color: var(--md-sys-color-on-surface-variant);
  font-family: 'JetBrains Mono', monospace;
  margin-left: auto;
  background: var(--md-sys-color-surface-variant);
  padding: 4px 10px;
  border-radius: var(--md-radius-extra-large);
}}

.sidebar-search {{
  padding: 12px 24px 24px;
}}

.sb-search-wrap {{
  position: relative;
}}

.sb-search-icon {{
  position: absolute; left: 16px; top: 50%;
  transform: translateY(-50%);
  color: var(--md-sys-color-on-surface-variant);
  pointer-events: none;
  display: flex;
}}

.sb-search-input {{
  width: 100%;
  padding: 14px 16px 14px 48px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  font-family: 'Inter', sans-serif;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface);
  outline: none;
  transition: all var(--md-transition);
}}

.sb-search-input::placeholder {{ color: var(--md-sys-color-on-surface-variant); }}
.sb-search-input:focus {{
  border-color: var(--md-sys-color-primary);
  box-shadow: 0 0 0 4px var(--md-sys-color-primary-container);
}}

.sidebar-nav {{
  flex: 1;
  overflow-y: auto;
  padding: 8px 0 24px;
  scrollbar-width: thin;
  scrollbar-color: var(--md-sys-color-outline-variant) transparent;
}}

.sidebar-nav::-webkit-scrollbar {{ width: 4px; }}
.sidebar-nav::-webkit-scrollbar-track {{ background: transparent; }}
.sidebar-nav::-webkit-scrollbar-thumb {{ background: var(--md-sys-color-outline-variant); border-radius: 4px; }}

/* Category label */
.nav-group {{ margin-bottom: 4px; }}

.nav-group-header {{
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 24px 8px;
  font-size: 0.875rem;
  font-weight: 600;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  color: var(--md-sys-color-on-surface-variant);
  cursor: pointer;
  user-select: none;
}}

.nav-group-header:hover {{ color: var(--md-sys-color-on-surface); }}

.nav-chevron {{
  margin-left: auto;
  transition: transform 0.15s;
  color: var(--md-sys-color-on-surface-variant);
  width: 18px;
  height: 18px;
}}

.nav-chevron.collapsed {{ transform: rotate(-90deg); }}

.nav-group-items {{ overflow: hidden; }}
.nav-group-items.collapsed {{ display: none; }}

.nav-item {{
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 12px 24px 12px 32px;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
  cursor: pointer;
  border-radius: 0 28px 28px 0;
  margin-right: 12px;
  transition: all var(--md-transition);
  line-height: 1.5;
}}

.nav-item:hover {{
  background: var(--md-sys-color-surface-container-high);
  color: var(--md-sys-color-on-surface);
}}

.nav-item.active {{
  background: var(--md-sys-color-secondary-container);
  color: var(--md-sys-color-on-secondary-container);
  font-weight: 500;
}}

.nav-item i {{
  flex-shrink: 0;
  width: 20px;
  height: 20px;
}}

/* Subcategory */
.nav-subgroup {{ margin-left: 12px; }}

.nav-subgroup-header {{
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 24px 10px 32px;
  font-size: 1rem;
  font-weight: 500;
  color: var(--md-sys-color-on-surface-variant);
  cursor: pointer;
  user-select: none;
}}

.nav-subgroup-header:hover {{ color: var(--md-sys-color-on-surface); }}

.nav-subgroup-items {{ overflow: hidden; }}
.nav-subgroup-items.collapsed {{ display: none; }}

.nav-item.sub {{ padding-left: 58px; }}

/* ─── Main area ──────────────────────────────────── */
.main {{
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-width: 0;
}}

/* ─── Header ─────────────────────────────────────── */
.header {{
  height: 80px;
  background: var(--md-sys-color-surface);
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
  display: flex;
  align-items: center;
  padding: 0 32px;
  gap: 20px;
  flex-shrink: 0;
  transition: background var(--md-transition), border-color var(--md-transition);
}}

.breadcrumb {{
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
  flex: 1;
  min-width: 0;
}}

.breadcrumb-home {{
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--md-sys-color-on-surface);
  cursor: pointer;
  padding: 8px 12px;
  border-radius: var(--md-radius-large);
  transition: background var(--md-transition);
  white-space: nowrap;
}}

.breadcrumb-home:hover {{ background: var(--md-sys-color-surface-container-high); }}

.breadcrumb-sep {{ opacity: 0.4; }}

.breadcrumb-current {{
  color: var(--md-sys-color-primary);
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}}

.header-right {{
  display: flex;
  align-items: center;
  gap: 12px;
  flex-shrink: 0;
}}

.header-btn {{
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 20px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  font-family: 'Inter', sans-serif;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface);
  cursor: pointer;
  transition: all var(--md-transition);
  white-space: nowrap;
}}

.header-btn:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-outline);
}}

.header-btn kbd {{
  background: var(--md-sys-color-surface-container);
  padding: 4px 10px;
  border-radius: var(--md-radius-small);
  font-size: 0.875rem;
  font-family: 'JetBrains Mono', monospace;
}}

/* Theme picker */
.theme-wrap {{ position: relative; }}

.theme-menu {{
  position: absolute;
  top: calc(100% + 8px);
  right: 0;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-large);
  box-shadow: var(--md-elevation-2);
  min-width: 200px;
  overflow: hidden;
  display: none;
  z-index: 200;
}}

.theme-menu.open {{ display: block; }}

.theme-opt {{
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px 24px;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface);
  cursor: pointer;
  transition: background var(--md-transition);
}}

.theme-opt:hover {{ background: var(--md-sys-color-surface-container-high); }}
.theme-opt.active {{
  background: var(--md-sys-color-secondary-container);
  color: var(--md-sys-color-on-secondary-container);
}}

.theme-swatch {{
  width: 24px; height: 24px;
  border-radius: 50%;
  border: 2px solid var(--md-sys-color-outline-variant);
  flex-shrink: 0;
}}

/* ─── Content ────────────────────────────────────── */
.content-wrap {{
  flex: 1;
  display: flex;
  overflow: hidden;
}}

.content {{
  flex: 1;
  overflow-y: auto;
  padding: 56px 72px;
  min-width: 0;
  scrollbar-width: thin;
  scrollbar-color: var(--md-sys-color-outline-variant) transparent;
}}

.content::-webkit-scrollbar {{ width: 8px; }}
.content::-webkit-scrollbar-track {{ background: transparent; }}
.content::-webkit-scrollbar-thumb {{ background: var(--md-sys-color-outline-variant); border-radius: 4px; }}

/* ─── TOC / Progress ─────────────────────────────── */
.toc {{
  width: 280px;
  border-left: 1px solid var(--md-sys-color-outline-variant);
  padding: 48px 0;
  overflow-y: auto;
  flex-shrink: 0;
  display: none;
}}

.toc.visible {{ display: block; }}

.toc-label {{
  padding: 0 24px 20px;
  font-size: 0.875rem;
  font-weight: 600;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  color: var(--md-sys-color-on-surface-variant);
}}

.toc-progress {{
  margin: 0 24px 16px;
  height: 4px;
  background: var(--md-sys-color-surface-variant);
  border-radius: var(--md-radius-extra-small);
  overflow: hidden;
}}

.toc-progress-fill {{
  height: 100%;
  background: var(--md-sys-color-primary);
  border-radius: var(--md-radius-extra-small);
  width: 0%;
  transition: width 0.1s;
}}

.toc-pct {{
  padding: 0 24px;
  font-size: 0.875rem;
  color: var(--md-sys-color-on-surface-variant);
  font-family: 'JetBrains Mono', monospace;
  margin-bottom: 16px;
}}

.toc-item {{
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 24px;
  font-size: 0.9375rem;
  color: var(--md-sys-color-on-surface-variant);
  cursor: pointer;
  transition: all var(--md-transition);
  border-left: 3px solid transparent;
}}

.toc-item:hover {{
  color: var(--md-sys-color-on-surface);
  background: var(--md-sys-color-surface-container-high);
}}
.toc-item.active {{
  color: var(--md-sys-color-primary);
  border-left-color: var(--md-sys-color-primary);
  background: var(--md-sys-color-primary-container);
}}

.toc-dot {{
  width: 4px; height: 4px;
  border-radius: 50%;
  background: currentColor;
  flex-shrink: 0;
  opacity: 0.6;
}}

.toc-text {{
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}}

/* ─── Home page ──────────────────────────────────── */
.hero {{
  padding: 48px 0 56px;
  max-width: 1000px;
}}

.hero-eyebrow {{
  display: inline-flex;
  align-items: center;
  padding: 8px 20px;
  background: var(--md-sys-color-primary-container);
  color: var(--md-sys-color-on-primary-container);
  font-size: 1rem;
  font-weight: 600;
  letter-spacing: 0.03em;
  border-radius: var(--md-radius-extra-large);
  margin-bottom: 28px;
}}

.hero-title {{
  font-size: 4rem;
  font-weight: 700;
  line-height: 1.2;
  letter-spacing: -0.02em;
  color: var(--md-sys-color-on-background);
  margin-bottom: 24px;
}}

.hero-desc {{
  font-size: 1.5rem;
  color: var(--md-sys-color-on-surface-variant);
  line-height: 1.6;
  max-width: 800px;
  margin-bottom: 48px;
}}

.hero-actions {{
  display: flex;
  gap: 20px;
  flex-wrap: wrap;
  margin-bottom: 48px;
}}

.hero-btn {{
  display: inline-flex;
  align-items: center;
  padding: 16px 36px;
  border-radius: var(--md-radius-extra-large);
  font-size: 1.125rem;
  font-weight: 600;
  text-decoration: none;
  cursor: pointer;
  border: none;
  font-family: 'Inter', sans-serif;
  transition: all var(--md-transition);
}}

.btn-primary {{
  background: var(--md-sys-color-primary);
  color: var(--md-sys-color-on-primary);
  box-shadow: var(--md-elevation-1);
}}

.btn-primary:hover {{
  filter: brightness(1.05);
  box-shadow: var(--md-elevation-2);
  transform: translateY(-2px);
}}

.btn-outline {{
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline);
  color: var(--md-sys-color-on-surface);
}}

.btn-outline:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-on-surface);
}}

.hero-badges {{
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
}}

.hero-badge {{
  padding: 8px 20px;
  background: var(--md-sys-color-surface-variant);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
}}

/* Quick links */
.quick-links {{
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(350px, 1fr));
  gap: 20px;
  margin-bottom: 56px;
}}

.quick-link {{
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 28px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-large);
  cursor: pointer;
  transition: all var(--md-transition);
  box-shadow: var(--md-elevation-1);
}}

.quick-link:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-primary);
  transform: translateY(-2px);
  box-shadow: var(--md-elevation-2);
}}

.ql-icon {{
  width: 56px; height: 56px;
  border-radius: var(--md-radius-medium);
  background: var(--md-sys-color-primary-container);
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--md-sys-color-primary);
  flex-shrink: 0;
}}

.ql-icon i {{
  width: 28px;
  height: 28px;
}}

.ql-body {{ flex: 1; min-width: 0; }}

.ql-title {{
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--md-sys-color-on-surface);
  margin-bottom: 6px;
}}

.ql-desc {{
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
}}

.ql-arrow {{
  color: var(--md-sys-color-on-surface-variant);
  flex-shrink: 0;
  width: 24px;
  height: 24px;
  transition: transform var(--md-transition);
}}
.quick-link:hover .ql-arrow {{
  transform: translateX(8px);
  color: var(--md-sys-color-primary);
}}

/* ─── Doc view ───────────────────────────────────── */
.doc-header {{
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 24px;
  margin-bottom: 48px;
  padding-bottom: 28px;
  border-bottom: 2px solid var(--md-sys-color-outline-variant);
}}

.doc-title {{
  font-size: 3rem;
  font-weight: 700;
  color: var(--md-sys-color-on-background);
  letter-spacing: -0.02em;
  line-height: 1.3;
}}

.doc-actions {{
  display: flex;
  gap: 14px;
  flex-shrink: 0;
}}

.doc-action-btn {{
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px 24px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  font-family: 'Inter', sans-serif;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface);
  cursor: pointer;
  transition: all var(--md-transition);
}}

.doc-action-btn:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-outline);
}}
.doc-action-btn.copied {{
  background: var(--md-sys-color-primary-container);
  border-color: var(--md-sys-color-primary);
  color: var(--md-sys-color-primary);
}}

/* ─── Markdown ───────────────────────────────────── */
.md {{
  color: var(--md-sys-color-on-surface);
  line-height: 1.7;
  max-width: 1100px;
  font-size: 1.125rem;
}}

.md h1, .md h2, .md h3, .md h4 {{
  color: var(--md-sys-color-on-background);
  font-weight: 700;
  letter-spacing: -0.02em;
  scroll-margin-top: 40px;
}}

.md h1 {{
  font-size: 2.5rem;
  margin: 0 0 28px;
  padding-bottom: 20px;
  border-bottom: 2px solid var(--md-sys-color-outline-variant);
}}
.md h2 {{
  font-size: 2rem;
  margin: 64px 0 28px;
}}
.md h3 {{
  font-size: 1.75rem;
  margin: 48px 0 20px;
}}
.md h4 {{
  font-size: 1.375rem;
  margin: 36px 0 16px;
}}

.md p {{ margin: 0 0 28px; }}
.md p:last-child {{ margin-bottom: 0; }}

.md a {{
  color: var(--md-sys-color-primary);
  text-decoration: none;
  font-weight: 500;
  border-bottom: 1px solid transparent;
  transition: border-color var(--md-transition);
}}
.md a:hover {{
  border-bottom-color: var(--md-sys-color-primary);
}}

.md code {{
  font-family: 'JetBrains Mono', monospace;
  font-size: 0.9em;
  padding: 4px 8px;
  background: var(--md-sys-color-surface-variant);
  border-radius: var(--md-radius-extra-small);
  color: var(--md-sys-color-on-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
}}

.md pre {{
  margin: 36px 0;
  border-radius: var(--md-radius-large);
  border: 1px solid var(--md-sys-color-outline-variant);
  overflow: hidden;
  background: var(--md-sys-color-surface-container);
  position: relative;
  font-size: 1rem;
  box-shadow: var(--md-elevation-1);
}}

.md pre code {{
  display: block;
  padding: 28px 32px;
  overflow-x: auto;
  line-height: 1.6;
  background: none;
  border: none;
  border-radius: 0;
  color: var(--md-sys-color-on-surface);
  font-size: 1rem;
}}

.copy-btn {{
  position: absolute;
  top: 20px; right: 20px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  font-family: 'Inter', sans-serif;
  font-size: 0.9375rem;
  color: var(--md-sys-color-on-surface);
  cursor: pointer;
  opacity: 0;
  transition: opacity var(--md-transition), background var(--md-transition), color var(--md-transition);
  box-shadow: var(--md-elevation-1);
}}

.md pre:hover .copy-btn {{ opacity: 1; }}
.copy-btn:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-outline);
}}
.copy-btn.ok {{
  background: var(--md-sys-color-primary-container);
  border-color: var(--md-sys-color-primary);
  color: var(--md-sys-color-primary);
  opacity: 1;
}}

.md table {{
  width: 100%;
  margin: 36px 0;
  border-collapse: collapse;
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-large);
  overflow: hidden;
  font-size: 1rem;
  box-shadow: var(--md-elevation-1);
}}

.md th {{
  padding: 18px 24px;
  background: var(--md-sys-color-surface-container);
  font-weight: 600;
  text-align: left;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface);
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
}}

.md td {{
  padding: 16px 24px;
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
  color: var(--md-sys-color-on-surface);
}}

.md tr:last-child td {{ border-bottom: none; }}
.md tr:hover td {{ background: var(--md-sys-color-surface-container-high); }}

.md ul, .md ol {{ margin: 0 0 28px 36px; }}
.md li {{ margin: 10px 0; }}

.md blockquote {{
  margin: 36px 0;
  padding: 28px 36px;
  border-left: 4px solid var(--md-sys-color-primary);
  background: var(--md-sys-color-primary-container);
  border-radius: 0 var(--md-radius-large) var(--md-radius-large) 0;
  font-style: italic;
}}

.md blockquote p {{
  color: var(--md-sys-color-on-primary-container);
  margin: 0;
  font-size: 1.25rem;
}}

.md hr {{
  border: none;
  border-top: 2px solid var(--md-sys-color-outline-variant);
  margin: 56px 0;
}}

.md img {{
  max-width: 100%;
  border-radius: var(--md-radius-large);
  border: 1px solid var(--md-sys-color-outline-variant);
  box-shadow: var(--md-elevation-1);
}}

/* ─── Mermaid Diagrams ───────────────────────────── */
.mermaid-wrapper {{
  background: var(--md-sys-color-surface-container);
  padding: 0;
  border-radius: var(--md-radius-large);
  border: 1px solid var(--md-sys-color-outline-variant);
  margin: 36px 0;
  overflow: hidden;
  box-shadow: var(--md-elevation-1);
  transition: all var(--md-transition);
}}

.mermaid-wrapper.resized {{
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  width: 90vw;
  height: 90vh;
  z-index: 1000;
  box-shadow: var(--md-elevation-4);
  background: var(--md-sys-color-surface);
}}

.mermaid-wrapper.resized .mermaid {{
  height: calc(100% - 70px);
  overflow: auto;
  padding: 32px;
}}

.mermaid-wrapper.fullscreen {{
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  z-index: 1000;
  border-radius: 0;
  box-shadow: none;
}}

.mermaid-wrapper.fullscreen .mermaid {{
  height: calc(100% - 70px);
  overflow: auto;
  padding: 40px;
  background: var(--md-sys-color-surface);
}}

.mermaid-controls {{
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  padding: 16px 20px;
  background: var(--md-sys-color-surface-variant);
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
}}

.mermaid-resize,
.mermaid-fullscreen {{
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 10px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-small);
  cursor: pointer;
  color: var(--md-sys-color-on-surface);
  transition: all var(--md-transition);
}}

.mermaid-resize:hover,
.mermaid-fullscreen:hover {{
  background: var(--md-sys-color-surface-container-high);
  border-color: var(--md-sys-color-primary);
  color: var(--md-sys-color-primary);
}}

.mermaid {{
  padding: 28px;
  text-align: center;
  transition: all var(--md-transition);
  min-height: 250px;
  display: flex;
  align-items: center;
  justify-content: center;
}}

.mermaid svg {{
  max-width: 100%;
  height: auto;
}}

/* Fullscreen overlay */
.mermaid-overlay {{
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(8px);
  z-index: 999;
  display: none;
}}

.mermaid-overlay.active {{
  display: block;
}}

.codehilite {{ background: transparent !important; }}

/* ─── Search results overlay ─────────────────────── */
.search-overlay {{
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(12px);
  z-index: 300;
  display: none;
  align-items: flex-start;
  justify-content: center;
  padding-top: 140px;
}}

.search-overlay.open {{ display: flex; }}

.search-modal {{
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  width: 100%;
  max-width: 700px;
  overflow: hidden;
  box-shadow: var(--md-elevation-3);
}}

.search-modal-input-wrap {{
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 24px 28px;
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
}}

.search-modal-input {{
  flex: 1;
  background: none;
  border: none;
  outline: none;
  font-family: 'Inter', sans-serif;
  font-size: 1.25rem;
  color: var(--md-sys-color-on-surface);
}}

.search-modal-input::placeholder {{ color: var(--md-sys-color-on-surface-variant); }}

.search-results-list {{ max-height: 500px; overflow-y: auto; }}

.search-result {{
  display: flex;
  align-items: center;
  gap: 20px;
  padding: 20px 28px;
  cursor: pointer;
  border-bottom: 1px solid var(--md-sys-color-outline-variant);
  transition: background var(--md-transition);
}}

.search-result:last-child {{ border-bottom: none; }}
.search-result:hover {{ background: var(--md-sys-color-surface-container-high); }}

.search-result-icon {{
  width: 52px; height: 52px;
  border-radius: var(--md-radius-medium);
  background: var(--md-sys-color-primary-container);
  display: flex; align-items: center; justify-content: center;
  color: var(--md-sys-color-primary);
  flex-shrink: 0;
}}

.search-result-body {{ flex: 1; min-width: 0; }}

.search-result-title {{
  font-size: 1.25rem;
  font-weight: 500;
  color: var(--md-sys-color-on-surface);
  margin-bottom: 6px;
}}

.search-result-cat {{
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
}}

.search-no-results {{
  padding: 56px;
  text-align: center;
  color: var(--md-sys-color-on-surface-variant);
  font-size: 1.25rem;
}}

.search-hint {{
  padding: 20px 28px;
  border-top: 1px solid var(--md-sys-color-outline-variant);
  display: flex;
  gap: 28px;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
  background: var(--md-sys-color-surface-container);
}}

.hint-key {{
  display: inline-flex;
  align-items: center;
  gap: 10px;
}}

kbd {{
  display: inline-flex;
  align-items: center;
  padding: 6px 12px;
  background: var(--md-sys-color-surface);
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-small);
  font-family: 'JetBrains Mono', monospace;
  font-size: 0.875rem;
  box-shadow: var(--md-elevation-1);
}}

/* ─── Footer ─────────────────────────────────────── */
.footer {{
  margin-top: 72px;
  padding: 28px 0;
  border-top: 2px solid var(--md-sys-color-outline-variant);
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 1rem;
  color: var(--md-sys-color-on-surface-variant);
}}

/* ─── Mobile overlay ─────────────────────────────── */
.sidebar-overlay {{
  display: none;
  position: fixed; inset: 0;
  background: rgba(0,0,0,0.4);
  backdrop-filter: blur(4px);
  z-index: 40;
}}

/* ─── Responsive ─────────────────────────────────── */
@media (max-width: 900px) {{
  .toc {{ display: none !important; }}
  .content {{ padding: 40px; }}
}}

@media (max-width: 640px) {{
  .sidebar {{
    position: fixed;
    left: 0; top: 0; bottom: 0;
    transform: translateX(-100%);
    transition: transform 0.2s ease;
    z-index: 50;
  }}
  .sidebar.open {{ transform: translateX(0); }}
  .sidebar-overlay.open {{ display: block; }}
  .mobile-menu-btn {{ display: flex !important; }}
  .content {{ padding: 28px; }}
  .hero-title {{ font-size: 2.75rem; }}
  .hero-desc {{ font-size: 1.25rem; }}
  .quick-links {{ grid-template-columns: 1fr; }}
}}

.mobile-menu-btn {{
  display: none;
  align-items: center;
  justify-content: center;
  width: 48px; height: 48px;
  background: none;
  border: 1px solid var(--md-sys-color-outline-variant);
  border-radius: var(--md-radius-extra-large);
  cursor: pointer;
  color: var(--md-sys-color-on-surface);
  margin-right: 12px;
}}

/* doc-view hidden by default */
.doc-view {{ display: none; }}

{pygments_styles}
    </style>
</head>
<body>

<div class="sidebar-overlay" id="sidebarOverlay" onclick="closeSidebar()"></div>
<div class="mermaid-overlay" id="mermaidOverlay" onclick="closeMermaidFullscreen()"></div>

<div class="layout">
  <!-- ── Sidebar ── -->
  <nav class="sidebar" id="sidebar">
    <div class="sidebar-top">
      <div class="sidebar-logo" onclick="showHome()">
        <div class="logo-name">{project_name}</div>
        <div class="logo-version">v{version}</div>
      </div>
      <div class="sidebar-search">
        <div class="sb-search-wrap">
          <span class="sb-search-icon">
            <i data-lucide="search" width="20" height="20"></i>
          </span>
          <input class="sb-search-input" id="sidebarSearch" type="text" placeholder="Search docs" onclick="openSearch()" readonly>
        </div>
      </div>
    </div>

    <div class="sidebar-nav" id="sidebarNav">
      <div class="nav-group">
        <div class="nav-group-header" onclick="toggleGroup('home')">
          <i data-lucide="home" width="20" height="20"></i>
          Overview
          <i data-lucide="chevron-down" class="nav-chevron" width="20" height="20"></i>
        </div>
        <div class="nav-group-items" id="ng-home">
          <div class="nav-item active" onclick="showHome()">
            <i data-lucide="home" width="22" height="22"></i>
            Home
          </div>
        </div>
      </div>
      {sidebar_content}
    </div>
  </nav>

  <!-- ── Main ── -->
  <div class="main">
    <header class="header">
      <button class="mobile-menu-btn" onclick="openSidebar()">
        <i data-lucide="menu" width="24" height="24"></i>
      </button>

      <div class="breadcrumb">
        <span class="breadcrumb-home" onclick="showHome()">
          <i data-lucide="home" width="18" height="18"></i>
          {project_name}
        </span>
        <span class="breadcrumb-sep">/</span>
        <span class="breadcrumb-current" id="breadcrumbCurrent">Home</span>
      </div>

      <div class="header-right">
        <button class="header-btn" onclick="openSearch()">
          <i data-lucide="search" width="18" height="18"></i>
          Search
          <kbd>⌘K</kbd>
        </button>

        <div class="theme-wrap">
          <button class="header-btn" id="themeBtn" onclick="toggleThemeMenu()">
            <i data-lucide="sun" id="themeIcon" width="18" height="18"></i>
            <span id="themeName">Light</span>
          </button>
          <div class="theme-menu" id="themeMenu">
            <div class="theme-opt" data-theme="light" onclick="setTheme('light')">
              <span class="theme-swatch" style="background:#ffffff;"></span> Light
            </div>
            <div class="theme-opt" data-theme="dark" onclick="setTheme('dark')">
              <span class="theme-swatch" style="background:#111315;"></span> Dark
            </div>
            <div class="theme-opt" data-theme="sepia" onclick="setTheme('sepia')">
              <span class="theme-swatch" style="background:#faf1e4;"></span> Sepia
            </div>
          </div>
        </div>
      </div>
    </header>

    <div class="content-wrap">
      <!-- Content area -->
      <div class="content" id="contentArea">

        <!-- Home view -->
        <div id="homeView">
          {hero_section}
          {action_cards}
          <div class="footer">
            <span>© {year} {project_name}</span>
            <span>v{version} · Updated {last_updated}</span>
          </div>
        </div>

        <!-- Doc view -->
        <div id="docView" class="doc-view">
          <div class="doc-header">
            <h1 class="doc-title" id="docTitle"></h1>
            <div class="doc-actions">
              <button class="doc-action-btn" id="copyLinkBtn" onclick="copyLink(this)">
                <i data-lucide="link" width="18" height="18"></i>
                Copy link
              </button>
            </div>
          </div>
          <div class="md" id="docContent"></div>
          <div class="footer">
            <span>© {year} {project_name}</span>
            <span>v{version} · Updated {last_updated}</span>
          </div>
        </div>

      </div>

      <!-- TOC -->
      <div class="toc" id="tocPanel">
        <div class="toc-label">On this page</div>
        <div class="toc-progress"><div class="toc-progress-fill" id="tocFill"></div></div>
        <div class="toc-pct" id="tocPct">0%</div>
        <div id="tocList"></div>
      </div>
    </div>
  </div>
</div>

<!-- Search overlay -->
<div class="search-overlay" id="searchOverlay" onclick="closeSearchOnOverlay(event)">
  <div class="search-modal">
    <div class="search-modal-input-wrap">
      <i data-lucide="search" width="24" height="24"></i>
      <input class="search-modal-input" id="searchModalInput" type="text" placeholder="Search documentation…" oninput="doSearch(this.value)" onkeydown="searchKeydown(event)" autocomplete="off">
    </div>
    <div class="search-results-list" id="searchResultsList"></div>
    <div class="search-hint">
      <span class="hint-key"><kbd>↑</kbd> <kbd>↓</kbd> navigate</span>
      <span class="hint-key"><kbd>↵</kbd> open</span>
      <span class="hint-key"><kbd>Esc</kbd> close</span>
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

let tocSections = [];
let currentId = null;
let searchIdx = -1;

// ── Initialize Lucide icons ────────────────────────
function initIcons() {{
  if (typeof lucide !== 'undefined') {{
    lucide.createIcons();
  }}
}}

// ── Theme ──────────────────────────────────────────
const THEMES = {{ light:'Light', dark:'Dark', sepia:'Sepia' }};

function setTheme(t) {{
  document.documentElement.setAttribute('data-theme', t);
  localStorage.setItem('docs-theme', t);
  document.getElementById('themeName').textContent = THEMES[t];

  const themeIcon = document.getElementById('themeIcon');
  if (themeIcon) {{
    const iconName = t === 'dark' ? 'moon' : (t === 'sepia' ? 'book' : 'sun');
    themeIcon.setAttribute('data-lucide', iconName);
    if (typeof lucide !== 'undefined') lucide.createIcons();
  }}

  document.querySelectorAll('.theme-opt').forEach(o => {{
    o.classList.toggle('active', o.dataset.theme === t);
  }});
  document.getElementById('themeMenu').classList.remove('open');

  setTimeout(() => rerenderMermaid(t), 100);
}}

function toggleThemeMenu() {{
  document.getElementById('themeMenu').classList.toggle('open');
}}

document.addEventListener('click', e => {{
  const wrap = document.querySelector('.theme-wrap');
  if (wrap && !wrap.contains(e.target)) {{
    document.getElementById('themeMenu').classList.remove('open');
  }}
}});

// ── Mermaid ────────────────────────────────────────
function getMermaidTheme(theme) {{
  const themes = {{
    light: {{
      theme: 'base',
      themeVariables: {{
        background: '#ffffff',
        primaryColor: '#3b5c9a',
        primaryTextColor: '#1a1c1e',
        primaryBorderColor: '#c3c7cf',
        lineColor: '#526070',
        secondaryColor: '#f0f0f4',
        tertiaryColor: '#ffffff',
        clusterBkg: '#f0f0f4',
        clusterBorder: '#c3c7cf',
        nodeBorder: '#c3c7cf',
        nodeTextColor: '#1a1c1e',
        edgeLabelBackground: '#ffffff',
        fontFamily: 'Inter, sans-serif',
        fontSize: '14px'
      }}
    }},
    dark: {{
      theme: 'base',
      themeVariables: {{
        background: '#111315',
        primaryColor: '#3b5c9a',
        primaryTextColor: '#e1e2e5',
        primaryBorderColor: '#43474e',
        lineColor: '#bac8db',
        secondaryColor: '#1e2024',
        tertiaryColor: '#111315',
        clusterBkg: '#1e2024',
        clusterBorder: '#43474e',
        nodeBorder: '#43474e',
        nodeTextColor: '#e1e2e5',
        edgeLabelBackground: '#292b30',
        fontFamily: 'Inter, sans-serif',
        fontSize: '14px'
      }}
    }},
    sepia: {{
      theme: 'base',
      themeVariables: {{
        background: '#faf1e4',
        primaryColor: '#8b5e2a',
        primaryTextColor: '#2c2416',
        primaryBorderColor: '#d5c4b4',
        lineColor: '#6f5c4b',
        secondaryColor: '#f0e4d6',
        tertiaryColor: '#faf1e4',
        clusterBkg: '#f0e4d6',
        clusterBorder: '#d5c4b4',
        nodeBorder: '#d5c4b4',
        nodeTextColor: '#2c2416',
        edgeLabelBackground: '#e8daca',
        fontFamily: 'Inter, sans-serif',
        fontSize: '14px'
      }}
    }}
  }};
  return themes[theme] || themes.light;
}}

function rerenderMermaid(theme) {{
  if (typeof mermaid === 'undefined') return;

  try {{
    const config = getMermaidTheme(theme);
    mermaid.initialize({{
      ...config,
      startOnLoad: false,
      securityLevel: 'loose',
      logLevel: 'error',
      flowchart: {{ useMaxWidth: true, htmlLabels: true, curve: 'basis' }},
      sequence: {{ useMaxWidth: true, showSequenceNumbers: true }},
      gantt: {{ useMaxWidth: true }},
      class: {{ useMaxWidth: true }},
      state: {{ useMaxWidth: true }},
      er: {{ useMaxWidth: true }},
      journey: {{ useMaxWidth: true }}
    }});

    document.querySelectorAll('.mermaid').forEach(el => {{
      const originalContent = el.getAttribute('data-src') || el.innerHTML;
      el.innerHTML = originalContent;
      el.removeAttribute('data-processed');
    }});

    mermaid.run();
  }} catch(e) {{
    console.warn('Mermaid render error:', e);
  }}
}}

function initMermaid(theme) {{
  if (typeof mermaid === 'undefined') {{
    setTimeout(() => initMermaid(theme), 150);
    return;
  }}

  const config = getMermaidTheme(theme);
  mermaid.initialize({{
    ...config,
    startOnLoad: true,
    securityLevel: 'loose',
    logLevel: 'error',
    flowchart: {{ useMaxWidth: true, htmlLabels: true, curve: 'basis' }},
    sequence: {{ useMaxWidth: true, showSequenceNumbers: true }},
    gantt: {{ useMaxWidth: true }},
    class: {{ useMaxWidth: true }},
    state: {{ useMaxWidth: true }},
    er: {{ useMaxWidth: true }},
    journey: {{ useMaxWidth: true }}
  }});

  document.querySelectorAll('.mermaid').forEach(el => {{
    el.setAttribute('data-src', el.innerHTML);
  }});

  mermaid.run();
}}

// ── Mermaid Controls ───────────────────────────────
function toggleMermaidSize(btn) {{
  const wrapper = btn.closest('.mermaid-wrapper');
  if (!wrapper) return;

  wrapper.classList.toggle('resized');
  const icon = btn.querySelector('i');
  if (wrapper.classList.contains('resized')) {{
    icon.setAttribute('data-lucide', 'minimize-2');
  }} else {{
    icon.setAttribute('data-lucide', 'maximize-2');
  }}
  if (typeof lucide !== 'undefined') lucide.createIcons();

  // Re-render mermaid to fit new size
  setTimeout(() => {{
    const mermaidEl = wrapper.querySelector('.mermaid');
    if (mermaidEl) {{
      const content = mermaidEl.getAttribute('data-src') || mermaidEl.innerHTML;
      mermaidEl.innerHTML = content;
      mermaidEl.removeAttribute('data-processed');
      const theme = localStorage.getItem('docs-theme') || 'light';
      rerenderMermaid(theme);
    }}
  }}, 50);
}}

function fullscreenMermaid(btn) {{
  const wrapper = btn.closest('.mermaid-wrapper');
  if (!wrapper) return;

  const overlay = document.getElementById('mermaidOverlay');
  wrapper.classList.toggle('fullscreen');
  overlay.classList.toggle('active');

  const icon = btn.querySelector('i');
  if (wrapper.classList.contains('fullscreen')) {{
    icon.setAttribute('data-lucide', 'minimize');
    document.body.style.overflow = 'hidden';
  }} else {{
    icon.setAttribute('data-lucide', 'fullscreen');
    document.body.style.overflow = '';
  }}
  if (typeof lucide !== 'undefined') lucide.createIcons();

  // Re-render mermaid to fit new size
  setTimeout(() => {{
    const mermaidEl = wrapper.querySelector('.mermaid');
    if (mermaidEl) {{
      const content = mermaidEl.getAttribute('data-src') || mermaidEl.innerHTML;
      mermaidEl.innerHTML = content;
      mermaidEl.removeAttribute('data-processed');
      const theme = localStorage.getItem('docs-theme') || 'light';
      rerenderMermaid(theme);
    }}
  }}, 50);
}}

function closeMermaidFullscreen() {{
  const wrapper = document.querySelector('.mermaid-wrapper.fullscreen');
  if (!wrapper) return;

  wrapper.classList.remove('fullscreen');
  document.getElementById('mermaidOverlay').classList.remove('active');
  document.body.style.overflow = '';

  const btn = wrapper.querySelector('.mermaid-fullscreen i');
  if (btn) {{
    btn.setAttribute('data-lucide', 'fullscreen');
    if (typeof lucide !== 'undefined') lucide.createIcons();
  }}
}}

// ── Sidebar toggle ─────────────────────────────────
function openSidebar() {{
  document.getElementById('sidebar').classList.add('open');
  document.getElementById('sidebarOverlay').classList.add('open');
}}
function closeSidebar() {{
  document.getElementById('sidebar').classList.remove('open');
  document.getElementById('sidebarOverlay').classList.remove('open');
}}

// ── Nav groups ─────────────────────────────────────
function toggleGroup(id) {{
  const items = document.getElementById('ng-' + id);
  if (!items) return;
  const btn = items.previousElementSibling;
  const chev = btn.querySelector('.nav-chevron');
  const c = items.classList.toggle('collapsed');
  if (chev) {{
    chev.classList.toggle('collapsed', c);
    chev.setAttribute('data-lucide', c ? 'chevron-right' : 'chevron-down');
    if (typeof lucide !== 'undefined') lucide.createIcons();
  }}
}}

function toggleSub(id) {{
  const items = document.getElementById('ns-' + id);
  if (!items) return;
  const btn = document.querySelector(`[data-sub="${{id}}"]`);
  const chev = btn && btn.querySelector('.nav-chevron');
  const c = items.classList.toggle('collapsed');
  if (chev) {{
    chev.classList.toggle('collapsed', c);
    chev.setAttribute('data-lucide', c ? 'chevron-right' : 'chevron-down');
    if (typeof lucide !== 'undefined') lucide.createIcons();
  }}
}}

// ── Navigation ─────────────────────────────────────
function showHome() {{
  document.getElementById('homeView').style.display = '';
  document.getElementById('docView').style.display = 'none';
  document.getElementById('breadcrumbCurrent').textContent = 'Home';
  setActiveNav(null);
  window.location.hash = '';
  document.getElementById('tocPanel').classList.remove('visible');
  closeSidebar();
  closeMermaidFullscreen();
  document.getElementById('contentArea').scrollTop = 0;
}}

function showPage(id) {{
  if (!PAGES[id]) return;
  document.getElementById('homeView').style.display = 'none';
  const dv = document.getElementById('docView');
  dv.style.display = '';
  document.getElementById('docView').style.display = 'block';
  document.getElementById('docContent').innerHTML = PAGES[id];
  document.getElementById('docTitle').textContent = TITLES[id] || id;
  document.getElementById('breadcrumbCurrent').textContent = TITLES[id] || id;
  setActiveNav(id);
  document.getElementById('contentArea').scrollTop = 0;
  window.location.hash = id;
  currentId = id;
  closeSidebar();
  closeMermaidFullscreen();
  setTimeout(() => {{
    addCopyButtons();
    const t = localStorage.getItem('docs-theme') || 'light';
    initMermaid(t);
    initIcons();
  }}, 80);
  buildToc();
}}

function setActiveNav(id) {{
  document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
  if (id) {{
    const el = document.querySelector(`[data-page="${{id}}"]`);
    if (el) el.classList.add('active');
  }} else {{
    const home = document.querySelector('[onclick="showHome()"]');
    if (home) home.classList.add('active');
  }}
}}

// ── TOC ────────────────────────────────────────────
function buildToc() {{
  const content = document.getElementById('docContent');
  const headings = content.querySelectorAll('h1,h2,h3');
  const list = document.getElementById('tocList');
  list.innerHTML = '';
  tocSections = [];

  headings.forEach((h, i) => {{
    if (!h.id) h.id = 'sec-' + i + '-' + h.textContent.toLowerCase().replace(/[^a-z0-9]+/g, '-');
    tocSections.push({{ id: h.id, el: h, level: +h.tagName[1] }});
    const item = document.createElement('div');
    item.className = 'toc-item';
    item.dataset.id = h.id;
    item.style.paddingLeft = ((h.tagName[1] - 1) * 16 + 24) + 'px';
    item.innerHTML = `<span class="toc-dot"></span><span class="toc-text">${{h.textContent}}</span>`;
    item.onclick = () => scrollToId(h.id);
    list.appendChild(item);
  }});

  const toc = document.getElementById('tocPanel');
  toc.classList.toggle('visible', tocSections.length > 0);
}}

function scrollToId(id) {{
  const el = document.getElementById(id);
  if (!el) return;
  const ca = document.getElementById('contentArea');
  ca.scrollTo({{ top: el.offsetTop - 56, behavior: 'smooth' }});
}}

document.getElementById('contentArea').addEventListener('scroll', updateToc);

function updateToc() {{
  const ca = document.getElementById('contentArea');
  const total = ca.scrollHeight - ca.clientHeight;
  const pct = total > 0 ? Math.min(100, Math.round((ca.scrollTop / total) * 100)) : 0;
  document.getElementById('tocFill').style.width = pct + '%';
  document.getElementById('tocPct').textContent = pct + '%';

  let active = null;
  tocSections.forEach(s => {{
    if (s.el.getBoundingClientRect().top < ca.getBoundingClientRect().top + 180) active = s.id;
  }});

  document.querySelectorAll('.toc-item').forEach(el => {{
    el.classList.toggle('active', el.dataset.id === active);
  }});
}}

// ── Copy buttons ───────────────────────────────────
function addCopyButtons() {{
  document.querySelectorAll('.md pre').forEach(pre => {{
    if (pre.querySelector('.copy-btn')) return;
    const btn = document.createElement('button');
    btn.className = 'copy-btn';
    btn.innerHTML = `<i data-lucide="copy" width="16" height="16"></i> Copy`;
    btn.onclick = async () => {{
      const code = pre.querySelector('code');
      if (!code) return;
      await navigator.clipboard.writeText(code.textContent);
      btn.classList.add('ok');
      btn.innerHTML = `<i data-lucide="check" width="16" height="16"></i> Copied!`;
      if (typeof lucide !== 'undefined') lucide.createIcons();
      setTimeout(() => {{
        btn.classList.remove('ok');
        btn.innerHTML = `<i data-lucide="copy" width="16" height="16"></i> Copy`;
        if (typeof lucide !== 'undefined') lucide.createIcons();
      }}, 2000);
    }};
    pre.appendChild(btn);
  }});
  if (typeof lucide !== 'undefined') lucide.createIcons();
}}

function copyLink(btn) {{
  navigator.clipboard.writeText(window.location.href);
  btn.classList.add('copied');
  btn.innerHTML = `<i data-lucide="check" width="18" height="18"></i> Copied!`;
  if (typeof lucide !== 'undefined') lucide.createIcons();
  setTimeout(() => {{
    btn.classList.remove('copied');
    btn.innerHTML = `<i data-lucide="link" width="18" height="18"></i> Copy link`;
    if (typeof lucide !== 'undefined') lucide.createIcons();
  }}, 2000);
}}

// ── Search ─────────────────────────────────────────
function openSearch() {{
  document.getElementById('searchOverlay').classList.add('open');
  setTimeout(() => document.getElementById('searchModalInput').focus(), 50);
  doSearch('');
}}

function closeSearch() {{
  document.getElementById('searchOverlay').classList.remove('open');
  document.getElementById('searchModalInput').value = '';
  searchIdx = -1;
}}

function closeSearchOnOverlay(e) {{
  if (e.target === document.getElementById('searchOverlay')) closeSearch();
}}

function doSearch(term) {{
  const list = document.getElementById('searchResultsList');
  const q = term.toLowerCase().trim();
  const results = Object.keys(TITLES).filter(id => {{
    if (!q) return true;
    return TITLES[id].toLowerCase().includes(q) ||
      (CATS[id] || '').toLowerCase().includes(q) ||
      (SUBCATS[id] || '').toLowerCase().includes(q);
  }}).slice(0, 12);

  if (!results.length) {{
    list.innerHTML = `<div class="search-no-results">No results for "<strong>${{term}}</strong>"</div>`;
    return;
  }}

  list.innerHTML = results.map((id, i) => `
    <div class="search-result" data-idx="${{i}}" data-id="${{id}}" onclick="pickResult('${{id}}')">
      <div class="search-result-icon">
        <i data-lucide="file-text" width="24" height="24"></i>
      </div>
      <div class="search-result-body">
        <div class="search-result-title">${{TITLES[id]}}</div>
        <div class="search-result-cat">${{CATS[id] || ''}}${{SUBCATS[id] ? ' › ' + SUBCATS[id] : ''}}</div>
      </div>
    </div>
  `).join('');

  if (typeof lucide !== 'undefined') lucide.createIcons();
  searchIdx = -1;
}}

function pickResult(id) {{
  closeSearch();
  showPage(id);
}}

function searchKeydown(e) {{
  const items = document.querySelectorAll('.search-result');
  if (e.key === 'ArrowDown') {{
    e.preventDefault();
    searchIdx = Math.min(searchIdx + 1, items.length - 1);
    highlightSearch(items);
  }}
  else if (e.key === 'ArrowUp') {{
    e.preventDefault();
    searchIdx = Math.max(searchIdx - 1, 0);
    highlightSearch(items);
  }}
  else if (e.key === 'Enter' && searchIdx >= 0) {{
    items[searchIdx]?.click();
  }}
  else if (e.key === 'Escape') {{
    closeSearch();
  }}
}}

function highlightSearch(items) {{
  items.forEach((el, i) => {{
    if (i === searchIdx) {{
      el.style.background = 'var(--md-sys-color-surface-container-high)';
      el.scrollIntoView({{ block: 'nearest' }});
    }} else {{
      el.style.background = '';
    }}
  }});
}}

// ── Keyboard shortcuts ─────────────────────────────
document.addEventListener('keydown', e => {{
  if ((e.metaKey || e.ctrlKey) && e.key === 'k') {{
    e.preventDefault();
    openSearch();
  }}
  if (e.key === '/' && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'TEXTAREA') {{
    e.preventDefault();
    openSearch();
  }}
  if (e.key === 'Escape' && document.getElementById('searchOverlay').classList.contains('open')) {{
    closeSearch();
  }}
  if (e.key === 'Escape' && document.querySelector('.mermaid-wrapper.fullscreen')) {{
    closeMermaidFullscreen();
  }}
}});

// ── Init ───────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {{
  const t = localStorage.getItem('docs-theme') || 'light';
  document.documentElement.setAttribute('data-theme', t);
  document.getElementById('themeName').textContent = THEMES[t] || 'Light';
  document.querySelectorAll('.theme-opt').forEach(o => o.classList.toggle('active', o.dataset.theme === t));

  if (window.location.hash) {{
    const id = window.location.hash.slice(1);
    if (PAGES[id]) showPage(id);
  }}

  addCopyButtons();
  initMermaid(t);
  initIcons();
}});
</script>
</body>
</html>"""

    def generate(self, config_file: str, output_file: str):
        with open(config_file, "r", encoding="utf-8") as f:
            if config_file.endswith(".json"):
                config = json.load(f)
            elif config_file.endswith((".yml", ".yaml")):
                config = yaml.safe_load(f)
            else:
                raise ValueError("Config must be .json, .yml, or .yaml")

        project_name = config.get("name", "Docs")
        project_version = config.get("version", "1.0.0")
        description = config.get("description", "Documentation")
        base_dir = os.path.dirname(os.path.abspath(config_file))

        sections = config.get("sections", [])
        categories = config.get("categories", [])

        if not categories:
            category_names = set(s.get("category", "General") for s in sections)
            categories = [
                {"name": name, "icon": "folder"} for name in sorted(category_names)
            ]

        sidebar_sections = {}
        page_categories = {}
        page_subcategories = {}

        for section in sections:
            category = section.get("category", "General")
            subcategory = section.get("subcategory", "")

            if category not in sidebar_sections:
                sidebar_sections[category] = {}
            if subcategory not in sidebar_sections[category]:
                sidebar_sections[category][subcategory] = []
            sidebar_sections[category][subcategory].append(section)

            title = section.get("title", "Untitled")
            section_id = (
                title.lower().replace(" ", "-").replace("/", "-").replace("&", "and")
            )
            page_categories[section_id] = category
            page_subcategories[section_id] = subcategory

        hero_section = self._get_hero_section(config)
        action_cards = self._get_action_cards()

        sidebar_html = []

        for category in categories:
            cat_name = category.get("name", "General")
            cat_id = re.sub(r"[^a-z0-9]+", "-", cat_name.lower())
            cat_sections = sidebar_sections.get(cat_name, {})

            if not cat_sections:
                continue

            sidebar_html.append(f"""
<div class="nav-group">
  <div class="nav-group-header" onclick="toggleGroup('{cat_id}')">
    <i data-lucide="folder" width="20" height="20"></i>
    {cat_name}
    <i data-lucide="chevron-down" class="nav-chevron" width="20" height="20"></i>
  </div>
  <div class="nav-group-items" id="ng-{cat_id}">
""")
            # Top-level items (no subcategory)
            for section in cat_sections.get("", []):
                title = section.get("title", "Untitled")
                sid = (
                    title.lower()
                    .replace(" ", "-")
                    .replace("/", "-")
                    .replace("&", "and")
                )
                icon = self._get_icon_name(section.get("icon", "description"))
                sidebar_html.append(
                    f'    <div class="nav-item" data-page="{sid}" onclick="showPage(\'{sid}\')">{icon}{title}</div>'
                )

            # Subcategories
            for sub_name, sub_items in cat_sections.items():
                if not sub_name:
                    continue
                sub_id = f"{cat_id}-{re.sub(r'[^a-z0-9]+', '-', sub_name.lower())}"
                sidebar_html.append(f"""
    <div class="nav-subgroup">
      <div class="nav-subgroup-header" data-sub="{sub_id}" onclick="toggleSub('{sub_id}')">
        <i data-lucide="chevron-down" width="18" height="18"></i>
        {sub_name}
        <i data-lucide="chevron-down" class="nav-chevron" width="18" height="18"></i>
      </div>
      <div class="nav-subgroup-items" id="ns-{sub_id}">
""")
                for section in sub_items:
                    title = section.get("title", "Untitled")
                    sid = (
                        title.lower()
                        .replace(" ", "-")
                        .replace("/", "-")
                        .replace("&", "and")
                    )
                    icon = self._get_icon_name(section.get("icon", "description"))
                    sidebar_html.append(
                        f'        <div class="nav-item sub" data-page="{sid}" onclick="showPage(\'{sid}\')">{icon}{title}</div>'
                    )
                sidebar_html.append("      </div>\n    </div>")

            sidebar_html.append("  </div>\n</div>")

        # Build pages
        pages_dict = {}
        titles_dict = {}

        for section in sections:
            title = section.get("title", "Untitled")
            description_sec = section.get("description", "")
            markdown_file = section.get("file", "")

            if markdown_file and not os.path.isabs(markdown_file):
                markdown_file = os.path.join(base_dir, markdown_file)

            sid = title.lower().replace(" ", "-").replace("/", "-").replace("&", "and")

            if markdown_file and os.path.exists(markdown_file):
                content = self.load_markdown(markdown_file)
            else:
                content = f"<h1>{title}</h1><p>{description_sec}</p>"

            pages_dict[sid] = content
            titles_dict[sid] = title

        pygments_styles = self._get_pygments_styles()
        current_year = datetime.now().year
        last_updated = datetime.now().strftime("%b %d, %Y")

        html = self.template.format(
            project_name=project_name,
            version=project_version,
            description=description,
            hero_section=hero_section,
            action_cards=action_cards,
            pygments_styles=pygments_styles,
            sidebar_content="\n".join(sidebar_html),
            pages_json=json.dumps(pages_dict),
            titles_json=json.dumps(titles_dict),
            categories_json=json.dumps(page_categories),
            subcategories_json=json.dumps(page_subcategories),
            year=current_year,
            last_updated=last_updated,
        )

        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(html)

        print(f"✓ Documentation generated: {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate documentation from markdown files"
    )
    parser.add_argument(
        "-c", "--config", required=True, help="Config file (.json or .yaml)"
    )
    parser.add_argument(
        "-o", "--output", default="docs/index.html", help="Output HTML file"
    )
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f"Error: Config file not found: {args.config}")
        return 1

    try:
        generator = DocGenerator()
        generator.generate(args.config, args.output)
        return 0
    except Exception as e:
        print(f"Error: {e}")
        raise


if __name__ == "__main__":
    exit(main())
