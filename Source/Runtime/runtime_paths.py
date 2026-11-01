import os,sys
from pathlib import Path
FROZEN=getattr(sys,'frozen',False)
SOURCE=Path(__file__).parent
PACK=Path(sys.executable).resolve().parent if FROZEN else SOURCE/'v20-memory-wallet'
STATE=Path(os.environ.get('LOCALAPPDATA','.'))/'MinecraftAppleTV'/'Store' if FROZEN else SOURCE
STATE.mkdir(parents=True,exist_ok=True)
CATALOG=PACK/'modern-store-root.json' if FROZEN else SOURCE/'modern-store-root.json'
