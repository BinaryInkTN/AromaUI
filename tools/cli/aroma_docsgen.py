#!/usr/bin/env python3

import os
import json
import argparse
import markdown
import yaml
from datetime import datetime
from typing import Dict, List
import re
from pygments import highlight
from pygments.lexers import get_lexer_by_name
from pygments.formatters import HtmlFormatter

class DocGenerator:
    def __init__(self):
        self.template = self._get_template()
    
    def _get_pygments_styles(self) -> str:
        light_formatter = HtmlFormatter(style='default', noclasses=False)
        dark_formatter = HtmlFormatter(style='monokai', noclasses=False)
        
        light_styles = light_formatter.get_style_defs('.codehilite')
        dark_styles = dark_formatter.get_style_defs('.codehilite')
        
        return f'''
        {light_styles}
        [data-theme="dark"] .codehilite {{
            {dark_styles}
        }}
        [data-theme="nord"] .codehilite {{
            {dark_styles}
        }}
        '''
    
    def _get_platform_filters(self, all_platforms: List[str]) -> str:
        platform_icons = {
            'linux': 'terminal',
            'windows': 'window',
            'android': 'android'
        }
        
        unique_platforms = sorted(set([p.lower() for p in all_platforms if p.lower() in platform_icons]))
        
        if not unique_platforms:
            return ''
        
        filters = ['<div class="platform-filters" role="tablist">']
        filters.append('''
            <button class="platform-filter active" data-platform="all" onclick="filterByPlatform('all')" role="tab">
                <span class="material-symbols-outlined">apps</span>
                <span>All</span>
            </button>
        ''')
        
        for platform in unique_platforms:
            icon = platform_icons.get(platform, 'devices')
            platform_display = platform.capitalize()
            
            filters.append(f'''
                <button class="platform-filter" data-platform="{platform}" onclick="filterByPlatform('{platform}')" role="tab">
                    <span class="material-symbols-outlined">{icon}</span>
                    <span>{platform_display}</span>
                </button>
            ''')
        
        filters.append('</div>')
        return '\n'.join(filters)
    
    def _get_hero_section(self, config: Dict) -> str:
        hero_config = config.get('hero', {})
        
        hero_title = hero_config.get('title', 'AromaSDK')
        description = hero_config.get('description', 'The complete software development kit for building cross-platform applications.')
        version = config.get('version', '1.0.0')
        
        badges = hero_config.get('badges', [])
        badges_html = ''
        if badges:
            badge_items = []
            for badge in badges:
                icon = badge.get('icon', 'code')
                text = badge.get('text', '')
                badge_items.append(f'''
                    <span class="hero-badge">
                        <span class="material-symbols-outlined">{icon}</span>
                        {text}
                    </span>
                ''')
            badges_html = f'<div class="hero-badges">{"".join(badge_items)}</div>'
        
        actions = hero_config.get('actions', [])
        actions_html = ''
        if actions:
            action_items = []
            for action in actions:
                icon = action.get('icon', 'download')
                text = action.get('text', 'Button')
                type_class = 'hero-button-primary' if action.get('primary', False) else 'hero-button-secondary'
                onclick = action.get('onclick', '')
                
                action_items.append(f'''
                    <button class="hero-button {type_class}" onclick="{onclick}">
                        <span class="material-symbols-outlined">{icon}</span>
                        {text}
                    </button>
                ''')
            actions_html = f'<div class="hero-actions">{"".join(action_items)}</div>'
        
        stats = hero_config.get('stats', [])
        stats_html = ''
        if stats:
            stat_items = []
            for stat in stats:
                value = stat.get('value', '0')
                label = stat.get('label', '')
                stat_items.append(f'''
                    <div class="hero-stat">
                        <span class="hero-stat-value">{value}</span>
                        <span class="hero-stat-label">{label}</span>
                    </div>
                ''')
            stats_html = f'<div class="hero-stats">{"".join(stat_items)}</div>'
        
        platform_icons = hero_config.get('platformIcons', [])
        platform_icons_html = ''
        if platform_icons:
            icon_items = []
            for platform in platform_icons:
                icon = platform.get('icon', 'code')
                title = platform.get('title', '')
                icon_items.append(f'''
                    <span class="hero-platform-icon" title="{title}">
                        <span class="material-symbols-outlined">{icon}</span>
                    </span>
                ''')
            platform_icons_html = f'<div class="hero-platform-icons">{"".join(icon_items)}</div>'
        
        hero_html = f'''
        <div class="hero-section">
            <div class="hero-content">
                <h1 class="hero-title">
                    {hero_title} <span>v{version}</span>
                </h1>
                <p class="hero-description">{description}</p>
                
                {badges_html}
                {actions_html}
                {stats_html}
            </div>
            
            {platform_icons_html}
        </div>
        '''
        
        return hero_html
    
    def _get_template(self) -> str:
        template = '''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{project_name} - Documentation</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Google+Sans:wght@400;500;600;700&family=Google+Sans+Mono:wght@400;500&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" />
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        :root {{
            --sidebar-width: 280px;
            --progress-indicator-width: 300px;
            --header-height: 64px;
        }}

        :root[data-theme="light"] {{
            --primary: #1a73e8;
            --primary-light: #e8f0fe;
            --surface-0: #ffffff;
            --surface-1: #f8f9fa;
            --surface-2: #f1f3f4;
            --surface-3: #e8eaed;
            --text-primary: #202124;
            --text-secondary: #5f6368;
            --text-tertiary: #80868b;
            --border: #dadce0;
            --border-dark: #bdc1c6;
            --shadow: 0 1px 2px 0 rgba(60,64,67,0.1);
            --shadow-hover: 0 1px 3px 0 rgba(60,64,67,0.2);
            --code-bg: #f8f9fa;
            --code-text: #202124;
            --hover-overlay: rgba(0,0,0,0.04);
            --footer-bg: #f8f9fa;
            --progress-bar-bg: #e8eaed;
            --progress-dot: #9aa0a6;
            --progress-dot-active: #1a73e8;
        }}

        :root[data-theme="dark"] {{
            --primary: #8ab4f8;
            --primary-light: #1e2a3a;
            --surface-0: #202124;
            --surface-1: #292a2d;
            --surface-2: #303134;
            --surface-3: #3c4043;
            --text-primary: #e8eaed;
            --text-secondary: #9aa0a6;
            --text-tertiary: #80868b;
            --border: #3c4043;
            --border-dark: #5f6368;
            --shadow: 0 1px 2px 0 rgba(0,0,0,0.3);
            --shadow-hover: 0 1px 3px 0 rgba(0,0,0,0.4);
            --code-bg: #303134;
            --code-text: #e8eaed;
            --hover-overlay: rgba(255,255,255,0.04);
            --footer-bg: #292a2d;
            --progress-bar-bg: #3c4043;
            --progress-dot: #5f6368;
            --progress-dot-active: #8ab4f8;
        }}

        :root[data-theme="sepia"] {{
            --primary: #8b5a2b;
            --primary-light: #f4ecd8;
            --surface-0: #f4ecd8;
            --surface-1: #e8dccc;
            --surface-2: #d8ccbc;
            --surface-3: #c8bcac;
            --text-primary: #3e2e23;
            --text-secondary: #5e4e3e;
            --text-tertiary: #7e6e5e;
            --border: #d8ccbc;
            --border-dark: #c8bcac;
            --shadow: 0 1px 2px 0 rgba(0,0,0,0.1);
            --shadow-hover: 0 1px 3px 0 rgba(0,0,0,0.15);
            --code-bg: #e8dccc;
            --code-text: #3e2e23;
            --hover-overlay: rgba(0,0,0,0.04);
            --footer-bg: #e8dccc;
            --progress-bar-bg: #d8ccbc;
            --progress-dot: #8b7b6b;
            --progress-dot-active: #8b5a2b;
        }}

        :root[data-theme="nord"] {{
            --primary: #88c0d0;
            --primary-light: #eceff4;
            --surface-0: #2e3440;
            --surface-1: #3b4252;
            --surface-2: #434c5e;
            --surface-3: #4c566a;
            --text-primary: #eceff4;
            --text-secondary: #e5e9f0;
            --text-tertiary: #d8dee9;
            --border: #434c5e;
            --border-dark: #4c566a;
            --shadow: 0 1px 2px 0 rgba(0,0,0,0.3);
            --shadow-hover: 0 1px 3px 0 rgba(0,0,0,0.4);
            --code-bg: #3b4252;
            --code-text: #eceff4;
            --hover-overlay: rgba(255,255,255,0.04);
            --footer-bg: #3b4252;
            --progress-bar-bg: #434c5e;
            --progress-dot: #4c566a;
            --progress-dot-active: #88c0d0;
        }}

        :root[data-theme="solarized"] {{
            --primary: #268bd2;
            --primary-light: #eee8d5;
            --surface-0: #fdf6e3;
            --surface-1: #eee8d5;
            --surface-2: #93a1a1;
            --surface-3: #839496;
            --text-primary: #002b36;
            --text-secondary: #073642;
            --text-tertiary: #586e75;
            --border: #93a1a1;
            --border-dark: #839496;
            --shadow: 0 1px 2px 0 rgba(0,0,0,0.1);
            --shadow-hover: 0 1px 3px 0 rgba(0,0,0,0.15);
            --code-bg: #eee8d5;
            --code-text: #002b36;
            --hover-overlay: rgba(0,0,0,0.04);
            --footer-bg: #eee8d5;
            --progress-bar-bg: #93a1a1;
            --progress-dot: #586e75;
            --progress-dot-active: #268bd2;
        }}

        body {{
            font-family: 'Google Sans', -apple-system, BlinkMacSystemFont, sans-serif;
            background: var(--surface-1);
            color: var(--text-primary);
            font-size: 14px;
            height: 100vh;
            overflow: hidden;
            line-height: 1.5;
            -webkit-font-smoothing: antialiased;
        }}

        .material-symbols-outlined {{
            font-size: 20px;
            font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 20;
        }}

        .app {{
            display: flex;
            height: 100vh;
            overflow: hidden;
            position: relative;
        }}

        .sidebar-overlay {{
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0,0,0,0.5);
            z-index: 90;
            opacity: 0;
            visibility: hidden;
            transition: opacity 0.2s;
        }}

        .sidebar-overlay.active {{
            opacity: 1;
            visibility: visible;
        }}

        .sidebar {{
            width: var(--sidebar-width);
            background: var(--surface-0);
            border-right: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            overflow-y: auto;
            flex-shrink: 0;
            z-index: 100;
        }}

        .sidebar-header {{
            padding: 1.5rem 1.5rem 1rem;
            border-bottom: 1px solid var(--border);
        }}

        .project-header {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            margin-bottom: 0.25rem;
        }}

        .project-name {{
            font-weight: 500;
            font-size: 1.25rem;
            color: var(--text-primary);
        }}

        .docs-badge {{
            display: inline-flex;
            align-items: center;
            padding: 0.125rem 0.5rem;
            background: var(--primary-light);
            color: var(--primary);
            font-size: 0.75rem;
            font-weight: 500;
            border-radius: 4px;
            text-transform: uppercase;
        }}

        .project-version {{
            color: var(--text-tertiary);
            font-size: 0.8125rem;
        }}

        .platform-filters {{
            display: flex;
            gap: 0.5rem;
            margin: 1.5rem 0 2rem;
            padding: 0.25rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 36px;
        }}

        .platform-filter {{
            flex: 1;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
            padding: 0.5rem 1rem;
            border: none;
            border-radius: 32px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.875rem;
            font-weight: 500;
            font-family: 'Google Sans', sans-serif;
            cursor: pointer;
        }}

        .platform-filter:hover {{
            background: var(--hover-overlay);
        }}

        .platform-filter.active {{
            background: var(--primary-light);
            color: var(--primary);
        }}

        .sidebar-nav {{
            padding: 1rem 0;
            flex: 1;
        }}

        .nav-category {{
            margin-bottom: 1rem;
        }}

        .category-header {{
            padding: 0.5rem 1.5rem;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            color: var(--text-tertiary);
            font-size: 0.75rem;
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.025em;
            cursor: pointer;
            user-select: none;
        }}

        .category-header:hover {{
            background: var(--hover-overlay);
        }}

        .category-header .material-symbols-outlined {{
            font-size: 16px;
        }}

        .category-items.collapsed {{
            display: none;
        }}

        .nav-item {{
            padding: 0.5rem 1.5rem 0.5rem 3.5rem;
            color: var(--text-secondary);
            font-size: 0.875rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            position: relative;
            margin-right: 0.5rem;
            border-radius: 0 24px 24px 0;
        }}

        .nav-item:hover {{
            background: var(--hover-overlay);
        }}

        .nav-item.active {{
            background: var(--primary-light);
            color: var(--primary);
        }}

        .nav-item.active::before {{
            content: '';
            position: absolute;
            left: 0;
            top: 0;
            bottom: 0;
            width: 4px;
            background: var(--primary);
            border-radius: 0 4px 4px 0;
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
            position: relative;
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
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            width: 40px;
            height: 40px;
            border-radius: 20px;
            display: none;
            align-items: center;
            justify-content: center;
        }}

        .menu-button:hover {{
            background: var(--hover-overlay);
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
            padding: 0.5rem;
            border-radius: 20px;
        }}

        .breadcrumb a:hover {{
            background: var(--hover-overlay);
        }}

        .breadcrumb span:last-child {{
            color: var(--text-primary);
            font-weight: 500;
        }}

        .header-right {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }}

        .stats {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.375rem 0.75rem;
            background: var(--surface-2);
            border-radius: 100px;
            font-size: 0.8125rem;
        }}

        .stat {{
            display: flex;
            align-items: center;
            gap: 0.375rem;
            color: var(--text-secondary);
        }}

        .theme-selector {{
            position: relative;
        }}

        .theme-button {{
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border: none;
            border-radius: 20px;
            color: var(--text-secondary);
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.875rem;
            font-family: 'Google Sans', sans-serif;
        }}

        .theme-button:hover {{
            background: var(--surface-3);
        }}

        .theme-dropdown {{
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 16px;
            box-shadow: var(--shadow-hover);
            display: none;
            z-index: 1000;
            min-width: 180px;
            overflow: hidden;
        }}

        .theme-dropdown.show {{
            display: block;
        }}

        .theme-option {{
            padding: 0.75rem 1rem;
            cursor: pointer;
            color: var(--text-secondary);
            font-size: 0.875rem;
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }}

        .theme-option:hover {{
            background: var(--hover-overlay);
        }}

        .theme-option.active {{
            background: var(--primary-light);
            color: var(--primary);
        }}

        .content-wrapper {{
            display: flex;
            flex: 1;
            overflow: hidden;
            position: relative;
        }}

        .content {{
            flex: 1;
            overflow-y: auto;
            padding: 2rem;
            scroll-behavior: smooth;
        }}

        .progress-indicator {{
            width: var(--progress-indicator-width);
            background: var(--surface-0);
            border-left: 1px solid var(--border);
            padding: 1.5rem 0;
            overflow-y: auto;
            display: none;
            flex-shrink: 0;
        }}

        .progress-indicator.visible {{
            display: block;
        }}

        .progress-header {{
            padding: 0 1rem 1rem 1rem;
            border-bottom: 1px solid var(--border);
            margin-bottom: 1rem;
        }}

        .progress-title {{
            font-size: 0.75rem;
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.025em;
            color: var(--text-tertiary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }}

        .progress-bar-container {{
            padding: 0 1rem 1rem 1rem;
        }}

        .progress-bar {{
            height: 4px;
            background: var(--progress-bar-bg);
            border-radius: 2px;
            overflow: hidden;
            margin-bottom: 0.5rem;
        }}

        .progress-fill {{
            height: 100%;
            background: var(--primary);
            width: 0%;
            transition: width 0.1s ease;
        }}

        .progress-percentage {{
            font-size: 0.75rem;
            color: var(--text-secondary);
            text-align: right;
        }}

        .section-list {{
            list-style: none;
            padding: 0;
            margin: 0;
        }}

        .section-item {{
            padding: 0.5rem 1rem;
            margin: 0.125rem 0;
            cursor: pointer;
            font-size: 0.8125rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
            border-left: 2px solid transparent;
            transition: all 0.2s ease;
        }}

        .section-item:hover {{
            background: var(--hover-overlay);
        }}

        .section-item.active {{
            color: var(--primary);
            border-left-color: var(--primary);
            background: var(--primary-light);
        }}

        .section-dot {{
            width: 6px;
            height: 6px;
            border-radius: 50%;
            background: var(--progress-dot);
            flex-shrink: 0;
        }}

        .section-item.active .section-dot {{
            background: var(--progress-dot-active);
            transform: scale(1.2);
        }}

        .section-title {{
            flex: 1;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }}

        .hero-section {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2.5rem;
            margin-bottom: 2rem;
            display: flex;
            align-items: flex-start;
            justify-content: space-between;
            gap: 2rem;
        }}

        .hero-content {{
            flex: 1;
        }}

        .hero-title {{
            font-size: 2rem;
            font-weight: 500;
            margin-bottom: 0.75rem;
            color: var(--text-primary);
            letter-spacing: -0.01em;
        }}

        .hero-title span {{
            background: var(--surface-2);
            color: var(--text-secondary);
            padding: 0.125rem 0.75rem;
            border-radius: 16px;
            font-size: 0.875rem;
            margin-left: 0.75rem;
            vertical-align: middle;
        }}

        .hero-description {{
            font-size: 1rem;
            color: var(--text-secondary);
            margin-bottom: 1.5rem;
            max-width: 600px;
        }}

        .hero-badges {{
            display: flex;
            gap: 0.75rem;
            margin-bottom: 1.5rem;
            flex-wrap: wrap;
        }}

        .hero-badge {{
            background: var(--surface-2);
            padding: 0.375rem 0.875rem;
            border-radius: 16px;
            display: flex;
            align-items: center;
            gap: 0.375rem;
            font-size: 0.8125rem;
            color: var(--text-secondary);
        }}

        .hero-actions {{
            display: flex;
            gap: 0.75rem;
            flex-wrap: wrap;
            margin-bottom: 1.5rem;
        }}

        .hero-button {{
            display: inline-flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.625rem 1.5rem;
            border-radius: 24px;
            font-weight: 500;
            font-size: 0.875rem;
            cursor: pointer;
            border: none;
            font-family: 'Google Sans', sans-serif;
        }}

        .hero-button-primary {{
            background: var(--primary);
            color: var(--surface-0);
        }}

        .hero-button-primary:hover {{
            background: var(--primary-dark);
            color : var(--text-primary);
        }}

        .hero-button-secondary {{
            background: var(--surface-2);
            color: var(--text-secondary);
        }}

        .hero-button-secondary:hover {{
            background: var(--surface-3);
        }}

        .hero-stats {{
            display: flex;
            gap: 2rem;
        }}

        .hero-stat {{
            display: flex;
            flex-direction: column;
        }}

        .hero-stat-value {{
            font-size: 1.25rem;
            font-weight: 500;
            color: var(--primary);
            margin-bottom: 0.125rem;
        }}

        .hero-stat-label {{
            font-size: 0.75rem;
            color: var(--text-tertiary);
        }}

        .hero-platform-icons {{
            display: flex;
            gap: 0.5rem;
            margin-top: 1rem;
        }}

        .hero-platform-icon {{
            width: 40px;
            height: 40px;
            background: var(--surface-2);
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            color: var(--text-secondary);
        }}

        .hero-platform-icon:hover {{
            background: var(--surface-3);
        }}

        .search-container {{
            max-width: 600px;
            margin: 0 auto 1.5rem;
            position: relative;
        }}

        .search-icon {{
            position: absolute;
            left: 1rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
            pointer-events: none;
        }}

        .search-input {{
            width: 100%;
            padding: 0.75rem 1rem 0.75rem 3rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 24px;
            font-family: 'Google Sans', sans-serif;
            font-size: 0.9375rem;
            color: var(--text-primary);
            outline: none;
        }}

        .search-input:focus {{
            border-color: var(--primary);
            box-shadow: 0 0 0 2px var(--primary-light);
        }}

        .search-clear {{
            position: absolute;
            right: 0.5rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
            cursor: pointer;
            display: none;
            width: 32px;
            height: 32px;
            border-radius: 16px;
            align-items: center;
            justify-content: center;
            background: var(--surface-0);
            border: none;
        }}

        .search-clear:hover {{
            background: var(--hover-overlay);
        }}

        .search-clear.visible {{
            display: flex;
        }}

        .cards-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
            gap: 1rem;
            max-width: 1200px;
            margin: 0 auto;
        }}

        .card {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1.25rem;
            cursor: pointer;
        }}

        .card:hover {{
            background: var(--surface-1);
            border-color: var(--primary);
        }}

        .card-icon {{
            width: 40px;
            height: 40px;
            background: var(--surface-2);
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 1rem;
            color: var(--primary);
        }}

        .card h3 {{
            font-weight: 500;
            font-size: 1rem;
            color: var(--text-primary);
            margin-bottom: 0.5rem;
        }}

        .card p {{
            color: var(--text-secondary);
            font-size: 0.875rem;
            margin-bottom: 1rem;
            line-height: 1.4;
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
            border-radius: 12px;
            font-size: 0.75rem;
            color: var(--text-tertiary);
        }}

        .card-platforms {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            margin-top: 0.75rem;
            padding-top: 0.75rem;
            border-top: 1px solid var(--border);
        }}

        .card-platform {{
            display: inline-flex;
            align-items: center;
            gap: 0.25rem;
            padding: 0.125rem 0.375rem;
            border-radius: 4px;
            font-size: 0.75rem;
            color: var(--text-tertiary);
            background: var(--surface-2);
        }}

        .doc-view {{
            display: none;
            max-width: 900px;
            margin: 0 auto;
        }}

        .doc-header {{
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 1.5rem;
            padding-bottom: 1rem;
            border-bottom: 1px solid var(--border);
        }}

        .doc-title {{
            font-size: 1.75rem;
            font-weight: 500;
            color: var(--text-primary);
        }}

        .doc-actions {{
            display: flex;
            gap: 0.5rem;
        }}

        .doc-action-button {{
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border: none;
            border-radius: 20px;
            color: var(--text-secondary);
            font-size: 0.875rem;
            font-family: 'Google Sans', sans-serif;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }}

        .doc-action-button:hover {{
            background: var(--surface-3);
        }}

        .doc-content {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 2rem;
        }}

        .footer {{
            background: var(--footer-bg);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            margin-top: 2rem;
            text-align: center;
            color: var(--text-tertiary);
            font-size: 0.8125rem;
            border-radius: 8px;
        }}

        .footer-content {{
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }}

        .footer-info {{
            display: flex;
            gap: 1.5rem;
        }}

        .markdown-body {{
            color: var(--text-primary);
        }}

        .markdown-body h1 {{
            font-size: 1.75rem;
            font-weight: 500;
            margin: 0 0 1rem;
            color: var(--text-primary);
            border-bottom: 1px solid var(--border);
            padding-bottom: 0.5rem;
            scroll-margin-top: 2rem;
        }}

        .markdown-body h2 {{
            font-size: 1.5rem;
            font-weight: 500;
            margin: 1.5rem 0 0.75rem;
            color: var(--text-primary);
            scroll-margin-top: 2rem;
        }}

        .markdown-body h3 {{
            font-size: 1.25rem;
            font-weight: 500;
            margin: 1.25rem 0 0.5rem;
            color: var(--text-primary);
            scroll-margin-top: 2rem;
        }}

        .markdown-body h4, .markdown-body h5, .markdown-body h6 {{
            scroll-margin-top: 2rem;
        }}

        .markdown-body p {{
            margin: 0.75rem 0;
            color: var(--text-secondary);
        }}

        .markdown-body a {{
            color: var(--primary);
            text-decoration: none;
        }}

        .markdown-body a:hover {{
            text-decoration: underline;
        }}

        .markdown-body pre {{
            margin: 1rem 0;
            position: relative;
            border-radius: 8px;
            overflow: hidden;
            background: var(--code-bg);
            border: 1px solid var(--border);
        }}

        .markdown-body pre .copy-button {{
            position: absolute;
            top: 0.5rem;
            right: 0.5rem;
            padding: 0.25rem 0.75rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 16px;
            color: var(--text-secondary);
            font-size: 0.75rem;
            font-family: 'Google Sans', sans-serif;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.375rem;
            opacity: 0;
        }}

        .markdown-body pre:hover .copy-button {{
            opacity: 1;
        }}

        .markdown-body pre .copy-button:hover {{
            background: var(--surface-2);
        }}

        .markdown-body pre .copy-button.copied {{
            background: var(--primary);
            color: white;
        }}

        .markdown-body code {{
            font-family: 'Google Sans Mono', monospace;
            font-size: 0.875em;
            padding: 0.2em 0.4em;
            background: var(--code-bg);
            border-radius: 4px;
            color: var(--code-text);
        }}

        .markdown-body pre code {{
            padding: 1rem;
            background: transparent;
            font-size: 0.8125rem;
            line-height: 1.5;
            display: block;
            overflow-x: auto;
        }}

        .markdown-body table {{
            width: 100%;
            margin: 1rem 0;
            border-collapse: collapse;
            border: 1px solid var(--border);
            border-radius: 8px;
            overflow: hidden;
        }}

        .markdown-body th {{
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            font-weight: 500;
            text-align: left;
            border-bottom: 1px solid var(--border);
        }}

        .markdown-body td {{
            padding: 0.5rem 1rem;
            border-bottom: 1px solid var(--border);
        }}

        .markdown-body tr:last-child td {{
            border-bottom: none;
        }}

        .markdown-body ul, .markdown-body ol {{
            margin: 0.75rem 0;
            padding-left: 1.5rem;
        }}

        .markdown-body li {{
            margin: 0.25rem 0;
        }}

        .empty-state {{
            text-align: center;
            padding: 3rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 8px;
            grid-column: 1 / -1;
        }}

        .empty-state .material-symbols-outlined {{
            font-size: 48px;
            color: var(--text-tertiary);
            margin-bottom: 1rem;
        }}

        .empty-state h3 {{
            font-size: 1.25rem;
            font-weight: 500;
            margin-bottom: 0.5rem;
            color: var(--text-primary);
        }}

        .codehilite {{
            background: transparent !important;
        }}
        
        {pygments_styles}

        @media (max-width: 768px) {{
            .menu-button {{
                display: flex;
            }}
            
            .sidebar {{
                position: fixed;
                left: 0;
                top: 0;
                bottom: 0;
                transform: translateX(-100%);
                transition: transform 0.2s;
            }}
            
            .sidebar.active {{
                transform: translateX(0);
            }}
            
            .stats {{
                display: none;
            }}
            
            .content {{
                padding: 1rem;
            }}
            
            .hero-section {{
                flex-direction: column;
                padding: 1.5rem;
            }}
            
            .hero-platform-icons {{
                justify-content: center;
            }}
            
            .cards-grid {{
                grid-template-columns: 1fr;
            }}
            
            .doc-header {{
                flex-direction: column;
                align-items: flex-start;
                gap: 1rem;
            }}
            
            .doc-actions {{
                width: 100%;
            }}
            
            .footer-content {{
                flex-direction: column;
                gap: 0.5rem;
            }}
            
            .footer-info {{
                flex-direction: column;
                gap: 0.25rem;
            }}
            
            .progress-indicator {{
                position: fixed;
                right: 0;
                top: var(--header-height);
                bottom: 0;
                background: var(--surface-0);
                box-shadow: var(--shadow-hover);
                transform: translateX(100%);
                transition: transform 0.2s;
                z-index: 95;
            }}
            
            .progress-indicator.visible {{
                transform: translateX(0);
            }}
        }}
    </style>
</head>
<body data-theme="light">
    <div class="app">
        <div class="sidebar-overlay" id="sidebarOverlay" onclick="toggleSidebar()"></div>
        
        <div class="sidebar" id="sidebar">
            <div class="sidebar-header">
                <div class="project-header">
                    <span class="project-name">{project_name}</span>
                    <span class="docs-badge">DOCS</span>
                </div>
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
                            <span>{category_count}</span>
                        </div>
                        <div class="stat">
                            <span class="material-symbols-outlined">description</span>
                            <span>{section_count}</span>
                        </div>
                    </div>
                    <div class="theme-selector">
                        <button class="theme-button" onclick="toggleThemeDropdown()">
                            <span class="material-symbols-outlined" id="themeIcon">light_mode</span>
                            <span id="current-theme-label">Light</span>
                        </button>
                        <div class="theme-dropdown" id="themeDropdown">
                            <div class="theme-option" onclick="setTheme('light', event)">
                                <span class="material-symbols-outlined">light_mode</span>
                                Light
                            </div>
                            <div class="theme-option" onclick="setTheme('dark', event)">
                                <span class="material-symbols-outlined">dark_mode</span>
                                Dark
                            </div>
                            <div class="theme-option" onclick="setTheme('sepia', event)">
                                <span class="material-symbols-outlined">book</span>
                                Sepia
                            </div>
                            <div class="theme-option" onclick="setTheme('nord', event)">
                                <span class="material-symbols-outlined">ac_unit</span>
                                Nord
                            </div>
                            <div class="theme-option" onclick="setTheme('solarized', event)">
                                <span class="material-symbols-outlined">wb_sunny</span>
                                Solarized
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <div class="content-wrapper">
                <div class="content" id="content">
                    <div id="home-view">
                        {hero_section}

                        <div class="search-container">
                            <span class="material-symbols-outlined search-icon">search</span>
                            <input type="text" class="search-input" id="searchInput" placeholder="Search documentation (Press / to focus)">
                            <button class="search-clear" id="searchClear" onclick="clearSearch()">
                                <span class="material-symbols-outlined">close</span>
                            </button>
                        </div>
                        
                        {platform_filters}
                        
                        <div class="cards-grid" id="cardsGrid">
                            {cards_html}
                        </div>

                        <div class="footer">
                            <div class="footer-content">
                                <div class="footer-copyright">
                                    <span>© {year} {project_name}</span>
                                </div>
                                <div class="footer-info">
                                    <span>Version {version}</span>
                                    <span>Last updated {last_updated}</span>
                                </div>
                            </div>
                        </div>
                    </div>

                    <div id="doc-view" class="doc-view">
                        <div class="doc-header">
                            <h1 class="doc-title" id="doc-title"></h1>
                            <div class="doc-actions">
                                <button class="doc-action-button" onclick="copyPageLink(event)">
                                    <span class="material-symbols-outlined">link</span>
                                    Copy link
                                </button>
                            </div>
                        </div>
                        <div class="doc-content markdown-body" id="doc-content"></div>
                        
                        <div class="footer">
                            <div class="footer-content">
                                <div class="footer-copyright">
                                    <span>© {year} {project_name}</span>
                                </div>
                                <div class="footer-info">
                                    <span>Version {version}</span>
                                    <span>Last updated {last_updated}</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>

                <div class="progress-indicator" id="progressIndicator">
                    <div class="progress-header">
                        <div class="progress-title">
                            <span class="material-symbols-outlined">timeline</span>
                            <span>On this page</span>
                        </div>
                    </div>
                    <div class="progress-bar-container">
                        <div class="progress-bar">
                            <div class="progress-fill" id="progressFill"></div>
                        </div>
                        <div class="progress-percentage" id="progressPercentage">0%</div>
                    </div>
                    <div id="sectionList" class="section-list"></div>
                </div>
            </div>
        </div>
    </div>

    <script>
        const pages = {pages_json};
        const titles = {titles_json};
        const cardPlatforms = {card_platforms_json};

        let searchTimeout;
        let currentPlatform = 'all';
        let currentDocId = null;
        let observer = null;
        let sections = [];

        (function() {{
            const savedTheme = localStorage.getItem('theme') || 'light';
            document.documentElement.setAttribute('data-theme', savedTheme);
        }})();

        function toggleThemeDropdown() {{
            const dropdown = document.getElementById('themeDropdown');
            dropdown.classList.toggle('show');
            
            if (dropdown.classList.contains('show')) {{
                document.addEventListener('click', function closeDropdown(e) {{
                    if (!dropdown.contains(e.target) && !e.target.closest('.theme-button')) {{
                        dropdown.classList.remove('show');
                        document.removeEventListener('click', closeDropdown);
                    }}
                }});
            }}
        }}

        function setTheme(theme, event) {{
            document.documentElement.setAttribute('data-theme', theme);
            localStorage.setItem('theme', theme);
            
            document.getElementById('themeDropdown').classList.remove('show');
            
            document.querySelectorAll('.theme-option').forEach(opt => opt.classList.remove('active'));
            event.target.closest('.theme-option').classList.add('active');
            
            const themeNames = {{
                'light': 'Light',
                'dark': 'Dark',
                'sepia': 'Sepia',
                'nord': 'Nord',
                'solarized': 'Solarized'
            }};
            
            document.getElementById('current-theme-label').textContent = themeNames[theme];
            
            const themeIcon = document.getElementById('themeIcon');
            const icons = {{
                'light': 'light_mode',
                'dark': 'dark_mode',
                'sepia': 'book',
                'nord': 'ac_unit',
                'solarized': 'wb_sunny'
            }};
            themeIcon.textContent = icons[theme];
        }}

        function filterByPlatform(platform) {{
            currentPlatform = platform;
            
            document.querySelectorAll('.platform-filter').forEach(btn => {{
                btn.classList.remove('active');
                if (btn.dataset.platform === platform) {{
                    btn.classList.add('active');
                }}
            }});
            
            const cards = document.querySelectorAll('.card');
            let visibleCount = 0;
            
            cards.forEach(card => {{
                const cardId = card.getAttribute('data-id');
                const platforms = cardPlatforms[cardId] || [];
                
                if (platform === 'all' || platforms.includes(platform)) {{
                    card.style.display = 'block';
                    visibleCount++;
                }} else {{
                    card.style.display = 'none';
                }}
            }});
            
            const searchTerm = document.getElementById('searchInput').value.toLowerCase().trim();
            if (searchTerm) filterCards();
            
            const emptyState = document.getElementById('empty-platform-state');
            if (visibleCount === 0) {{
                if (!emptyState) {{
                    const newEmptyState = document.createElement('div');
                    newEmptyState.id = 'empty-platform-state';
                    newEmptyState.className = 'empty-state';
                    newEmptyState.innerHTML = `
                        <span class="material-symbols-outlined">devices_off</span>
                        <h3>No content for this platform</h3>
                        <p>Try selecting a different filter</p>
                    `;
                    document.getElementById('cardsGrid').appendChild(newEmptyState);
                }}
            }} else if (emptyState) {{
                emptyState.remove();
            }}
        }}

        function toggleSidebar() {{
            document.getElementById('sidebar').classList.toggle('active');
            document.getElementById('sidebarOverlay').classList.toggle('active');
        }}

        function toggleCategory(id) {{
            const items = document.getElementById('category-' + id);
            const header = items.previousElementSibling;
            const icon = header.querySelector('.material-symbols-outlined');
            
            items.classList.toggle('collapsed');
            icon.textContent = items.classList.contains('collapsed') ? 'chevron_right' : 'expand_more';
        }}

        function initializeCopyButtons() {{
            document.querySelectorAll('.markdown-body pre').forEach(pre => {{
                if (!pre.querySelector('.copy-button')) {{
                    const button = document.createElement('button');
                    button.className = 'copy-button';
                    button.innerHTML = '<span class="material-symbols-outlined">content_copy</span><span>Copy</span>';
                    
                    button.addEventListener('click', async (e) => {{
                        e.stopPropagation();
                        const code = pre.querySelector('code');
                        if (code) {{
                            await navigator.clipboard.writeText(code.textContent || '');
                            button.classList.add('copied');
                            button.innerHTML = '<span class="material-symbols-outlined">check</span><span>Copied!</span>';
                            
                            setTimeout(() => {{
                                button.classList.remove('copied');
                                button.innerHTML = '<span class="material-symbols-outlined">content_copy</span><span>Copy</span>';
                            }}, 2000);
                        }}
                    }});
                    
                    pre.appendChild(button);
                }}
            }});
        }}

        function extractSections() {{
            const content = document.getElementById('doc-content');
            if (!content) return [];
            
            const headings = content.querySelectorAll('h1, h2, h3, h4, h5, h6');
            const sections = [];
            
            headings.forEach((heading, index) => {{
                if (!heading.id) {{
                    heading.id = 'section-' + index + '-' + heading.textContent.toLowerCase().replace(/[^a-z0-9]+/g, '-');
                }}
                
                sections.push({{
                    id: heading.id,
                    title: heading.textContent,
                    level: parseInt(heading.tagName[1]),
                    element: heading
                }});
            }});
            
            return sections;
        }}

        function updateProgressIndicator() {{
            const content = document.getElementById('doc-content');
            if (!content) return;
            
            const scrollContainer = document.getElementById('content');
            const containerHeight = scrollContainer.clientHeight;
            const scrollTop = scrollContainer.scrollTop;
            const contentHeight = content.scrollHeight;
            const maxScroll = contentHeight - containerHeight;
            
            let percentage = maxScroll > 0 ? Math.round((scrollTop / maxScroll) * 100) : 0;
            if(percentage > 100) percentage = 100;
            document.getElementById('progressFill').style.width = percentage + '%';
            document.getElementById('progressPercentage').textContent = percentage + '%';
            
            let currentSection = null;
            let minDistance = Infinity;
            
            sections.forEach(section => {{
                const element = section.element;
                const rect = element.getBoundingClientRect();
                
                const distance = Math.abs(rect.top - 100);
                
                if (distance < minDistance && rect.top < window.innerHeight * 0.7) {{
                    minDistance = distance;
                    currentSection = section;
                }}
            }});
            
            document.querySelectorAll('.section-item').forEach(item => {{
                item.classList.remove('active');
                if (currentSection && item.dataset.sectionId === currentSection.id) {{
                    item.classList.add('active');
                    
                    item.scrollIntoView({{
                        block: 'nearest',
                        behavior: 'auto'
                    }});
                }}
            }});
        }}

        function buildSectionList() {{
            const sectionList = document.getElementById('sectionList');
            sectionList.innerHTML = '';
            
            sections.forEach(section => {{
                const item = document.createElement('div');
                item.className = 'section-item';
                item.dataset.sectionId = section.id;
                item.setAttribute('onclick', 'scrollToSection(\\'' + section.id + '\\')');
                
                const dot = document.createElement('span');
                dot.className = 'section-dot';
                
                const title = document.createElement('span');
                title.className = 'section-title';
                title.textContent = section.title;
                
                item.style.paddingLeft = ((section.level - 1) * 16 + 8) + 'px';
                
                item.appendChild(dot);
                item.appendChild(title);
                sectionList.appendChild(item);
            }});
        }}

        function scrollToSection(sectionId) {{
            const element = document.getElementById(sectionId);
            if (element) {{
                const content = document.getElementById('content');
                const rect = element.getBoundingClientRect();
                const contentRect = content.getBoundingClientRect();
                
                content.scrollTo({{
                    top: content.scrollTop + (rect.top - contentRect.top - 20),
                    behavior: 'smooth'
                }});
            }}
        }}

        function initializeProgressTracking() {{
            if (observer) {{
                observer.disconnect();
            }}
            
            sections = extractSections();
            
            buildSectionList();
            
            const indicator = document.getElementById('progressIndicator');
            if (sections.length > 0) {{
                indicator.classList.add('visible');
                
                const content = document.getElementById('content');
                content.addEventListener('scroll', updateProgressIndicator);
                updateProgressIndicator();
                
                observer = new MutationObserver(() => {{
                    sections = extractSections();
                    buildSectionList();
                    updateProgressIndicator();
                }});
                
                observer.observe(document.getElementById('doc-content'), {{
                    childList: true,
                    subtree: true,
                    characterData: true
                }});
            }} else {{
                indicator.classList.remove('visible');
            }}
        }}

        const searchInput = document.getElementById('searchInput');
        const searchClear = document.getElementById('searchClear');
        const cardsGrid = document.getElementById('cardsGrid');

        function filterCards() {{
            const term = searchInput.value.toLowerCase().trim();
            
            searchClear.classList.toggle('visible', term.length > 0);
            
            let visible = 0;
            const cards = cardsGrid.children;
            
            for (let i = 0; i < cards.length; i++) {{
                const card = cards[i];
                if (card.id === 'empty-platform-state' || card.id === 'empty-search-state') continue;
                
                const cardId = card.getAttribute('data-id');
                const platforms = cardPlatforms[cardId] || [];
                const matchesPlatform = currentPlatform === 'all' || platforms.includes(currentPlatform);
                
                if (!matchesPlatform) {{
                    card.style.display = 'none';
                    continue;
                }}
                
                const title = card.querySelector('h3')?.textContent.toLowerCase() || '';
                const desc = card.querySelector('p')?.textContent.toLowerCase() || '';
                
                if (title.includes(term) || desc.includes(term)) {{
                    card.style.display = 'block';
                    visible++;
                }} else {{
                    card.style.display = 'none';
                }}
            }}
            
            const emptySearchState = document.getElementById('empty-search-state');
            if (visible === 0 && term.length > 0 && !emptySearchState) {{
                const newEmptyState = document.createElement('div');
                newEmptyState.id = 'empty-search-state';
                newEmptyState.className = 'empty-state';
                newEmptyState.innerHTML = `
                    <span class="material-symbols-outlined">search_off</span>
                    <h3>No results found</h3>
                    <p>Try different keywords</p>
                `;
                cardsGrid.appendChild(newEmptyState);
            }} else if (emptySearchState && (visible > 0 || term.length === 0)) {{
                emptySearchState.remove();
            }}
        }}

        function clearSearch() {{
            searchInput.value = '';
            filterCards();
            searchInput.focus();
        }}

        function showHome() {{
            document.getElementById('home-view').style.display = 'block';
            document.getElementById('doc-view').style.display = 'none';
            document.getElementById('current-section').textContent = 'Home';
            
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            document.querySelector('[onclick="showHome()"]').classList.add('active');
            
            if (window.innerWidth <= 768) toggleSidebar();
            
            window.location.hash = '';
            filterByPlatform('all');
            
            document.getElementById('progressIndicator').classList.remove('visible');
        }}

        function showPage(id) {{
            if (!pages[id]) return;
            
            document.getElementById('home-view').style.display = 'none';
            document.getElementById('doc-view').style.display = 'block';
            document.getElementById('doc-content').innerHTML = pages[id];
            document.getElementById('doc-title').textContent = titles[id];
            document.getElementById('current-section').textContent = titles[id];
            
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            const activeNav = document.querySelector(`[onclick="showPage('${{id}}')"]`);
            if (activeNav) activeNav.classList.add('active');
            
            if (window.innerWidth <= 768) toggleSidebar();
            
            document.getElementById('content').scrollTop = 0;
            window.location.hash = id;
            
            currentDocId = id;
            
            initializeCopyButtons();
            initializeProgressTracking();
        }}

        function copyPageLink(event) {{
            navigator.clipboard.writeText(window.location.href);
            const button = event.target.closest('.doc-action-button');
            const originalText = button.innerHTML;
            button.innerHTML = '<span class="material-symbols-outlined">check</span>Copied!';
            setTimeout(() => {{
                button.innerHTML = originalText;
            }}, 2000);
        }}

        document.addEventListener('keydown', (e) => {{
            if (e.key === '/' && !e.ctrlKey && !e.metaKey && document.activeElement?.tagName !== 'INPUT') {{
                e.preventDefault();
                searchInput.focus();
            }}
            
            if ((e.ctrlKey || e.metaKey) && e.key === 'k') {{
                e.preventDefault();
                searchInput.focus();
            }}
        }});

        document.addEventListener('DOMContentLoaded', () => {{
            searchInput.addEventListener('input', () => {{
                clearTimeout(searchTimeout);
                searchTimeout = setTimeout(filterCards, 200);
            }});

            const savedTheme = localStorage.getItem('theme') || 'light';
            const themeOptions = document.querySelectorAll('.theme-option');
            for (let option of themeOptions) {{
                if (option.textContent.toLowerCase().includes(savedTheme)) {{
                    setTheme(savedTheme, {{ target: option }});
                    break;
                }}
            }}
            
            if (window.location.hash) {{
                const id = window.location.hash.substring(1);
                if (pages[id]) showPage(id);
            }}
            
            initializeCopyButtons();
        }});
    </script>
</body>
</html>'''
        return template
    
    def _get_icon_name(self, icon_name: str) -> str:
        return icon_name.lower()
    
    def _process_markdown(self, content: str) -> str:
        extensions = [
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
        
        html = markdown.markdown(
            content,
            extensions=extensions
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
        all_platforms = set()
        card_platforms = {}
        
        for section in sections:
            category = section.get('category', 'General')
            if category not in sidebar_sections:
                sidebar_sections[category] = []
            sidebar_sections[category].append(section)
            
            platforms = section.get('platforms', [])
            for platform in platforms:
                all_platforms.add(platform.lower())
            
            title = section.get('title', 'Untitled')
            section_id = title.lower().replace(' ', '-')
            card_platforms[section_id] = [p.lower() for p in platforms]
        
        hero_section = self._get_hero_section(config)
        platform_filters = self._get_platform_filters(list(all_platforms))
        
        sidebar_content = []
        category_count = len(categories)
        
        for category in categories:
            category_name = category.get('name', 'General')
            category_id = category_name.lower().replace(' ', '-')
            category_sections = sidebar_sections.get(category_name, [])
            
            if category_sections:
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
                    icon = self._get_icon_name(section.get('icon', 'description'))
                    
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
            icon = self._get_icon_name(section.get('icon', 'description'))
            markdown_file = section.get('file', '')
            category = section.get('category', 'General')
            platforms = section.get('platforms', [])
            
            if markdown_file and not os.path.isabs(markdown_file):
                markdown_file = os.path.join(base_dir, markdown_file)
            
            section_id = title.lower().replace(' ', '-')
            
            if markdown_file and os.path.exists(markdown_file):
                content = self.load_markdown(markdown_file)
            else:
                content = f'<h1>{title}</h1><p>{description}</p>'
            
            pages_dict[section_id] = content
            titles_dict[section_id] = title
            
            platform_icons = ''
            for platform in platforms:
                platform_lower = platform.lower()
                icon_map = {
                    'linux': 'terminal',
                    'windows': 'window',
                    'android': 'android'
                }
                icon_name = icon_map.get(platform_lower, 'devices')
                platform_icons += f'''
                    <span class="card-platform">
                        <span class="material-symbols-outlined">{icon_name}</span>
                        <span>{platform}</span>
                    </span>
                '''
            
            cards_html.append(f'''
                <div class="card" onclick="showPage('{section_id}')" data-id="{section_id}">
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
                    </div>
                    {f'<div class="card-platforms">{platform_icons}</div>' if platform_icons else ''}
                </div>
            ''')
        
        pygments_styles = self._get_pygments_styles()
        current_year = datetime.now().year
        last_updated = datetime.now().strftime('%b %d, %Y')
        
        html = self.template.format(
            project_name=project_name,
            version=project_version,
            hero_section=hero_section,
            platform_filters=platform_filters,
            category_count=category_count,
            section_count=len(sections),
            pygments_styles=pygments_styles,
            sidebar_content='\n'.join(sidebar_content),
            cards_html='\n'.join(cards_html),
            pages_json=json.dumps(pages_dict),
            titles_json=json.dumps(titles_dict),
            card_platforms_json=json.dumps(card_platforms),
            year=current_year,
            last_updated=last_updated
        )
        
        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html)
        
        print(f'Documentation generated: {output_file}')

def main():
    parser = argparse.ArgumentParser(description='Generate Google-style documentation from markdown files')
    parser.add_argument('-c', '--config', required=True, help='Configuration file (JSON or YAML)')
    parser.add_argument('-o', '--output', default='docs/index.html', help='Output HTML file path')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.config):
        print(f'Error: Config file not found: {args.config}')
        return 1
    
    try:
        generator = DocGenerator()
        generator.generate(args.config, args.output)
        return 0
    except Exception as e:
        print(f'Error generating documentation: {e}')
        return 1

if __name__ == '__main__':
    exit(main())