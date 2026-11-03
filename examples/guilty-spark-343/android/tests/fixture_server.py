"""Loopback-only synthetic workspace for Android integration checks. No real user data."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import json

LOGIN = """<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Guilty Spark 343</title>
<body style='background:#0a111b;color:#edf6ff;font:18px sans-serif;padding:24px'><h1>Guilty Spark 343</h1><p>Integration test server</p>
<input id=code type=password aria-label='Access code'><button id=login>Connect</button><p id=error></p>
<script>document.querySelector('#login').onclick=async()=>{let r=await fetch('/auth',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({token:document.querySelector('#code').value})});if(r.ok)location.replace('/');else document.querySelector('#error').textContent='Access code was not accepted.'}</script>"""
MODELS = """<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Guilty Spark 343</title>
<body style='background:#0a111b;color:#edf6ff;font:18px sans-serif;padding:24px'><h1>Your models</h1><a id=model href='/m/fixture/chat.html' style='color:#6de3da'>Fixture model</a>"""
CHAT = """<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Guilty Spark 343</title>
<body style='background:#0a111b;color:#edf6ff;font:18px sans-serif;padding:24px'><h1>Fixture model</h1><p>Synthetic test conversation</p>
<textarea id=prompt aria-label='Message'></textarea><button id=send>Send</button><p id=reply></p>
<script>document.querySelector('#send').onclick=async()=>{let r=await fetch('/chat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:document.querySelector('#prompt').value})});document.querySelector('#reply').textContent=(await r.json()).reply;}</script>"""

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def respond(self, code, content, content_type='text/html', cookie=False):
        data = content.encode()
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        if cookie:
            self.send_header('Set-Cookie', 'fixture_session=accepted; HttpOnly; SameSite=Strict; Path=/')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        signed_in = 'fixture_session=accepted' in self.headers.get('Cookie', '')
        if self.path == '/unavailable':
            self.respond(503, 'Synthetic unavailable page')
        elif self.path == '/':
            self.respond(200, MODELS if signed_in else LOGIN)
        elif self.path == '/m/fixture/chat.html' and signed_in:
            self.respond(200, CHAT)
        else:
            self.respond(401, 'Sign in')

    def do_POST(self):
        size = int(self.headers.get('Content-Length', '0'))
        if size > 4096:
            return self.respond(413, '{}', 'application/json')
        data = json.loads(self.rfile.read(size) or '{}')
        if self.path == '/auth':
            accepted = data.get('token') == 'fixture-only'
            self.respond(200 if accepted else 401, '{}', 'application/json', accepted)
        elif self.path == '/chat' and 'fixture_session=accepted' in self.headers.get('Cookie', ''):
            self.respond(200, json.dumps({'reply': 'Synthetic reply received on Android.'}), 'application/json')
        else:
            self.respond(401, '{}', 'application/json')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=34344)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Synthetic Android fixture on loopback port {server.server_port}', flush=True)
    server.serve_forever()
