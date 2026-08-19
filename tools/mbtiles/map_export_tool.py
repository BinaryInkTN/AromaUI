#!/usr/bin/env python3
import os
import sqlite3
import io
import math
import time
import argparse
import sys
import shutil
import subprocess
import struct
import requests
import json
import random
import threading
from typing import Tuple, List, Optional, Dict, Set
from concurrent.futures import ThreadPoolExecutor, as_completed

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("Warning: Pillow not installed. JPEG compression disabled.")

try:
    import osmium
    HAS_OSMIUM = True
except ImportError:
    HAS_OSMIUM = False
    print("Warning: osmium not installed. OSM processing disabled.")

try:
    from rich.console import Console
    from rich.progress import Progress, SpinnerColumn, BarColumn, TextColumn, TimeElapsedColumn, TimeRemainingColumn
    from rich.panel import Panel
    from rich.table import Table
    from rich import box
    HAS_RICH = True
except ImportError:
    HAS_RICH = False
    print("Note: Install rich for TUI: pip install rich")
    class Console:
        def print(self, *args, **kwargs):
            print(*args)
    console = Console()

if HAS_RICH:
    console = Console()

try:
    from deep_translator import GoogleTranslator
    HAS_TRANSLATOR = True
except ImportError:
    HAS_TRANSLATOR = False
    print("Note: Install deep-translator for Arabic name translation: pip install deep-translator")

TILE_SOURCES = {
    "cartodb_light": "https://a.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
    "cartodb_dark": "https://a.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png",
    "wikimedia": "https://maps.wikimedia.org/osm-intl/{z}/{x}/{y}.png",
    "satellite": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    "3d": "https://api.maptiler.com/maps/streets-v4/{z}/{x}/{y}.png?key=EzPpRuk2HP5W2r4wdrHv"
}

USER_AGENTS = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
]

MAX_RETRIES = 3
RETRY_DELAY = 5
BATCH_SIZE = 100
MAX_WORKERS = 1
RATE_LIMIT_DELAY = 1

OSRM_SOURCES = {
    "tunisia": "https://download.geofabrik.de/africa/tunisia-latest.osm.pbf",
    "monaco": "https://download.geofabrik.de/europe/monaco-latest.osm.pbf",
}

PRESETS = {
    "tunisia": (7.5, 30.2, 11.6, 37.5, "tunisia"),
    "tunis": (10.13, 36.78, 10.22, 36.85, "tunisia"),
    "ariana": (10.126659, 36.824782, 10.243172, 36.885757, "tunisia"),
    "sfax": (10.70, 34.70, 10.78, 34.78, "tunisia"),
    "sousse": (10.60, 35.80, 10.65, 35.85, "tunisia"),
    "monaco": (7.40, 43.72, 7.44, 43.75, "monaco"),
}

def deg2num(lat_deg: float, lon_deg: float, zoom: int) -> Tuple[int, int]:
    lat_rad = math.radians(lat_deg)
    n = 2.0 ** zoom
    xtile = int((lon_deg + 180.0) / 360.0 * n)
    ytile = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return xtile, ytile

def num2deg(xtile: int, ytile: int, zoom: int) -> Tuple[float, float]:
    n = 2.0 ** zoom
    lon_deg = xtile / n * 360.0 - 180.0
    lat_rad = math.atan(math.sinh(math.pi * (1 - 2 * ytile / n)))
    lat_deg = math.degrees(lat_rad)
    return lat_deg, lon_deg

def get_tiles_for_bbox(west: float, south: float, east: float, north: float, zoom: int) -> List[Tuple[int, int, int]]:
    x1, y1 = deg2num(north, west, zoom)
    x2, y2 = deg2num(south, east, zoom)
    x_min, x_max = min(x1, x2), max(x1, x2)
    y_min, y_max = min(y1, y2), max(y1, y2)
    tiles = []
    max_tiles = 2 ** zoom
    for x in range(x_min, x_max + 1):
        for y in range(y_min, y_max + 1):
            if 0 <= x < max_tiles and 0 <= y < max_tiles:
                tiles.append((zoom, x, y))
    return tiles

def get_exact_bounds_from_tiles(west: float, south: float, east: float, north: float, min_zoom: int) -> Tuple[float, float, float, float]:
    x1, y1 = deg2num(north, west, min_zoom)
    x2, y2 = deg2num(south, east, min_zoom)
    x_min = min(x1, x2)
    x_max = max(x1, x2)
    y_min = min(y1, y2)
    y_max = max(y1, y2)
    tile_north, tile_west = num2deg(x_min, y_min, min_zoom)
    tile_south, tile_east = num2deg(x_max + 1, y_max + 1, min_zoom)
    return tile_west, tile_south, tile_east, tile_north

def get_random_user_agent() -> str:
    return random.choice(USER_AGENTS)

def translate_name(name: str, source_lang: str = 'ar', target_lang: str = 'fr') -> str:
    if not name or not HAS_TRANSLATOR:
        return name
    if all(ord(c) < 128 for c in name):
        return name
    try:
        translator = GoogleTranslator(source=source_lang, target=target_lang)
        translated = translator.translate(name)
        if translated and translated != name:
            return translated
    except Exception:
        pass
    return name

def is_arabic_text(text: str) -> bool:
    if not text:
        return False
    return any('\u0600' <= c <= '\u06FF' or '\u0750' <= c <= '\u077F' or
               '\u08A0' <= c <= '\u08FF' or '\uFB50' <= c <= '\uFDFF' or
               '\uFE70' <= c <= '\uFEFF' for c in text)

def get_preferred_name(name: str, name_fr: str = '', name_en: str = '') -> str:
    if not name:
        return ''
    if name_fr and not is_arabic_text(name_fr):
        return name_fr
    if name_en and not is_arabic_text(name_en):
        return name_en
    if not is_arabic_text(name):
        return name
    return translate_name(name, 'ar', 'fr')

def _bbox_slug(bbox: Tuple[float, float, float, float]) -> str:
    return "_".join(f"{v:.4f}".replace("-", "m").replace(".", "p") for v in bbox)

def _tmp_pbf_path(output_pbf: str) -> str:
    directory, filename = os.path.split(output_pbf)
    return os.path.join(directory, f".tmp_{filename}")

def clip_pbf_to_bbox(input_pbf: str, output_pbf: str, bbox: Tuple[float, float, float, float]) -> str:
    if os.path.exists(output_pbf):
        console.print(f"[yellow]Clipped OSM data already exists: {output_pbf}[/yellow]")
        return output_pbf

    west, south, east, north = bbox
    osmium_bin = shutil.which("osmium")

    if osmium_bin:
        console.print("[bold cyan]Clipping OSM data to bbox (osmium-tool)...[/bold cyan]")
        bbox_str = f"{west},{south},{east},{north}"
        tmp_output = _tmp_pbf_path(output_pbf)
        cmd = [osmium_bin, "extract", "-b", bbox_str, "--strategy", "smart",
               "-f", "pbf", "-o", tmp_output, "--overwrite", input_pbf]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0 and os.path.exists(tmp_output):
            os.replace(tmp_output, output_pbf)
            console.print(f"[green]Clipped OSM data written to {output_pbf}[/green]")
            return output_pbf
        console.print(f"[red]osmium extract failed ({result.returncode}): {result.stderr.strip()}[/red]")
        if os.path.exists(tmp_output):
            os.remove(tmp_output)
        console.print("[yellow]Falling back to pure-Python clipping...[/yellow]")

    return _clip_pbf_python(input_pbf, output_pbf, bbox)

def _clip_pbf_python(input_pbf: str, output_pbf: str, bbox: Tuple[float, float, float, float]) -> str:
    if not HAS_OSMIUM:
        raise RuntimeError(
            "Clipping OSM data requires either the 'osmium' CLI (osmium-tool) "
            "or the pyosmium library ('pip install osmium'), and neither is available."
        )
    west, south, east, north = bbox

    class NodeCollector(osmium.SimpleHandler):
        def __init__(self):
            super().__init__()
            self.node_ids_in_bbox: Set[int] = set()

        def node(self, n):
            if n.location.valid() and west <= n.location.lon <= east and south <= n.location.lat <= north:
                self.node_ids_in_bbox.add(n.id)

    console.print("[cyan]Pass 1/3: scanning nodes in bbox...[/cyan]")
    node_collector = NodeCollector()
    node_collector.apply_file(input_pbf, locations=False)
    nodes_in_bbox = node_collector.node_ids_in_bbox
    console.print(f"  Found {len(nodes_in_bbox):,} nodes in bbox")

    class WayCollector(osmium.SimpleHandler):
        def __init__(self, nodes_in_bbox: Set[int]):
            super().__init__()
            self.nodes_in_bbox = nodes_in_bbox
            self.way_ids: Set[int] = set()
            self.needed_node_ids: Set[int] = set(nodes_in_bbox)

        def way(self, w):
            if any(nd.ref in self.nodes_in_bbox for nd in w.nodes):
                self.way_ids.add(w.id)
                self.needed_node_ids.update(nd.ref for nd in w.nodes)

    console.print("[cyan]Pass 2/3: scanning ways referencing bbox nodes...[/cyan]")
    way_collector = WayCollector(nodes_in_bbox)
    way_collector.apply_file(input_pbf, locations=False)
    console.print(f"  Found {len(way_collector.way_ids):,} ways intersecting bbox")

    needed_node_ids = way_collector.needed_node_ids
    way_ids = way_collector.way_ids

    console.print("[cyan]Pass 3/3: writing clipped data...[/cyan]")
    tmp_output = _tmp_pbf_path(output_pbf)
    if os.path.exists(tmp_output):
        os.remove(tmp_output)
    writer = osmium.SimpleWriter(tmp_output)

    class ClipWriterHandler(osmium.SimpleHandler):
        def __init__(self, writer, needed_node_ids: Set[int], way_ids: Set[int]):
            super().__init__()
            self.writer = writer
            self.needed_node_ids = needed_node_ids
            self.way_ids = way_ids

        def node(self, n):
            if n.id in self.needed_node_ids:
                self.writer.add_node(n)

        def way(self, w):
            if w.id in self.way_ids:
                self.writer.add_way(w)

        def relation(self, r):
            for m in r.members:
                if (m.type == 'w' and m.ref in self.way_ids) or \
                   (m.type == 'n' and m.ref in self.needed_node_ids):
                    self.writer.add_relation(r)
                    return

    try:
        clip_handler = ClipWriterHandler(writer, needed_node_ids, way_ids)
        clip_handler.apply_file(input_pbf, locations=False)
    finally:
        writer.close()

    os.replace(tmp_output, output_pbf)
    console.print(f"[green]Clipped OSM data written to {output_pbf}[/green]")
    return output_pbf

def create_mbtiles(filepath: str, metadata: dict) -> sqlite3.Connection:
    if os.path.exists(filepath):
        console.print(f"[yellow]Opening existing database for resume: {filepath}[/yellow]")
        conn = sqlite3.connect(filepath)
        cursor = conn.cursor()
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='tiles'")
        if not cursor.fetchone():
            conn.close()
            os.remove(filepath)
            return create_mbtiles(filepath, metadata)
        cursor.execute("PRAGMA journal_mode=WAL")
        cursor.execute("PRAGMA synchronous=OFF")
        cursor.execute("PRAGMA cache_size=-100000")
        for key, value in metadata.items():
            cursor.execute("INSERT OR REPLACE INTO metadata (name, value) VALUES (?, ?)", (key, str(value)))
        conn.commit()
        return conn
    else:
        console.print(f"[green]Creating new database: {filepath}[/green]")
        conn = sqlite3.connect(filepath)
        cursor = conn.cursor()
        cursor.execute("PRAGMA journal_mode=WAL")
        cursor.execute("PRAGMA synchronous=OFF")
        cursor.execute("PRAGMA cache_size=-100000")
        cursor.execute("PRAGMA page_size=4096")
        cursor.execute("""
            CREATE TABLE tiles (
                zoom_level INTEGER NOT NULL,
                tile_column INTEGER NOT NULL,
                tile_row INTEGER NOT NULL,
                tile_data BLOB,
                PRIMARY KEY (zoom_level, tile_column, tile_row)
            ) WITHOUT ROWID
        """)
        cursor.execute("""
            CREATE TABLE metadata (
                name TEXT NOT NULL,
                value TEXT,
                PRIMARY KEY (name)
            ) WITHOUT ROWID
        """)
        cursor.execute("CREATE INDEX IF NOT EXISTS tiles_zoom_level_idx ON tiles (zoom_level)")
        required_metadata = {
            'name': metadata.get('name', 'untitled'),
            'type': 'baselayer',
            'version': '1.0',
            'description': metadata.get('description', ''),
            'format': metadata.get('format', 'png'),
            'bounds': metadata.get('bounds', '-180,-85.0511,180,85.0511'),
            'center': metadata.get('center', '0,0,0'),
            'minzoom': str(metadata.get('minzoom', '0')),
            'maxzoom': str(metadata.get('maxzoom', '0')),
            'attribution': metadata.get('attribution', ''),
        }
        for key, value in required_metadata.items():
            cursor.execute("INSERT OR REPLACE INTO metadata (name, value) VALUES (?, ?)", (key, str(value)))
        conn.commit()
        return conn

def download_tile(url_template: str, z: int, x: int, y: int, tile_format: str = 'png', jpeg_quality: int = 80, existing_tiles: set = None) -> Tuple[int, int, int, Optional[bytes]]:
    tms_y = (2 ** z) - 1 - y
    if existing_tiles and (z, x, tms_y) in existing_tiles:
        return (z, x, tms_y, None)
    for attempt in range(MAX_RETRIES):
        try:
            url = url_template.format(z=z, x=x, y=y)
            headers = {
                "User-Agent": get_random_user_agent(),
                "Accept": "image/png,image/*;q=0.8,*/*;q=0.5",
                "Accept-Language": "en-US,en;q=0.5",
                "Accept-Encoding": "gzip, deflate",
                "Connection": "keep-alive",
            }
            response = requests.get(url, headers=headers, timeout=30)
            if response.status_code == 200:
                content = response.content
                if len(content) < 40:
                    if attempt < MAX_RETRIES - 1:
                        time.sleep(RETRY_DELAY * (attempt + 1))
                    continue
                try:
                    if HAS_PIL:
                        img = Image.open(io.BytesIO(content))
                        img.verify()
                        if tile_format == 'jpeg':
                            img = Image.open(io.BytesIO(content))
                            if img.mode != 'RGB':
                                img = img.convert('RGB')
                            output = io.BytesIO()
                            img.save(output, format='JPEG', quality=jpeg_quality, optimize=True, progressive=False)
                            return (z, x, tms_y, output.getvalue())
                        else:
                            return (z, x, tms_y, content)
                    else:
                        return (z, x, tms_y, content)
                except Exception:
                    if attempt < MAX_RETRIES - 1:
                        time.sleep(RETRY_DELAY * (attempt + 1))
                    continue
            elif response.status_code == 429:
                retry_after = int(response.headers.get('Retry-After', RETRY_DELAY * (attempt + 1) * 3))
                console.print(f"[yellow]Rate limited. Waiting {retry_after} seconds...[/yellow]")
                time.sleep(retry_after)
                continue
            elif response.status_code in (500, 502, 503, 504):
                time.sleep(RETRY_DELAY * (attempt + 1) * 2)
                continue
            elif response.status_code in (404, 204, 403):
                return (z, x, tms_y, None)
            else:
                if attempt < MAX_RETRIES - 1:
                    time.sleep(RETRY_DELAY)
        except requests.exceptions.RequestException as e:
            if attempt < MAX_RETRIES - 1:
                time.sleep(RETRY_DELAY * (attempt + 1))
            else:
                console.print(f"[red]Error downloading tile {z}/{x}/{y}: {e}[/red]")
    time.sleep(RATE_LIMIT_DELAY)
    return (z, x, tms_y, None)

class POIExtractor:
    def __init__(self, pbf_file: str, bbox: Tuple[float, float, float, float]):
        self.pbf_file = pbf_file
        self.bbox = bbox
        self.pois = {}
        self.poi_categories = {
            'gas_stations': ['fuel', 'charging_station'],
            'restaurants': ['restaurant'],
            'cafes': ['cafe'],
            'fast_food': ['fast_food'],
            'supermarkets': ['supermarket'],
            'convenience': ['convenience'],
            'hotels': ['hotel', 'motel', 'guest_house', 'hostel'],
            'banks': ['bank'],
            'atms': ['atm'],
            'pharmacies': ['pharmacy'],
            'hospitals': ['hospital'],
            'clinics': ['clinic', 'doctors', 'dentist'],
            'schools': ['school', 'kindergarten'],
            'universities': ['university', 'college'],
            'parking': ['parking', 'parking_entrance'],
            'fuel': ['fuel'],
            'charging_stations': ['charging_station'],
            'car_repair': ['car_repair', 'car_workshop'],
            'car_wash': ['car_wash'],
            'shops': [],
            'other_pois': []
        }
        for cat in self.poi_categories:
            self.pois[cat] = []

    def extract_pois(self) -> Dict:
        if not HAS_OSMIUM:
            console.print("[red]Error: osmium not installed. Cannot extract POIs.[/red]")
            return {}
        console.print("[bold cyan]Extracting Points of Interest...[/bold cyan]")

        class POIHandler(osmium.SimpleHandler):
            def __init__(self, extractor):
                super().__init__()
                self.extractor = extractor
                self.poi_count = 0
            def node(self, n):
                if not n.location.valid():
                    return
                lat = n.location.lat
                lon = n.location.lon
                west, south, east, north = self.extractor.bbox
                if not (west <= lon <= east and south <= lat <= north):
                    return
                if len(n.tags) == 0:
                    return
                self.extractor._classify_poi(n.id, lat, lon, dict(n.tags))
                self.poi_count += 1

        handler = POIHandler(self)
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            TextColumn("[green]{task.fields[count]:,}[/green] POIs found"),
            TimeElapsedColumn(),
        ) as progress:
            task = progress.add_task("[cyan]Processing OSM data...", count=0)
            processing_thread = threading.Thread(target=handler.apply_file, args=(self.pbf_file,), kwargs={'locations': True})
            processing_thread.start()
            while processing_thread.is_alive():
                progress.update(task, count=handler.poi_count)
                time.sleep(0.1)
            processing_thread.join()
            progress.update(task, count=handler.poi_count)

        table = Table(title="POI Statistics", box=box.ROUNDED)
        table.add_column("Category", style="cyan")
        table.add_column("Count", style="magenta", justify="right")
        total_pois = 0
        for category, pois in self.pois.items():
            if len(pois) > 0:
                table.add_row(category.replace('_', ' ').title(), f"{len(pois):,}")
                total_pois += len(pois)
        table.add_row("[bold]Total[/bold]", f"[bold]{total_pois:,}[/bold]", style="bold green")
        console.print(table)
        return self.pois

    def _classify_poi(self, osm_id, lat, lon, tags):
        name = tags.get('name', '')
        name_fr = tags.get('name:fr', '')
        name_en = tags.get('name:en', '')
        display_name = get_preferred_name(name, name_fr, name_en)
        poi = {
            'id': osm_id,
            'lat': lat,
            'lon': lon,
            'name': name,
            'name_fr': display_name,
            'tags': tags,
            'phone': tags.get('phone', ''),
            'website': tags.get('website', ''),
            'opening_hours': tags.get('opening_hours', ''),
        }
        amenity = tags.get('amenity', '')
        if amenity:
            for category, amenities in self.poi_categories.items():
                if amenity in amenities:
                    poi['type'] = category
                    self.pois[category].append(poi)
                    return
        shop = tags.get('shop', '')
        if shop:
            if shop in ['supermarket', 'grocery']:
                poi['type'] = 'supermarkets'
                self.pois['supermarkets'].append(poi)
            elif shop in ['convenience', 'kiosk']:
                poi['type'] = 'convenience'
                self.pois['convenience'].append(poi)
            else:
                poi['type'] = 'shops'
                self.pois['shops'].append(poi)
            return
        tourism = tags.get('tourism', '')
        if tourism:
            if tourism in ['hotel', 'motel', 'guest_house', 'hostel']:
                poi['type'] = 'hotels'
                self.pois['hotels'].append(poi)
                return
        poi['type'] = 'other_pois'
        self.pois['other_pois'].append(poi)

    def save_to_sqlite(self, db_file: str):
        console.print(f"[bold cyan]Saving POIs to SQLite database: {db_file}[/bold cyan]")
        conn = sqlite3.connect(db_file)
        cursor = conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS pois (
                poi_id INTEGER PRIMARY KEY AUTOINCREMENT,
                osm_id INTEGER,
                category TEXT,
                name TEXT,
                name_fr TEXT,
                lat REAL,
                lon REAL,
                phone TEXT,
                website TEXT,
                opening_hours TEXT,
                tags TEXT
            )
        """)
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_pois_category ON pois(category)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_pois_location ON pois(lat, lon)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_pois_name ON pois(name)")
        total_inserted = 0
        total_pois = sum(len(pois) for pois in self.pois.values())
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            TextColumn("({task.completed}/{task.total})"),
            TimeElapsedColumn(),
            TimeRemainingColumn(),
        ) as progress:
            task = progress.add_task("[cyan]Saving POIs...", total=total_pois)
            for category, pois in self.pois.items():
                for poi in pois:
                    cursor.execute("""
                        INSERT INTO pois (osm_id, category, name, name_fr, lat, lon,
                                        phone, website, opening_hours, tags)
                        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """, (
                        poi['id'], category, poi.get('name', ''), poi.get('name_fr', ''),
                        poi['lat'], poi['lon'], poi.get('phone', ''), poi.get('website', ''),
                        poi.get('opening_hours', ''), json.dumps(poi.get('tags', {}))
                    ))
                    total_inserted += 1
                    progress.update(task, advance=1)
        conn.commit()
        conn.close()
        console.print(f"[green]Saved {total_inserted:,} POIs to {db_file}[/green]")
        return db_file

class OSRMProcessor:
    def __init__(self, region: str, bbox: Tuple[float, float, float, float], output_dir: str = "osm_data", clip_to_bbox: bool = True):
        self.region = region
        self.bbox = bbox
        self.output_dir = output_dir
        self.clip_to_bbox = clip_to_bbox
        self.osrm_url = OSRM_SOURCES.get(region)
        os.makedirs(output_dir, exist_ok=True)

    def download_osm_data(self) -> str:
        if not self.osrm_url:
            raise ValueError(f"No OSM data source for region: {self.region}")
        pbf_file = os.path.join(self.output_dir, f"{self.region}.osm.pbf")
        if os.path.exists(pbf_file):
            console.print(f"[yellow]OSM data already exists: {pbf_file}[/yellow]")
        else:
            console.print(f"[bold cyan]Downloading OSM data for {self.region}...[/bold cyan]")
            headers = {"User-Agent": get_random_user_agent()}
            response = requests.get(self.osrm_url, stream=True, headers=headers, timeout=300)
            response.raise_for_status()
            total_size = int(response.headers.get('content-length', 0))
            downloaded = 0
            with Progress(
                SpinnerColumn(),
                TextColumn("[progress.description]{task.description}"),
                BarColumn(),
                TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
                TextColumn("({task.completed:.1f}/{task.total:.1f} MB)"),
                TimeElapsedColumn(),
                TimeRemainingColumn(),
            ) as progress:
                task = progress.add_task("[cyan]Downloading OSM data...", total=total_size / (1024 * 1024))
                tmp_pbf_file = pbf_file + ".part"
                with open(tmp_pbf_file, 'wb') as f:
                    for chunk in response.iter_content(chunk_size=8192):
                        if chunk:
                            f.write(chunk)
                            downloaded += len(chunk)
                            progress.update(task, completed=downloaded / (1024 * 1024))
                os.replace(tmp_pbf_file, pbf_file)

        if not self.clip_to_bbox:
            return pbf_file

        clipped_file = os.path.join(
            self.output_dir, f"{self.region}_{_bbox_slug(self.bbox)}.osm.pbf"
        )
        return clip_pbf_to_bbox(pbf_file, clipped_file, self.bbox)

ROUTING_BIN_HEADER = b"OSRM_PROD_V3\x00\x00\x00"
assert len(ROUTING_BIN_HEADER) == 15

HIGHWAY_SPEEDS_KMH = {
    'motorway': 110, 'motorway_link': 60,
    'trunk': 100, 'trunk_link': 50,
    'primary': 80, 'primary_link': 50,
    'secondary': 60, 'secondary_link': 40,
    'tertiary': 50, 'tertiary_link': 30,
    'unclassified': 40,
    'residential': 30,
    'living_street': 20,
    'service': 20,
    'track': 20,
}

def _haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    R = 6371000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

def build_routing_bin(pbf_file: str, output_bin: str) -> str:
    if not HAS_OSMIUM:
        raise RuntimeError("Building the routing graph requires pyosmium ('pip install osmium').")

    console.print("[bold cyan]Building routing graph (.bin) for the app's offline router...[/bold cyan]")

    class WayNodeCollector(osmium.SimpleHandler):
        def __init__(self):
            super().__init__()
            self.used_node_ids: Set[int] = set()
            self.way_count = 0

        def way(self, w):
            if w.tags.get('highway', '') not in HIGHWAY_SPEEDS_KMH:
                return
            self.way_count += 1
            for nd in w.nodes:
                self.used_node_ids.add(nd.ref)

    console.print("[cyan]Pass 1/3: scanning routable ways...[/cyan]")
    way_collector = WayNodeCollector()
    way_collector.apply_file(pbf_file, locations=False)
    console.print(f"  Found {way_collector.way_count:,} routable ways, {len(way_collector.used_node_ids):,} nodes")
    if way_collector.way_count == 0:
        raise RuntimeError("No routable roads found in this bbox.")

    class NodeLocator(osmium.SimpleHandler):
        def __init__(self, wanted: Set[int]):
            super().__init__()
            self.wanted = wanted
            self.locations: Dict[int, Tuple[float, float]] = {}

        def node(self, n):
            if n.id in self.wanted and n.location.valid():
                self.locations[n.id] = (n.location.lat, n.location.lon)

    console.print("[cyan]Pass 2/3: resolving node coordinates...[/cyan]")
    locator = NodeLocator(way_collector.used_node_ids)
    locator.apply_file(pbf_file, locations=False)
    node_ids = list(locator.locations.keys())
    node_index: Dict[int, int] = {osm_id: i for i, osm_id in enumerate(node_ids)}
    console.print(f"  Resolved {len(node_ids):,} node locations")
    if not node_ids:
        raise RuntimeError("No valid node coordinates found.")

    class EdgeBuilder(osmium.SimpleHandler):
        def __init__(self, node_index: Dict[int, int], locations: Dict[int, Tuple[float, float]]):
            super().__init__()
            self.node_index = node_index
            self.locations = locations
            self.edges: List[Tuple[int, int, float, float, int]] = []

        def way(self, w):
            speed_kmh = HIGHWAY_SPEEDS_KMH.get(w.tags.get('highway', ''))
            if speed_kmh is None:
                return
            maxspeed = w.tags.get('maxspeed', '')
            digits = ''.join(c for c in maxspeed if c.isdigit())
            if digits:
                try:
                    speed_kmh = int(digits)
                except ValueError:
                    pass
            speed_ms = max(speed_kmh, 5) / 3.6
            oneway = w.tags.get('oneway', '')
            is_roundabout = 1 if w.tags.get('junction', '') == 'roundabout' else 0

            refs = [nd.ref for nd in w.nodes if nd.ref in self.node_index]
            for a, b in zip(refs, refs[1:]):
                lat1, lon1 = self.locations[a]
                lat2, lon2 = self.locations[b]
                dist = _haversine_m(lat1, lon1, lat2, lon2)
                if dist <= 0:
                    continue
                weight = dist / speed_ms
                i_a, i_b = self.node_index[a], self.node_index[b]
                if oneway in ('yes', '1', 'true'):
                    self.edges.append((i_a, i_b, weight, dist, is_roundabout))
                elif oneway == '-1':
                    self.edges.append((i_b, i_a, weight, dist, is_roundabout))
                else:
                    self.edges.append((i_a, i_b, weight, dist, is_roundabout))
                    self.edges.append((i_b, i_a, weight, dist, is_roundabout))

    console.print("[cyan]Pass 3/3: building edges...[/cyan]")
    edge_builder = EdgeBuilder(node_index, locator.locations)
    edge_builder.apply_file(pbf_file, locations=False)
    console.print(f"  Built {len(edge_builder.edges):,} directed edges")

    lats = [locator.locations[i][0] for i in node_ids]
    lons = [locator.locations[i][1] for i in node_ids]
    min_lat, max_lat = min(lats), max(lats)
    min_lon, max_lon = min(lons), max(lons)

    console.print(f"[cyan]Writing {output_bin}...[/cyan]")
    tmp_output = output_bin + ".tmp"
    with open(tmp_output, 'wb') as f:
        f.write(ROUTING_BIN_HEADER)
        f.write(struct.pack('<dddd', min_lat, min_lon, max_lat, max_lon))
        f.write(struct.pack('<I', len(node_ids)))
        for i, osm_id in enumerate(node_ids):
            lat, lon = locator.locations[osm_id]
            f.write(struct.pack('<IddB', i, lat, lon, 0))
        f.write(struct.pack('<I', len(edge_builder.edges)))
        for i, (from_idx, to_idx, weight, dist, is_roundabout) in enumerate(edge_builder.edges):
            f.write(struct.pack('<IIIddBBI', i, from_idx, to_idx, weight, dist, 0, 0, is_roundabout))
    os.replace(tmp_output, output_bin)

    console.print(
        f"[green]Routing graph written: {output_bin} "
        f"({len(node_ids):,} nodes, {len(edge_builder.edges):,} edges)[/green]"
    )
    return output_bin

def main():
    parser = argparse.ArgumentParser(
        description="Map Tile, OSM, and Routing Data Downloader",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --preset ariana --zoom 10-16 --output ariana.mbtiles
  %(prog)s --preset ariana --extract-pois --poi-db pois.db
  %(prog)s --preset ariana --build-routing --routing-bin routing.bin

Presets: tunisia, tunis, ariana, sfax, sousse, monaco
Sources: cartodb_light, cartodb_dark, wikimedia, satellite
        """
    )
    parser.add_argument("--preset", choices=PRESETS.keys(), help="Use a predefined area")
    parser.add_argument("--bbox", help="Bounding box: west,south,east,north")
    parser.add_argument("--zoom", default="7-12", help="Zoom range: min-max (default: 7-12)")
    parser.add_argument("--output", default="", help="Output MBTiles file")
    parser.add_argument("--source", default="cartodb_dark", choices=TILE_SOURCES.keys())
    parser.add_argument("--workers", type=int, default=MAX_WORKERS, help=f"Parallel workers (default: {MAX_WORKERS})")
    parser.add_argument("--tile-format", choices=['png', 'jpeg'], default='jpeg', help="Tile format")
    parser.add_argument("--jpeg-quality", type=int, default=80, help="JPEG quality 1-100")
    parser.add_argument("--extract-pois", action="store_true", help="Extract Points of Interest")
    parser.add_argument("--poi-db", default="pois.db", help="SQLite database for POIs")
    parser.add_argument("--poi-json", default="", help="JSON file for POIs")
    parser.add_argument("--download-osm", action="store_true", help="Download OSM data")
    parser.add_argument("--no-clip-osm", action="store_true", help="Keep full OSM extract instead of clipping to bbox")
    parser.add_argument("--build-routing", action="store_true", help="Build .bin routing graph for the app")
    parser.add_argument("--routing-bin", default="", help="Output path for .bin routing graph")
    parser.add_argument("--yes", action="store_true", help="Skip confirmation prompt")
    args = parser.parse_args()

    if args.preset:
        preset = PRESETS[args.preset]
        west, south, east, north = preset[0], preset[1], preset[2], preset[3]
        osrm_region = preset[4] if len(preset) > 4 else args.preset
        console.print(f"[green]Using preset: {args.preset} (OSM region: {osrm_region})[/green]")
    elif args.bbox:
        try:
            parts = args.bbox.split(",")
            west, south, east, north = map(float, parts)
            osrm_region = "tunisia"
        except ValueError:
            console.print("[red]Error: Invalid bbox format. Use: west,south,east,north[/red]")
            sys.exit(1)
    else:
        console.print("[red]Error: Must specify --preset or --bbox[/red]")
        sys.exit(1)

    if args.workers > MAX_WORKERS:
        console.print(f"[yellow]Warning: Limiting workers to {MAX_WORKERS}[/yellow]")
        args.workers = MAX_WORKERS

    try:
        zoom_parts = args.zoom.split("-")
        min_zoom, max_zoom = int(zoom_parts[0]), int(zoom_parts[1])
    except ValueError:
        console.print("[red]Error: Invalid zoom format. Use: min-max[/red]")
        sys.exit(1)

    console.print(Panel.fit(
        f"[bold]Map Data Downloader[/bold]\n"
        f"Area: {west:.4f}, {south:.4f} to {east:.4f}, {north:.4f}\n"
        f"Raster tiles: {'Yes' if args.output else 'No'}"
        f"{f' (zoom {min_zoom}-{max_zoom})' if args.output else ''}\n"
        f"POIs: {'Yes' if args.extract_pois else 'No'}\n"
        f"OSM data: {'Yes' if args.download_osm else 'No'}"
        f"{' (clipped to bbox)' if args.download_osm and not args.no_clip_osm else ''}\n"
        f"Routing bin: {'Yes' if args.build_routing else 'No'}",
        title="Configuration",
        border_style="cyan"
    ))

    if not args.yes:
        response = input("Start processing? (y/n): ").lower()
        if response != 'y':
            console.print("[yellow]Cancelled.[/yellow]")
            sys.exit(0)

    pbf_file = None
    if args.extract_pois or args.download_osm or args.build_routing:
        osrm_processor = OSRMProcessor(
            osrm_region, (west, south, east, north), clip_to_bbox=not args.no_clip_osm
        )
        pbf_file = osrm_processor.download_osm_data()

    if args.extract_pois and pbf_file:
        console.print(Panel.fit("[bold cyan]POI Extraction[/bold cyan]", border_style="cyan"))
        poi_extractor = POIExtractor(pbf_file, (west, south, east, north))
        pois = poi_extractor.extract_pois()
        if pois:
            if args.poi_db:
                poi_extractor.save_to_sqlite(args.poi_db)
            if args.poi_json:
                console.print(f"[bold cyan]Saving POIs to JSON: {args.poi_json}[/bold cyan]")
                output_data = {
                    'metadata': {
                        'version': '1.0',
                        'bbox': [west, south, east, north],
                        'total_pois': sum(len(p) for p in pois.values())
                    },
                    'pois': pois
                }
                with open(args.poi_json, 'w', encoding='utf-8') as f:
                    json.dump(output_data, f, ensure_ascii=False, indent=2)

    routing_bin_file = None
    if args.build_routing and pbf_file:
        console.print(Panel.fit("[bold cyan]Routing Graph Build[/bold cyan]", border_style="cyan"))
        try:
            if args.routing_bin:
                routing_bin_file = args.routing_bin
            else:
                base_name = os.path.splitext(os.path.basename(pbf_file))[0]
                routing_bin_file = os.path.join(os.path.dirname(pbf_file), f"{base_name}.bin")
            routing_bin_file = build_routing_bin(pbf_file, routing_bin_file)
        except Exception as e:
            console.print(f"[red]Error building routing graph: {e}[/red]")
            import traceback
            traceback.print_exc()
            sys.exit(1)

    if args.output:
        console.print(Panel.fit("[bold cyan]Raster Tile Download[/bold cyan]", border_style="cyan"))
        tile_url = TILE_SOURCES[args.source]
        output_file = args.output
        console.print("[cyan]Calculating tiles...[/cyan]")
        all_tiles = []
        for zoom in range(min_zoom, max_zoom + 1):
            tiles = get_tiles_for_bbox(west, south, east, north, zoom)
            all_tiles.extend(tiles)
            console.print(f"  Zoom {zoom}: {len(tiles)} tiles")
        total_tiles = len(all_tiles)
        estimated_size = total_tiles * (15 if args.tile_format == 'png' else 8) / 1024
        console.print(f"\nTotal: {total_tiles:,} tiles")
        console.print(f"Estimated size: {estimated_size:.1f} MB")
        exact_west, exact_south, exact_east, exact_north = get_exact_bounds_from_tiles(west, south, east, north, min_zoom)
        metadata = {
            'name': os.path.splitext(os.path.basename(output_file))[0],
            'type': 'baselayer',
            'version': '1.0',
            'description': f'Raster tiles - {args.source}',
            'format': args.tile_format,
            'bounds': f"{exact_west},{exact_south},{exact_east},{exact_north}",
            'center': f"{(exact_west + exact_east) / 2},{(exact_south + exact_north) / 2},{min_zoom}",
            'minzoom': min_zoom,
            'maxzoom': max_zoom,
            'attribution': 'OpenStreetMap contributors'
        }
        conn = create_mbtiles(output_file, metadata)
        cursor = conn.cursor()
        existing_tiles = set()
        cursor.execute("SELECT zoom_level, tile_column, tile_row FROM tiles")
        for row in cursor.fetchall():
            existing_tiles.add((row[0], row[1], row[2]))
        if existing_tiles:
            console.print(f"[yellow]Found {len(existing_tiles):,} existing tiles. Will skip them.[/yellow]")
            all_tiles = [(z, x, y) for z, x, y in all_tiles if (z, x, (2**z - 1 - y)) not in existing_tiles]
            total_tiles = len(all_tiles)
        success = 0
        failed = 0
        processed = 0
        start_time = time.time()
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            TextColumn("({task.completed}/{task.total})"),
            TextColumn("[green]{task.fields[success]} ok[/green]"),
            TextColumn("[red]{task.fields[failed]} failed[/red]"),
            TimeElapsedColumn(),
            TimeRemainingColumn(),
        ) as progress:
            task = progress.add_task("[cyan]Downloading tiles...", total=total_tiles, success=0, failed=0)
            with ThreadPoolExecutor(max_workers=args.workers) as executor:
                for i in range(0, total_tiles, BATCH_SIZE):
                    batch = all_tiles[i:i + BATCH_SIZE]
                    futures = [executor.submit(download_tile, tile_url, z, x, y, args.tile_format, args.jpeg_quality, existing_tiles) for z, x, y in batch]
                    for future in as_completed(futures):
                        z, x, tms_y, data = future.result()
                        processed += 1
                        if data is not None:
                            cursor.execute("INSERT OR REPLACE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?, ?, ?, ?)", (z, x, tms_y, data))
                            success += 1
                        else:
                            if (z, x, tms_y) in existing_tiles:
                                success += 1
                            else:
                                failed += 1
                        progress.update(task, advance=1, success=success, failed=failed)
                        if processed % 100 == 0:
                            conn.commit()
        conn.commit()
        conn.close()
        elapsed = time.time() - start_time
        file_size = os.path.getsize(output_file) / (1024 * 1024)
        table = Table(title="Raster Tile Download Complete", box=box.ROUNDED)
        table.add_column("Metric", style="cyan")
        table.add_column("Value", style="magenta", justify="right")
        table.add_row("File", output_file)
        table.add_row("Size", f"{file_size:.1f} MB")
        table.add_row("Time", f"{elapsed:.0f} seconds")
        table.add_row("Tiles", f"{success:,} successful, {failed:,} failed")
        if processed > 0:
            table.add_row("Success rate", f"{(success / processed) * 100:.1f}%")
        console.print(table)

    summary_parts = []
    if args.output:
        summary_parts.append(f"Raster tiles: {args.output}")
    if args.extract_pois:
        summary_parts.append(f"POI database: {args.poi_db}")
    if args.download_osm:
        summary_parts.append(f"OSM data: downloaded ({'clipped to bbox' if not args.no_clip_osm else 'full extract'})")
    if routing_bin_file:
        summary_parts.append(f"Routing graph: {routing_bin_file}")

    console.print(Panel.fit(
        "[bold green]Processing Complete![/bold green]\n" + "\n".join(summary_parts),
        title="Summary",
        border_style="green"
    ))

if __name__ == "__main__":
    main()