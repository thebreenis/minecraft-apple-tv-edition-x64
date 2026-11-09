"""Local account preflight. Reads identity metadata only, never token stores."""
import socket,struct,time
import os,sys
import startup_diagnostics as diagnostics
from dataclasses import dataclass

@dataclass(frozen=True)
class Accounts:
    xbox_id: str=''
    xbox_xuid: str=''
    xbox_tag: str=''
    store_id: str=''
    store_label: str=''

def normalize(value):
    return str(value or '').strip().lower()

def windows_accounts():
    import winreg
    def value(path,name):
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path) as key:return winreg.QueryValueEx(key,name)[0]
        except OSError as error:
            diagnostics.once('identity_read_failed_'+name, field=name, winerror=getattr(error,'winerror',None))
            return ''
    xbox=r'Software\Microsoft\XboxLive'
    xid=normalize(value(xbox,'AccountId'));sid=normalize(value(r'Software\Microsoft\Windows\CurrentVersion\Store','PrimaryWebAccountId'))
    tag=str(value(xbox,'Gamertag'));label=''
    if sid and sid==xid:label=tag
    elif sid:
        # CID-to-email metadata only. Do not inspect credentials or token caches.
        path=r'Software\Microsoft\IdentityCRL\UserExtendedProperties'
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path) as key:
                for i in range(winreg.QueryInfoKey(key)[0]):
                    name=winreg.EnumKey(key,i)
                    if normalize(value(path+'\\'+name,'cid'))==sid:label=name;break
        except OSError:pass
        if not label:label='Microsoft account '+sid
    return Accounts(xid,str(value(xbox,'Xuid')),tag,sid,label)

def decision(accounts,bridge_xuid=''):
    # Gamertag is optional display metadata, not an account identifier.
    if not all((accounts.xbox_id,accounts.xbox_xuid,accounts.store_id)):return 'unverified'
    if accounts.xbox_id!=accounts.store_id:return 'windows_mismatch'
    if str(bridge_xuid)!=accounts.xbox_xuid:return 'reauth'
    return 'ready'

def describe(accounts,bridge_tag='Not connected'):
    xbox_label=accounts.xbox_tag or ('Account detected (Gamertag unavailable)' if accounts.xbox_id and accounts.xbox_xuid else 'Account could not be verified')
    store_label=accounts.store_label or ('Same account as Xbox app' if accounts.store_id and accounts.store_id==accounts.xbox_id else 'Account detected (name unavailable)' if accounts.store_id else 'Account could not be verified')
    return '\n'.join(['Xbox app: '+xbox_label,
        'Microsoft Store: '+store_label,
        'Apple TV bridge: '+(bridge_tag or 'Not connected')])

def bridge_request(host,port,method,url):
    fields=[method.encode(),url.encode(),b'',b'']
    with socket.create_connection((host,port),5) as client:
        client.settimeout(10);stream=client.makefile('rwb',buffering=0)
        stream.write(b'ATVB1'+struct.pack('<IIII',*(len(x) for x in fields))+b''.join(fields))
        def exact(n):
            b=b''
            while len(b)<n:
                c=stream.read(n-len(b))
                if not c:raise ConnectionError('Bridge disconnected')
                b+=c
            return b
        status=struct.unpack('<I',exact(4))[0]
        sizes=struct.unpack('<IIIIIII',exact(28))
        if max(sizes)>1048576:raise ValueError('Invalid bridge response')
        data=[exact(n).decode() for n in sizes]
        if status:raise RuntimeError('Bridge request rejected')
        return data

def bridge_identity(host='127.0.0.1',port=32115):
    try:
        fields=bridge_request(host,port,'GET','https://xboxlive.com')
        return fields[2],fields[3]
    except (OSError,RuntimeError):return '', ''

def stop_bridge(host,port):
    try:
        fields=bridge_request(host,port,'CONTROL','local://account-switch')
    except (OSError,RuntimeError):
        fields=[]
    if not fields or fields[0]!='STOPPING':
        # Older bridges may reject or sign CONTROL as an ordinary request.
        # The fallback verifies the owning executable before stopping it.
        stop_legacy_bridge(port)
    for _ in range(50):
        try:
            with socket.create_connection((host,port),0.2):pass
        except OSError:return
        time.sleep(.1)
    raise RuntimeError('Previous bridge has not stopped')

def listener_pids(raw,port,current_pid):
    pids=set()
    for i in range(struct.unpack_from('<I',raw)[0]):
        row=struct.unpack_from('<IIIIII',raw,4+i*24)
        local_port=int.from_bytes(struct.pack('<I',row[2])[:2],'big')
        # Match an unconnected listening endpoint, not the unstable state enum
        # exposed by current Windows builds. Exclude closed/ownerless rows.
        if local_port==port and row[3]==0 and row[4]==0 and row[5] not in (0,current_pid):
            pids.add(row[5])
    return pids

REAUTH_MESSAGE = ('Account configuration has changed. Please re-authenticate with the account '
                  'that is logged into both the Xbox app and the Microsoft Store.')

def stop_legacy_bridge(port):
    import ctypes,os,subprocess,json
    from ctypes import wintypes
    api=ctypes.WinDLL('iphlpapi');size=wintypes.ULONG(0)
    api.GetExtendedTcpTable(None,ctypes.byref(size),False,2,5,0)
    buffer=ctypes.create_string_buffer(size.value)
    if api.GetExtendedTcpTable(buffer,ctypes.byref(size),False,2,5,0):raise RuntimeError('Cannot identify previous bridge')
    pids=listener_pids(buffer.raw,port,os.getpid())
    if len(pids)!=1:raise RuntimeError('Cannot uniquely identify previous bridge')
    pid=pids.pop()
    command=f'$p=Get-CimInstance Win32_Process -Filter "ProcessId={pid}"; $p | Select-Object Name,CommandLine | ConvertTo-Json -Compress'
    info=json.loads(subprocess.check_output(['powershell.exe','-NoProfile','-Command',command],text=True,creationflags=0x08000000))
    name=info['Name'].lower();line=info.get('CommandLine','') or ''
    valid=(name in ('python.exe','pythonw.exe') and 'appletv_auth_bridge.py' in line) or name=='minecraft-appletv-edition.exe'
    if not valid:raise RuntimeError('Port is not owned by the Apple TV bridge; refusing to stop it')
    subprocess.run(['taskkill.exe','/PID',str(pid),'/F'],check=True,capture_output=True,creationflags=0x08000000)

def reject_message(accounts,bridge_tag):
    if decision(accounts) == 'unverified':
        alert(describe(accounts,bridge_tag)+'\n\nWindows account information could not be read or is incomplete. This does not necessarily mean the apps are signed out. Bridge sign-in has not started. Open the Xbox app and Microsoft Store with the same account, then retry. If this continues, run Collect-Diagnostics.cmd and share the diagnostic report. No account was changed.')
        return
    alert(describe(accounts,bridge_tag)+'\n\nSign into the SAME Microsoft account in the Xbox app and Microsoft Store, then reopen Minecraft: Apple TV Edition. The game cannot continue.')

def alert(message):
    if os.name=='nt' and getattr(sys,'frozen',False):
        import ctypes
        ctypes.windll.user32.MessageBoxW(None,message,'Minecraft: Apple TV Edition',0x10|0x10000)
    else:print(message,flush=True)

def wait_before_exit():
    if getattr(sys,'frozen',False):return
    try:input('\nPress Enter to close this launcher.')
    except (EOFError,OSError):pass
