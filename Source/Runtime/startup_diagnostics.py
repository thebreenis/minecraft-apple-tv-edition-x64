"""Bounded, structured startup diagnostics; never log tokens or raw exceptions."""
import json, os, sys, time, threading, platform
from pathlib import Path

ROOT = Path(os.environ.get('LOCALAPPDATA', '.')) / 'MinecraftAppleTV' / 'Diagnostics'
PATH = ROOT / 'startup.jsonl'
_lock = threading.Lock()
_seen = set()

def event(stage, **fields):
    try:
        with _lock:
            ROOT.mkdir(parents=True, exist_ok=True)
            if PATH.exists() and PATH.stat().st_size > 1024 * 1024:
                PATH.replace(ROOT / 'startup.previous.jsonl')
            record = dict(time=time.strftime('%Y-%m-%dT%H:%M:%S%z'), pid=os.getpid(), stage=stage, **fields)
            with PATH.open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(record, ensure_ascii=True) + '\n')
    except (OSError, ValueError):
        pass  # Diagnostics must never prevent launch or sign-in.

def once(stage, **fields):
    with _lock:
        if stage in _seen:
            return
        _seen.add(stage)
    event(stage, **fields)

def start():
    event('launcher_started', windows=platform.version(), architecture=platform.machine(),
          frozen=bool(getattr(sys, 'frozen', False)), python=platform.python_version())

def accounts(value, verdict, bridge_present):
    event('account_preflight', verdict=verdict, xbox_account_present=bool(value.xbox_id),
          xbox_xuid_present=bool(value.xbox_xuid), xbox_tag_present=bool(value.xbox_tag),
          store_account_present=bool(value.store_id),
          windows_accounts_match=bool(value.xbox_id and value.store_id and value.xbox_id == value.store_id),
          bridge_present=bool(bridge_present))
