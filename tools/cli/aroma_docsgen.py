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
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" />
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        :root {{
            --sidebar-width: 280px;
            --header-height: 70px;
            --transition-speed: 0.3s;
            --transition-timing: cubic-bezier(0.4, 0, 0.2, 1);
        }}

        :root[data-theme="light"] {{
            --primary: #2563eb;
            --primary-light: #3b82f6;
            --primary-dark: #1d4ed8;
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
            --code-bg: #f1f5f9;
            --code-text: #0f172a;
            --code-keyword: #7c3aed;
            --code-string: #059669;
            --code-comment: #64748b;
            --code-function: #2563eb;
            --code-number: #d97706;
            --hover-overlay: rgba(0,0,0,0.04);
            --overlay-bg: rgba(0,0,0,0.5);
            --copy-btn-bg: #ffffff;
            --copy-btn-hover: #f1f5f9;
            --footer-bg: #f1f5f9;
        }}

        :root[data-theme="dark"] {{
            --primary: #3b82f6;
            --primary-light: #60a5fa;
            --primary-dark: #2563eb;
            --secondary: #94a3b8;
            --success: #4ade80;
            --warning: #fbbf24;
            --error: #f87171;
            --surface-0: #0f172a;
            --surface-1: #1e293b;
            --surface-2: #334155;
            --surface-3: #475569;
            --text-primary: #f8fafc;
            --text-secondary: #cbd5e1;
            --text-tertiary: #94a3b8;
            --border: #334155;
            --border-dark: #475569;
            --shadow-sm: 0 1px 2px 0 rgb(0 0 0 / 0.3);
            --shadow: 0 4px 6px -1px rgb(0 0 0 / 0.4);
            --shadow-lg: 0 10px 15px -3px rgb(0 0 0 / 0.4);
            --code-bg: #1e1e2e;
            --code-text: #f8fafc;
            --code-keyword: #f38ba8;
            --code-string: #a6e3a1;
            --code-comment: #7f849c;
            --code-function: #89b4fa;
            --code-number: #fab387;
            --hover-overlay: rgba(255,255,255,0.04);
            --overlay-bg: rgba(0,0,0,0.7);
            --copy-btn-bg: #334155;
            --copy-btn-hover: #475569;
            --footer-bg: #1e293b;
        }}

        :root[data-theme="sepia"] {{
            --primary: #8b5a2b;
            --primary-light: #a67b5b;
            --primary-dark: #6b4423;
            --secondary: #8b7e6e;
            --success: #6b8e4c;
            --warning: #c49a6c;
            --error: #b55a4a;
            --surface-0: #f4ecd8;
            --surface-1: #e8dccc;
            --surface-2: #d8ccbc;
            --surface-3: #c8bcac;
            --text-primary: #3e2e23;
            --text-secondary: #5e4e3e;
            --text-tertiary: #7e6e5e;
            --border: #d8ccbc;
            --border-dark: #c8bcac;
            --shadow-sm: 0 1px 2px 0 rgba(0,0,0,0.1);
            --shadow: 0 4px 6px -1px rgba(0,0,0,0.1);
            --shadow-lg: 0 10px 15px -3px rgba(0,0,0,0.1);
            --code-bg: #e8dccc;
            --code-text: #3e2e23;
            --code-keyword: #8b5a2b;
            --code-string: #6b8e4c;
            --code-comment: #8b7e6e;
            --code-function: #a67b5b;
            --code-number: #b55a4a;
            --hover-overlay: rgba(0,0,0,0.04);
            --overlay-bg: rgba(0,0,0,0.3);
            --copy-btn-bg: #d8ccbc;
            --copy-btn-hover: #c8bcac;
            --footer-bg: #e8dccc;
        }}

        :root[data-theme="nord"] {{
            --primary: #88c0d0;
            --primary-light: #8fbcbb;
            --primary-dark: #81a1c1;
            --secondary: #d8dee9;
            --success: #a3be8c;
            --warning: #ebcb8b;
            --error: #bf616a;
            --surface-0: #2e3440;
            --surface-1: #3b4252;
            --surface-2: #434c5e;
            --surface-3: #4c566a;
            --text-primary: #eceff4;
            --text-secondary: #e5e9f0;
            --text-tertiary: #d8dee9;
            --border: #434c5e;
            --border-dark: #4c566a;
            --shadow-sm: 0 1px 2px 0 rgba(0,0,0,0.3);
            --shadow: 0 4px 6px -1px rgba(0,0,0,0.4);
            --shadow-lg: 0 10px 15px -3px rgba(0,0,0,0.4);
            --code-bg: #3b4252;
            --code-text: #eceff4;
            --code-keyword: #81a1c1;
            --code-string: #a3be8c;
            --code-comment: #4c566a;
            --code-function: #88c0d0;
            --code-number: #b48ead;
            --hover-overlay: rgba(255,255,255,0.04);
            --overlay-bg: rgba(0,0,0,0.7);
            --copy-btn-bg: #434c5e;
            --copy-btn-hover: #4c566a;
            --footer-bg: #3b4252;
        }}

        :root[data-theme="solarized"] {{
            --primary: #268bd2;
            --primary-light: #6c71c4;
            --primary-dark: #2aa198;
            --secondary: #657b83;
            --success: #859900;
            --warning: #b58900;
            --error: #dc322f;
            --surface-0: #fdf6e3;
            --surface-1: #eee8d5;
            --surface-2: #93a1a1;
            --surface-3: #839496;
            --text-primary: #002b36;
            --text-secondary: #073642;
            --text-tertiary: #586e75;
            --border: #93a1a1;
            --border-dark: #839496;
            --shadow-sm: 0 1px 2px 0 rgba(0,0,0,0.1);
            --shadow: 0 4px 6px -1px rgba(0,0,0,0.1);
            --shadow-lg: 0 10px 15px -3px rgba(0,0,0,0.1);
            --code-bg: #eee8d5;
            --code-text: #002b36;
            --code-keyword: #268bd2;
            --code-string: #859900;
            --code-comment: #586e75;
            --code-function: #6c71c4;
            --code-number: #b58900;
            --hover-overlay: rgba(0,0,0,0.04);
            --overlay-bg: rgba(0,0,0,0.3);
            --copy-btn-bg: #93a1a1;
            --copy-btn-hover: #839496;
            --footer-bg: #eee8d5;
        }}

        body {{
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
            background: var(--surface-1);
            color: var(--text-primary);
            line-height: 1.6;
            font-size: 15px;
            height: 100vh;
            overflow: hidden;
        }}

        .material-symbols-outlined {{
            font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 20;
            font-size: 20px;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .app {{
            display: flex;
            height: 100vh;
            overflow: hidden;
            background: var(--surface-1);
            position: relative;
        }}

        .sidebar-overlay {{
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: var(--overlay-bg);
            z-index: 90;
            opacity: 0;
            visibility: hidden;
            transition: all var(--transition-speed) var(--transition-timing);
            backdrop-filter: blur(2px);
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
            transition: transform var(--transition-speed) var(--transition-timing);
            z-index: 100;
        }}

        .sidebar-header {{
            padding: 1.5rem 1.5rem 1rem;
            border-bottom: 1px solid var(--border);
        }}

        .project-header {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            margin-bottom: 0.5rem;
            flex-wrap: wrap;
        }}

        .project-name {{
            font-weight: 600;
            font-size: 1.25rem;
            color: var(--text-primary);
            word-break: break-word;
        }}

        .docs-badge {{
            display: inline-flex;
            align-items: center;
            gap: 0.25rem;
            padding: 0.25rem 0.75rem;
            background: var(--primary);
            color: white;
            font-size: 0.75rem;
            font-weight: 500;
            border-radius: 100px;
            letter-spacing: 0.025em;
            text-transform: uppercase;
            white-space: nowrap;
        }}

        .docs-badge .material-symbols-outlined {{
            font-size: 14px;
            color: white;
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
            margin-bottom: 1rem;
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
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .category-header:hover {{
            background: var(--hover-overlay);
            color: var(--text-primary);
        }}

        .category-header .material-symbols-outlined {{
            font-size: 16px;
            transition: transform var(--transition-speed) var(--transition-timing);
        }}

        .category-items {{
            margin-top: 0.25rem;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .category-items.collapsed {{
            display: none;
        }}

        .nav-item {{
            padding: 0.5rem 1.5rem 0.5rem 3.5rem;
            color: var(--text-secondary);
            font-size: 0.9375rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            position: relative;
            transition: all var(--transition-speed) var(--transition-timing);
            margin-right: 1rem;
            border-radius: 0 24px 24px 0;
            word-break: break-word;
        }}

        .nav-item:hover {{
            background: var(--hover-overlay);
            color: var(--text-primary);
            transform: translateX(4px);
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
            border-radius: 0 2px 2px 0;
            animation: slideIn 0.2s var(--transition-timing);
        }}

        @keyframes slideIn {{
            from {{
                transform: scaleY(0);
            }}
            to {{
                transform: scaleY(1);
            }}
        }}

        .nav-item .material-symbols-outlined {{
            font-size: 18px;
            color: var(--text-tertiary);
            transition: all var(--transition-speed) var(--transition-timing);
            flex-shrink: 0;
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
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .header-left {{
            display: flex;
            align-items: center;
            gap: 1rem;
            min-width: 0;
        }}

        .menu-button {{
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            width: 42px;
            height: 42px;
            border-radius: 21px;
            display: none;
            align-items: center;
            justify-content: center;
            transition: all var(--transition-speed) var(--transition-timing);
            flex-shrink: 0;
        }}

        .menu-button:hover {{
            background: var(--hover-overlay);
            color: var(--primary);
            transform: scale(1.05);
        }}

        .breadcrumb {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            color: var(--text-tertiary);
            font-size: 0.875rem;
            min-width: 0;
            flex-wrap: wrap;
        }}

        .breadcrumb a {{
            color: var(--text-secondary);
            text-decoration: none;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.25rem;
            padding: 0.5rem;
            border-radius: 6px;
            transition: all var(--transition-speed) var(--transition-timing);
            flex-shrink: 0;
        }}

        .breadcrumb a:hover {{
            background: var(--hover-overlay);
            color: var(--primary);
            transform: translateX(-2px);
        }}

        .breadcrumb span:last-child {{
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }}

        .header-right {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            flex-shrink: 0;
        }}

        .stats {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border-radius: 100px;
            font-size: 0.875rem;
            height: 42px;
            transition: all var(--transition-speed) var(--transition-timing);
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

        .theme-selector {{
            position: relative;
        }}

        .theme-button {{
            padding: 0.5rem 1.25rem;
            background: var(--surface-2);
            border: none;
            border-radius: 100px;
            color: var(--text-secondary);
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.875rem;
            height: 42px;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .theme-button:hover {{
            background: var(--surface-3);
            color: var(--text-primary);
            transform: scale(1.02);
        }}

        .theme-button .material-symbols-outlined {{
            font-size: 18px;
        }}

        .theme-dropdown {{
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.75rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 16px;
            box-shadow: var(--shadow-lg);
            display: none;
            z-index: 1000;
            min-width: 180px;
            overflow: hidden;
            animation: dropdownFade 0.2s var(--transition-timing);
        }}

        @keyframes dropdownFade {{
            from {{
                opacity: 0;
                transform: translateY(-10px);
            }}
            to {{
                opacity: 1;
                transform: translateY(0);
            }}
        }}

        .theme-dropdown.show {{
            display: block;
        }}

        .theme-option {{
            padding: 0.875rem 1.25rem;
            cursor: pointer;
            color: var(--text-secondary);
            font-size: 0.9375rem;
            display: flex;
            align-items: center;
            gap: 0.75rem;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .theme-option:hover {{
            background: var(--hover-overlay);
            color: var(--text-primary);
            padding-left: 1.75rem;
        }}

        .theme-option.active {{
            background: var(--surface-2);
            color: var(--primary);
        }}

        .theme-option .material-symbols-outlined {{
            font-size: 18px;
        }}

        .content {{
            flex: 1;
            overflow-y: auto;
            padding: 2rem;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .warning-banner {{
            background: var(--surface-0);
            border: 1px solid var(--warning);
            border-radius: 16px;
            padding: 1rem 1.5rem;
            margin-bottom: 2rem;
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 1rem;
            animation: slideDown 0.4s var(--transition-timing);
            box-shadow: var(--shadow);
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        @keyframes slideDown {{
            from {{
                opacity: 0;
                transform: translateY(-30px);
            }}
            to {{
                opacity: 1;
                transform: translateY(0);
            }}
        }}

        .warning-content {{
            display: flex;
            align-items: center;
            gap: 1rem;
            flex: 1;
        }}

        .warning-content .material-symbols-outlined {{
            color: var(--warning);
            font-size: 24px;
            animation: pulse 2s infinite;
        }}

        @keyframes pulse {{
            0%, 100% {{
                opacity: 1;
            }}
            50% {{
                opacity: 0.7;
            }}
        }}

        .warning-text strong {{
            color: var(--warning);
            font-weight: 600;
        }}

        .warning-text p {{
            margin: 0.25rem 0 0;
            font-size: 0.9375rem;
            color: var(--text-secondary);
        }}

        .warning-close {{
            width: 40px;
            height: 40px;
            background: none;
            border: none;
            color: var(--text-tertiary);
            cursor: pointer;
            border-radius: 20px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all var(--transition-speed) var(--transition-timing);
            flex-shrink: 0;
        }}

        .warning-close:hover {{
            background: var(--hover-overlay);
            color: var(--text-primary);
            transform: rotate(90deg);
        }}

        .search-container {{
            max-width: 600px;
            margin: 0 auto 2.5rem;
            position: relative;
        }}

        .search-icon {{
            position: absolute;
            left: 1.25rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
            pointer-events: none;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .search-input {{
            width: 100%;
            padding: 1rem 1.25rem 1rem 3.5rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 32px;
            font-family: 'Inter', sans-serif;
            font-size: 1rem;
            color: var(--text-primary);
            outline: none;
            transition: all var(--transition-speed) var(--transition-timing);
            box-shadow: var(--shadow-sm);
        }}

        .search-input:focus {{
            border-color: var(--primary);
            box-shadow: 0 0 0 4px rgba(37, 99, 235, 0.15);
            transform: scale(1.01);
        }}

        .search-clear {{
            position: absolute;
            right: 1rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-tertiary);
            cursor: pointer;
            display: none;
            width: 36px;
            height: 36px;
            border-radius: 18px;
            align-items: center;
            justify-content: center;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .search-clear:hover {{
            background: var(--hover-overlay);
            color: var(--text-primary);
            transform: translateY(-50%) scale(1.1);
        }}

        .search-clear.visible {{
            display: flex;
        }}

        .search-stats {{
            margin-top: 0.75rem;
            font-size: 0.875rem;
            color: var(--text-tertiary);
            text-align: center;
            animation: fadeIn 0.3s var(--transition-timing);
        }}

        @keyframes fadeIn {{
            from {{
                opacity: 0;
            }}
            to {{
                opacity: 1;
            }}
        }}

        .cards-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
            gap: 1.5rem;
            max-width: 1400px;
            margin: 0 auto;
            padding-bottom: 2rem;
        }}

        .card {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 20px;
            padding: 1.5rem;
            cursor: pointer;
            transition: all var(--transition-speed) var(--transition-timing);
            box-shadow: var(--shadow-sm);
            animation: cardFadeIn 0.4s var(--transition-timing);
        }}

        @keyframes cardFadeIn {{
            from {{
                opacity: 0;
                transform: translateY(20px);
            }}
            to {{
                opacity: 1;
                transform: translateY(0);
            }}
        }}

        .card:hover {{
            border-color: var(--primary);
            box-shadow: var(--shadow-lg);
            transform: translateY(-4px) scale(1.02);
        }}

        .card-icon {{
            width: 52px;
            height: 52px;
            background: var(--surface-2);
            border-radius: 14px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 1.25rem;
            color: var(--primary);
            transition: all var(--transition-speed) var(--transition-timing);
            flex-shrink: 0;
        }}

        .card:hover .card-icon {{
            background: var(--primary);
            color: white;
            transform: rotate(5deg) scale(1.1);
        }}

        .card-icon .material-symbols-outlined {{
            font-size: 26px;
        }}

        .card-header {{
            margin-bottom: 0.5rem;
        }}

        .card h3 {{
            font-weight: 600;
            font-size: 1.125rem;
            color: var(--text-primary);
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .card:hover h3 {{
            color: var(--primary);
        }}

        .card p {{
            color: var(--text-secondary);
            font-size: 0.9375rem;
            margin-bottom: 1.5rem;
            line-height: 1.5;
            word-break: break-word;
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
            padding: 0.375rem 0.875rem;
            background: var(--surface-2);
            border-radius: 100px;
            font-size: 0.8125rem;
            color: var(--text-tertiary);
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .card:hover .card-category {{
            background: var(--surface-3);
            color: var(--text-secondary);
        }}

        .card-arrow {{
            color: var(--text-tertiary);
            transition: all var(--transition-speed) var(--transition-timing);
            opacity: 0;
            transform: translateX(-10px);
            flex-shrink: 0;
        }}

        .card:hover .card-arrow {{
            opacity: 1;
            transform: translateX(0);
            color: var(--primary);
        }}

        .doc-view {{
            display: none;
            max-width: 100%;
            margin: 0;
            animation: fadeIn 0.4s var(--transition-timing);
        }}

        .doc-header {{
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 2rem;
            padding-bottom: 1rem;
            border-bottom: 1px solid var(--border);
        }}

        .doc-title-container {{
            flex: 1;
        }}

        .doc-title {{
            font-size: 2rem;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 0.25rem;
            word-break: break-word;
        }}

        .doc-meta {{
            display: flex;
            align-items: center;
            gap: 1rem;
            color: var(--text-tertiary);
            font-size: 0.875rem;
        }}

        .doc-meta-item {{
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }}

        .doc-actions {{
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }}

        .doc-action-button {{
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border: none;
            border-radius: 8px;
            color: var(--text-secondary);
            font-size: 0.875rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .doc-action-button:hover {{
            background: var(--surface-3);
            color: var(--text-primary);
            transform: translateY(-2px);
        }}

        .doc-action-button .material-symbols-outlined {{
            font-size: 18px;
        }}

        .doc-content {{
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 24px;
            padding: 3rem;
            box-shadow: var(--shadow);
            overflow-x: auto;
            margin-bottom: 2rem;
        }}

        .footer {{
            background: var(--footer-bg);
            border-top: 1px solid var(--border);
            padding: 1.5rem 2rem;
            margin-top: 2rem;
            text-align: center;
            color: var(--text-tertiary);
            font-size: 0.875rem;
            border-radius: 16px;
        }}

        .footer-content {{
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
            flex-wrap: wrap;
            gap: 1rem;
        }}

        .footer-copyright {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }}

        .footer-copyright .material-symbols-outlined {{
            font-size: 16px;
            color: var(--text-tertiary);
        }}

        .footer-info {{
            display: flex;
            align-items: center;
            gap: 1.5rem;
        }}

        .footer-info-item {{
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }}

        .markdown-body {{
            color: var(--text-primary);
            width: 100%;
            max-width: 100%;
            overflow-x: auto;
        }}

        .markdown-body h1 {{
            font-size: 2.5rem;
            font-weight: 600;
            margin: 0 0 1.5rem;
            letter-spacing: -0.02em;
            color: var(--text-primary);
            border-bottom: 2px solid var(--border);
            padding-bottom: 0.75rem;
            animation: slideInFromLeft 0.4s var(--transition-timing);
            word-break: break-word;
        }}

        @keyframes slideInFromLeft {{
            from {{
                opacity: 0;
                transform: translateX(-20px);
            }}
            to {{
                opacity: 1;
                transform: translateX(0);
            }}
        }}

        .markdown-body h2 {{
            font-size: 1.875rem;
            font-weight: 600;
            margin: 2.5rem 0 1rem;
            color: var(--text-primary);
            border-bottom: 1px solid var(--border);
            padding-bottom: 0.5rem;
            word-break: break-word;
        }}

        .markdown-body h3 {{
            font-size: 1.5rem;
            font-weight: 600;
            margin: 2rem 0 1rem;
            color: var(--text-primary);
            word-break: break-word;
        }}

        .markdown-body h4 {{
            font-size: 1.25rem;
            font-weight: 600;
            margin: 1.5rem 0 0.75rem;
            color: var(--text-primary);
            word-break: break-word;
        }}

        .markdown-body p {{
            margin: 1.25rem 0;
            color: var(--text-secondary);
            line-height: 1.7;
            font-size: 1rem;
            word-break: break-word;
        }}

        .markdown-body a {{
            color: var(--primary);
            text-decoration: none;
            border-bottom: 1px solid transparent;
            transition: border-color var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .markdown-body a:hover {{
            border-bottom-color: var(--primary);
        }}

        .markdown-body pre {{
            margin: 1.5rem 0;
            position: relative;
            border-radius: 12px;
            overflow: hidden;
            transition: all var(--transition-speed) var(--transition-timing);
        }}

        .markdown-body pre:hover {{
            box-shadow: var(--shadow-lg);
            transform: translateY(-2px);
        }}

        .markdown-body pre .copy-button {{
            position: absolute;
            top: 0.75rem;
            right: 0.75rem;
            padding: 0.5rem 1rem;
            background: var(--copy-btn-bg);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--text-secondary);
            font-size: 0.8125rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            opacity: 0;
            transform: translateY(-5px);
            transition: all var(--transition-speed) var(--transition-timing);
            z-index: 10;
            font-family: 'Inter', sans-serif;
            font-weight: 500;
            box-shadow: var(--shadow-sm);
        }}

        .markdown-body pre:hover .copy-button {{
            opacity: 1;
            transform: translateY(0);
        }}

        .markdown-body pre .copy-button:hover {{
            background: var(--copy-btn-hover);
            color: var(--text-primary);
            transform: scale(1.05);
            border-color: var(--primary);
        }}

        .markdown-body pre .copy-button.copied {{
            background: var(--success);
            color: white;
            border-color: var(--success);
        }}

        .markdown-body pre .copy-button .material-symbols-outlined {{
            font-size: 16px;
        }}

        .markdown-body code {{
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.875em;
            padding: 0.2em 0.4em;
            background: var(--code-bg);
            border-radius: 6px;
            color: var(--code-text);
            word-break: break-word;
        }}

        .markdown-body pre code {{
            padding: 1.5rem;
            background: var(--code-bg);
            color: var(--code-text);
            font-size: 0.875rem;
            line-height: 1.6;
            display: block;
            overflow-x: auto;
            white-space: pre;
            word-break: normal;
        }}

        .markdown-body blockquote {{
            margin: 1.5rem 0;
            padding: 1rem 1.5rem;
            border-left: 4px solid var(--primary);
            background: var(--surface-2);
            border-radius: 0 16px 16px 0;
            color: var(--text-secondary);
            font-style: italic;
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .markdown-body blockquote:hover {{
            border-left-width: 6px;
            transform: translateX(4px);
        }}

        .markdown-body table {{
            width: 100%;
            margin: 1.5rem 0;
            border-collapse: collapse;
            border-radius: 16px;
            overflow: hidden;
            box-shadow: var(--shadow);
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .markdown-body table:hover {{
            box-shadow: var(--shadow-lg);
            transform: translateY(-2px);
        }}

        .markdown-body th {{
            padding: 0.875rem 1.25rem;
            background: var(--surface-2);
            font-weight: 600;
            text-align: left;
            border: none;
            color: var(--text-primary);
            word-break: break-word;
        }}

        .markdown-body td {{
            padding: 0.875rem 1.25rem;
            border: none;
            border-top: 1px solid var(--border);
            color: var(--text-secondary);
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .markdown-body tr:hover td {{
            background: var(--hover-overlay);
            padding-left: 1.5rem;
        }}

        .markdown-body hr {{
            margin: 2.5rem 0;
            border: none;
            border-top: 2px solid var(--border);
        }}

        .markdown-body ul, .markdown-body ol {{
            margin: 1.25rem 0;
            padding-left: 2rem;
            color: var(--text-secondary);
            word-break: break-word;
        }}

        .markdown-body li {{
            margin: 0.5rem 0;
            transition: all var(--transition-speed) var(--transition-timing);
            word-break: break-word;
        }}

        .markdown-body li:hover {{
            transform: translateX(4px);
            color: var(--text-primary);
        }}

        .markdown-body li > ul, .markdown-body li > ol {{
            margin: 0.25rem 0 0.25rem 1.5rem;
        }}

        .markdown-body img {{
            max-width: 100%;
            border-radius: 12px;
            box-shadow: var(--shadow);
            transition: all var(--transition-speed) var(--transition-timing);
            height: auto;
        }}

        .markdown-body img:hover {{
            box-shadow: var(--shadow-lg);
            transform: scale(1.01);
        }}

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

        .empty-state {{
            text-align: center;
            padding: 4rem 2rem;
            background: var(--surface-0);
            border: 1px solid var(--border);
            border-radius: 24px;
            grid-column: 1 / -1;
            box-shadow: var(--shadow-sm);
            animation: fadeIn 0.4s var(--transition-timing);
            margin-bottom: 2rem;
        }}

        .empty-state .material-symbols-outlined {{
            font-size: 56px;
            color: var(--text-tertiary);
            margin-bottom: 1rem;
            animation: bounce 2s infinite;
        }}

        @keyframes bounce {{
            0%, 100% {{
                transform: translateY(0);
            }}
            50% {{
                transform: translateY(-10px);
            }}
        }}

        .empty-state h3 {{
            font-size: 1.5rem;
            font-weight: 600;
            margin-bottom: 0.5rem;
            color: var(--text-primary);
            word-break: break-word;
        }}

        .empty-state p {{
            color: var(--text-tertiary);
            font-size: 1rem;
            word-break: break-word;
        }}

        .codehilite {{
            background: transparent !important;
        }}
        
        {pygments_styles}

        @media (max-width: 768px) {{
            :root {{
                --header-height: 60px;
            }}
            
            .menu-button {{
                display: flex;
            }}
            
            .sidebar {{
                position: fixed;
                left: 0;
                top: 0;
                bottom: 0;
                transform: translateX(-100%);
                box-shadow: var(--shadow-lg);
                height: 100vh;
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
            
            .cards-grid {{
                grid-template-columns: 1fr;
            }}
            
            .doc-content {{
                padding: 1.5rem;
            }}
            
            .markdown-body h1 {{
                font-size: 2rem;
            }}
            
            .markdown-body h2 {{
                font-size: 1.5rem;
            }}
            
            .markdown-body h3 {{
                font-size: 1.25rem;
            }}
            
            .theme-button span:not(.material-symbols-outlined) {{
                display: none;
            }}
            
            .theme-button {{
                padding: 0.5rem;
                width: 42px;
                justify-content: center;
            }}
            
            .doc-header {{
                flex-direction: column;
                align-items: flex-start;
                gap: 1rem;
            }}
            
            .doc-actions {{
                width: 100%;
                justify-content: flex-start;
            }}
            
            .footer-content {{
                flex-direction: column;
                text-align: center;
            }}
            
            .footer-info {{
                flex-direction: column;
                gap: 0.75rem;
            }}
        }}
    </style>
</head>
<body>
    <div class="app">
        <div class="sidebar-overlay" id="sidebarOverlay" onclick="toggleSidebar()"></div>
        
        <div class="sidebar" id="sidebar">
            <div class="sidebar-header">
                <div class="project-header">
                    <span class="project-name">{project_name}</span>
                    <span class="docs-badge">
                        <span class="material-symbols-outlined">menu_book</span>
                        DOCS
                    </span>
                </div>
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
                    <button class="menu-button" onclick="toggleSidebar()" aria-label="Toggle sidebar">
                        <span class="material-symbols-outlined">menu</span>
                    </button>
                    <div class="breadcrumb" id="breadcrumb">
                        <a onclick="showHome()" aria-label="Go to home">
                            <span class="material-symbols-outlined">home</span>
                        </a>
                        <span class="material-symbols-outlined">chevron_right</span>
                        <span id="current-section">Home</span>
                    </div>
                </div>
                <div class="header-right">
                    <div class="stats" aria-label="Documentation statistics">
                        <div class="stat">
                            <span class="material-symbols-outlined" aria-hidden="true">category</span>
                            <span>{category_count}</span>
                        </div>
                        <div class="stat">
                            <span class="material-symbols-outlined" aria-hidden="true">description</span>
                            <span>{section_count}</span>
                        </div>
                    </div>
                    <div class="theme-selector">
                        <button class="theme-button" onclick="toggleThemeDropdown()" aria-label="Select theme" aria-haspopup="true" aria-expanded="false">
                            <span class="material-symbols-outlined" aria-hidden="true">palette</span>
                            <span id="current-theme-label">Light</span>
                        </button>
                        <div class="theme-dropdown" id="themeDropdown" role="menu" aria-label="Theme options">
                            <div class="theme-option active" onclick="setTheme('light', event)" role="menuitem">
                                <span class="material-symbols-outlined" aria-hidden="true">light_mode</span>
                                Light
                            </div>
                            <div class="theme-option" onclick="setTheme('dark', event)" role="menuitem">
                                <span class="material-symbols-outlined" aria-hidden="true">dark_mode</span>
                                Dark
                            </div>
                            <div class="theme-option" onclick="setTheme('sepia', event)" role="menuitem">
                                <span class="material-symbols-outlined" aria-hidden="true">book</span>
                                Sepia
                            </div>
                            <div class="theme-option" onclick="setTheme('nord', event)" role="menuitem">
                                <span class="material-symbols-outlined" aria-hidden="true">ac_unit</span>
                                Nord
                            </div>
                            <div class="theme-option" onclick="setTheme('solarized', event)" role="menuitem">
                                <span class="material-symbols-outlined" aria-hidden="true">wb_sunny</span>
                                Solarized
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <div class="content" id="content">
                <div id="home-view">
                    <div class="warning-banner" id="alphaWarning" role="alert">
                        <div class="warning-content">
                            <span class="material-symbols-outlined" aria-hidden="true">warning</span>
                            <div class="warning-text">
                                <strong>Alpha Software</strong>
                                <p>AromaUI is currently in alpha and unstable. Not recommended for critical applications.</p>
                            </div>
                        </div>
                        <button class="warning-close" onclick="dismissWarning()" aria-label="Dismiss warning">
                            <span class="material-symbols-outlined" aria-hidden="true">close</span>
                        </button>
                    </div>

                    <div class="search-container">
                        <span class="material-symbols-outlined search-icon" aria-hidden="true">search</span>
                        <input type="text" class="search-input" id="searchInput" placeholder="Search documentation... (Press / to focus)" aria-label="Search documentation">
                        <button class="search-clear" id="searchClear" onclick="clearSearch()" aria-label="Clear search">
                            <span class="material-symbols-outlined" aria-hidden="true">close</span>
                        </button>
                        <div class="search-stats" id="searchStats" aria-live="polite"></div>
                    </div>
                    
                    <div class="cards-grid" id="cardsGrid" role="grid" aria-label="Documentation sections">
                        {cards_html}
                    </div>

                    <div class="footer">
                        <div class="footer-content">
                            <div class="footer-copyright">
                                <span class="material-symbols-outlined">copyright</span>
                                <span>{year} {project_name}. All rights reserved.</span>
                            </div>
                            <div class="footer-info">
                                <div class="footer-info-item">
                                    <span class="material-symbols-outlined">description</span>
                                    <span>v{version}</span>
                                </div>
                                <div class="footer-info-item">
                                    <span class="material-symbols-outlined">update</span>
                                    <span>Last updated: {last_updated}</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>

                <div id="doc-view" class="doc-view">
                    <div class="doc-header">
                        <div class="doc-title-container">
                            <h1 class="doc-title" id="doc-title"></h1>
                            <div class="doc-meta" id="doc-meta">
                                <span class="doc-meta-item" id="doc-category">
                                    <span class="material-symbols-outlined">folder</span>
                                    <span></span>
                                </span>
                                <span class="doc-meta-item" id="doc-file">
                                    <span class="material-symbols-outlined">description</span>
                                    <span></span>
                                </span>
                            </div>
                        </div>
                        <div class="doc-actions">
                            <button class="doc-action-button" id="editGithubBtn" onclick="editOnGithub()">
                                <span class="material-symbols-outlined">edit</span>
                                Edit on GitHub
                            </button>
                            <button class="doc-action-button" onclick="copyPageLink()">
                                <span class="material-symbols-outlined">link</span>
                                Copy Link
                            </button>
                        </div>
                    </div>
                    <div class="doc-content markdown-body" id="doc-content"></div>
                    
                    <div class="footer">
                        <div class="footer-content">
                            <div class="footer-copyright">
                                <span class="material-symbols-outlined">copyright</span>
                                <span>{year} {project_name}. All rights reserved.</span>
                            </div>
                            <div class="footer-info">
                                <div class="footer-info-item">
                                    <span class="material-symbols-outlined">description</span>
                                    <span>v{version}</span>
                                </div>
                                <div class="footer-info-item">
                                    <span class="material-symbols-outlined">update</span>
                                    <span>Last updated: {last_updated}</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        // Global variables
        const pages = {pages_json};
        const titles = {titles_json};
        const markdownFiles = {markdown_files_json};

        let searchTimeout;
        let currentPageId = null;

        // Theme functions
        function toggleThemeDropdown() {{
            const dropdown = document.getElementById('themeDropdown');
            const button = document.querySelector('.theme-button');
            const isExpanded = button.getAttribute('aria-expanded') === 'true';
            
            dropdown.classList.toggle('show');
            button.setAttribute('aria-expanded', (!isExpanded).toString());
            
            if (dropdown.classList.contains('show')) {{
                document.addEventListener('click', closeThemeDropdown);
            }}
        }}

        function closeThemeDropdown(event) {{
            const dropdown = document.getElementById('themeDropdown');
            const button = document.querySelector('.theme-button');
            
            if (!button.contains(event.target) && !dropdown.contains(event.target)) {{
                dropdown.classList.remove('show');
                button.setAttribute('aria-expanded', 'false');
                document.removeEventListener('click', closeThemeDropdown);
            }}
        }}

        function setTheme(theme, event) {{
            document.documentElement.setAttribute('data-theme', theme);
            localStorage.setItem('theme', theme);
            
            const dropdown = document.getElementById('themeDropdown');
            const button = document.querySelector('.theme-button');
            dropdown.classList.remove('show');
            button.setAttribute('aria-expanded', 'false');
            
            const options = document.querySelectorAll('.theme-option');
            options.forEach(opt => opt.classList.remove('active'));
            
            if (event) {{
                event.target.closest('.theme-option').classList.add('active');
            }} else {{
                const activeOption = Array.from(options).find(opt => 
                    opt.textContent.toLowerCase().includes(theme)
                );
                if (activeOption) activeOption.classList.add('active');
            }}
            
            const themeNames = {{
                'light': 'Light',
                'dark': 'Dark',
                'sepia': 'Sepia',
                'nord': 'Nord',
                'solarized': 'Solarized'
            }};
            document.getElementById('current-theme-label').textContent = themeNames[theme];
        }}

        // Sidebar functions
        function toggleSidebar() {{
            const sidebar = document.getElementById('sidebar');
            const overlay = document.getElementById('sidebarOverlay');
            const isActive = sidebar.classList.contains('active');
            
            sidebar.classList.toggle('active');
            overlay.classList.toggle('active');
            
            document.body.style.overflow = isActive ? '' : 'hidden';
        }}

        function toggleCategory(id) {{
            const items = document.getElementById('category-' + id);
            if (!items) return;
            
            const header = items.previousElementSibling;
            const icon = header.querySelector('.material-symbols-outlined');
            
            items.classList.toggle('collapsed');
            
            if (items.classList.contains('collapsed')) {{
                icon.style.transform = 'rotate(0deg)';
                icon.textContent = 'chevron_right';
            }} else {{
                icon.style.transform = 'rotate(90deg)';
                icon.textContent = 'expand_more';
            }}
        }}

        // Copy button function
        function initializeCopyButtons() {{
            document.querySelectorAll('.markdown-body pre').forEach(pre => {{
                if (!pre.querySelector('.copy-button')) {{
                    const button = document.createElement('button');
                    button.className = 'copy-button';
                    button.innerHTML = '<span class="material-symbols-outlined">content_copy</span><span>Copy</span>';
                    button.setAttribute('aria-label', 'Copy code to clipboard');
                    
                    button.addEventListener('click', async (e) => {{
                        e.stopPropagation();
                        const code = pre.querySelector('code');
                        if (code) {{
                            try {{
                                await navigator.clipboard.writeText(code.textContent || '');
                                button.classList.add('copied');
                                button.innerHTML = '<span class="material-symbols-outlined">check</span><span>Copied!</span>';
                                
                                setTimeout(() => {{
                                    button.classList.remove('copied');
                                    button.innerHTML = '<span class="material-symbols-outlined">content_copy</span><span>Copy</span>';
                                }}, 2000);
                            }} catch (err) {{
                                console.error('Failed to copy:', err);
                                button.innerHTML = '<span class="material-symbols-outlined">error</span><span>Failed</span>';
                                
                                setTimeout(() => {{
                                    button.innerHTML = '<span class="material-symbols-outlined">content_copy</span><span>Copy</span>';
                                }}, 2000);
                            }}
                        }}
                    }});
                    
                    pre.style.position = 'relative';
                    pre.appendChild(button);
                }}
            }});
        }}

        // Search functions
        const searchInput = document.getElementById('searchInput');
        const searchClear = document.getElementById('searchClear');
        const searchStats = document.getElementById('searchStats');
        const cardsGrid = document.getElementById('cardsGrid');

        function filterCards() {{
            const term = searchInput.value.toLowerCase().trim();
            
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
                
                const title = card.querySelector('h3')?.textContent.toLowerCase() || '';
                const desc = card.querySelector('p')?.textContent.toLowerCase() || '';
                const category = card.querySelector('.card-category')?.textContent.toLowerCase() || '';
                
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
                        <span class="material-symbols-outlined" aria-hidden="true">search_off</span>
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

        function dismissWarning() {{
            const warning = document.getElementById('alphaWarning');
            warning.style.display = 'none';
            localStorage.setItem('alphaWarningDismissed', 'true');
        }}

        function showHome() {{
            document.getElementById('home-view').style.display = 'block';
            document.getElementById('doc-view').style.display = 'none';
            document.getElementById('current-section').textContent = 'Home';
            
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            const homeItem = document.querySelector('[onclick="showHome()"]');
            if (homeItem) homeItem.classList.add('active');
            
            if (window.innerWidth <= 768) {{
                toggleSidebar();
            }}
            
            window.location.hash = '';
            currentPageId = null;
            clearSearch();
            
            if (localStorage.getItem('alphaWarningDismissed') === 'true') {{
                document.getElementById('alphaWarning').style.display = 'none';
            }}
        }}

        function showPage(id) {{
            if (!pages[id]) return;
            
            document.getElementById('home-view').style.display = 'none';
            document.getElementById('doc-view').style.display = 'block';
            document.getElementById('doc-content').innerHTML = pages[id];
            document.getElementById('doc-title').textContent = titles[id];
            document.getElementById('current-section').textContent = titles[id];
            
            // Update meta info
            const categorySpan = document.querySelector('#doc-category span:last-child');
            const fileSpan = document.querySelector('#doc-file span:last-child');
            
            // Find category from nav
            const activeNav = document.querySelector(`[onclick="showPage('${{id}}')"]`);
            if (activeNav) {{
                const categoryHeader = activeNav.closest('.nav-category')?.querySelector('.category-header span:last-child');
                if (categoryHeader && categorySpan) {{
                    categorySpan.textContent = categoryHeader.textContent;
                }}
            }}
            
            // Set filename
            if (markdownFiles[id] && fileSpan) {{
                fileSpan.textContent = markdownFiles[id].split('/').pop() || 'unknown.md';
            }}
            
            document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
            if (activeNav) activeNav.classList.add('active');
            
            if (window.innerWidth <= 768) {{
                toggleSidebar();
            }}
            
            document.getElementById('content').scrollTop = 0;
            window.location.hash = id;
            currentPageId = id;
            
            initializeCopyButtons();
        }}

        const github_url = 'https://github.com/BinaryInkTN/AromaUI/blob/main';

        function editOnGithub() {{
            const id = window.location.hash.substring(1);
            if (id && markdownFiles[id]) {{
                window.open(`${{github_url}}/docs/${{markdownFiles[id].split('/').pop()}}`, '_blank');
            }}
        }}

        function copyPageLink() {{
            navigator.clipboard.writeText(window.location.href);
            alert('Link copied to clipboard!');
        }}

        function handleKeyDown(e) {{
            if (e.key === '/' && !e.ctrlKey && !e.metaKey && document.activeElement?.tagName !== 'INPUT') {{
                e.preventDefault();
                searchInput.focus();
            }}
            
            if (e.key === 'Escape' && document.activeElement === searchInput) {{
                searchInput.blur();
            }}
            
            if ((e.ctrlKey || e.metaKey) && e.key === 'k') {{
                e.preventDefault();
                searchInput.focus();
            }}
        }}

        function handleResize() {{
            if (window.innerWidth > 768) {{
                const sidebar = document.getElementById('sidebar');
                const overlay = document.getElementById('sidebarOverlay');
                sidebar.classList.remove('active');
                overlay.classList.remove('active');
                document.body.style.overflow = '';
            }}
        }}

        function handleHashChange() {{
            if (window.location.hash) {{
                const id = window.location.hash.substring(1);
                if (pages[id]) {{
                    showPage(id);
                }} else {{
                    showHome();
                }}
            }} else {{
                showHome();
            }}
        }}

        // Initialize everything when the page loads
        document.addEventListener('DOMContentLoaded', function() {{
            // Set up event listeners
            document.addEventListener('keydown', handleKeyDown);
            window.addEventListener('resize', handleResize);
            window.addEventListener('hashchange', handleHashChange);
            
            searchInput.addEventListener('input', () => {{
                clearTimeout(searchTimeout);
                searchTimeout = setTimeout(filterCards, 300);
            }});

            // Initialize theme
            const savedTheme = localStorage.getItem('theme') || 'light';
            setTheme(savedTheme);
            
            // Handle warning banner
            if (localStorage.getItem('alphaWarningDismissed') === 'true') {{
                document.getElementById('alphaWarning').style.display = 'none';
            }}
            
            // Handle initial hash
            handleHashChange();
            
            // Initialize copy buttons if needed
            initializeCopyButtons();
        }});
    </script>
</body>
</html>'''
        return template
    
    def _get_icon_name(self, icon_name: str) -> str:
        return icon_name.lower()
    
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
        markdown_files = {}
        
        for section in sections:
            category = section.get('category', 'General')
            if category not in sidebar_sections:
                sidebar_sections[category] = []
            sidebar_sections[category].append(section)
            
            # Store markdown file path for edit on GitHub
            markdown_file = section.get('file', '')
            if markdown_file and not os.path.isabs(markdown_file):
                markdown_file = os.path.join(base_dir, markdown_file)
            title = section.get('title', 'Untitled')
            section_id = title.lower().replace(' ', '-')
            markdown_files[section_id] = markdown_file
        
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
                <div class="card" onclick="showPage('{section_id}')" role="gridcell" tabindex="0" onkeydown="if(event.key==='Enter'||event.key===' '){{showPage('{section_id}')}}">
                    <div class="card-icon" aria-hidden="true">
                        <span class="material-symbols-outlined">{icon}</span>
                    </div>
                    <div class="card-header">
                        <h3>{title}</h3>
                    </div>
                    <p>{description}</p>
                    <div class="card-footer">
                        <span class="card-category">
                            <span class="material-symbols-outlined" aria-hidden="true">folder</span>
                            {category}
                        </span>
                        <span class="material-symbols-outlined card-arrow" aria-hidden="true">arrow_forward</span>
                    </div>
                </div>
            ''')
        
        pygments_styles = self._get_pygments_styles()
        current_year = datetime.now().year
        last_updated = datetime.now().strftime('%B %d, %Y')
        
        html = self.template.format(
            project_name=project_name,
            version=project_version,
            category_count=category_count,
            section_count=len(sections),
            pygments_styles=pygments_styles,
            sidebar_content='\n'.join(sidebar_content),
            cards_html='\n'.join(cards_html),
            pages_json=json.dumps(pages_dict),
            titles_json=json.dumps(titles_dict),
            markdown_files_json=json.dumps(markdown_files),
            year=current_year,
            last_updated=last_updated
        )
        
        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html)
        
        print(f'Documentation generated: {output_file}')

def main():
    parser = argparse.ArgumentParser(description='Generate beautiful documentation from markdown files')
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