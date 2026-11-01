"""Read-only loopback wallet adapter. Existing Xbox bridge remains untouched."""
import json
import socketserver
import struct
import threading
import time
from live_marketplace import Marketplace

lock = threading.Lock()
client = None
cached = None
updated = 0

def wallet():
    global client, cached, updated
    with lock:
        if cached is not None and time.monotonic()-updated < 60:
            return cached
        if client is None:
            client = Marketplace()
        try:
            inv = client.inventory()
        except Exception:
            client = None
            raise
        coins = [x for x in inv['virtualCurrencyBalances'] if x['type']=='Minecoin' and x['currencyStack']=='Total']
        if len(coins) != 1:
            raise ValueError('Expected one real Minecoin balance')
        # Translate only server-confirmed permanent ownership. Do not manufacture
        # legacy signed receipts or turn subscriptions into perpetual licenses.
        items = [{'type':x['packId'],'productId':x['offerIds'][0],
                  'quantity':x['amount'],'friendlyName':'','purchaseReceipt':'','creatorId':''}
                 for x in inv['entitlements'] if x.get('ownership')=='Purchased'
                 and x.get('packId') and x.get('offerIds') and x.get('type')=='Item']
        cached = json.dumps({'Balance':{'currencies':[{'currencyType':'xmc','amount':coins[0]['amount']}]},
                            'EntitlementReceipt':'','Inventory':{'items':items}},separators=(',',':')).encode()
        updated = time.monotonic()
        print('Refreshed real balance',coins[0]['amount'],'permanent item entries',len(items),flush=True)
        return cached

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        if self.request.recv(4) != b'WAL1':
            return
        try:
            data=wallet()
            self.request.sendall(struct.pack('<I',len(data))+data)
        except Exception as e:
            print('Wallet unavailable',type(e).__name__,flush=True)
            self.request.sendall(struct.pack('<I',0))

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

if __name__=='__main__':
    wallet()
    with Server(('127.0.0.1',32121), Handler) as server:
        print('Read-only wallet adapter ready on 32121',flush=True)
        server.serve_forever()
