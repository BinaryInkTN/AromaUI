#!/usr/bin/env python3
import subprocess
import json
import time
import os
import sys
import argparse
import signal
import statistics
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple, Optional
import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass, asdict
import re
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

DEFAULT_NUM_TRIALS = 100
DEFAULT_QT_BINARY = "./stress_test_qt"
DEFAULT_AROMA_BINARY = "./build/qt_comparison_example"
DEFAULT_TIMEOUT_SECONDS = 120
MEMORY_SAMPLE_INTERVAL = 0.1
WARMUP_TRIALS = 3

PERF_EVENTS = [
    "cpu-cycles",
    "instructions",
    "cache-misses",
    "cache-references",
    "branch-misses",
    "branches",
    "task-clock",
    "context-switches",
    "cpu-migrations",
    "page-faults",
]

@dataclass
class TrialMetrics:
    trial_id: int
    binary: str
    wall_time_ms: float = 0.0
    cpu_time_user_ms: float = 0.0
    cpu_time_sys_ms: float = 0.0
    exit_code: int = -1
    uss_kb: Optional[float] = None
    pss_kb: Optional[float] = None
    rss_kb: Optional[float] = None
    cpu_cycles: Optional[float] = None
    instructions: Optional[float] = None
    cache_misses: Optional[float] = None
    cache_references: Optional[float] = None
    branch_misses: Optional[float] = None
    branches: Optional[float] = None
    task_clock_ms: Optional[float] = None
    context_switches: Optional[float] = None
    cpu_migrations: Optional[float] = None
    page_faults: Optional[float] = None
    ipc: Optional[float] = None
    cache_miss_rate: Optional[float] = None
    branch_miss_rate: Optional[float] = None
    fps_average: Optional[float] = None
    fps_min: Optional[float] = None
    fps_max: Optional[float] = None
    fps_windowed_average: Optional[float] = None
    total_frames: Optional[int] = None
    cpu_time_total_ms: Optional[float] = None
    cpu_utilization_pct: Optional[float] = None
    stdout_raw: Optional[str] = None
    stderr_raw: Optional[str] = None

def check_prerequisites(qt_binary: str, aroma_binary: str) -> bool:
    missing = []
    for name, path in [("Qt binary", qt_binary), ("Aroma binary", aroma_binary)]:
        if not os.path.isfile(path):
            missing.append(f"{name} not found at: {path}")
        elif not os.access(path, os.X_OK):
            missing.append(f"{name} is not executable: {path}")
    for tool in ["smem", "perf", "/usr/bin/time"]:
        if subprocess.run(["which", tool], capture_output=True).returncode != 0:
            missing.append(f"'{tool}' not found in PATH")
    if not os.environ.get('DISPLAY'):
        missing.append("DISPLAY environment variable not set")
    if missing:
        for m in missing:
            print(f"- {m}", file=sys.stderr)
        return False
    return True

def get_pids_in_pgroup(pgid: int) -> List[int]:
    try:
        out = subprocess.check_output(["ps", "-eo", "pid,pgid"], text=True)
        pids = []
        for line in out.splitlines()[1:]:
            parts = line.split()
            if len(parts) == 2 and int(parts[1]) == pgid:
                pids.append(int(parts[0]))
        return pids
    except Exception:
        return []

def get_smem_by_pids(pids: List[int]) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    if not pids:
        return None, None, None
    try:
        result = subprocess.run(
            ["smem", "-c", "pid uss pss rss", "--no-header"],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0 and result.stdout.strip():
            total_uss, total_pss, total_rss = 0.0, 0.0, 0.0
            found = False
            for line in result.stdout.strip().split("\n"):
                parts = line.strip().split()
                if len(parts) >= 4:
                    try:
                        pid = int(parts[0])
                        if pid in pids:
                            total_uss += float(parts[1])
                            total_pss += float(parts[2])
                            total_rss += float(parts[3])
                            found = True
                    except ValueError:
                        continue
            if found:
                return total_uss, total_pss, total_rss
    except Exception:
        pass
    return None, None, None

def sample_memory_during_run(pgid: int) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    max_uss, max_pss, max_rss = 0.0, 0.0, 0.0
    process_alive = True
    while process_alive:
        pids = get_pids_in_pgroup(pgid)
        if not pids:
            process_alive = False
            time.sleep(0.5)
            uss, pss, rss = get_smem_by_pids(get_pids_in_pgroup(pgid))
            if uss and uss > max_uss: max_uss = uss
            if pss and pss > max_pss: max_pss = pss
            if rss and rss > max_rss: max_rss = rss
            break
        uss, pss, rss = get_smem_by_pids(pids)
        if uss and uss > max_uss: max_uss = uss
        if pss and pss > max_pss: max_pss = pss
        if rss and rss > max_rss: max_rss = rss
        time.sleep(MEMORY_SAMPLE_INTERVAL)
    return (max_uss if max_uss > 0 else None,
            max_pss if max_pss > 0 else None,
            max_rss if max_rss > 0 else None)

def parse_perf_stat(output: str) -> Dict[str, float]:
    metrics = {}
    patterns = {
        "cpu-cycles": [r"([\d,]+(?:\.\d+)?)\s+cpu[-_]?[a-z]*/cpu-cycles/", r"([\d,]+(?:\.\d+)?)\s+cpu-cycles"],
        "instructions": [r"([\d,]+(?:\.\d+)?)\s+cpu[-_]?[a-z]*/instructions/", r"([\d,]+(?:\.\d+)?)\s+instructions"],
        "cache-misses": r"([\d,]+(?:\.\d+)?)\s+cache-misses",
        "cache-references": r"([\d,]+(?:\.\d+)?)\s+cache-references",
        "branch-misses": r"([\d,]+(?:\.\d+)?)\s+branch-misses",
        "branches": r"([\d,]+(?:\.\d+)?)\s+branches",
        "task-clock": r"([\d,]+(?:\.\d+)?)\s+msec\s+task-clock",
        "context-switches": r"([\d,]+(?:\.\d+)?)\s+context-switches",
        "cpu-migrations": r"([\d,]+(?:\.\d+)?)\s+cpu-migrations",
        "page-faults": r"([\d,]+(?:\.\d+)?)\s+page-faults",
    }
    for key, pattern in patterns.items():
        if isinstance(pattern, list):
            total = 0.0
            found = False
            for p in pattern:
                matches = re.findall(p, output)
                if matches:
                    for m in matches:
                        total += float(m.replace(",", ""))
                    found = True
            if found:
                metrics[key] = total
        else:
            matches = re.findall(pattern, output)
            if matches:
                total = sum(float(m.replace(",", "")) for m in matches)
                metrics[key] = total
    if "cpu-cycles" not in metrics:
        elapsed_match = re.search(r"([\d,.]+)\s+seconds time elapsed", output)
        if elapsed_match:
            metrics["duration_sec"] = float(elapsed_match.group(1).replace(",", ""))
    if "cpu-cycles" in metrics and "instructions" in metrics:
        if metrics["cpu-cycles"] > 0:
            metrics["ipc"] = metrics["instructions"] / metrics["cpu-cycles"]
    if "cache-misses" in metrics and "cache-references" in metrics:
        if metrics["cache-references"] > 0:
            metrics["cache-miss-rate"] = (metrics["cache-misses"] / metrics["cache-references"] * 100)
    if "branch-misses" in metrics and "branches" in metrics:
        if metrics["branches"] > 0:
            metrics["branch-miss-rate"] = (metrics["branch-misses"] / metrics["branches"] * 100)
    return metrics

def extract_fps_from_output(stdout: str) -> Dict[str, float]:
    fps_data = {}
    patterns = {
        "fps_average": [r"Average FPS:\s*(\d+\.?\d*)", r"avg[_\s]*fps:\s*(\d+\.?\d*)", r"FPS:\s*(\d+\.?\d*)", r"fps_average[=:]\s*(\d+\.?\d*)", r"Average frame rate:\s*(\d+\.?\d*)"],
        "fps_windowed_average": [r"Windowed average FPS:\s*(\d+\.?\d*)", r"windowed[_\s]*fps:\s*(\d+\.?\d*)"],
        "fps_min": [r"Windowed min FPS:\s*(\d+\.?\d*)", r"min[_\s]*fps:\s*(\d+\.?\d*)", r"Minimum FPS:\s*(\d+\.?\d*)"],
        "fps_max": [r"Windowed max FPS:\s*(\d+\.?\d*)", r"max[_\s]*fps:\s*(\d+\.?\d*)", r"Maximum FPS:\s*(\d+\.?\d*)"],
        "total_frames": [r"Total frames:\s*(\d+)", r"frames:\s*(\d+)", r"frame count:\s*(\d+)"],
    }
    for key, patterns_list in patterns.items():
        for pattern in patterns_list:
            match = re.search(pattern, stdout, re.IGNORECASE)
            if match:
                fps_data[key] = float(match.group(1))
                break
    return fps_data

def run_trial_with_timing(args_tuple: Tuple[int, str, str, int], skip_perf: bool = False, warmup: bool = False) -> Optional[TrialMetrics]:
    trial_id, binary_path, binary_label, timeout = args_tuple
    metrics = TrialMetrics(trial_id=trial_id, binary=binary_label)
    if not os.path.isfile(binary_path) or not os.access(binary_path, os.X_OK):
        return None
    env = os.environ.copy()
    if 'DISPLAY' not in env:
        env['DISPLAY'] = ':0'

    time_output_file = f"/tmp/benchmark_time_{trial_id}_{binary_label}.txt"
    perf_output_file = f"/tmp/benchmark_perf_{trial_id}_{binary_label}.txt"

    if not skip_perf:
        cmd = ["perf", "stat", "-o", perf_output_file, "-e", ",".join(PERF_EVENTS), "--", "/usr/bin/time", "-v", "-o", time_output_file, binary_path]
    else:
        cmd = ["/usr/bin/time", "-v", "-o", time_output_file, binary_path]

    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
            preexec_fn=os.setsid if hasattr(os, 'setsid') else None
        )
        pgid = os.getpgid(process.pid)
        memory_results = [None, None, None]
        
        def memory_sampler():
            memory_results[0], memory_results[1], memory_results[2] = sample_memory_during_run(pgid)

        memory_thread = threading.Thread(target=memory_sampler, daemon=True)
        memory_thread.start()

        stdout, stderr = "", ""
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            if hasattr(os, 'killpg'):
                os.killpg(pgid, signal.SIGTERM)
            else:
                process.kill()
            try:
                stdout, stderr = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                if hasattr(os, 'killpg'):
                    os.killpg(pgid, signal.SIGKILL)
                else:
                    process.kill()
                stdout, stderr = process.communicate()
        
        metrics.stdout_raw = stdout[:1000] if stdout else ""
        metrics.stderr_raw = stderr[:1000] if stderr else ""
        memory_thread.join(timeout=5)
        
        metrics.uss_kb = memory_results[0]
        metrics.pss_kb = memory_results[1]
        metrics.rss_kb = memory_results[2]
        metrics.exit_code = process.returncode

        if not warmup:
            try:
                if os.path.exists(time_output_file):
                    with open(time_output_file, "r") as f:
                        time_output = f.read()
                    wall_match = re.search(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([\d:.]+)", time_output)
                    if wall_match:
                        time_str = wall_match.group(1)
                        parts = time_str.split(":")
                        if len(parts) == 2:
                            seconds = float(parts[0]) * 60 + float(parts[1])
                        elif len(parts) == 3:
                            seconds = float(parts[0]) * 3600 + float(parts[1]) * 60 + float(parts[2])
                        else:
                            seconds = float(time_str)
                        metrics.wall_time_ms = seconds * 1000
                    user_match = re.search(r"User time \(seconds\): ([\d.]+)", time_output)
                    if user_match:
                        metrics.cpu_time_user_ms = float(user_match.group(1)) * 1000
                    sys_match = re.search(r"System time \(seconds\): ([\d.]+)", time_output)
                    if sys_match:
                        metrics.cpu_time_sys_ms = float(sys_match.group(1)) * 1000
                    cpu_pct_match = re.search(r"Percent of CPU this job got: ([\d.]+)%", time_output)
                    if cpu_pct_match:
                        metrics.cpu_utilization_pct = float(cpu_pct_match.group(1))
                    vol_match = re.search(r"Voluntary context switches: ([\d]+)", time_output)
                    if vol_match:
                        metrics.context_switches = float(vol_match.group(1))
                    invol_match = re.search(r"Involuntary context switches: ([\d]+)", time_output)
                    if invol_match and metrics.context_switches:
                        metrics.context_switches += float(invol_match.group(1))
                    if metrics.cpu_time_user_ms is not None and metrics.cpu_time_sys_ms is not None:
                        metrics.cpu_time_total_ms = metrics.cpu_time_user_ms + metrics.cpu_time_sys_ms
                        if metrics.wall_time_ms > 0 and metrics.cpu_utilization_pct is None:
                            metrics.cpu_utilization_pct = (metrics.cpu_time_total_ms / metrics.wall_time_ms) * 100
            except Exception:
                pass

            if not skip_perf and os.path.exists(perf_output_file):
                try:
                    with open(perf_output_file, "r") as f:
                        perf_output = f.read()
                    perf_metrics = parse_perf_stat(perf_output)
                    for k, v in perf_metrics.items():
                        setattr(metrics, k, v)
                except Exception:
                    pass

            if stdout:
                fps_data = extract_fps_from_output(stdout)
                for key, value in fps_data.items():
                    setattr(metrics, key, value)

        if os.path.exists(time_output_file): os.unlink(time_output_file)
        if os.path.exists(perf_output_file): os.unlink(perf_output_file)

        return metrics
    except Exception:
        return None

def run_all_trials(qt_binary: str, aroma_binary: str, num_trials: int = DEFAULT_NUM_TRIALS, timeout: int = DEFAULT_TIMEOUT_SECONDS, skip_perf: bool = False, save_dir: Optional[Path] = None) -> Dict[str, List[TrialMetrics]]:
    results = {"Qt": [], "Aroma": []}
    for label, binary in [("Qt", qt_binary), ("Aroma", aroma_binary)]:
        for i in range(WARMUP_TRIALS):
            run_trial_with_timing((-1, binary, label, timeout), skip_perf=skip_perf, warmup=True)
    for i in range(num_trials):
        qt_metrics = run_trial_with_timing((i, qt_binary, "Qt", timeout), skip_perf=skip_perf)
        if qt_metrics:
            results["Qt"].append(qt_metrics)
        aroma_metrics = run_trial_with_timing((i, aroma_binary, "Aroma", timeout), skip_perf=skip_perf)
        if aroma_metrics:
            results["Aroma"].append(aroma_metrics)
    for label in results:
        results[label].sort(key=lambda m: m.trial_id)
    return results

def compute_summary(all_results: Dict[str, List[TrialMetrics]]) -> Dict[str, Dict]:
    summary = {}
    numeric_fields = [
        "wall_time_ms", "cpu_time_user_ms", "cpu_time_sys_ms", "cpu_time_total_ms",
        "uss_kb", "pss_kb", "rss_kb",
        "cpu_cycles", "instructions", "cache_misses", "cache_references",
        "branch_misses", "branches", "task_clock_ms",
        "context_switches", "cpu_migrations", "page_faults",
        "ipc", "cache_miss_rate", "branch_miss_rate", "cpu_utilization_pct",
        "fps_average", "fps_windowed_average", "fps_min", "fps_max",
        "total_frames",
    ]
    for label, trials in all_results.items():
        summary[label] = {}
        for field in numeric_fields:
            values = [getattr(t, field) for t in trials if hasattr(t, field) and getattr(t, field) is not None]
            if values:
                try:
                    summary[label][field] = {
                        "mean": statistics.mean(values),
                        "median": statistics.median(values),
                        "stdev": statistics.stdev(values) if len(values) > 1 else 0,
                        "min": min(values),
                        "max": max(values),
                        "p95": float(np.percentile(values, 95)),
                        "count": len(values),
                        "values": values,
                    }
                except Exception:
                    pass
    return summary

def setup_plot_style():
    plt.style.use('default')
    plt.rcParams.update({
        'figure.figsize': (10, 6),
        'figure.dpi': 100,
        'font.size': 13,
        'axes.titlesize': 16,
        'axes.labelsize': 14,
        'legend.fontsize': 13,
        'xtick.labelsize': 12,
        'ytick.labelsize': 12,
    })

def plot_bar_comparison(all_results: Dict[str, List[TrialMetrics]], metric: str, title: str, ylabel: str, save_path: Path, higher_better: bool = False):
    setup_plot_style()
    fig, ax = plt.subplots(figsize=(10, 7))
    means, stds, labels, all_values = [], [], [], []
    for label, color in [("Qt", '#2196F3'), ("Aroma", '#4CAF50')]:
        values = [getattr(t, metric) for t in all_results[label] if hasattr(t, metric) and getattr(t, metric) is not None]
        if values:
            means.append(statistics.mean(values))
            stds.append(statistics.stdev(values) if len(values) > 1 else 0)
            labels.append(label)
            all_values.append(values)
    if not labels or len(means) != 2:
        plt.close()
        return
    x = np.arange(len(labels))
    bars = ax.bar(x, means, yerr=stds, capsize=15, color=['#2196F3', '#4CAF50'], alpha=0.85, width=0.5, edgecolor='black', linewidth=1.5)
    for i, (values_list, bar) in enumerate(zip(all_values, bars)):
        jitter = np.random.normal(0, 0.05, len(values_list))
        ax.scatter(np.full_like(values_list, i) + jitter, values_list, alpha=0.3, color='black', s=20, zorder=3)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontweight='bold')
    ax.set_title(title, fontweight='bold', pad=20)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3, axis='y')
    if max(means) > 1000:
        ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:,.0f}'))
    else:
        ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:.2f}'))
    for bar, mean, std in zip(bars, means, stds):
        height = bar.get_height()
        offset = max(means) * 0.02 if max(means) > 0 else 1
        ax.text(bar.get_x() + bar.get_width()/2., height + std + offset, f'{mean:,.1f}\n±{std:,.1f}', ha='center', va='bottom', fontsize=11, fontweight='bold')
    if means[0] != means[1] and means[0] > 0 and means[1] > 0:
        diff_pct = abs(means[0] - means[1]) / max(means[0], means[1]) * 100
        winner = "Aroma" if ((higher_better and means[1] > means[0]) or (not higher_better and means[1] < means[0])) else "Qt"
        winner_color = '#4CAF50' if winner == "Aroma" else '#2196F3'
        ax.text(0.5, 0.95, f'{winner} better by {diff_pct:.1f}%', transform=ax.transAxes, ha='center', va='top', fontsize=13, fontweight='bold', color=winner_color, bbox=dict(boxstyle='round,pad=0.5', facecolor='white', alpha=0.8))
    plt.tight_layout()
    plt.savefig(save_path, bbox_inches='tight', dpi=150)
    plt.close()

def generate_all_plots(all_results: Dict[str, List[TrialMetrics]], save_dir: Path):
    has_perf = any(hasattr(t, "cpu_cycles") and getattr(t, "cpu_cycles") is not None for trials in all_results.values() for t in trials)
    has_fps = any(hasattr(t, "fps_average") and getattr(t, "fps_average") is not None for trials in all_results.values() for t in trials)
    has_memory = any(hasattr(t, "uss_kb") and getattr(t, "uss_kb") is not None for trials in all_results.values() for t in trials)
    plot_configs = [
        ("wall_time_ms", "Wall Clock Time", "ms", False),
        ("cpu_time_user_ms", "CPU User Time", "ms", False),
        ("cpu_time_sys_ms", "CPU System Time", "ms", False),
        ("cpu_utilization_pct", "CPU Utilization", "%", True),
    ]
    if has_memory:
        plot_configs.extend([
            ("uss_kb", "USS Memory (Unique Set Size)", "KB", False),
            ("pss_kb", "PSS Memory (Proportional Set Size)", "KB", False),
            ("rss_kb", "RSS Memory (Resident Set Size)", "KB", False),
        ])
    if has_perf:
        plot_configs.extend([
            ("cpu_cycles", "CPU Cycles", "count", False),
            ("instructions", "Instructions Executed", "count", False),
            ("ipc", "Instructions Per Cycle", "IPC", True),
            ("cache_misses", "Cache Misses", "count", False),
            ("cache_miss_rate", "Cache Miss Rate", "%", False),
            ("branch_misses", "Branch Misses", "count", False),
            ("branch_miss_rate", "Branch Miss Rate", "%", False),
            ("context_switches", "Context Switches", "count", False),
            ("cpu_migrations", "CPU Migrations", "count", False),
            ("page_faults", "Page Faults", "count", False),
            ("task_clock_ms", "Task Clock", "ms", False),
        ])
    if has_fps:
        plot_configs.extend([
            ("fps_average", "Average FPS", "FPS", True),
            ("fps_min", "Minimum FPS", "FPS", True),
            ("fps_max", "Maximum FPS", "FPS", True),
            ("fps_windowed_average", "Windowed Average FPS", "FPS", True),
            ("total_frames", "Total Frames", "count", True),
        ])
    for metric, title, unit, higher_better in plot_configs:
        save_path = save_dir / f"bar_{metric}.png"
        try:
            plot_bar_comparison(all_results, metric, f"{title}", unit, save_path, higher_better)
        except Exception:
            pass
    try:
        plot_combined_figure(all_results, save_dir, has_perf, has_fps, has_memory)
    except Exception:
        pass

def plot_combined_figure(all_results: Dict[str, List[TrialMetrics]], save_dir: Path, has_perf: bool, has_fps: bool, has_memory: bool):
    setup_plot_style()
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    subplots = [(axes[0, 0], "wall_time_ms", "Wall Clock Time (ms)", False), (axes[0, 1], "cpu_time_user_ms", "CPU User Time (ms)", False)]
    if has_memory:
        subplots.extend([(axes[1, 0], "uss_kb", "USS Memory (KB)", False), (axes[1, 1], "pss_kb", "PSS Memory (KB)", False)])
    else:
        subplots.extend([(axes[1, 0], "cpu_time_sys_ms", "CPU System Time (ms)", False), (axes[1, 1], "cpu_utilization_pct", "CPU Utilization (%)", True)])
    for ax, metric, title, higher_better in subplots:
        means, stds, labels = [], [], []
        for label, color in [("Qt", '#2196F3'), ("Aroma", '#4CAF50')]:
            values = [getattr(t, metric) for t in all_results[label] if hasattr(t, metric) and getattr(t, metric) is not None]
            if values:
                means.append(statistics.mean(values))
                stds.append(statistics.stdev(values) if len(values) > 1 else 0)
                labels.append(label)
        if len(means) == 2:
            x = np.arange(len(labels))
            bars = ax.bar(x, means, yerr=stds, capsize=12, color=['#2196F3', '#4CAF50'], alpha=0.85, width=0.5, edgecolor='black', linewidth=1.5)
            ax.set_xticks(x)
            ax.set_xticklabels(labels, fontweight='bold')
            ax.set_title(title, fontweight='bold', pad=15)
            ax.grid(True, alpha=0.3, axis='y')
            if max(means) > 1000:
                ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:,.0f}'))
            for bar, mean, std in zip(bars, means, stds):
                height = bar.get_height()
                offset = max(means) * 0.02 if max(means) > 0 else 1
                ax.text(bar.get_x() + bar.get_width()/2., height + std + offset, f'{mean:,.1f}\n±{std:,.1f}', ha='center', va='bottom', fontsize=9, fontweight='bold')
            if means[0] != means[1] and means[0] > 0 and means[1] > 0:
                diff_pct = abs(means[0] - means[1]) / max(means[0], means[1]) * 100
                winner = "Aroma" if ((higher_better and means[1] > means[0]) or (not higher_better and means[1] < means[0])) else "Qt"
                winner_color = '#4CAF50' if winner == "Aroma" else '#2196F3'
                ax.text(0.5, 0.95, f'{winner} better by {diff_pct:.1f}%', transform=ax.transAxes, ha='center', va='top', fontsize=10, fontweight='bold', color=winner_color, bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8))
    plt.suptitle("Qt vs Aroma UI - Timing and Memory Comparison", fontsize=18, fontweight='bold', y=1.02)
    plt.tight_layout()
    plt.savefig(save_dir / "combined_timing_memory.png", bbox_inches='tight', dpi=150)
    plt.close()

    if has_perf:
        fig, axes = plt.subplots(2, 3, figsize=(18, 12))
        perf_subplots = [
            (axes[0, 0], "cpu_cycles", "CPU Cycles", False),
            (axes[0, 1], "instructions", "Instructions", False),
            (axes[0, 2], "ipc", "Instructions Per Cycle", True),
            (axes[1, 0], "cache_miss_rate", "Cache Miss Rate (%)", False),
            (axes[1, 1], "branch_miss_rate", "Branch Miss Rate (%)", False),
            (axes[1, 2], "context_switches", "Context Switches", False),
        ]
        for ax, metric, title, higher_better in perf_subplots:
            means, stds, labels = [], [], []
            for label, color in [("Qt", '#2196F3'), ("Aroma", '#4CAF50')]:
                values = [getattr(t, metric) for t in all_results[label] if hasattr(t, metric) and getattr(t, metric) is not None]
                if values:
                    means.append(statistics.mean(values))
                    stds.append(statistics.stdev(values) if len(values) > 1 else 0)
                    labels.append(label)
            if len(means) == 2:
                x = np.arange(len(labels))
                bars = ax.bar(x, means, yerr=stds, capsize=12, color=['#2196F3', '#4CAF50'], alpha=0.85, width=0.5, edgecolor='black', linewidth=1.5)
                ax.set_xticks(x)
                ax.set_xticklabels(labels, fontweight='bold')
                ax.set_title(title, fontweight='bold', pad=15)
                ax.grid(True, alpha=0.3, axis='y')
                if max(means) > 1000:
                    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:,.0f}'))
                if means[0] != means[1] and means[0] > 0 and means[1] > 0:
                    diff_pct = abs(means[0] - means[1]) / max(means[0], means[1]) * 100
                    winner = "Aroma" if ((higher_better and means[1] > means[0]) or (not higher_better and means[1] < means[0])) else "Qt"
                    winner_color = '#4CAF50' if winner == "Aroma" else '#2196F3'
                    ax.text(0.5, 0.95, f'{winner} better by {diff_pct:.1f}%', transform=ax.transAxes, ha='center', va='top', fontsize=9, fontweight='bold', color=winner_color, bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8))
        plt.suptitle("Qt vs Aroma UI - CPU Performance Comparison", fontsize=18, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(save_dir / "combined_perf_metrics.png", bbox_inches='tight', dpi=150)
        plt.close()

    if has_fps:
        fig, axes = plt.subplots(1, 2, figsize=(14, 6))
        fps_subplots = [(axes[0], "fps_average", "Average FPS", True), (axes[1], "total_frames", "Total Frames", True)]
        for ax, metric, title, higher_better in fps_subplots:
            means, stds, labels = [], [], []
            for label, color in [("Qt", '#2196F3'), ("Aroma", '#4CAF50')]:
                values = [getattr(t, metric) for t in all_results[label] if hasattr(t, metric) and getattr(t, metric) is not None]
                if values:
                    means.append(statistics.mean(values))
                    stds.append(statistics.stdev(values) if len(values) > 1 else 0)
                    labels.append(label)
            if len(means) == 2:
                x = np.arange(len(labels))
                bars = ax.bar(x, means, yerr=stds, capsize=12, color=['#2196F3', '#4CAF50'], alpha=0.85, width=0.5, edgecolor='black', linewidth=1.5)
                ax.set_xticks(x)
                ax.set_xticklabels(labels, fontweight='bold')
                ax.set_title(title, fontweight='bold', pad=15)
                ax.grid(True, alpha=0.3, axis='y')
                if max(means) > 1000:
                    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x:,.0f}'))
                if means[0] != means[1] and means[0] > 0 and means[1] > 0:
                    diff_pct = abs(means[0] - means[1]) / max(means[0], means[1]) * 100
                    winner = "Aroma" if ((higher_better and means[1] > means[0]) or (not higher_better and means[1] < means[0])) else "Qt"
                    winner_color = '#4CAF50' if winner == "Aroma" else '#2196F3'
                    ax.text(0.5, 0.95, f'{winner} better by {diff_pct:.1f}%', transform=ax.transAxes, ha='center', va='top', fontsize=10, fontweight='bold', color=winner_color, bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8))
        plt.suptitle("Qt vs Aroma UI - FPS Comparison", fontsize=18, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(save_dir / "combined_fps.png", bbox_inches='tight', dpi=150)
        plt.close()

def generate_text_report(all_results: Dict[str, List[TrialMetrics]], summary: Dict[str, Dict], save_dir: Path):
    report = []
    report.append("=" * 70)
    report.append("Qt vs Aroma UI - Performance Benchmark Report")
    report.append("=" * 70)
    report.append(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report.append(f"Trials per binary: {len(all_results.get('Qt', []))}")
    report.append("")
    categories = {
        "Timing Metrics": [("wall_time_ms", "Wall Clock Time", "ms", False), ("cpu_time_user_ms", "CPU User Time", "ms", False), ("cpu_time_sys_ms", "CPU System Time", "ms", False), ("cpu_time_total_ms", "Total CPU Time", "ms", False), ("cpu_utilization_pct", "CPU Utilization", "%", True)],
        "Memory Metrics": [("uss_kb", "USS Memory (Unique Set Size)", "KB", False), ("pss_kb", "PSS Memory (Proportional Set Size)", "KB", False), ("rss_kb", "RSS Memory (Resident Set Size)", "KB", False)],
        "CPU Performance Counters": [("cpu_cycles", "CPU Cycles", "count", False), ("instructions", "Instructions", "count", False), ("ipc", "Instructions Per Cycle", "IPC", True), ("task_clock_ms", "Task Clock", "ms", False), ("context_switches", "Context Switches", "count", False), ("cpu_migrations", "CPU Migrations", "count", False), ("page_faults", "Page Faults", "count", False)],
        "Cache Performance": [("cache_misses", "Cache Misses", "count", False), ("cache_references", "Cache References", "count", False), ("cache_miss_rate", "Cache Miss Rate", "%", False)],
        "Branch Prediction": [("branches", "Branches", "count", False), ("branch_misses", "Branch Misses", "count", False), ("branch_miss_rate", "Branch Miss Rate", "%", False)],
        "FPS Performance": [("fps_average", "Average FPS", "FPS", True), ("fps_min", "Minimum FPS", "FPS", True), ("fps_max", "Maximum FPS", "FPS", True), ("fps_windowed_average", "Windowed Average FPS", "FPS", True), ("total_frames", "Total Frames", "count", True)],
    }
    for category, metrics in categories.items():
        has_data = False
        category_report = []
        for metric, name, unit, higher_better in metrics:
            if metric in summary.get("Qt", {}) and metric in summary.get("Aroma", {}):
                has_data = True
                qt = summary["Qt"][metric]
                ar = summary["Aroma"][metric]
                category_report.append(f"\n  {name} ({unit}):")
                category_report.append(f"    Qt:    {qt['mean']:,.2f} ± {qt['stdev']:,.2f} (min={qt['min']:,.2f}, max={qt['max']:,.2f})")
                category_report.append(f"    Aroma: {ar['mean']:,.2f} ± {ar['stdev']:,.2f} (min={ar['min']:,.2f}, max={ar['max']:,.2f})")
                if qt['mean'] > 0 and ar['mean'] > 0:
                    diff_pct = abs(qt['mean'] - ar['mean']) / max(qt['mean'], ar['mean']) * 100
                    winner = "Aroma" if ((higher_better and ar['mean'] > qt['mean']) or (not higher_better and ar['mean'] < qt['mean'])) else "Qt"
                    category_report.append(f"    → {winner} better by {diff_pct:.1f}%")
        if has_data:
            report.append(f"\n{'='*70}")
            report.append(f"{category}")
            report.append(f"{'='*70}")
            report.extend(category_report)
    report.append("\n" + "=" * 70)
    report.append("Statistical Analysis")
    report.append("=" * 70)
    report.append("")
    report.append("Performance differences (Aroma vs Qt):")
    improvements = []
    for metric, name, unit, higher_better in [("wall_time_ms", "Wall Clock Time", "ms", False), ("cpu_time_user_ms", "CPU User Time", "ms", False), ("uss_kb", "USS Memory", "KB", False), ("pss_kb", "PSS Memory", "KB", False), ("fps_average", "Average FPS", "FPS", True)]:
        if metric in summary.get("Qt", {}) and metric in summary.get("Aroma", {}):
            qt_mean = summary["Qt"][metric]["mean"]
            ar_mean = summary["Aroma"][metric]["mean"]
            if qt_mean > 0 and ar_mean > 0:
                if higher_better:
                    diff_pct = ((ar_mean - qt_mean) / qt_mean) * 100
                    improvements.append(f"  {name}: {'Aroma' if diff_pct > 0 else 'Qt'} is {abs(diff_pct):.1f}% better")
                else:
                    diff_pct = ((qt_mean - ar_mean) / qt_mean) * 100
                    improvements.append(f"  {name}: {'Aroma' if diff_pct > 0 else 'Qt'} is {abs(diff_pct):.1f}% better (lower is better)")
    if improvements:
        report.extend(improvements)
    else:
        report.append("  No significant differences found")
    report.append("\n" + "=" * 70)
    report.append("Raw Data Files")
    report.append("=" * 70)
    report.append(f"  JSON results: benchmark_results.json")
    report.append(f"  Summary stats: summary_statistics.json")
    report.append("\n" + "=" * 70)
    report_text = "\n".join(report)
    report_path = save_dir / "report.txt"
    report_path.write_text(report_text)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trials", type=int, default=DEFAULT_NUM_TRIALS)
    parser.add_argument("--qt", type=str, default=DEFAULT_QT_BINARY)
    parser.add_argument("--aroma", type=str, default=DEFAULT_AROMA_BINARY)
    parser.add_argument("--output", type=str, default=None)
    parser.add_argument("--no-perf", action="store_true")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    qt_binary = args.qt
    aroma_binary = args.aroma
    timeout = args.timeout
    num_trials = 3 if args.quick else args.trials
    output_dir = Path(args.output) if args.output else Path(f"benchmark_results_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
    check_prerequisites(qt_binary, aroma_binary)
    output_dir.mkdir(parents=True, exist_ok=True)
    results = run_all_trials(qt_binary, aroma_binary, num_trials=num_trials, timeout=timeout, skip_perf=args.no_perf, save_dir=output_dir)
    if not results["Qt"] and not results["Aroma"]:
        sys.exit(1)
    json_path = output_dir / "benchmark_results.json"
    serializable = {}
    for label, trials in results.items():
        serializable[label] = []
        for t in trials:
            d = asdict(t)
            if "stdout_raw" in d: d["stdout_raw"] = d["stdout_raw"][:500] if d["stdout_raw"] else ""
            if "stderr_raw" in d: d["stderr_raw"] = d["stderr_raw"][:500] if d["stderr_raw"] else ""
            serializable[label].append(d)
    json_path.write_text(json.dumps(serializable, indent=2))
    summary = compute_summary(results)
    summary_path = output_dir / "summary_statistics.json"
    summary_serializable = {}
    for label, metrics in summary.items():
        summary_serializable[label] = {}
        for metric, stats in metrics.items():
            summary_serializable[label][metric] = {k: v for k, v in stats.items() if k != "values"}
    summary_path.write_text(json.dumps(summary_serializable, indent=2))
    generate_all_plots(results, output_dir)
    generate_text_report(results, summary, output_dir)

if __name__ == "__main__":
    main()