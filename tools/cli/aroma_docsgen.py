#!/usr/bin/env python3

import os
import json
import argparse
import markdown
import yaml
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional, Any
import re
from pygments import highlight
from pygments.lexers import get_lexer_by_name, guess_lexer
from pygments.formatters import HtmlFormatter

class DocGenerator:
    def __init__(self):
        self.template = self._get_template()
    
    def _get_pygments_styles(self) -> str:
        # Get styles for both light and dark themes
        light_formatter = HtmlFormatter(style='default', noclasses=False)
        dark_formatter = HtmlFormatter(style='monokai', noclasses=False)
        
        light_styles = light_formatter.get_style_defs('.codehilite')
        dark_styles = dark_formatter.get_style_defs('.codehilite')
        
        return f'''
        /* Light mode syntax highlighting */
        {light_styles}
        
        /* Dark mode syntax highlighting */
        [data-theme="dark"] .codehilite {{
            {dark_styles}
        }}
        '''
    
    def _get_template(self) -> str:
        template = '''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{project_name} • Documentation</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0,1" rel="stylesheet" />
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        :root {{
            --primary: #2563eb;
            --primary-dark: #1d4ed8;
            --primary-light: #3b82f6;
            --secondary: #64748b;
            --success: #22c55e;
            --warning: #f59e0b;
            --error: #ef4444;
            --surface-0: #ffffff;
            --surface-1: #f8fafc;
            --surface-2: #f1f5f9;
            --surface-3: #e2e8f0;
            --text-primary: #0f172a;
            --text-secondary: #334155;
            --text-tertiary: #64748b;
            --border: #e2e8f0;
            --border-dark: #cbd5e1;
            --shadow-sm: 0 1px 2px 0 rgb(0 0 0 / 0.05);
            --shadow: 0 4px 6px -1px rgb(0 0 0 / 0.1);
            --shadow-lg: 0 10px 15px -3px rgb(0 0 0 / 0.1);
            --sidebar-width: 280px;
            --header-height: 64px;
            
            /* Code highlighting colors - Light */
            --code-bg: #f8fafc;
            --code-text: #1e293b;
            --code-keyword: #7c3aed;
            --code-string: #059669;
            --code-comment: #64748b;
            --code-function: #2563eb;
            --code-number: #d97706;
            --code-operator: #334155;
            --code-punctuation: #475569;
        }}

        [data-theme="dark"] {{
            --primary: #3b82f6;
            --primary-dark: #2563eb;
            --primary-light: #60a5fa;
            --surface-0: #0f172a;
            --surface-1: #1e293b;
            --surface-2: #334155;
            --surface-3: #475569;
            --text-primary: #f8fafc;
            --text-secondary: #cbd5e1;
            --text-tertiary: #94a3b8;
            --border: #334155;
            --border-dark: #475569;
            
            /* Code highlighting colors - Dark */
            --code-bg: #1e1e2e;
            --code-text: #f8fafc;
            --code-keyword: #f38ba8;
            --code-string: #a6e3a1;
            --code-comment: #7f849c;
            --code-function: #89b4fa;
            --code-number: #fab387;
            --code-operator: #94e2d5;
            --code-punctuation: #bac2de;
        }}

        body {{
            font-family: 'Inter', sans-serif;
            background: var(--surface-1);
            color: var(--text-primary);
            line-height: 1.6;
            font-size: 15px;
            height: 100vh;
            overflow: hidden;
        }}

        .material-symbols-outlined {{
            font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24;
            font-size: 20px;
        }}

        .app {{
            display: flex;
            height: 100vh;
            overflow: hidden;
        }}

        .sidebar {{
            width: var(--sidebar-width);
            background: var(--surface-0);
            border-right: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            overflow-y: auto;
            flex-shrink: 0;
        }}

        .sidebar-header {{
            padding: 1.5rem;
            border-bottom: 1px solid var(--border);
        }}

        .project-name {{
            font-weight: 600;
            font-size: 1.25rem;
            color: var(--text-primary);
            margin-bottom: 0.25rem;
        }}

        .project-version {{
            color: var(--text-tertiary);
            font-size: 0.875rem;
        }}

        .sidebar-nav {{
            padding: 1rem 0;
            flex: 1;
        }}

        .nav-category {{
            margin-bottom: 1.5rem;
        }}

        .category-header {{
            padding: 0.5rem 1.5rem;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            color: var(--text-tertiary);
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            cursor: pointer;
            user-select: none;
        }}

        .category-header .material-symbols-outlined {{
            font-size: 16px;
        }}

        .category-items {{
            margin-top: 0.25rem;
        }}

        .category-items.collapsed {{
            display: none;
        }}

        .nav-item {{
            padding: 0.625rem 1.5rem 0.625rem 3.5rem;
            color: var(--text-secondary);
            font-size: 0.9375rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            position: relative;
            transition: all 0.2s;
        }}

        .nav-item:hover {{
            background: var(--surface-2);
            color: var(--text-primary);
        }}

        .nav-item.active {{
            background: var(--surface-2);
            color: var(--primary);
            font-weight: 500;
        }}

        .nav-item.active::before {{
            content: '';
            position: absolute;
            left: 0;
            top: 0;
            bottom: 0;
            width: 3px;
            background: var(--primary);
        }}

        .nav-item .material-symbols-outlined {{
            font-size: 18px;
            color: var(--text-tertiary);
        }}

        .nav-item.active .material-symbols-outlined {{
            color: var(--primary);
        }}

        .main {{
            flex: 1;
            display: flex;
            flex-direction: column;
            overflow: hidden;
        }}

        .header {{
            height: var(--header-height);
            background: var(--surface-0);
            border-bottom: 1px solid var(--border);
            padding: 0 1.5rem;
            display: flex;
            align-items: center;
            justify-content: space-between;
            flex-shrink: 0;
        }}

        .header-left {{
            display: flex;
            align-items: center;
            gap: 1rem;
        }}

        .menu-button {{
            display: none;
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
        }}

        .breadcrumb {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            color: var(--text-tertiary);
            font-size: 0.875rem;
        }}

        .breadcrumb a {{
            color: var(--text-secondary);
            text-decoration: none;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.25rem;
        }}

        .breadcrumb a:hover {{
            color: var(--primary);
        }}

        .header-right {{
            display: flex;
            align-items: center;
            gap: 1rem;
        }}

        .stats {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.375rem 0.75rem;
            background: var(--surface-2);
            border-radius: 6px;
            font-size: 0.875rem;
        }}

        .stat {{
            display: flex;
            align-items: center;
            gap: 0.375rem;
            color: var(--text-secondary);
        }}

        .stat .material-symbols-outlined {{
            font-size: 16px;
            color: var(--text-tertiary);
        }}

        .theme-toggle {{
            padding: 0.375rem;
            background: var(--surface-2);
            border: none;
            border-radius: 6px;
            color: var(--text-secondary);
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
        }}

        .theme-toggle:hover {{
            background: var(--surface-3);
        }}

        .content {{
            flex: 1;
            overflow-y: auto;
            padding: 2rem;
        }}

        .search-container {{
            max-width: 600px;
            margin: 0 auto 2rem;
            position: relative;
        }}

        .search-icon {{
            position: absolute;
            left: 1rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
        }}

        .search-input {{
            width: 100%;
            padding: 0.875rem 1rem 0.875rem 3rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 8px;
            font-family: 'Inter', sans-serif;
            font-size: 0.9375rem;
            color: var(--text-primary);
            outline: none;
            transition: all 0.2s;
        }}

        .search-input:focus {{
            border-color: var(--primary);
            box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1);
        }}

        .search-clear {{
            position: absolute;
            right: 1rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
            cursor: pointer;
            display: none;
        }}

        .search-clear.visible {{
            display: block;
        }}

        .search-stats {{
            margin-top: 0.5rem;
            font-size: 0.875rem;
            color: var(--text-tertiary);
        }}

        .cards-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
            gap: 1.5rem;
            max-width: 1400px;
            margin: 0 auto;
        }}

        .card {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 10px;
            padding: 1.5rem;
            cursor: pointer;
            transition: all 0.2s;
        }}

        .card:hover {{
            border-color: var(--primary);
            box-shadow: var(--shadow-lg);
            transform: translateY(-2px);
        }}

        .card-icon {{
            width: 48px;
            height: 48px;
            background: var(--surface-2);
            border-radius: 10px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 1.25rem;
            color: var(--primary);
        }}

        .card-icon .material-symbols-outlined {{
            font-size: 24px;
        }}

        .card h3 {{
            font-weight: 600;
            font-size: 1.125rem;
            margin-bottom: 0.5rem;
            color: var(--text-primary);
        }}

        .card p {{
            color: var(--text-secondary);
            font-size: 0.9375rem;
            margin-bottom: 1.25rem;
            line-height: 1.5;
        }}

        .card-footer {{
            display: flex;
            align-items: center;
            justify-content: space-between;
        }}

        .card-category {{
            display: inline-flex;
            align-items: center;
            gap: 0.375rem;
            padding: 0.25rem 0.75rem;
            background: var(--surface-2);
            border-radius: 4px;
            font-size: 0.8125rem;
            color: var(--text-tertiary);
        }}

        .card-arrow {{
            color: var(--text-tertiary);
            transition: transform 0.2s;
        }}

        .card:hover .card-arrow {{
            transform: translateX(4px);
            color: var(--primary);
        }}

        .doc-view {{
            display: none;
            max-width: 900px;
            margin: 0 auto;
        }}

        .doc-content {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2.5rem;
        }}

        .markdown-body {{
            color: var(--text-primary);
        }}

        .markdown-body h1 {{
            font-size: 2.25rem;
            font-weight: 600;
            margin: 0 0 1.5rem;
            letter-spacing: -0.02em;
        }}

        .markdown-body h2 {{
            font-size: 1.5rem;
            font-weight: 600;
            margin: 2rem 0 1rem;
            padding-bottom: 0.5rem;
            border-bottom: 1px solid var(--border);
        }}

        .markdown-body h3 {{
            font-size: 1.25rem;
            font-weight: 600;
            margin: 1.5rem 0 1rem;
        }}

        .markdown-body p {{
            margin: 1.25rem 0;
            color: var(--text-secondary);
        }}

        .markdown-body code {{
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.875em;
            padding: 0.2em 0.4em;
            background: var(--code-bg);
            border-radius: 4px;
            color: var(--code-text);
        }}

        .markdown-body pre {{
            margin: 1.5rem 0;
            padding: 1.25rem;
            background: var(--code-bg);
            border-radius: 8px;
            overflow-x: auto;
            border: 1px solid var(--border);
        }}

        .markdown-body pre code {{
            padding: 0;
            background: none;
            color: var(--code-text);
            font-size: 0.875rem;
            line-height: 1.7;
        }}

        /* Code highlighting */
        .markdown-body .k, .markdown-body .kd, .markdown-body .kn, .markdown-body .kp, .markdown-body .kr, .markdown-body .kt {{
            color: var(--code-keyword);
            font-weight: 500;
        }}

        .markdown-body .s, .markdown-body .s1, .markdown-body .s2, .markdown-body .sb, .markdown-body .sc, .markdown-body .sd, .markdown-body .se, .markdown-body .sh, .markdown-body .si, .markdown-body .sx {{
            color: var(--code-string);
        }}

        .markdown-body .c, .markdown-body .c1, .markdown-body .cm, .markdown-body .cp, .markdown-body .cs {{
            color: var(--code-comment);
            font-style: italic;
        }}

        .markdown-body .nf, .markdown-body .na, .markdown-body .nc {{
            color: var(--code-function);
        }}

        .markdown-body .m, .markdown-body .mi, .markdown-body .mf, .markdown-body .mh, .markdown-body .mo {{
            color: var(--code-number);
        }}

        .markdown-body .o, .markdown-body .ow {{
            color: var(--code-operator);
        }}

        .markdown-body .p {{
            color: var(--code-punctuation);
        }}

        .markdown-body .err {{
            color: var(--error);
            background: none;
        }}

        .markdown-body .gd {{
            color: var(--error);
        }}

        .markdown-body .gi {{
            color: var(--success);
        }}

        .markdown-body .gh {{
            color: var(--primary);
            font-weight: 500;
        }}

        .markdown-body .gu {{
            color: var(--text-tertiary);
        }}

        .markdown-body blockquote {{
            margin: 1.5rem 0;
            padding: 0.75rem 1.5rem;
            border-left: 4px solid var(--primary);
            background: var(--surface-2);
            color: var(--text-secondary);
        }}

        .markdown-body table {{
            width: 100%;
            margin: 1.5rem 0;
            border-collapse: collapse;
        }}

        .markdown-body th {{
            padding: 0.75rem 1rem;
            background: var(--surface-2);
            font-weight: 600;
            text-align: left;
            border: 1px solid var(--border);
        }}

        .markdown-body td {{
            padding: 0.75rem 1rem;
            border: 1px solid var(--border);
            color: var(--text-secondary);
        }}

        .markdown-body hr {{
            margin: 2rem 0;
            border: none;
            border-top: 1px solid var(--border);
        }}

        .empty-state {{
            text-align: center;
            padding: 4rem 2rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 12px;
        }}

        .empty-state .material-symbols-outlined {{
            font-size: 48px;
            color: var(--text-tertiary);
            margin-bottom: 1rem;
        }}

        .empty-state h3 {{
            font-size: 1.25rem;
            margin-bottom: 0.5rem;
        }}

        .empty-state p {{
            color: var(--text-tertiary);
        }}

        footer {{
            margin-top: 3rem;
            padding: 2rem 0;
            text-align: center;
            border-top: 1px solid var(--border);
            color: var(--text-tertiary);
            font-size: 0.875rem;
        }}

        .codehilite {{
            background: transparent !important;
        }}
        
        {pygments_styles}

        @media (max-width: 768px) {{
            .sidebar {{
                position: fixed;
                left: -280px;
                transition: left 0.3s;
                z-index: 100;
                height: 100vh;
            }}
            
            .sidebar.active {{
                left: 0;
            }}
            
            .menu-button {{
                display: block;
            }}
            
            .stats {{
                display: none;
            }}
            
            .content {{
                padding: 1rem;
            }}
            
            .cards-grid {{
                grid-template-columns: 1fr;
            }}
            
            .doc-content {{
                padding: 1.5rem;
            }}
        }}
    </style>
</head>
<body>
    <div class="app">
        <div class="sidebar" id="sidebar">
            <div class="sidebar-header">
                <div class="project-name">{project_name}</div>
                <div class="project-version">v{version}</div>
            </div>
            <div class="sidebar-nav">
                <div class="nav-category">
                    <div class="category-header" onclick="toggleCategory('home')">
                        <span class="material-symbols-outlined">chevron_right</span>
                        <span>Overview</span>
                    </div>
                    <div class="category-items" id="category-home">
                        <div class="nav-item active" onclick="showHome()">
                            <span class="material-symbols-outlined">home</span>
                            Home
                        </div>
                    </div>
                </div>
                {sidebar_content}
            </div>
        </div>

        <div class="main">
            <div class="header">
                <div class="header-left">
                    <button class="menu-button" onclick="toggleSidebar()">
                        <span class="material-symbols-outlined">menu</span>
                    </button>
                    <div class="breadcrumb" id="breadcrumb">
                        <a onclick="showHome()">
                            <span class="material-symbols-outlined">home</span>
                        </a>
                        <span class="material-symbols-outlined">chevron_right</span>
                        <span id="current-section">Home</span>
                    </div>
                </div>
                <div class="header-right">
                    <div class="stats">
                        <div class="stat">
                            <span class="material-symbols-outlined">category</span>
                            {category_count}
                        </div>
                        <div class="stat">
                            <span class="material-symbols-outlined">description</span>
                            {section_count}
                        </div>
                    </div>
                    <button class="theme-toggle" onclick="toggleTheme()">
                        <span class="material-symbols-outlined">light_mode</span>
                    </button>
                </div>
            </div>

            <div class="content" id="content">
                <div id="home-view">
                                <h1 style="margin-bottom: 2rem; text-align: center; font-size: 2rem; font-weight: 200;">Welcome to the {project_name} Documentation</h1>

                    <div class="search-container">
                        <span class="material-symbols-outlined search-icon">search</span>
                        <input type="text" class="search-input" id="searchInput" placeholder="Search documentation...">
                        <span class="material-symbols-outlined search-clear" id="searchClear" onclick="clearSearch()">close</span>
                        <div class="search-stats" id="searchStats"></div>
                    </div>
                    
                    <div class="cards-grid" id="cardsGrid">
                        {cards_html}
                    </div>
                </div>

                <div id="doc-view" class="doc-view">
                    <div class="doc-content markdown-body" id="doc-content"></div>
                </div>

            </div>
        </div>
    </div>

    <script>
        const pages = {pages_json};
        const titles = {titles_json};

        function toggleTheme() {{
            const html = document.documentElement;
            const theme = html.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
            html.setAttribute('data-theme', theme);
            localStorage.setItem('theme', theme);
            document.querySelector('.theme-toggle .material-symbols-outlined').textContent = 
                theme === 'dark' ? 'dark_mode' : 'light_mode';
        }}

        const savedTheme = localStorage.getItem('theme') || 'light';
        document.documentElement.setAttribute('data-theme', savedTheme);
        document.querySelector('.theme-toggle .material-symbols-outlined').textContent = 
            savedTheme === 'dark' ? 'dark_mode' : 'light_mode';

        function toggleSidebar() {{
            document.getElementById('sidebar').classList.toggle('active');
        }}

        function toggleCategory(id) {{
            const items = document.getElementById('category-' + id);
            const header = items.previousElementSibling;
            const icon = header.querySelector('.material-symbols-outlined');
            
            items.classList.toggle('collapsed');
            icon.textContent = items.classList.contains('collapsed') ? 'chevron_right' : 'expand_more';
        }}

        const searchInput = document.getElementById('searchInput');
        const searchClear = document.getElementById('searchClear');
        const searchStats = document.getElementById('searchStats');
        const cardsGrid = document.getElementById('cardsGrid');

        function filterCards() {{
            const term = searchInput.value.toLowerCase();
            
            if (term.length > 0) {{
                searchClear.classList.add('visible');
            }} else {{
                searchClear.classList.remove('visible');
            }}
            
            let visible = 0;
            const cards = cardsGrid.children;
            
            for (let i = 0; i < cards.length; i++) {{
                const card = cards[i];
                if (card.id === 'empty-search-state') continue;
                
                const title = card.querySelector('h3').textContent.toLowerCase();
                const desc = card.querySelector('p').textContent.toLowerCase();
                const category = card.querySelector('.card-category').textContent.toLowerCase();
                
                if (title.includes(term) || desc.includes(term) || category.includes(term)) {{
                    card.style.display = 'block';
                    visible++;
                }} else {{
                    card.style.display = 'none';
                }}
            }}
            
            if (term.length > 0) {{
                searchStats.textContent = `Found ${{visible}} section${{visible !== 1 ? 's' : ''}}`;
            }} else {{
                searchStats.textContent = '';
            }}
            
            let emptyState = document.getElementById('empty-search-state');
            if (visible === 0 && term.length > 0) {{
                if (!emptyState) {{
                    emptyState = document.createElement('div');
                    emptyState.id = 'empty-search-state';
                    emptyState.className = 'empty-state';
                    emptyState.innerHTML = `
                        <span class="material-symbols-outlined">search_off</span>
                        <h3>No results found</h3>
                        <p>Try adjusting your search terms</p>
                    `;
                    cardsGrid.appendChild(emptyState);
                }}
            }} else if (emptyState) {{
                emptyState.remove();
            }}
        }}

        function clearSearch() {{
            searchInput.value = '';
            filterCards();
            searchInput.focus();
        }}

        let searchTimeout;
        searchInput.addEventListener('input', () => {{
            clearTimeout(searchTimeout);
            searchTimeout = setTimeout(filterCards, 300);
        }});

        function showHome() {{
            document.getElementById('home-view').style.display = 'block';
            document.getElementById('doc-view').style.display = 'none';
            document.getElementById('current-section').textContent = 'Home';
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            document.querySelector('[onclick="showHome()"]').classList.add('active');
            document.getElementById('sidebar').classList.remove('active');
            window.location.hash = '';
            clearSearch();
        }}

        function showPage(id) {{
            document.getElementById('home-view').style.display = 'none';
            document.getElementById('doc-view').style.display = 'block';
            document.getElementById('doc-content').innerHTML = pages[id];
            document.getElementById('current-section').textContent = titles[id];
            
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            const activeItem = Array.from(document.querySelectorAll('.nav-item')).find(
                i => i.getAttribute('onclick') && i.getAttribute('onclick').includes(id)
            );
            if (activeItem) activeItem.classList.add('active');
            
            document.getElementById('sidebar').classList.remove('active');
            document.getElementById('content').scrollTop = 0;
            window.location.hash = id;
        }}

        window.onload = () => {{
            if (window.location.hash) {{
                const id = window.location.hash.substring(1);
                if (pages[id]) showPage(id);
                else showHome();
            }} else {{
                showHome();
            }}
        }};

        window.onhashchange = () => {{
            if (window.location.hash) {{
                const id = window.location.hash.substring(1);
                if (pages[id]) showPage(id);
                else showHome();
            }} else {{
                showHome();
            }}
        }};
    </script>
</body>
</html>'''
        return template
    
    def _get_icon_name(self, icon_name: str) -> str:
        icon_map = {
            'bluetooth': 'bluetooth',
            'wifi': 'wifi',
            'permissions': 'security',
            'api': 'cloud',
            'settings': 'settings',
            'network': 'device_hub',
            'security': 'lock',
            'database': 'database',
            'code': 'code',
            'docs': 'description',
            'guide': 'explore',
            'example': 'code_blocks',
            'config': 'tune',
            'home': 'home',
            'user': 'person',
            'cog': 'settings_applications',
            'search': 'search',
            'download': 'download',
            'upload': 'upload',
            'refresh': 'refresh',
            'warning': 'warning',
            'error': 'error',
            'info': 'info',
            'question': 'help',
            'plus': 'add',
            'edit': 'edit',
            'delete': 'delete',
            'save': 'save',
            'copy': 'content_copy',
            'link': 'link',
            'calendar': 'calendar_today',
            'clock': 'schedule',
            'folder': 'folder',
            'archive': 'archive',
            'default': 'description'
        }
        return icon_map.get(icon_name.lower(), icon_map['default'])
    
    def _process_markdown(self, content: str) -> str:
        html = markdown.markdown(
            content,
            extensions=[
                'extra',
                'codehilite',
                'toc',
                'tables',
                'fenced_code',
                'attr_list',
                'def_list',
                'abbr',
                'footnotes'
            ]
        )
        return html
    
    def load_markdown(self, file_path: str) -> str:
        try:
            if not os.path.exists(file_path):
                return f'<h1>File not found</h1><p>{file_path}</p>'
            
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            return self._process_markdown(content)
            
        except Exception as e:
            return f'<h1>Error</h1><p>{e}</p>'
    
    def generate(self, config_file: str, output_file: str):
        with open(config_file, 'r', encoding='utf-8') as f:
            if config_file.endswith('.json'):
                config = json.load(f)
            elif config_file.endswith(('.yml', '.yaml')):
                config = yaml.safe_load(f)
            else:
                raise ValueError("Config must be .json, .yml, or .yaml")
        
        project_name = config.get('name', 'Documentation')
        project_version = config.get('version', '1.0.0')
        base_dir = os.path.dirname(os.path.abspath(config_file))
        
        sections = config.get('sections', [])
        categories = config.get('categories', [])
        
        if not categories:
            category_names = set(s.get('category', 'General') for s in sections)
            categories = [{'name': name, 'icon': 'folder'} for name in sorted(category_names)]
        
        sidebar_sections = {}
        for section in sections:
            category = section.get('category', 'General')
            if category not in sidebar_sections:
                sidebar_sections[category] = []
            sidebar_sections[category].append(section)
        
        sidebar_content = []
        category_count = len(categories)
        
        for category in categories:
            category_name = category.get('name', 'General')
            category_id = category_name.lower().replace(' ', '-')
            category_sections = sidebar_sections.get(category_name, [])
            
            sidebar_content.append(f'''
                <div class="nav-category">
                    <div class="category-header" onclick="toggleCategory('{category_id}')">
                        <span class="material-symbols-outlined">expand_more</span>
                        <span>{category_name}</span>
                    </div>
                    <div class="category-items" id="category-{category_id}">
            ''')
            
            for section in category_sections:
                title = section.get('title', 'Untitled')
                section_id = title.lower().replace(' ', '-')
                icon = self._get_icon_name(section.get('icon', 'default'))
                
                sidebar_content.append(f'''
                        <div class="nav-item" onclick="showPage('{section_id}')">
                            <span class="material-symbols-outlined">{icon}</span>
                            {title}
                        </div>
                ''')
            
            sidebar_content.append('</div></div>')
        
        cards_html = []
        pages_dict = {}
        titles_dict = {}
        
        for section in sections:
            title = section.get('title', 'Untitled')
            description = section.get('description', '')
            icon = self._get_icon_name(section.get('icon', 'default'))
            markdown_file = section.get('file', '')
            category = section.get('category', 'General')
            
            if markdown_file and not os.path.isabs(markdown_file):
                markdown_file = os.path.join(base_dir, markdown_file)
            
            section_id = title.lower().replace(' ', '-')
            
            if markdown_file and os.path.exists(markdown_file):
                content = self.load_markdown(markdown_file)
            else:
                content = f'<h1>{title}</h1><p>{description}</p>'
            
            pages_dict[section_id] = content
            titles_dict[section_id] = title
            
            cards_html.append(f'''
                <div class="card" onclick="showPage('{section_id}')">
                    <div class="card-icon">
                        <span class="material-symbols-outlined">{icon}</span>
                    </div>
                    <h3>{title}</h3>
                    <p>{description}</p>
                    <div class="card-footer">
                        <span class="card-category">
                            <span class="material-symbols-outlined">folder</span>
                            {category}
                        </span>
                        <span class="material-symbols-outlined card-arrow">arrow_forward</span>
                    </div>
                </div>
            ''')
        
        pygments_styles = self._get_pygments_styles()
        
        html = self.template.format(
            project_name=project_name,
            version=project_version,
            category_count=category_count,
            section_count=len(sections),
            pygments_styles=pygments_styles,
            sidebar_content='\n'.join(sidebar_content),
            cards_html='\n'.join(cards_html),
            pages_json=json.dumps(pages_dict),
            titles_json=json.dumps(titles_dict)
        )
        
        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html)
        
        print(f'Documentation generated: {output_file}')

def create_example_config():
    config = {
        "name": "SDK Documentation",
        "version": "2.1.0",
        "categories": [
            {"name": "Connectivity", "icon": "network"},
            {"name": "Security", "icon": "security"},
            {"name": "Networking", "icon": "api"},
            {"name": "Setup", "icon": "settings"},
            {"name": "Guides", "icon": "guide"}
        ],
        "sections": [
            {
                "title": "Bluetooth API",
                "description": "Bluetooth LE and Classic API reference",
                "icon": "bluetooth",
                "category": "Connectivity",
                "file": "docs/bluetooth.md"
            },
            {
                "title": "WiFi Manager",
                "description": "Network connectivity and configuration",
                "icon": "wifi",
                "category": "Connectivity",
                "file": "docs/wifi.md"
            },
            {
                "title": "Permissions System",
                "description": "Runtime permission handling",
                "icon": "permissions",
                "category": "Security",
                "file": "docs/permissions.md"
            },
            {
                "title": "REST API Client",
                "description": "HTTP client and data models",
                "icon": "api",
                "category": "Networking",
                "file": "docs/api.md"
            },
            {
                "title": "Configuration",
                "description": "Library settings and initialization",
                "icon": "settings",
                "category": "Setup",
                "file": "docs/config.md"
            },
            {
                "title": "Code Examples",
                "description": "Sample code and patterns",
                "icon": "code",
                "category": "Guides",
                "file": "docs/examples.md"
            }
        ]
    }
    
    with open('docs-config.json', 'w', encoding='utf-8') as f:
        json.dump(config, f, indent=2)

    os.makedirs('docs', exist_ok=True)
    
    example_files = {
        'bluetooth.md': '# Bluetooth API\n\n## Overview\nBluetooth API provides support for Bluetooth LE and Classic devices.\n\n## Features\n- Device discovery\n- Connection management\n- Service handling\n\n## Example\n```python\nfrom sdk.bluetooth import BluetoothManager\n\nmanager = BluetoothManager()\nmanager.start_scan()\n\ndef on_device_found(device):\n    print(f"Found: {device.name}")\n```',
        'wifi.md': '# WiFi Manager\n\n## Overview\nManage WiFi connections and network scanning.\n\n## Usage\n```python\nfrom sdk.wifi import WiFiManager\n\nwifi = WiFiManager()\nnetworks = wifi.scan()\nwifi.connect("SSID", "password")\n```',
        'permissions.md': '# Permissions System\n\n## Overview\nHandle runtime permissions.\n\n## Example\n```python\nfrom sdk.permissions import PermissionManager\n\npm = PermissionManager()\npm.request("camera")\n```',
        'api.md': '# REST API Client\n\n## Overview\nHTTP client with built-in features.\n\n## Example\n```python\nfrom sdk.api import APIClient\n\nclient = APIClient("https://api.example.com")\nresponse = client.get("/users")\n```',
        'config.md': '# Configuration\n\n## Overview\nLibrary configuration options.\n\n## Settings\n```python\nfrom sdk import config\n\nconfig.set("logging", True)\nconfig.set("timeout", 30)\n```',
        'examples.md': '# Code Examples\n\n## Complete Examples\n\n### Bluetooth Scanning\n```python\nfrom sdk.bluetooth import BluetoothManager\n\nmanager = BluetoothManager()\nfor device in manager.scan(timeout=5):\n    print(device.name)\n```'
    }
    
    for filename, content in example_files.items():
        with open(f'docs/{filename}', 'w', encoding='utf-8') as f:
            f.write(content)
    
    print('Example configuration created: docs-config.json')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--config', default='docs-config.json')
    parser.add_argument('-o', '--output', default='docs/index.html')
    parser.add_argument('--init', action='store_true')
    
    args = parser.parse_args()
    
    if args.init:
        create_example_config()
        return
    
    if not os.path.exists(args.config):
        print(f'Config not found: {args.config}')
        return
    
    generator = DocGenerator()
    generator.generate(args.config, args.output)

if __name__ == '__main__':
    main()