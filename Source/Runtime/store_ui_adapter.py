"""Account-aware Store presentation and free-only claiming. Never buys paid offers."""
import io
import json
import re
import socketserver
import struct
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import requests
from PIL import Image
from live_marketplace import Marketplace, objects, ENTITLEMENTS, minecoin_balance
from probe_playfab_from_bridge import get_bridge_fields
session_gamertag = None
from wallet_https_server import _modern_items
modern = {x["id"]:x for x in _modern_items()}

from runtime_paths import STATE as ROOT, PACK
CACHE=ROOT/'store-gallery-cache'
CACHE.mkdir(exist_ok=True)
DOCS=ROOT/'store-detail-cache'
DOCS.mkdir(exist_ok=True)
catalog=json.loads((PACK/'LegacyOffersCatalog.json').read_text(encoding='utf-8-sig'))
docs={r['document']['productId']:r['document'] for r in catalog['results']}
client=None
inventory=None
updated=0
account_xuid=None
lock=threading.RLock()

def refresh(force=False):
    global client,inventory,updated,account_xuid,session_gamertag
    with lock:
        identity=get_bridge_fields()
        if identity[2]!=account_xuid:
            client=None;inventory=None;updated=0
            account_xuid=identity[2];session_gamertag=identity[3]
        if not force and inventory is not None and time.monotonic()-updated<60:return inventory
        if client is None:client=Marketplace()
        try: inventory=client.inventory()
        except Exception:
            client=None
            raise
        updated=time.monotonic()
        return inventory

def states():
    global session_gamertag
    inv=refresh()
    if session_gamertag is None: session_gamertag=get_bridge_fields()[3]
    owned={oid for e in inv['entitlements'] if e.get('ownership')=='Purchased' for oid in e.get('offerIds',[])}
    def status(oid):
        item=modern.get(oid,{})
        price=item.get('price',{})
        free=type(price.get('listPrice')) is int and price['listPrice']==0 and price.get('virtualCurrencyType')=='Minecoin'
        compatibility='I' if oid=='c8d1696b-87cb-4904-9952-2be30ef5d763' else 'U'
        if not price:
            compatibility='S' if 'coming soon' in item.get('description','').lower() else 'N'
        return f"{oid}|{int(oid in owned)}|{int(free)}|{compatibility}\n"
    with lock: ids=list(docs)
    return (''.join(status(oid) for oid in ids)+'BAL|'+str(minecoins(inv))+'\nTAG|'+session_gamertag.replace('\n','')+'\n').encode()


def detail(oid):
    path=DOCS/(oid+'.json')
    if path.exists():return path.read_bytes()
    refresh()
    page=client.detail(oid)
    d=json.loads(json.dumps(docs[oid]))
    d['id']=oid
    for key in ('title','description'):
        if isinstance(d.get(key),str):d[key]={'en-US':d[key]}
    d['title']={locale:text.replace('\u2013','-') for locale,text in d['title'].items()}
    # The old adapter's 1.1.5 fields were placeholders, not compatibility evidence.
    for key in ('minClientVersion','clientVersion'):d.pop(key,None)
    galleries=[x for x in objects(page) if x.get('controlId')=='ImageGallery']
    urls=[]
    if galleries:
        for x in objects(galleries[0]):
            u=x.get('url')
            if isinstance(u,str) and u.startswith('https://') and u not in urls:urls.append(u)
    images=[]
    for index,url in enumerate(urls):
        name=f'{oid}-{index}.png'
        (CACHE/(name+'.url')).write_text(url,encoding='utf-8')
        local=f'http://127.0.0.1:32123/{name}'
        images.append({'imagePurpose':'screenshot','uri':local,'url':local})
    d['images']=d.get('images',[])+images
    body=json.dumps(d,separators=(',',':')).encode()
    path.write_bytes(body)
    print('Detail ready',oid,'screenshots',len(images),flush=True)
    return body

def claim(oid):
    # Called only by an explicit free Claim button. Price is always rechecked live.
    with lock:
        inv=refresh(True)
        if any(oid in e.get('offerIds',[]) and e.get('ownership')=='Purchased' for e in inv['entitlements']):return states()
        page=client.detail(oid)
        item=next((x for x in objects(page) if x.get('id')==oid and 'price' in x),None)
        if item is None: raise ValueError('No live purchase price; item may be coming soon')
        price=item['price']
        if type(price.get('listPrice')) is not int or price['listPrice']!=0 or price.get('virtualCurrencyType')!='Minecoin':
            raise ValueError('Paid or unverified acquisition prohibited')
        r=client.http.post(ENTITLEMENTS+'/transaction/virtual',headers=client.headers,
            json={'OfferId':oid,'StoreId':'','VirtualCurrency':{'Amount':'0','Type':'Minecoin'}},timeout=30)
        r.raise_for_status()
        deadline=time.monotonic()+30
        while True:
            after=refresh(True)
            if inv['virtualCurrencyBalances']!=after['virtualCurrencyBalances']:raise RuntimeError('Unexpected balance change')
            if any(oid in e.get('offerIds',[]) and e.get('ownership')=='Purchased' for e in after['entitlements']):break
            if time.monotonic()>=deadline:raise RuntimeError('Ownership not confirmed')
            time.sleep(1)
        print('Confirmed free claim',oid,flush=True)
        return states()

PAID_TEST_ID='ddd2bcc0-0996-4031-993a-0f541266c3dc'
def minecoins(inv):
    return minecoin_balance(inv)

def buy_test(oid):
    if oid!=PAID_TEST_ID:raise ValueError('Only the selected test offer is allowed')
    with lock:
        before=refresh(True)
        if any(oid in e.get('offerIds',[]) and e.get('ownership')=='Purchased' for e in before['entitlements']):return states()
        permit=ROOT/'paid-test-authorization.json'
        authorization=json.loads(permit.read_text())
        if authorization.get('offerId')!=oid or authorization.get('amount')!=160 or time.time()>authorization.get('expires',0):raise ValueError('No valid one-use authorization')
        page=client.detail(oid)
        item=next((x for x in objects(page) if x.get('id')==oid and 'price' in x),None)
        price=item.get('price',{}) if item else {}
        if type(price.get('listPrice')) is not int or price['listPrice']!=160 or price.get('virtualCurrencyType')!='Minecoin':raise ValueError('Live price changed')
        if minecoins(before)<160:raise ValueError('Insufficient balance')
        attempt=ROOT/('paid-test-attempt-'+uuid.uuid4().hex+'.json')
        # Consume authorization before POST; timeouts must not cause a second debit.
        permit.replace(attempt)
        record={'offerId':oid,'amount':160,'balanceBefore':minecoins(before),'status':'submitted'}
        attempt.write_text(json.dumps(record))
        try:
            r=client.http.post(ENTITLEMENTS+'/transaction/virtual',headers={**client.headers,'inventoryversion':before['inventoryVersion'],'inventoryetag':before['inventoryVersion']},
                json={'OfferId':oid,'StoreId':'','VirtualCurrency':{'Amount':'160','Type':'Minecoin'}},timeout=30)
            record['httpStatus']=r.status_code
            r.raise_for_status()
            deadline=time.monotonic()+30
            while True:
                after=refresh(True)
                owned=any(oid in e.get('offerIds',[]) and e.get('ownership')=='Purchased' for e in after['entitlements'])
                if owned and minecoins(before)-minecoins(after)==160:break
                if time.monotonic()>deadline:raise RuntimeError('Transaction requires reconciliation')
                time.sleep(1)
            record.update(status='confirmed',balanceAfter=minecoins(after),owned=True)
            attempt.write_text(json.dumps(record,indent=2))
            print('Paid test confirmed',oid,'160 Minecoins',flush=True)
            return states()
        except Exception as e:
            record['status']='requires reconciliation';record['errorType']=type(e).__name__
            attempt.write_text(json.dumps(record,indent=2))
            raise


def exact(sock,n):
    b=b''
    while len(b)<n:
        c=sock.recv(n-len(b))
        if not c:raise ConnectionError()
        b+=c
    return b

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(45)
        try:
            op=exact(self.request,4)
            if op==b'ALL1':body=states()
            elif op==b'SKN1':
                import sys
                from skin_store_route import resolve
                size=struct.unpack('<I',exact(self.request,4))[0]
                if not 1<=size<=165:raise ValueError('Invalid skin key')
                key=exact(self.request,size).decode('ascii')
                with lock:body=resolve(sys.modules[__name__],key)
            elif op in (b'ARM1',b'PRP1',b'PRP2',b'PRP3',b'CAN2',b'TOP2',b'CAN1',b'TOP1',b'REC1',b'TOK1',b'RDM1'):
                from coin_topup import handle as topup,parse_pending
                pending=None
                attempt=None
                keys=None
                product='9NHGX9G9JF1X'
                if op in (b'PRP1',b'PRP2',b'PRP3',b'CAN2',b'TOP2',b'TOP1',b'REC1',b'RDM1'):
                    size=struct.unpack('<I',exact(self.request,4))[0]
                    if not 1<=size<=73728:raise ValueError('Invalid receipt observation size')
                    data=exact(self.request,size)
                    if op in (b'PRP2',b'PRP3'):
                        product,data=data.split(b'\n',1)
                        product=product.decode('ascii')
                    if op in (b'CAN2',b'TOP2'):
                        attempt,data=data.split(b'\n',1)
                        attempt=attempt.decode('ascii')
                    if op==b'RDM1':
                        lines=data.decode('ascii').splitlines();count=int(lines[0])
                        if not 0<=count<=16 or len(lines)!=count+3:raise ValueError('Invalid key submission')
                        keys={'collections':lines[-2],'purchase':lines[-1]}
                        data=('\n'.join(lines[:-2])+'\n').encode()
                    pending=parse_pending(data)
                body=topup(op.decode(),pending,keys,product=product,attempt=attempt)
                if body in (b'confirmed',b'acknowledge',b'settled'):refresh(True)
            else:
                oid=exact(self.request,36).decode('ascii')
                if op==b'SND1':
                    print('Native completion sound:',oid,flush=True)
                    body=b'ok'
                    self.request.sendall(struct.pack('<I',len(body))+body)
                    return
                if oid not in docs:raise ValueError('Unknown offer')
                if op==b'DET1':body=detail(oid)
                elif op==b'CLM1':body=claim(oid)
                elif op==b'BUY1':body=buy_test(oid)
                else:raise ValueError('Unknown operation')
            self.request.sendall(struct.pack('<I',len(body))+body)
        except Exception as e:
            print('UI request failed',type(e).__name__,flush=True)
            self.request.sendall(struct.pack('<I',0))

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True

class ImageHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        name=self.path.split('?',1)[0].lstrip('/')
        if not re.fullmatch(r'[0-9a-f-]{36}-\d+\.png',name):self.send_error(404);return
        p=CACHE/name
        if not p.exists():
            source=CACHE/(name+'.url')
            if not source.exists():self.send_error(404);return
            try:
                r=requests.get(source.read_text(encoding='utf-8'),timeout=20);r.raise_for_status()
                with Image.open(io.BytesIO(r.content)) as im:
                    out=io.BytesIO();im.convert('RGBA').save(out,'PNG');data=out.getvalue()
                p.write_bytes(data)
            except Exception:
                self.send_error(502);return
        data=p.read_bytes();self.send_response(200);self.send_header('Content-Type','image/png');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass

if __name__=='__main__':
    from session_presence import run as presence_loop
    threading.Thread(target=presence_loop,daemon=True).start()
    refresh()
    threading.Thread(target=ThreadingHTTPServer(('127.0.0.1',32123),ImageHandler).serve_forever,daemon=True).start()
    print('Store UI adapter ready 32122; gallery 32123',flush=True)
    # Warm representative pages without delaying the socket listener.
    def warm():
        for oid in docs:
            try:detail(oid)
            except Exception as e:print('Warm failed',type(e).__name__,flush=True)
    threading.Thread(target=warm,daemon=True).start()
    with Server(('127.0.0.1',32122),Handler) as server:server.serve_forever()
