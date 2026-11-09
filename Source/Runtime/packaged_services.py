"""Packaged local adapters. No external Python, machine paths, or saved credentials."""
import threading
import startup_diagnostics as diagnostics
_started=False
_servers=[]
def start():
    global _started
    if _started:return
    import catalog_image_server as images
    import live_wallet_adapter as wallet
    import store_ui_adapter as store
    from http.server import ThreadingHTTPServer
    from session_presence import run
    definitions=[(ThreadingHTTPServer,32118,images.Handler),(wallet.Server,32121,wallet.Handler),(store.Server,32122,store.Handler),(ThreadingHTTPServer,32123,store.ImageHandler)]
    opened=[]
    try:
        for cls,port,handler in definitions:opened.append(cls(('127.0.0.1',port),handler))
    except Exception as error:
        diagnostics.event('adapter_start_failed', error_type=type(error).__name__, winerror=getattr(error,'winerror',None))
        for server in opened:server.server_close()
        raise
    _servers.extend(opened)
    for server in opened:threading.Thread(target=server.serve_forever,daemon=True).start()
    threading.Thread(target=run,daemon=True).start()
    _started=True
    diagnostics.event('adapters_ready', ports=[32118,32121,32122,32123])
