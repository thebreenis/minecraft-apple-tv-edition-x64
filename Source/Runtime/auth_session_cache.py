"""Current-user DPAPI cache for unexpired bridge sessions; never plaintext tokens."""
import ctypes,json,os,time
from ctypes import wintypes
from datetime import datetime
from pathlib import Path
from cryptography.hazmat.primitives import serialization

CACHE=Path(os.environ.get('LOCALAPPDATA','.'))/'MinecraftAppleTV'/'auth-session.bin'
class Blob(ctypes.Structure):
    _fields_=[('size',wintypes.DWORD),('data',ctypes.POINTER(ctypes.c_ubyte))]

def protect(data,decrypt=False):
    if os.name!='nt':raise OSError('Windows protection unavailable')
    buf=ctypes.create_string_buffer(data);source=Blob(len(data),ctypes.cast(buf,ctypes.POINTER(ctypes.c_ubyte)));out=Blob()
    api=ctypes.WinDLL('crypt32',use_last_error=True)
    fn=api.CryptUnprotectData if decrypt else api.CryptProtectData
    fn.argtypes=[ctypes.POINTER(Blob),ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(Blob)]
    fn.restype=wintypes.BOOL
    if not fn(ctypes.byref(source),None,None,None,None,1,ctypes.byref(out)):raise ctypes.WinError(ctypes.get_last_error())
    try:return ctypes.string_at(out.data,out.size)
    finally:
        free=ctypes.WinDLL('kernel32').LocalFree;free.argtypes=[ctypes.c_void_p];free.restype=ctypes.c_void_p;free(out.data)

def clear():
    CACHE.unlink(missing_ok=True)

def save(state):
    data={'version':1,'xuid':state.xuid,'xsts':state.xsts,'playfab':state.playfab_xsts,
          'proof':state.proof_key.private_key.private_bytes(serialization.Encoding.DER,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()).hex()}
    encrypted=protect(json.dumps(data).encode())
    CACHE.parent.mkdir(parents=True,exist_ok=True)
    temp=CACHE.with_suffix('.tmp');temp.write_bytes(encrypted);temp.replace(CACHE)

def load(expected_xuid):
    try:
        data=json.loads(protect(CACHE.read_bytes(),decrypt=True))
        if data['version']!=1 or data['xuid']!=expected_xuid:return None
        xbox=data['xsts']['DisplayClaims']['xui'][0]
        playfab=data['playfab']['DisplayClaims']['xui'][0]
        if xbox['xid']!=expected_xuid or playfab['uhs']!=xbox['uhs']:return None
        if playfab.get('xid',expected_xuid)!=expected_xuid:return None
        for key in ('xsts','playfab'):
            token=data[key]
            if datetime.fromisoformat(token['NotAfter'].replace('Z','+00:00')).timestamp()<=time.time()+120:return None
        from appletv_auth_bridge import BridgeState
        from appletv_auth_probe import ProofKey
        proof=ProofKey();proof.private_key=serialization.load_der_private_key(bytes.fromhex(data['proof']),password=None)
        return BridgeState(proof,data['xsts'],data['playfab'])
    except (OSError,ValueError,KeyError,TypeError,IndexError):return None
