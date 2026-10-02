#!/usr/bin/env python3
import http.server
import socketserver
import json
import os
import subprocess
import urllib.parse
import shutil

PORT = 8081
DIRECTORY = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.join(DIRECTORY, "workspace")
AROMA_CLI = os.path.abspath(os.path.join(DIRECTORY, "../../tools/cli/aroma.py"))

if not os.path.exists(WORKSPACE):
    os.makedirs(WORKSPACE)

class IDEHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def do_GET(self):
        parsed_path = urllib.parse.urlparse(self.path)
        if parsed_path.path == '/api/projects':
            self.handle_get_projects()
        else:
            super().do_GET()

    def do_POST(self):
        parsed_path = urllib.parse.urlparse(self.path)
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length) if content_length > 0 else b'{}'

        try:
            data = json.loads(post_data.decode('utf-8'))
        except json.JSONDecodeError:
            data = {}

        if parsed_path.path == '/api/project/create':
            self.handle_create_project(data)
        elif parsed_path.path == '/api/project/open':
            self.handle_open_project(data)
        elif parsed_path.path == '/api/project/save':
            self.handle_save_project(data)
        elif parsed_path.path == '/api/project/build':
            self.handle_build_project(data)
        elif parsed_path.path == '/api/project/run':
            self.handle_run_project(data)
        else:
            self.send_error(404, "Endpoint not found")

    def _send_json(self, response_data, status=200):
        self.send_response(status)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(response_data).encode('utf-8'))

    def handle_get_projects(self):
        projects = []
        for item in os.listdir(WORKSPACE):
            if os.path.isdir(os.path.join(WORKSPACE, item)):
                projects.append(item)
        self._send_json({"projects": projects})

    def handle_create_project(self, data):
        name = data.get('name')
        if not name:
            return self._send_json({"error": "Project name is required"}, 400)

        proj_dir = os.path.join(WORKSPACE, name)
        if os.path.exists(proj_dir):
            return self._send_json({"error": "Project already exists"}, 400)

        try:
            cmd = f'python3 "{AROMA_CLI}" create {name}'
            proc = subprocess.run(cmd, shell=True, cwd=WORKSPACE, input="\n" * 20, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if proc.returncode != 0:
                return self._send_json({"error": "Build failed", "log": proc.stdout}, 500)
            self._send_json({"success": True, "log": proc.stdout})
        except Exception as e:
            self._send_json({"error": str(e)}, 500)

    def handle_open_project(self, data):
        name = data.get('name')
        proj_dir = os.path.join(WORKSPACE, name)
        if not os.path.exists(proj_dir):
            return self._send_json({"error": "Project not found"}, 404)

        files_to_read = [
            "aroma.json",
            "src/main.c",
            "src/ui.aroma"
        ]

        result = {}
        for f in files_to_read:
            fpath = os.path.join(proj_dir, f)
            if os.path.exists(fpath):
                with open(fpath, 'r', encoding='utf-8') as file:
                    result[f] = file.read()
            else:
                result[f] = ""

        self._send_json({"files": result})

    def handle_save_project(self, data):
        name = data.get('name')
        files = data.get('files', {})
        proj_dir = os.path.join(WORKSPACE, name)

        if not os.path.exists(proj_dir):
            return self._send_json({"error": "Project not found"}, 404)

        try:
            for filepath, content in files.items():

                safe_path = os.path.abspath(os.path.join(proj_dir, filepath))
                if not safe_path.startswith(proj_dir):
                    continue
                os.makedirs(os.path.dirname(safe_path), exist_ok=True)
                with open(safe_path, 'w', encoding='utf-8') as file:
                    file.write(content)
            self._send_json({"success": True})
        except Exception as e:
            self._send_json({"error": str(e)}, 500)

    def handle_build_project(self, data):
        name = data.get('name')
        target = data.get('target', 'linux')
        proj_dir = os.path.join(WORKSPACE, name)

        if not os.path.exists(proj_dir):
            return self._send_json({"error": "Project not found"}, 404)

        try:
            cmd = f'python3 "{AROMA_CLI}" build {target}'
            proc = subprocess.Popen(cmd, shell=True, cwd=proj_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            out, _ = proc.communicate()
            if proc.returncode != 0:
                return self._send_json({"error": "Build failed", "log": out}, 500)
            self._send_json({"success": True, "log": out})
        except Exception as e:
            self._send_json({"error": str(e)}, 500)

    def handle_run_project(self, data):
        name = data.get('name')
        target = data.get('target', 'linux')
        proj_dir = os.path.join(WORKSPACE, name)

        if not os.path.exists(proj_dir):
            return self._send_json({"error": "Project not found"}, 404)

        try:
            cmd = f'python3 "{AROMA_CLI}" run {target}'

            subprocess.Popen(cmd, shell=True, cwd=proj_dir)
            self._send_json({"success": True, "log": f"Running native app on {target} natively..."})
        except Exception as e:
            self._send_json({"error": str(e)}, 500)

if __name__ == '__main__':
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), IDEHandler) as httpd:
        print(f"IDE Server running at http://localhost:{PORT}")
        httpd.serve_forever()
