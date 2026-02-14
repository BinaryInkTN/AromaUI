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

class DocGenerator:
    def __init__(self):
        self.template = self._get_template()
    
    def _get_template(self) -> str:
        # Using triple quotes with escaped curly braces for the template
        template = '''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{project_name} • Documentation</title>
    
    <!-- Fonts & Icons -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:ital,opsz,wght@0,14..32,100..900;1,14..32,100..900&family=JetBrains+Mono:ital,wght@0,100..800;1,100..800&display=swap" rel="stylesheet">
    <link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0,1" rel="stylesheet" />
    
    <style>
        /* ===== RESET & VARIABLES ===== */
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        :root {{
            /* Light Theme (Default) */
            --md-primary: #006A6A;
            --md-primary-light: #489E9E;
            --md-primary-dark: #004040;
            --md-secondary: #625B71;
            --md-secondary-light: #918999;
            --md-secondary-dark: #444056;
            --md-tertiary: #7E5260;
            --md-tertiary-light: #B08796;
            --md-tertiary-dark: #4F2D3B;
            --md-error: #BA1A1A;
            --md-error-light: #FFB4AB;
            --md-error-dark: #93000A;
            --md-background: #F8FDF8;
            --md-surface: #FFFFFF;
            --md-surface-variant: #DAE5E5;
            --md-outline: #6F7979;
            --md-outline-variant: #BEC9C9;
            --md-shadow: rgba(0,0,0,0.08);
            
            /* Surface Colors */
            --surface-1: #FFFFFF;
            --surface-2: #F5FBFB;
            --surface-3: #ECF2F2;
            --surface-4: #E2E8E8;
            --surface-5: #D9DFDF;
            
            /* Text Colors */
            --text-primary: #161D1D;
            --text-secondary: #494F4F;
            --text-disabled: #6C7373;
            --text-inverse: #FFFFFF;
            
            /* Component Variables */
            --sidebar-width: 320px;
            --header-height: 72px;
            --border-radius-sm: 12px;
            --border-radius-md: 20px;
            --border-radius-lg: 28px;
            --border-radius-xl: 36px;
            
            /* Shadows */
            --shadow-sm: 0 2px 8px var(--md-shadow);
            --shadow-md: 0 4px 20px var(--md-shadow);
            --shadow-lg: 0 8px 30px var(--md-shadow);
            --shadow-xl: 0 12px 40px var(--md-shadow);
            
            /* Animations */
            --transition-fast: 0.2s cubic-bezier(0.2, 0, 0, 1);
            --transition-base: 0.3s cubic-bezier(0.2, 0, 0, 1);
            --transition-slow: 0.5s cubic-bezier(0.2, 0, 0, 1);
        }}

        /* Dark Theme */
        [data-theme="dark"] {{
            --md-primary: #9BCFCF;
            --md-primary-light: #B7EBEB;
            --md-primary-dark: #7EB3B3;
            --md-secondary: #CBC2DB;
            --md-secondary-light: #E7DEF7;
            --md-secondary-dark: #B0A7BF;
            --md-tertiary: #EFB8C8;
            --md-tertiary-light: #FFD9E4;
            --md-tertiary-dark: #D49AAE;
            --md-error: #FFB4AB;
            --md-error-light: #FFDAD6;
            --md-error-dark: #93000A;
            --md-background: #1A1C1C;
            --md-surface: #252828;
            --md-surface-variant: #404A4A;
            --md-outline: #909A9A;
            --md-outline-variant: #404A4A;
            --md-shadow: rgba(0,0,0,0.4);
            
            --surface-1: #252828;
            --surface-2: #2D3131;
            --surface-3: #353939;
            --surface-4: #3D4141;
            --surface-5: #454A4A;
            
            --text-primary: #E1E3E3;
            --text-secondary: #C1C6C6;
            --text-disabled: #9CA4A4;
            --text-inverse: #1A1C1C;
        }}

        body {{
            font-family: 'Inter', sans-serif;
            background-color: var(--md-background);
            color: var(--text-primary);
            line-height: 1.6;
            font-size: 16px;
            display: flex;
            flex-direction: column;
            height: 100vh;
            overflow: hidden;
            transition: background-color var(--transition-base), color var(--transition-base);
        }}

        /* Material Symbols */
        .material-symbols-outlined {{
            font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24;
            font-size: 24px;
            vertical-align: middle;
        }}
        
        .material-symbols-outlined.fill {{
            font-variation-settings: 'FILL' 1, 'wght' 400, 'GRAD' 0, 'opsz' 24;
        }}

        /* ===== TOP BAR ===== */
        .top-bar {{
            background-color: var(--surface-1);
            border-bottom: 1px solid var(--md-outline-variant);
            padding: 0 2rem;
            height: var(--header-height);
            display: flex;
            align-items: center;
            justify-content: space-between;
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            z-index: 100;
            backdrop-filter: blur(10px);
            -webkit-backdrop-filter: blur(10px);
            background-color: rgba(var(--surface-1-rgb), 0.8);
            transition: background-color var(--transition-base), border-color var(--transition-base);
        }}

        .logo-area {{
            display: flex;
            align-items: center;
            gap: 1.2rem;
        }}

        .logo {{
            width: 48px;
            height: 48px;
            background: linear-gradient(135deg, var(--md-primary), var(--md-primary-dark));
            border-radius: 16px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            box-shadow: var(--shadow-md);
            transition: transform var(--transition-fast), box-shadow var(--transition-base);
        }}

        .logo:hover {{
            transform: scale(1.05) rotate(5deg);
            box-shadow: var(--shadow-lg);
        }}

        .logo .material-symbols-outlined {{
            font-size: 28px;
            color: white;
        }}

        .project-info {{
            display: flex;
            flex-direction: column;
        }}

        .project-name {{
            font-weight: 700;
            color: var(--text-primary);
            font-size: 1.3rem;
            letter-spacing: -0.02em;
        }}

        .project-version {{
            font-size: 0.8rem;
            color: var(--text-secondary);
        }}

        .top-bar-right {{
            display: flex;
            align-items: center;
            gap: 2rem;
        }}

        .stats {{
            display: flex;
            gap: 1.5rem;
        }}

        .stat-item {{
            display: flex;
            align-items: center;
            gap: 0.8rem;
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border-radius: 40px;
            border: 1px solid var(--md-outline-variant);
            transition: background-color var(--transition-base), border-color var(--transition-base);
        }}

        .stat-item .material-symbols-outlined {{
            font-size: 20px;
            color: var(--md-primary);
        }}

        .stat-label {{
            font-size: 0.9rem;
            font-weight: 500;
            color: var(--text-secondary);
        }}

        .stat-value {{
            font-weight: 600;
            color: var(--text-primary);
        }}

        .theme-toggle {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.5rem;
            background: var(--surface-2);
            border-radius: 40px;
            border: 1px solid var(--md-outline-variant);
            cursor: pointer;
            transition: all var(--transition-fast);
        }}

        .theme-toggle:hover {{
            background: var(--surface-3);
            transform: scale(1.05);
        }}

        .theme-toggle .material-symbols-outlined {{
            font-size: 20px;
            color: var(--md-primary);
        }}

        .date-badge {{
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.5rem 1rem;
            background: var(--surface-2);
            border-radius: 40px;
            color: var(--text-secondary);
            font-size: 0.9rem;
            border: 1px solid var(--md-outline-variant);
            transition: background-color var(--transition-base), border-color var(--transition-base);
        }}

        .date-badge .material-symbols-outlined {{
            font-size: 20px;
            color: var(--md-primary);
        }}

        /* ===== MAIN WRAPPER ===== */
        .doc-wrapper {{
            display: flex;
            margin-top: var(--header-height);
            height: calc(100vh - var(--header-height));
            overflow: hidden;
        }}

        /* ===== SIDEBAR ===== */
        .sidebar {{
            width: var(--sidebar-width);
            background-color: var(--surface-1);
            border-right: 1px solid var(--md-outline-variant);
            display: flex;
            flex-direction: column;
            overflow-y: auto;
            flex-shrink: 0;
            padding: 2rem 0;
            transition: background-color var(--transition-base), border-color var(--transition-base);
        }}

        .sidebar::-webkit-scrollbar {{
            width: 8px;
        }}

        .sidebar::-webkit-scrollbar-track {{
            background: var(--surface-2);
        }}

        .sidebar::-webkit-scrollbar-thumb {{
            background-color: var(--md-outline-variant);
            border-radius: 8px;
            border: 2px solid var(--surface-1);
        }}

        .sidebar-section {{
            margin-bottom: 2rem;
        }}

        .category-header {{
            padding: 0.8rem 1.5rem;
            margin-bottom: 0.3rem;
            display: flex;
            align-items: center;
            gap: 0.8rem;
            cursor: pointer;
            user-select: none;
            border-radius: 0 40px 40px 0;
            transition: background-color var(--transition-fast);
        }}

        .category-header:hover {{
            background-color: var(--surface-2);
        }}

        .category-header .material-symbols-outlined {{
            font-size: 20px;
            color: var(--md-primary);
            transition: transform var(--transition-fast);
        }}

        .category-header.collapsed .material-symbols-outlined:first-child {{
            transform: rotate(-90deg);
        }}

        .category-name {{
            font-weight: 600;
            color: var(--text-primary);
            font-size: 0.9rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            flex: 1;
        }}

        .category-count {{
            background: var(--surface-3);
            padding: 0.2rem 0.6rem;
            border-radius: 20px;
            font-size: 0.8rem;
            color: var(--text-secondary);
        }}

        .category-items {{
            transition: all var(--transition-base);
            overflow: hidden;
        }}

        .category-items.collapsed {{
            display: none;
        }}

        .sidebar-item {{
            padding: 0.7rem 1.5rem 0.7rem 4rem;
            font-size: 0.95rem;
            color: var(--text-secondary);
            border-left: 4px solid transparent;
            transition: all var(--transition-fast);
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 12px;
            position: relative;
        }}

        .sidebar-item .material-symbols-outlined {{
            font-size: 20px;
            color: var(--text-disabled);
            transition: all var(--transition-fast);
        }}

        .sidebar-item:hover {{
            background-color: var(--surface-2);
            border-left-color: var(--md-outline);
        }}

        .sidebar-item:hover .material-symbols-outlined {{
            color: var(--md-primary);
        }}

        .sidebar-item.active {{
            background-color: var(--md-surface-variant);
            border-left-color: var(--md-primary);
            color: var(--md-primary-dark);
            font-weight: 500;
        }}

        .sidebar-item.active .material-symbols-outlined {{
            color: var(--md-primary);
            font-variation-settings: 'FILL' 1;
        }}

        /* ===== MAIN CONTENT ===== */
        .main-content {{
            flex: 1;
            overflow-y: auto;
            padding: 2rem;
            background: var(--md-background);
            transition: background-color var(--transition-base);
        }}

        .main-content::-webkit-scrollbar {{
            width: 10px;
        }}

        .main-content::-webkit-scrollbar-track {{
            background: var(--surface-2);
        }}

        .main-content::-webkit-scrollbar-thumb {{
            background-color: var(--md-outline-variant);
            border-radius: 10px;
            border: 2px solid var(--surface-1);
        }}

        /* ===== SEARCH BAR ===== */
        .search-container {{
            margin: 1rem 0 2rem;
            position: relative;
        }}

        .search-bar {{
            width: 100%;
            padding: 1.2rem 1.2rem 1.2rem 4rem;
            background: var(--surface-1);
            border: 2px solid var(--md-outline-variant);
            border-radius: 60px;
            font-size: 1.1rem;
            color: var(--text-primary);
            transition: all var(--transition-base);
            outline: none;
        }}

        .search-bar:focus {{
            border-color: var(--md-primary);
            box-shadow: 0 0 0 4px var(--md-primary-light);
            background: var(--surface-2);
        }}

        .search-bar::placeholder {{
            color: var(--text-disabled);
        }}

        .search-icon {{
            position: absolute;
            left: 1.5rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-disabled);
            pointer-events: none;
        }}

        .search-clear {{
            position: absolute;
            right: 1.5rem;
            top: 50%;
            transform: translateY(-50%);
            color: var(--text-disabled);
            cursor: pointer;
            display: none;
        }}

        .search-clear.visible {{
            display: block;
        }}

        .search-clear:hover {{
            color: var(--md-primary);
        }}

        .search-stats {{
            margin-top: 0.8rem;
            font-size: 0.9rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }}

        /* ===== HOME VIEW ===== */
        .home-view {{
            max-width: 1400px;
            margin: 0 auto;
        }}

        .hero {{
            text-align: center;
            margin: 1rem 0 2rem;
            padding: 0 2rem;
        }}

        .hero h1 {{
            font-weight: 800;
            font-size: 3.5rem;
            background: linear-gradient(135deg, var(--md-primary-dark) 0%, var(--md-primary) 50%, var(--md-tertiary) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            margin-bottom: 1rem;
            letter-spacing: -0.03em;
            line-height: 1.2;
        }}

        .hero .lead {{
            font-size: 1.3rem;
            color: var(--text-secondary);
            max-width: 800px;
            margin: 0 auto;
            line-height: 1.6;
        }}

        /* ===== CARDS GRID ===== */
        .cards-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
            gap: 2rem;
            margin: 2rem 0;
        }}

        .doc-card {{
            background: var(--surface-1);
            border: 1px solid var(--md-outline-variant);
            border-radius: var(--border-radius-lg);
            padding: 2rem;
            transition: all var(--transition-base);
            cursor: pointer;
            position: relative;
            overflow: hidden;
            display: flex;
            flex-direction: column;
        }}

        .doc-card:hover {{
            transform: translateY(-6px);
            box-shadow: var(--shadow-xl);
            border-color: transparent;
        }}

        .doc-card::before {{
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            height: 4px;
            background: linear-gradient(90deg, var(--md-primary), var(--md-tertiary));
            opacity: 0;
            transition: opacity var(--transition-base);
        }}

        .doc-card:hover::before {{
            opacity: 1;
        }}

        .card-icon {{
            width: 64px;
            height: 64px;
            background: linear-gradient(135deg, var(--md-primary-light), var(--md-primary));
            border-radius: 20px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 1.5rem;
            box-shadow: var(--shadow-md);
        }}

        .card-icon .material-symbols-outlined {{
            font-size: 36px;
            color: white;
        }}

        .doc-card h3 {{
            font-weight: 700;
            font-size: 1.5rem;
            margin-bottom: 0.8rem;
            color: var(--text-primary);
            letter-spacing: -0.02em;
        }}

        .doc-card p {{
            color: var(--text-secondary);
            margin-bottom: 1.5rem;
            line-height: 1.6;
            flex: 1;
        }}

        .card-footer {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: auto;
        }}

        .card-tag {{
            background: var(--surface-2);
            padding: 0.4rem 1.2rem;
            border-radius: 40px;
            font-size: 0.85rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
            border: 1px solid var(--md-outline-variant);
        }}

        .card-tag .material-symbols-outlined {{
            font-size: 18px;
        }}

        .card-arrow {{
            color: var(--md-primary);
            transition: transform var(--transition-fast);
        }}

        .doc-card:hover .card-arrow {{
            transform: translateX(8px);
        }}

        /* Empty State */
        .empty-state {{
            text-align: center;
            padding: 4rem 2rem;
            background: var(--surface-1);
            border-radius: var(--border-radius-lg);
            border: 2px dashed var(--md-outline-variant);
        }}

        .empty-state .material-symbols-outlined {{
            font-size: 64px;
            color: var(--text-disabled);
            margin-bottom: 1rem;
        }}

        .empty-state h3 {{
            font-size: 1.5rem;
            color: var(--text-primary);
            margin-bottom: 0.5rem;
        }}

        .empty-state p {{
            color: var(--text-secondary);
        }}

        /* ===== DOC VIEW ===== */
        .doc-view {{
            display: none;
            max-width: 1000px;
            margin: 0 auto;
        }}

        .breadcrumb {{
            display: flex;
            align-items: center;
            gap: 0.8rem;
            margin-bottom: 1.5rem;
            padding: 1rem 1.5rem;
            background: var(--surface-1);
            border-radius: var(--border-radius-md);
            box-shadow: var(--shadow-sm);
            color: var(--text-secondary);
            font-size: 0.95rem;
            border: 1px solid var(--md-outline-variant);
            transition: background-color var(--transition-base), border-color var(--transition-base);
        }}

        .breadcrumb a {{
            color: var(--md-primary);
            text-decoration: none;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
            transition: all var(--transition-fast);
        }}

        .breadcrumb a:hover {{
            color: var(--md-primary-dark);
            transform: translateX(-2px);
        }}

        .breadcrumb .material-symbols-outlined {{
            font-size: 20px;
        }}

        .doc-content {{
            background: var(--surface-1);
            border-radius: var(--border-radius-lg);
            padding: 3rem;
            box-shadow: var(--shadow-lg);
            border: 1px solid var(--md-outline-variant);
            transition: background-color var(--transition-base), border-color var(--transition-base), box-shadow var(--transition-base);
        }}

        /* ===== MARKDOWN STYLING ===== */
        .markdown-content {{
            color: var(--text-primary);
            line-height: 1.8;
        }}

        .markdown-content h1 {{
            font-size: 2.5rem;
            font-weight: 700;
            margin: 2.5rem 0 1.5rem;
            color: var(--text-primary);
            letter-spacing: -0.02em;
        }}

        .markdown-content h1:first-child {{
            margin-top: 0;
        }}

        .markdown-content h2 {{
            font-size: 2rem;
            font-weight: 600;
            margin: 2rem 0 1rem;
            color: var(--text-primary);
            padding-bottom: 0.5rem;
            border-bottom: 2px solid var(--md-outline-variant);
        }}

        .markdown-content h3 {{
            font-size: 1.5rem;
            font-weight: 600;
            margin: 1.5rem 0 1rem;
            color: var(--text-primary);
        }}

        .markdown-content p {{
            margin: 1.2rem 0;
            color: var(--text-secondary);
        }}

        .markdown-content a {{
            color: var(--md-primary);
            text-decoration: none;
            border-bottom: 2px solid var(--md-primary-light);
            transition: all var(--transition-fast);
        }}

        .markdown-content a:hover {{
            border-bottom-color: var(--md-primary);
        }}

        .markdown-content code {{
            background: var(--surface-2);
            color: var(--md-tertiary-dark);
            padding: 0.2rem 0.4rem;
            border-radius: var(--border-radius-sm);
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.9em;
            border: 1px solid var(--md-outline-variant);
        }}

        .markdown-content pre {{
            background: var(--surface-2);
            color: var(--text-primary);
            padding: 1.5rem;
            border-radius: var(--border-radius-md);
            overflow-x: auto;
            margin: 1.5rem 0;
            border: 1px solid var(--md-outline-variant);
        }}

        .markdown-content pre code {{
            background: none;
            color: inherit;
            padding: 0;
            border: none;
            font-size: 0.9rem;
            line-height: 1.6;
        }}

        .markdown-content blockquote {{
            border-left: 4px solid var(--md-primary);
            padding: 1rem 1.5rem;
            margin: 1.5rem 0;
            background: var(--surface-2);
            border-radius: 0 var(--border-radius-md) var(--border-radius-md) 0;
            color: var(--text-secondary);
        }}

        .markdown-content table {{
            width: 100%;
            border-collapse: collapse;
            margin: 1.5rem 0;
            border-radius: var(--border-radius-md);
            overflow: hidden;
            box-shadow: var(--shadow-sm);
        }}

        .markdown-content th {{
            background: var(--surface-2);
            color: var(--text-primary);
            font-weight: 600;
            padding: 1rem;
            text-align: left;
            border: 1px solid var(--md-outline-variant);
        }}

        .markdown-content td {{
            padding: 1rem;
            border: 1px solid var(--md-outline-variant);
            color: var(--text-secondary);
        }}

        /* ===== FOOTER ===== */
        footer {{
            margin-top: 3rem;
            padding: 2rem 0;
            text-align: center;
            border-top: 1px solid var(--md-outline-variant);
            color: var(--text-secondary);
            font-size: 0.9rem;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
        }}

        footer .material-symbols-outlined {{
            font-size: 18px;
            color: var(--md-primary);
        }}

        /* ===== RESPONSIVE ===== */
        @media (max-width: 1024px) {{
            .sidebar {{
                width: 280px;
            }}
            
            .stats {{
                display: none;
            }}
        }}

        @media (max-width: 768px) {{
            .top-bar {{
                padding: 0 1rem;
            }}
            
            .date-badge {{
                display: none;
            }}
            
            .sidebar {{
                position: fixed;
                left: -280px;
                transition: left var(--transition-base);
                z-index: 90;
                height: calc(100vh - var(--header-height));
                box-shadow: var(--shadow-xl);
            }}
            
            .sidebar.active {{
                left: 0;
            }}
            
            .main-content {{
                padding: 1rem;
            }}
            
            .hero h1 {{
                font-size: 2.2rem;
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
    <!-- Top Bar -->
    <div class="top-bar">
        <div class="logo-area">
         
            <div class="project-info">
                <span class="project-name">{project_name}</span>
                <span class="project-version">v{version}</span>
            </div>
        </div>
        
        <div class="top-bar-right">
            <div class="stats">
                <div class="stat-item">
                    <span class="material-symbols-outlined">category</span>
                    <span class="stat-label">Categories</span>
                    <span class="stat-value">{category_count}</span>
                </div>
                <div class="stat-item">
                    <span class="material-symbols-outlined">description</span>
                    <span class="stat-label">Sections</span>
                    <span class="stat-value">{section_count}</span>
                </div>
            </div>
            
            <div class="theme-toggle" onclick="toggleTheme()" id="themeToggle">
                <span class="material-symbols-outlined">light_mode</span>
            </div>
            
            <div class="date-badge">
                <span class="material-symbols-outlined">calendar_today</span>
                {generation_date}
            </div>
            
            <span class="material-symbols-outlined menu-icon" onclick="toggleSidebar()" style="display: none;">menu</span>
        </div>
    </div>

    <!-- Main Wrapper -->
    <div class="doc-wrapper">
        <!-- Sidebar -->
        <div class="sidebar" id="sidebar">
            <div class="sidebar-section">
                <div class="category-header" onclick="toggleCategory('home')">
                    <span class="material-symbols-outlined">chevron_right</span>
                    <span class="material-symbols-outlined">home</span>
                    <span class="category-name">Overview</span>
                </div>
                <div class="category-items" id="category-home">
                    <div class="sidebar-item active" onclick="showHome()" id="nav-home">
                        <span class="material-symbols-outlined">home</span>
                        Home
                    </div>
                </div>
            </div>
            
            {sidebar_content}
        </div>

        <!-- Main Content -->
        <div class="main-content" id="main-content">
            <!-- Home View -->
            <div id="home-view" class="home-view">
                <div class="hero">
                    <h1>{project_name}</h1>
                    <p class="lead">{description}</p>
                </div>
                
                <!-- Search Bar -->
                <div class="search-container">
                    <span class="material-symbols-outlined search-icon">search</span>
                    <input type="text" class="search-bar" id="searchInput" placeholder="Search documentation..." autocomplete="off">
                    <span class="material-symbols-outlined search-clear" id="searchClear" onclick="clearSearch()">close</span>
                    <div class="search-stats" id="searchStats"></div>
                </div>
                
                <div class="cards-grid" id="cardsGrid">
                    {cards_html}
                </div>
            </div>

            <!-- Doc View -->
            <div id="doc-view" class="doc-view">
                <div class="breadcrumb">
                    <a onclick="showHome()">
                        <span class="material-symbols-outlined">arrow_back</span>
                        All sections
                    </a>
                    <span class="material-symbols-outlined">chevron_right</span>
                    <span id="current-section"></span>
                </div>
                <div class="doc-content markdown-content" id="doc-content">
                    <!-- Content loads here -->
                </div>
            </div>

            <footer>
                <span class="material-symbols-outlined">code</span>
                Generated with {project_name} Documentation Generator
            </footer>
        </div>
    </div>

    <!-- Scripts -->
    <script>
        // Theme management
        function getTheme() {{
            return localStorage.getItem('theme') || 'light';
        }}

        function setTheme(theme) {{
            document.documentElement.setAttribute('data-theme', theme);
            localStorage.setItem('theme', theme);
            
            // Update icon
            const icon = document.querySelector('#themeToggle .material-symbols-outlined');
            if (icon) {{
                icon.textContent = theme === 'dark' ? 'dark_mode' : 'light_mode';
            }}
        }}

        function toggleTheme() {{
            const current = getTheme();
            setTheme(current === 'dark' ? 'light' : 'dark');
        }}

        // Initialize theme
        setTheme(getTheme());

        // Toggle sidebar on mobile
        function toggleSidebar() {{
            document.getElementById('sidebar').classList.toggle('active');
        }}

        // Show/hide menu icon based on screen size
        function checkScreenSize() {{
            const menuIcon = document.querySelector('.menu-icon');
            if (window.innerWidth <= 768) {{
                menuIcon.style.display = 'block';
            }} else {{
                menuIcon.style.display = 'none';
                document.getElementById('sidebar').classList.remove('active');
            }}
        }}

        window.addEventListener('resize', checkScreenSize);
        checkScreenSize();

        // Page data
        const pages = {pages_json};
        const titles = {titles_json};

        // Search functionality
        const searchInput = document.getElementById('searchInput');
        const searchClear = document.getElementById('searchClear');
        const searchStats = document.getElementById('searchStats');
        const cardsGrid = document.getElementById('cardsGrid');
        
        function filterCards() {{
            const searchTerm = searchInput.value.toLowerCase().trim();
            
            if (searchTerm.length > 0) {{
                searchClear.classList.add('visible');
            }} else {{
                searchClear.classList.remove('visible');
            }}
            
            let visibleCount = 0;
            const cards = cardsGrid.children;
            
            for (let i = 0; i < cards.length; i++) {{
                const card = cards[i];
                // Skip empty state element if it exists
                if (card.id === 'empty-search-state') continue;
                
                const title = card.querySelector('h3').textContent.toLowerCase();
                const desc = card.querySelector('p').textContent.toLowerCase();
                const tags = card.querySelector('.card-tag').textContent.toLowerCase();
                
                if (title.includes(searchTerm) || desc.includes(searchTerm) || tags.includes(searchTerm)) {{
                    card.style.display = 'flex';
                    visibleCount++;
                }} else {{
                    card.style.display = 'none';
                }}
            }}
            
            // Update stats
            if (searchTerm.length > 0) {{
                searchStats.innerHTML = 'Found ' + visibleCount + ' matching section' + (visibleCount !== 1 ? 's' : '');
            }} else {{
                searchStats.innerHTML = '';
            }}
            
            // Show empty state if no results
            let emptyState = document.getElementById('empty-search-state');
            if (visibleCount === 0 && searchTerm.length > 0) {{
                if (!emptyState) {{
                    emptyState = document.createElement('div');
                    emptyState.id = 'empty-search-state';
                    emptyState.className = 'empty-state';
                    emptyState.innerHTML = `
                        <span class="material-symbols-outlined">search_off</span>
                        <h3>No results found</h3>
                        <p>Try adjusting your search terms or browse all sections</p>
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

        // Debounce search for better performance
        let searchTimeout;
        searchInput.addEventListener('input', function() {{
            clearTimeout(searchTimeout);
            searchTimeout = setTimeout(filterCards, 300);
        }});

        // Category toggle
        function toggleCategory(categoryId) {{
            const items = document.getElementById('category-' + categoryId);
            const header = items.previousElementSibling;
            
            if (items) {{
                items.classList.toggle('collapsed');
                header.classList.toggle('collapsed');
                
                // Save state to localStorage
                const collapsedCategories = JSON.parse(localStorage.getItem('collapsedCategories') || '{{}}');
                collapsedCategories[categoryId] = items.classList.contains('collapsed');
                localStorage.setItem('collapsedCategories', JSON.stringify(collapsedCategories));
            }}
        }}

        // Restore category states
        function restoreCategoryStates() {{
            const collapsedCategories = JSON.parse(localStorage.getItem('collapsedCategories') || '{{}}');
            
            for (const [categoryId, collapsed] of Object.entries(collapsedCategories)) {{
                const items = document.getElementById('category-' + categoryId);
                if (items && collapsed) {{
                    items.classList.add('collapsed');
                    items.previousElementSibling.classList.add('collapsed');
                }}
            }}
        }}

        function showHome() {{
            document.getElementById('home-view').style.display = 'block';
            document.getElementById('doc-view').style.display = 'none';
            
            // Update active state in sidebar
            document.querySelectorAll('.sidebar-item').forEach(item => {{
                item.classList.remove('active');
            }});
            const navHome = document.getElementById('nav-home');
            if (navHome) navHome.classList.add('active');
            
            // Close sidebar on mobile
            document.getElementById('sidebar').classList.remove('active');
            
            // Update URL
            window.location.hash = '';
            
            // Clear search when returning home
            clearSearch();
        }}

        function showPage(pageId) {{
            document.getElementById('home-view').style.display = 'none';
            document.getElementById('doc-view').style.display = 'block';
            
            // Load content
            document.getElementById('doc-content').innerHTML = pages[pageId];
            document.getElementById('current-section').innerHTML = titles[pageId] || pageId;
            
            // Update active state in sidebar
            document.querySelectorAll('.sidebar-item').forEach(item => {{
                item.classList.remove('active');
            }});
            
            // Find and highlight the clicked item
            const activeItem = Array.from(document.querySelectorAll('.sidebar-item')).find(
                item => item.getAttribute('onclick') && item.getAttribute('onclick').includes(pageId)
            );
            if (activeItem) {{
                activeItem.classList.add('active');
            }}
            
            // Close sidebar on mobile
            document.getElementById('sidebar').classList.remove('active');
            
            // Update URL
            window.location.hash = pageId;
            
            // Scroll to top
            document.querySelector('.main-content').scrollTop = 0;
        }}

        // Load page from hash on startup
        window.onload = function() {{
            restoreCategoryStates();
            
            if (window.location.hash) {{
                const pageId = window.location.hash.substring(1);
                if (pages[pageId]) {{
                    showPage(pageId);
                }} else {{
                    showHome();
                }}
            }} else {{
                showHome();
            }}
        }};

        // Handle browser back/forward
        window.onhashchange = function() {{
            if (window.location.hash) {{
                const pageId = window.location.hash.substring(1);
                if (pages[pageId]) {{
                    showPage(pageId);
                }} else {{
                    showHome();
                }}
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
            'users': 'group',
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
            'email': 'mail',
            'phone': 'call',
            'camera': 'camera_alt',
            'video': 'videocam',
            'image': 'image',
            'chart': 'bar_chart',
            'folder': 'folder',
            'archive': 'archive',
            'pdf': 'picture_as_pdf',
            'text': 'article',
            'plugin': 'extension',
            'toolbox': 'handyman',
            'wrench': 'build',
            'brush': 'brush',
            'map': 'map',
            'location': 'location_on',
            'globe': 'public',
            'heart': 'favorite',
            'star': 'star',
            'flag': 'flag',
            'tag': 'sell',
            'bookmark': 'bookmark',
            'comment': 'comment',
            'chat': 'chat',
            'notification': 'notifications',
            'alert': 'notifications_active',
            'default': 'description'
        }
        return icon_map.get(icon_name.lower(), icon_map['default'])
    
    def _process_markdown(self, content: str) -> str:
        """Process markdown content and add syntax highlighting"""
        # Convert markdown to HTML
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
                return f'''
                <div class="note-block warning">
                    <span class="material-symbols-outlined">warning</span>
                    <div class="note-content">
                        <strong>File not found</strong>
                        <p>{file_path}</p>
                    </div>
                </div>
                '''
            
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            return self._process_markdown(content)
            
        except Exception as e:
            return f'''
            <div class="note-block danger">
                <span class="material-symbols-outlined">error</span>
                <div class="note-content">
                    <strong>Error loading {file_path}</strong>
                    <p>{e}</p>
                </div>
            </div>
            '''
    
    def generate(self, config_file: str, output_file: str):
        # Load config
        with open(config_file, 'r', encoding='utf-8') as f:
            if config_file.endswith('.json'):
                config = json.load(f)
            elif config_file.endswith(('.yml', '.yaml')):
                config = yaml.safe_load(f)
            else:
                raise ValueError("Config must be .json, .yml, or .yaml")
        
        project_name = config.get('name', 'Documentation')
        project_version = config.get('version', '1.0.0')
        project_description = config.get('description', 'Documentation and API references')
        base_dir = os.path.dirname(os.path.abspath(config_file))
        
        # Get categories from config
        categories = config.get('categories', [])
        sections = config.get('sections', [])
        
        # If no categories defined, create default from sections
        if not categories:
            category_names = set(section.get('category', 'General') for section in sections)
            categories = [{'name': name, 'icon': 'folder'} for name in sorted(category_names)]
        
        cards_html = []
        pages_dict = {}
        titles_dict = {}
        sidebar_sections = {}
        
        # Group sections by category
        for section in sections:
            category = section.get('category', 'General')
            if category not in sidebar_sections:
                sidebar_sections[category] = []
            sidebar_sections[category].append(section)
        
        # Generate sidebar with categories
        sidebar_content = []
        category_count = len(categories)
        
        for category in categories:
            category_name = category.get('name', 'General')
            category_icon = self._get_icon_name(category.get('icon', 'folder'))
            category_id = category_name.lower().replace(' ', '-').replace('/', '-')
            
            # Get sections for this category
            category_sections = sidebar_sections.get(category_name, [])
            
            sidebar_content.append(f'''
            <div class="sidebar-section">
                <div class="category-header" onclick="toggleCategory('{category_id}')">
                    <span class="material-symbols-outlined">chevron_right</span>
                    <span class="material-symbols-outlined">{category_icon}</span>
                    <span class="category-name">{category_name}</span>
                    <span class="category-count">{len(category_sections)}</span>
                </div>
                <div class="category-items" id="category-{category_id}">
            ''')
            
            for section in category_sections:
                title = section.get('title', 'Untitled')
                section_id = title.lower().replace(' ', '-').replace('/', '-').replace('\\', '-')
                icon = self._get_icon_name(section.get('icon', 'default'))
                
                sidebar_content.append(f'''
                <div class="sidebar-item" onclick="showPage('{section_id}')">
                    <span class="material-symbols-outlined">{icon}</span>
                    {title}
                </div>
                ''')
            
            sidebar_content.append('''
                </div>
            </div>
            ''')
        
        # Generate cards and load content
        for section in sections:
            title = section.get('title', 'Untitled')
            description = section.get('description', '')
            icon = self._get_icon_name(section.get('icon', 'default'))
            markdown_file = section.get('file', '')
            category = section.get('category', 'General')
            
            if markdown_file and not os.path.isabs(markdown_file):
                markdown_file = os.path.join(base_dir, markdown_file)
            
            section_id = title.lower().replace(' ', '-').replace('/', '-').replace('\\', '-')
            
            # Load content
            if markdown_file and os.path.exists(markdown_file):
                content = self.load_markdown(markdown_file)
            else:
                content = f'''
                <h1>{title}</h1>
                <p>{description}</p>
                <div class="note-block warning">
                    <span class="material-symbols-outlined">warning</span>
                    <div class="note-content">
                        <strong>No content file specified</strong>
                        <p>Please add a markdown file to this section.</p>
                    </div>
                </div>
                '''
            
            pages_dict[section_id] = content
            titles_dict[section_id] = title
            file_display = os.path.basename(markdown_file) if markdown_file else "no file"
            
            # Create card
            card = f'''
            <div class="doc-card" onclick="showPage('{section_id}')" data-category="{category}">
                <div class="card-icon">
                    <span class="material-symbols-outlined">{icon}</span>
                </div>
                <h3>{title}</h3>
                <p>{description}</p>
                <div class="card-footer">
                    <span class="card-tag">
                        <span class="material-symbols-outlined">folder</span> {category}
                    </span>
                    <span class="material-symbols-outlined card-arrow">arrow_forward</span>
                </div>
            </div>
            '''
            cards_html.append(card)
        
        # Generate final HTML
        html = self.template.format(
            project_name=project_name,
            version=project_version,
            description=project_description,
            category_count=category_count,
            section_count=len(sections),
            generation_date=datetime.now().strftime("%B %d, %Y"),
            sidebar_content='\n'.join(sidebar_content),
            cards_html='\n'.join(cards_html),
            pages_json=json.dumps(pages_dict),
            titles_json=json.dumps(titles_dict)
        )
        
        # Create output directory
        os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
        
        # Write file
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html)
        
        print(f'''
    Documentation generated successfully: {output_file}
     - Sections: {len(sections)}
     - Categories: {category_count}
        ''')

def create_example_config():
    config = {
        "name": "My Library SDK",
        "version": "2.1.0",
        "description": "Complete API documentation for Bluetooth, WiFi, Permissions, and more",
        "categories": [
            {
                "name": "Connectivity",
                "icon": "network",
                "description": "Network and device connectivity modules"
            },
            {
                "name": "Security",
                "icon": "security",
                "description": "Security and permissions management"
            },
            {
                "name": "Networking",
                "icon": "api",
                "description": "HTTP clients and network protocols"
            },
            {
                "name": "Setup",
                "icon": "settings",
                "description": "Configuration and initialization"
            },
            {
                "name": "Guides",
                "icon": "guide",
                "description": "Tutorials and best practices"
            }
        ],
        "sections": [
            {
                "title": "Bluetooth API",
                "description": "Bluetooth LE and Classic API reference with examples",
                "icon": "bluetooth",
                "category": "Connectivity",
                "file": "docs/bluetooth.md"
            },
            {
                "title": "WiFi Manager",
                "description": "Network connectivity, scanning, and configuration",
                "icon": "wifi",
                "category": "Connectivity",
                "file": "docs/wifi.md"
            },
            {
                "title": "Permissions System",
                "description": "Runtime permission handling and security policies",
                "icon": "permissions",
                "category": "Security",
                "file": "docs/permissions.md"
            },
            {
                "title": "REST API Client",
                "description": "HTTP client, endpoints, and data models",
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
                "description": "Sample code and usage patterns",
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
        'bluetooth.md': '# Bluetooth API\n\n## Overview\nThe Bluetooth API provides comprehensive support for both Bluetooth Classic and Bluetooth Low Energy (BLE) devices.\n\n## Features\n- Device discovery and scanning\n- Connection management\n- Service and characteristic handling\n- Pairing and bonding\n- Advertising support\n\n## Basic Usage\n```python\nfrom mylib.bluetooth import BluetoothManager\n\nmanager = BluetoothManager()\nmanager.start_scan()\n```',
        'wifi.md': '# WiFi Manager\n\n## Overview\nManage WiFi connections, scan networks, and configure settings.\n\n## Features\n- Network scanning\n- Connection management\n- Signal strength monitoring\n- Security configuration',
        'permissions.md': '# Permissions System\n\n## Overview\nHandle runtime permissions and security policies.\n\n## Usage\n```python\nfrom mylib.permissions import PermissionManager\n\nperm_manager = PermissionManager()\nperm_manager.request_permission("camera")\n```',
        'api.md': '# REST API Client\n\n## Overview\nHTTP client with built-in retry logic, caching, and error handling.\n\n## Features\n- GET, POST, PUT, DELETE requests\n- Automatic retries\n- Response caching\n- Error handling',
        'config.md': '# Configuration\n\n## Overview\nConfigure library settings and initialization parameters.\n\n## Options\n- Logging level\n- Cache size\n- Timeout settings\n- Retry policies',
        'examples.md': '# Code Examples\n\n## Basic Examples\n\n### Bluetooth Scanning\n```python\nfrom mylib.bluetooth import BluetoothManager\n\nmanager = BluetoothManager()\ndevices = manager.scan(timeout=5)\nfor device in devices:\n    print(f"Found: {device.name}")\n```'
    }
    
    for filename, content in example_files.items():
        with open(f'docs/{filename}', 'w', encoding='utf-8') as f:
            f.write(content)
    
    print('''
    Example configuration created: docs-config.json
    ''')

def main():
    parser = argparse.ArgumentParser(description='Generate beautiful Material You documentation')
    parser.add_argument('-c', '--config', default='docs-config.json', help='Configuration file')
    parser.add_argument('-o', '--output', default='docs/index.html', help='Output HTML file')
    parser.add_argument('--init', action='store_true', help='Create example config')
    
    args = parser.parse_args()
    
    if args.init:
        create_example_config()
        return
    
    if not os.path.exists(args.config):
        print(f'''
    Configuration file not found: {args.config}
        ''')
        return
    
    generator = DocGenerator()
    generator.generate(args.config, args.output)

if __name__ == '__main__':
    main()