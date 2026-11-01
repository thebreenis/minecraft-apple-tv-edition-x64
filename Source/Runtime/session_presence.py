"""Presence heartbeat through the existing in-memory Apple TV signer. No login."""
import json
import socket
import struct
import time
import requests
from appletv_auth_bridge import game_is_running, log_event
from probe_playfab_from_bridge import read_exact, get_bridge_fields

def signed_request(method, url, body=b''):
    fields=[method.encode(),url.encode(),b'',body]
    with socket.create_connection(('127.0.0.1',32115),timeout=5) as client:
        stream=client.makefile('rwb',buffering=0)
        stream.write(b'ATVB1'+struct.pack('<IIII',*(len(x) for x in fields))+b''.join(fields))
        status=struct.unpack('<I',read_exact(stream,4))[0]
        sizes=struct.unpack('<IIIIIII',read_exact(stream,28))
        values=[read_exact(stream,n).decode() for n in sizes]
        if status:raise RuntimeError('Existing signer rejected request')
    response=requests.request(method,url,data=body,headers={
        'Authorization':values[0],'Signature':values[1],
        'Content-Type':'application/json','x-xbl-contract-version':'3'},timeout=15)
    response.raise_for_status()
    return response,values[2]

def publish(active):
    # /current resolves the title from the authenticated Apple TV title token.
    body=b'{"state":"active"}' if active else b''
    xuid=get_bridge_fields()[2]
    response,xuid=signed_request('POST' if active else 'DELETE',f'https://userpresence.xboxlive.com/users/xuid({xuid})/devices/current/titles/current',body)
    log_event(f'PRESENCE heartbeat {active=} HTTP {response.status_code}')
    return xuid

def verify():
    xuid=get_bridge_fields()[2]
    response,_=signed_request('GET',f'https://userpresence.xboxlive.com/users/xuid({xuid})?level=all')
    payload=response.json()
    return [{'device':d.get('type'),'id':t.get('id'),'name':t.get('name'),'state':t.get('state')}
            for d in payload.get('devices',[]) for t in d.get('titles',[])]

def run():
    # Prevent duplicate heartbeat threads across helper restarts.
    with socket.socket() as guard:
        try:guard.bind(('127.0.0.1',32124))
        except OSError:return
        previous=False
        due=0
        while True:
            active=game_is_running()
            if (active and time.monotonic()>=due) or active!=previous:
                previous=active
                try:
                    publish(active)
                except Exception as error:log_event(f'PRESENCE heartbeat error {type(error).__name__}')
                due=time.monotonic()+120
            time.sleep(5)

if __name__=='__main__':run()
