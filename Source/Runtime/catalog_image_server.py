"""Serve only known cached Marketplace artwork, normalized for the legacy decoder."""
import io
import json
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

import requests
from PIL import Image
from wallet_https_server import _modern_items

from runtime_paths import STATE as ROOT, PACK
CACHE = ROOT / 'catalog-image-cache'
CACHE.mkdir(exist_ok=True)

ids = {row['document']['productId'] for row in json.loads(
    (PACK / 'LegacyOffersCatalog.json').read_text(encoding='utf-8'))['results']}
items = {item['id']:item for item in _modern_items() if item['id'] in ids}

def cache_image(item):
    target = CACHE / (item['id'] + '.png')
    if target.exists():
        return True
    try:
        url = item.get('thumbnail', {}).get('url', '')
        if not url.startswith('https://'):
            return False
        response = requests.get(url, timeout=25)
        response.raise_for_status()
        with Image.open(io.BytesIO(response.content)) as source:
            source.convert('RGBA').save(target, 'PNG')
        return True
    except Exception as error:
        print('image unavailable', item['id'], type(error).__name__, flush=True)
        return False

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        item_id = parse_qs(urlsplit(self.path).query).get('id', [''])[0]
        path = CACHE / (item_id + '.png') if item_id in items else None
        if path and not path.exists():cache_image(items[item_id])
        if not path or not path.exists():
            self.send_error(404)
            return
        body = path.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', 'image/png')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'public, max-age=3600')
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *_):
        pass

if __name__ == '__main__':
    with ThreadPoolExecutor(max_workers=8) as pool:
        count = sum(pool.map(cache_image, items.values()))
    print(f'Cached {count}/{len(items)} artwork images; serving 127.0.0.1:32118', flush=True)
    ThreadingHTTPServer(('127.0.0.1',32118),Handler).serve_forever()
