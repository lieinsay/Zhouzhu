#!/usr/bin/env python3
"""舟渚内容工坊：Python 标准库服务，独立于 Godot 和 Node 运行时。"""
import argparse
import ipaddress
import json
import mimetypes
import os
import re
import signal
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

from studio.common import StudioError, js_number, loads, quote
from studio.media import MediaLibrary
from studio.art import ArtLibrary
from studio.models import ModelLibrary
from studio.store import WorkspaceStore
from studio.validation import validate_project

TOOL_ROOT = Path(__file__).resolve().parent
MAX_BODY_BYTES = 5 * 1024 * 1024
CSP = "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"
MIME = {'.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.ico': 'image/x-icon'}


def make_cpp_proposal(project, baseline):
    if any(row['severity'] == 'error' for row in validate_project(project, baseline)):
        raise StudioError(422, '草稿仍有错误，请先修复后导出 C++ 建议片段')
    def cpp_float(value):
        text = js_number(value)
        return text + ('.0' if '.' not in text and 'e' not in text else '') + 'f'
    items = '\n'.join('    { '+', '.join([quote(row['id']), quote(row['name']), js_number(row['stack']), *(cpp_float(n) for n in row['color'])])+' },' for row in project['items'])
    recipes = []
    for row in project['recipes']:
        inputs = ', '.join('{ '+quote(n['item'])+', '+js_number(n['count'])+' }' for n in row['ingredients'])
        recipes.append('    { '+', '.join([quote(row['id']), quote(row['product']), js_number(row['yield']), 'Station::'+row['station']])+', { '+inputs+' } },')
    return ('// 开发者人工审阅建议，工具未改写游戏。\n'
            '// 仅覆盖道具与配方表；其他集合请按 JSON 变更报告单独实现。\n'
            '// 需核对源 SHA-256、既有 enum/行为和资产，再自行应用、构建、验证。\n\n'
            '// src/crops.cpp · namespace catalog\nconst ItemDef ITEMS[] = {\n'+items+'\n};\n\n'
            '// src/recipes.cpp · namespace recipes\nconst RecipeDef RECIPES[] = {\n'+'\n'.join(recipes)+'\n};\n')


def parse_allowed_network(value):
    if value is None:
        return None
    try:
        if not isinstance(value, str) or not re.fullmatch(r'[^/]+/(?:\d|[12]\d|3[0-2])', value):
            raise ValueError()
        network = ipaddress.ip_network(value, strict=True)
        if network.version != 4:
            raise ValueError()
        return network
    except ValueError as error:
        raise ValueError('允许网段必须是规范的 IPv4 CIDR，例如 10.8.0.0/24') from error


def validate_network_address(value):
    try:
        address = ipaddress.ip_address(value)
        if address.version != 4 or address.is_loopback or address.is_unspecified or address.is_multicast or str(address) != value:
            raise ValueError()
        # 由操作系统确认是本机真实地址；不依赖 shell、网络插件或第三方包。
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind((value, 0))
    except (ValueError, TypeError, OSError) as error:
        raise ValueError('网络监听地址必须是本机已有的非回环 IPv4，不能使用通配地址或主机名') from error


class Listener(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = os.name != 'nt'
    request_queue_size = 64

    def server_bind(self):
        if os.name == 'nt':
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    server_version = 'ZhouzhuContentStudio/Python'

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def send_payload(self, status, value, headers=None, content_type='application/json; charset=utf-8'):
        if isinstance(value, bytes):
            data = value
        elif isinstance(value, str):
            data = value.encode('utf-8')
        else:
            data = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode('utf-8')
        self.send_response(status)
        response_headers = {'Content-Type': content_type, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Content-Length': str(len(data)), **(headers or {})}
        if status >= 400:
            self.close_connection = True
            response_headers['Connection'] = 'close'
        for name, item in response_headers.items():
            self.send_header(name, item)
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(data)

    def check_access(self):
        studio = self.server.studio
        local_address, port = self.connection.getsockname()[:2]
        if studio.allowed_network is not None and local_address == studio.network_address:
            if ipaddress.ip_address(self.client_address[0]) not in studio.allowed_network:
                raise StudioError(403, '客户端地址不在已允许的 VPN 网段')
        hosts = {f'127.0.0.1:{port}', f'localhost:{port}'}
        if studio.network_address:
            hosts.add(f'{studio.network_address}:{port}')
        if len(self.headers.get_all('Host', [])) != 1 or self.headers['Host'] not in hosts:
            raise StudioError(403, 'Host 必须是本机开发工具地址')
        if self.command in ('GET', 'HEAD'):
            return
        origin = self.headers.get('Origin')
        if origin and origin != 'http://' + self.headers['Host']:
            raise StudioError(403, '只允许同源页面写入草稿')
        if not origin and self.headers.get('Sec-Fetch-Site') not in (None, 'same-origin'):
            raise StudioError(403, '只允许同源页面写入草稿')

    def read_exact(self, size):
        data = self.rfile.read(size)
        if len(data) != size:
            raise StudioError(400, 'JSON 请求内容不完整')
        return data

    def read_body(self):
        if not re.match(r'^application/json(?:\s*;|$)', self.headers.get('Content-Type', ''), re.I):
            raise StudioError(415, '写入必须使用 application/json')
        lengths = self.headers.get_all('Content-Length', [])
        transfer = self.headers.get('Transfer-Encoding')
        if len(lengths) > 1 or (transfer and lengths):
            raise StudioError(400, '请求长度声明不正确')
        if transfer:
            if transfer.lower() != 'chunked':
                raise StudioError(400, '不支持此请求传输格式')
            pieces, total = [], 0
            while True:
                line = self.rfile.readline(1025)
                if len(line) > 1024 or not line.endswith(b'\r\n') or not re.fullmatch(b'[a-fA-F0-9]+', line[:-2].split(b';', 1)[0]):
                    raise StudioError(400, 'JSON 分块请求格式不正确')
                size = int(line[:-2].split(b';', 1)[0], 16)
                if total + size > MAX_BODY_BYTES:
                    raise StudioError(413, 'JSON 请求超过 5 MiB')
                if size == 0:
                    trailer_size = 0
                    while True:
                        trailer = self.rfile.readline(1025)
                        trailer_size += len(trailer)
                        if not trailer or not trailer.endswith(b'\r\n') or trailer_size > 8192:
                            raise StudioError(400, 'JSON 分块请求结尾不正确')
                        if trailer == b'\r\n':
                            break
                    break
                pieces.append(self.read_exact(size))
                if self.read_exact(2) != b'\r\n':
                    raise StudioError(400, 'JSON 分块请求格式不正确')
                total += size
            data = b''.join(pieces)
        else:
            length = self.headers.get('Content-Length', '0')
            if not re.fullmatch(r'\d+', length):
                raise StudioError(400, '请求长度声明不正确')
            size = int(length)
            if size > MAX_BODY_BYTES:
                raise StudioError(413, 'JSON 请求超过 5 MiB')
            data = self.read_exact(size)
        try:
            return loads(data.decode('utf-8'))
        except (ValueError, UnicodeDecodeError, RecursionError) as error:
            raise StudioError(400, 'JSON 请求格式不正确') from error

    def dispatch(self):
        self.check_access()
        studio = self.server.studio
        try:
            raw_path = urlsplit(self.path).path
            if re.search(r'%(?![a-fA-F0-9]{2})', raw_path):
                raise ValueError()
            path = unquote(raw_path, encoding='utf-8', errors='strict')
        except (ValueError, UnicodeDecodeError) as error:
            raise StudioError(400, 'URL 格式不正确') from error
        if '\\' in path or '\0' in path or ':' in path or '..' in path.split('/'):
            raise StudioError(403, '不允许访问目录外文件')
        if path.startswith('/api/'):
            if path=='/api/art/models' and self.command=='GET':return self.send_payload(200,studio.model_art.snapshot())
            if path in ['/api/art/models/stage','/api/art/models/apply'] and self.command=='POST':return self.send_payload(200,getattr(studio.model_art,path.rsplit('/',1)[1])(self.read_body()))
            for kind,method,content_type in [('image','image','image/png'),('model','model','application/json; charset=utf-8')]:
                prefix='/api/art/models/'+kind+'/'
                if path.startswith(prefix) and self.command=='GET':
                    parts=path[len(prefix):].split('/')
                    if len(parts)!=2:raise StudioError(404,'模型版本路径错误')
                    return self.send_payload(200,getattr(studio.model_art,method)(*parts),content_type=content_type)
            if path == '/api/art' and self.command == 'GET':
                return self.send_payload(200, studio.art.snapshot())
            if path.startswith('/api/art/image/') and self.command == 'GET':
                parts = path[len('/api/art/image/'):].split('/')
                if len(parts) != 2:
                    raise StudioError(404, '美术图片路径错误')
                return self.send_payload(200, studio.art.image(*parts), content_type='image/png')
            if path.startswith('/api/art/model/') and self.command == 'GET':
                parts=path[len('/api/art/model/'):].split('/')
                if len(parts)!=2:raise StudioError(404,'美术模型路径错误')
                return self.send_payload(200,studio.art.model(*parts),content_type='application/json; charset=utf-8')
            if path.startswith('/api/art/snapshot/') and self.command=='GET':
                return self.send_payload(200,studio.art.snapshot_model(path[len('/api/art/snapshot/'):]),content_type='application/json; charset=utf-8')
            if path == '/api/art/stage' and self.command == 'POST':
                return self.send_payload(200, studio.art.stage(self.read_body()))
            if path == '/api/art/apply' and self.command == 'POST':
                return self.send_payload(200, studio.art.apply(self.read_body()))
            if self.command == 'GET':
                if path.startswith('/api/models/'):
                    return self.send_payload(200, studio.media.read_model(path[len('/api/models/'):]))
                workspace = studio.store.get_workspace()
                if path == '/api/assets':
                    return self.send_payload(200, studio.media.get_assets(workspace['project']))
                if path == '/api/workspace':
                    return self.send_payload(200, workspace)
                if path == '/api/export':
                    return self.send_payload(200, json.dumps(workspace['project'], ensure_ascii=False, allow_nan=False, indent=2) + '\n', {'Content-Disposition': 'attachment; filename="zhouzhu-content-draft.json"'})
                if path == '/api/report':
                    return self.send_payload(200, dict(revision=workspace['revision'], sources=workspace['baseline']['meta']['sources'], issues=workspace['issues'], changes=workspace['changes']))
                if path == '/api/patch':
                    return self.send_payload(200, make_cpp_proposal(workspace['project'], workspace['baseline']), {'Content-Disposition': 'attachment; filename="zhouzhu-items-recipes-proposal.cpp.txt"'}, 'text/plain; charset=utf-8')
            if self.command == 'POST' and path == '/api/models':
                return self.send_payload(201, {'model': studio.media.upload(self.read_body())})
            if self.command == 'POST' and path in ('/api/workspace', '/api/import'):
                data = self.read_body()
                if not isinstance(data, dict) or set(data) - {'project', 'revision'}:
                    raise StudioError(400, '写入格式为 {project, revision}')
                return self.send_payload(200, studio.store.save_project(data.get('project'), data.get('revision')))
            if self.command == 'POST' and path == '/api/reset':
                data = self.read_body()
                if not isinstance(data, dict) or set(data) - {'revision'}:
                    raise StudioError(400, '重置格式为 {revision}')
                return self.send_payload(200, studio.store.reset_project(data.get('revision')))
            raise StudioError(404, 'API 路径或方法不存在')
        if self.command not in ('GET', 'HEAD'):
            raise StudioError(405, '静态资源仅支持读取')
        validation = path == '/lib/validation.mjs'
        root = (TOOL_ROOT / 'lib') if validation else studio.public_directory
        file = (root / ('validation.mjs' if validation else 'index.html' if path == '/' else path.lstrip('/'))).resolve()
        if not file.is_relative_to(root.resolve()):
            raise StudioError(403, '不允许通过链接访问目录外文件')
        if not file.is_file():
            raise StudioError(404, '静态文件不存在')
        cache = 'private, max-age=3600' if re.fullmatch(r'/assets/thumbnails/[a-zA-Z0-9_-]+-v\d+\.png', path) else 'no-store'
        return self.send_payload(200, file.read_bytes(), {'Cache-Control': cache, 'Content-Security-Policy': CSP}, MIME.get(file.suffix.lower(), mimetypes.guess_type(file.name)[0] or 'application/octet-stream'))

    def handle_request(self):
        try:
            self.dispatch()
        except (ConnectionError, BrokenPipeError):
            self.close_connection = True
        except (TimeoutError, socket.timeout):
            self.send_payload(408, {'error': '请求读取超时'})
        except Exception as error:
            self.send_payload(getattr(error, 'status', 500), {'error': str(error)})

    do_GET = do_HEAD = do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = handle_request

    def log_message(self, format, *args):
        if not self.server.studio.quiet:
            super().log_message(format, *args)


class StudioServer:
    def __init__(self, repository_root=TOOL_ROOT.parent.parent, workspace_directory=TOOL_ROOT / 'workspace', public_directory=TOOL_ROOT / 'public', network_address=None, allowed_network=None, quiet=False):
        self.allowed_network = parse_allowed_network(allowed_network)
        if self.allowed_network and not network_address:
            raise ValueError('允许网段需要同时指定网络监听地址')
        if network_address is not None:
            validate_network_address(network_address)
        self.network_address = network_address
        self.public_directory = Path(public_directory).resolve()
        self.art = ArtLibrary(repository_root, workspace_directory)
        self.model_art=ModelLibrary(repository_root,workspace_directory)
        self.media = MediaLibrary(self.public_directory, workspace_directory)
        self.media.art = self.art
        self.store = WorkspaceStore(repository_root, workspace_directory, self.media)
        self.quiet = quiet
        self.servers, self.threads = [], []
        self._close_lock = threading.Lock()

    def start(self, port=4176):
        try:
            if self.servers:
                raise ValueError('服务已经启动')
            for address in ['127.0.0.1', *([self.network_address] if self.network_address else [])]:
                server = Listener((address, port), Handler)
                server.studio = self
                port = server.server_address[1]
                self.servers.append(server)
            for server in self.servers:
                thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.1}, daemon=True)
                thread.start()
                self.threads.append(thread)
            return f'http://127.0.0.1:{port}'
        except Exception:
            self.close()
            raise

    def close(self):
        with self._close_lock:
            for index, server in enumerate(self.servers):
                if index < len(self.threads):
                    server.shutdown()
                server.server_close()
            for thread in self.threads:
                thread.join(timeout=3)
            self.servers, self.threads = [], []


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description='舟渚·内容工坊 Python 服务（标准库，无需 pip）')
    parser.add_argument('--root', type=Path, default=TOOL_ROOT.parent.parent)
    parser.add_argument('--workspace', type=Path, default=TOOL_ROOT / 'workspace')
    parser.add_argument('--port', type=int, default=4176)
    parser.add_argument('--network-address')
    parser.add_argument('--allow-network')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('端口必须为 1～65535 的整数')
    studio = None
    try:
        studio = StudioServer(args.root, args.workspace, network_address=args.network_address, allowed_network=args.allow_network)
        url = studio.start(args.port)
        print(f'Zhouzhu 内容工坊（Python）：{url}', flush=True)
        if args.network_address:
            print(f'网络入口：http://{args.network_address}:{args.port}', flush=True)
        if args.allow_network:
            print(f'允许客户端：{args.allow_network}', flush=True)
        print(f'草稿：{studio.store.file}\n源码只读，游戏不会加载此草稿。', flush=True)
        stop = threading.Event()
        for name in ('SIGINT', 'SIGTERM'):
            signal.signal(getattr(signal, name), lambda *_: stop.set())
        stop.wait()
    except KeyboardInterrupt:
        pass
    except (ValueError, OSError, StudioError) as error:
        parser.exit(1, f'{error}\n')
    finally:
        if studio:
            studio.close()


if __name__ == '__main__':
    main()
