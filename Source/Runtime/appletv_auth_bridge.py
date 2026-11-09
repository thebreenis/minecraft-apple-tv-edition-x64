"""In-memory Apple TV Xbox authentication/signing bridge for the UWP hook."""

import argparse
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import requests
import account_guard
import startup_diagnostics as diagnostics

from appletv_auth_probe import (
    DEFAULT_MSA_CLIENT_ID,
    ProofKey,
    acquire_msa_token,
    find_title_token,
    request_device_token,
    request_user_token,
    request_xsts_token,
    require_token,
)


MAGIC = b"ATVB1"
MAX_FIELD = 16 * 1024 * 1024
GAME_AUMID = "Microsoft.MinecraftUWP_8wekyb3d8bbwe!App"
LOG_PATH = Path(os.environ.get("LOCALAPPDATA", ".")) / "MinecraftAppleTV" / "requests.log"
LOG_LOCK = threading.Lock()
WALLET_PORT = 32116
THUMBNAIL_PORT = 32117
PLAYFAB_LOGIN_URL = "https://20CA2.playfabapi.com/Client/LoginWithXbox"
PLAYFAB_RELYING_PARTIES = (
    "http://playfab.xboxlive.com/",
    "http://playfab.xboxlive.com",
    "https://playfab.xboxlive.com/",
)

# Minecraft 1.1.5 asks a now-retired Xbox Commerce host for this legacy
# bootstrap document before it loads the Marketplace catalog.  Supplying an
# honest empty wallet lets the client continue without inventing currency,
# ownership, receipts, or purchases.
EMPTY_WALLET = {
    "Balance": {"currencies": [{"currencyType": "minecoin", "amount": 0}]},
    "EntitlementReceipt": "",
    "Inventory": {"items": []},
}


def log_event(message: str) -> None:
    with LOG_LOCK:
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        with LOG_PATH.open("a", encoding="utf-8") as stream:
            stream.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}\n")


def recv_exact(stream, size: int) -> bytes:
    value = stream.read(size)
    if len(value) != size:
        raise ConnectionError("client disconnected")
    return value


def read_fields(stream, count: int) -> list[bytes]:
    sizes = struct.unpack("<" + "I" * count, recv_exact(stream, 4 * count))
    if any(size > MAX_FIELD for size in sizes):
        raise ValueError("bridge field is unreasonably large")
    return [recv_exact(stream, size) for size in sizes]


def write_response(stream, status: int, fields: list[str]) -> None:
    encoded = [value.encode("utf-8") for value in fields]
    stream.write(struct.pack("<I", status))
    stream.write(struct.pack("<" + "I" * len(encoded), *(map(len, encoded))))
    for value in encoded:
        stream.write(value)
    stream.flush()


class BridgeState:
    def __init__(self, proof_key: ProofKey, xsts: dict, playfab_xsts: dict):
        claims = xsts["DisplayClaims"]["xui"][0]
        playfab_claims = playfab_xsts["DisplayClaims"]["xui"][0]
        self.xsts = xsts
        self.playfab_xsts = playfab_xsts
        self.proof_key = proof_key
        self.authorization = f"XBL3.0 x={claims['uhs']};{xsts['Token']}"
        self.playfab_authorization = (
            f"XBL3.0 x={playfab_claims['uhs']};{playfab_xsts['Token']}"
        )
        self.xuid = claims["xid"]
        self.gamertag = claims.get("gtg", "")
        self.user_hash = claims["uhs"]
        self.age_group = claims.get("agg", "Adult")
        privileges = claims.get("prv", "")
        self.privileges = (
            " ".join(map(str, privileges))
            if isinstance(privileges, list)
            else str(privileges)
        )


def publish_presence(state: BridgeState, *, active: bool = True) -> None:
    url = (
        "https://userpresence.xboxlive.com/users/"
        f"xuid({state.xuid})/devices/current/titles/current"
    )
    body = json.dumps(
        {"state": "active" if active else "inactive"}, separators=(",", ":")
    ).encode("utf-8")
    signature = state.proof_key.sign_request(
        "POST", url, body, authorization=state.authorization
    )
    try:
        response = requests.post(
            url,
            data=body,
            headers={
                "Authorization": state.authorization,
                "Signature": signature,
                "Content-Type": "application/json",
                "x-xbl-contract-version": "3",
            },
            timeout=15,
        )
        log_event(f"PRESENCE {'active' if active else 'inactive'} -> {response.status_code}")
    except requests.RequestException as error:
        log_event(f"PRESENCE ERROR {type(error).__name__}: {error}")


def presence_heartbeat(state: BridgeState) -> None:
    while True:
        publish_presence(state)
        time.sleep(240)


class BridgeHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        try:
            if recv_exact(self.rfile, len(MAGIC)) != MAGIC:
                raise ValueError("bad bridge magic")
            method, url, _headers, body = read_fields(self.rfile, 4)
            method_text = method.decode("utf-8")
            url_text = url.decode("utf-8")
            print(f"Apple TV signing: {method_text} {url_text}", flush=True)
            log_event(f"SIGN {method_text} {url_text}")
            # Do not persist URLs, headers or tokens in shareable diagnostics.
            diagnostics.once('bridge_signing_request_received')
            state: BridgeState = self.server.bridge_state
            if method_text=='CONTROL' and url_text=='local://account-switch':
                write_response(self.wfile,0,['STOPPING','','','','','',''])
                threading.Thread(target=self.server.shutdown,daemon=True).start()
                return
            authorization = (
                state.playfab_authorization
                if ".playfabapi.com" in url_text.lower()
                else state.authorization
            )
            signature = state.proof_key.sign_request(
                method_text,
                url_text,
                body,
                authorization=authorization,
            )
            write_response(
                self.wfile,
                0,
                [
                    authorization,
                    signature,
                    state.xuid,
                    state.gamertag,
                    state.user_hash,
                    state.age_group,
                    state.privileges,
                ],
            )
        except Exception as error:
            log_event(f"ERROR {type(error).__name__}: {error}")
            try:
                write_response(self.wfile, 1, [str(error), "", "", "", ""])
            except Exception:
                pass


class BridgeServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, bridge_state: BridgeState):
        super().__init__(address, handler)
        self.bridge_state = bridge_state


class WalletHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        body = json.dumps(EMPTY_WALLET, separators=(",", ":")).encode("utf-8")
        log_event(f"WALLET GET {self.path} -> {body.decode('utf-8')}")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args) -> None:
        return


def load_legacy_thumbnail() -> bytes:
    """Load a package-local image that Minecraft 1.1.5 can decode reliably."""
    relative = Path("UAP.Assets") / "minecraft" / "icons" / "MCLogo.scale-100.png"
    candidates = (
        Path(sys.executable).resolve().parent / relative,
        Path(__file__).resolve().parent / "v20-memory-wallet" / relative,
        Path(__file__).resolve().parent / relative,
    )
    for candidate in candidates:
        try:
            payload = candidate.read_bytes()
            if payload.startswith(b"\x89PNG\r\n\x1a\n"):
                log_event(f"THUMBNAIL loaded {candidate} ({len(payload)} bytes)")
                return payload
        except OSError:
            pass
    raise FileNotFoundError("packaged Marketplace thumbnail was not found")


class ThumbnailHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        try:
            body = self.server.thumbnail
            status = 200
            content_type = "image/png"
        except AttributeError:
            body = b"thumbnail unavailable"
            status = 503
            content_type = "text/plain; charset=utf-8"
        log_event(f"THUMBNAIL GET {self.path} -> {status} ({len(body)} bytes)")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "public, max-age=3600")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args) -> None:
        return


def launch_game(reuse_services=False) -> None:
    diagnostics.event('game_launch_requested', reuse_services=bool(reuse_services))
    import auth_progress
    auth_progress.check_cancelled()
    if getattr(sys,'frozen',False) and not reuse_services:
        import packaged_services
        packaged_services.start()
    subprocess.Popen(["explorer.exe", f"shell:AppsFolder\\{GAME_AUMID}"])
    diagnostics.event('game_activation_dispatched')
    auth_progress.close_when_game_starts(game_is_running)


def bridge_is_running(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.5):
            return True
    except OSError:
        return False


def game_is_running() -> bool:
    result = subprocess.run(
        ["tasklist.exe", "/FI", "IMAGENAME eq Minecraft.Windows.exe", "/NH"],
        capture_output=True,
        text=True,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    return "Minecraft.Windows.exe" in result.stdout


def stop_after_game(server: BridgeServer) -> None:
    for _ in range(120):
        if game_is_running():
            break
        time.sleep(0.5)
    else:
        server.shutdown()
        return

    while game_is_running():
        time.sleep(1.0)
    server.shutdown()


def find_playfab_xsts(
    session: requests.Session,
    proof_key: ProofKey,
    user_token: str,
    device_token: str,
    title_token: str,
    timeout: float,
) -> dict:
    failures = []
    for relying_party in PLAYFAB_RELYING_PARTIES:
        response = request_xsts_token(
            session,
            proof_key,
            user_token,
            device_token,
            title_token,
            timeout,
            relying_party=relying_party,
        )
        if not response.ok or not response.json().get("Token"):
            failures.append(f"{relying_party}: XSTS HTTP {response.status_code}")
            continue

        candidate = response.json()
        claims = candidate["DisplayClaims"]["xui"][0]
        authorization = f"XBL3.0 x={claims['uhs']};{candidate['Token']}"
        login = session.post(
            PLAYFAB_LOGIN_URL,
            headers={"Content-Type": "application/json"},
            json={
                "CreateAccount": True,
                "TitleId": "20CA2",
                "XboxToken": authorization,
            },
            timeout=timeout,
        )
        try:
            payload = login.json()
        except ValueError:
            payload = {}
        log_event(
            f"PLAYFAB PROBE relying_party={relying_party} "
            f"http={login.status_code} code={payload.get('code')} "
            f"error={payload.get('error', 'none')}"
        )
        print(
            f"PlayFab probe [{relying_party}]: HTTP {login.status_code}, "
            f"code={payload.get('code')}, error={payload.get('error', 'none')}"
        )
        if login.ok and payload.get("code") == 200:
            return candidate
        failures.append(
            f"{relying_party}: Login HTTP {login.status_code} "
            f"{payload.get('error', payload.get('errorMessage', 'unknown error'))}"
        )
    raise RuntimeError("PlayFab authorization failed: " + "; ".join(failures))


def authenticate(timeout: float, *, browser_sign_in: bool = False) -> BridgeState:
    session = requests.Session()
    proof_key = ProofKey()
    device = require_token(
        request_device_token(session, proof_key, "AppleTV", "10.2", timeout),
        "Device authentication",
    )
    print("Device authentication: OK")
    msa = acquire_msa_token(
        session,
        DEFAULT_MSA_CLIENT_ID,
        timeout,
        open_browser=browser_sign_in,
    )
    print("Microsoft account authentication: OK")
    user = require_token(
        request_user_token(session, proof_key, "d=" + msa, timeout),
        "Xbox user authentication",
    )
    title = find_title_token(session, proof_key, msa, device["Token"], timeout)
    print("Apple TV title authentication: OK")
    xsts = require_token(
        request_xsts_token(
            session,
            proof_key,
            user["Token"],
            device["Token"],
            title["Token"],
            timeout,
        ),
        "XSTS authorization",
    )
    playfab_xsts = find_playfab_xsts(
        session,
        proof_key,
        user["Token"],
        device["Token"],
        title["Token"],
        timeout,
    )
    print("PlayFab XSTS authorization: OK")
    return BridgeState(proof_key, xsts, playfab_xsts)


def launcher_visible(visible):
    # Only the packaged console belongs to us; never hide a developer's shell.
    if os.name=='nt' and getattr(sys,'frozen',False):
        import ctypes
        kernel=ctypes.WinDLL('kernel32');kernel.GetConsoleWindow.restype=ctypes.c_void_p
        window=kernel.GetConsoleWindow()
        if window:
            user=ctypes.WinDLL('user32');user.ShowWindow.argtypes=[ctypes.c_void_p,ctypes.c_int]
            user.ShowWindow(window,5 if visible else 0)


def monitor_accounts(initial,bridge_xuid,bridge_tag):
    for _ in range(120):
        if game_is_running():
            diagnostics.event('game_process_observed')
            break
        time.sleep(.5)
    while game_is_running():
        current=account_guard.windows_accounts()
        verdict=account_guard.decision(current,bridge_xuid)
        if verdict!='ready':
            diagnostics.accounts(current, verdict, bool(bridge_xuid))
            diagnostics.event('game_stopped_account_check')
            launcher_visible(True)
            # Close this game's process only; never sign out other Windows apps.
            subprocess.run(['taskkill.exe','/IM','Minecraft.Windows.exe','/F'],capture_output=True,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            account_guard.reject_message(current,bridge_tag)
            if verdict=='reauth':
                print('Reopening the launcher to reconnect the bridge.',flush=True)
                subprocess.Popen(['explorer.exe',r'shell:AppsFolder\Microsoft.MinecraftUWP_8wekyb3d8bbwe!Launcher'])
            else:account_guard.wait_before_exit()
            return
        time.sleep(2)

def main() -> int:
    diagnostics.start()
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=32115)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--browser-sign-in",
        action="store_true",
        help="open the Microsoft device sign-in page and copy its code",
    )
    parser.add_argument(
        "--launch",
        action="store_true",
        help="launch the installed Minecraft UWP package after authentication",
    )
    parser.add_argument(
        "--exit-with-game",
        action="store_true",
        help="close the bridge automatically when Minecraft closes",
    )
    args = parser.parse_args()

    if args.launch:
        accounts=account_guard.windows_accounts()
        bridge_xuid,bridge_tag=account_guard.bridge_identity(args.host,args.port)
        verdict=account_guard.decision(accounts,bridge_xuid)
        diagnostics.accounts(accounts,verdict,bool(bridge_xuid))
        print(account_guard.describe(accounts,bridge_tag),flush=True)
        if verdict in ('windows_mismatch','unverified'):
            account_guard.reject_message(accounts,bridge_tag)
            account_guard.wait_before_exit()
            return 2
        if verdict=='ready':
            launcher_visible(False)
            launch_game(reuse_services=True)
            monitor_accounts(accounts,bridge_xuid,bridge_tag)
            return 0
        if bridge_xuid:
            account_guard.alert(account_guard.describe(accounts,bridge_tag)+'\n\n'+account_guard.REAUTH_MESSAGE)
            print('Bridge account differs. Reconnecting to '+(accounts.xbox_tag or 'the matching Xbox and Store account')+'...',flush=True)
            account_guard.stop_bridge(args.host,args.port)

    # Cache only the packaged launcher; developer probes remain non-persistent.
    cache=None
    state=None
    if args.launch and getattr(sys,'frozen',False):
        import auth_session_cache as cache
        state=cache.load(accounts.xbox_xuid)
        diagnostics.event('session_cache_checked', usable=state is not None)
    if state is None:
        diagnostics.event('interactive_authentication_started')
        state = authenticate(args.timeout, browser_sign_in=args.browser_sign_in)
        diagnostics.event('interactive_authentication_completed')
    if args.launch:
        import auth_progress
        auth_progress.check_cancelled()
        accounts=account_guard.windows_accounts()
        diagnostics.accounts(accounts,account_guard.decision(accounts,state.xuid),True)
        if account_guard.decision(accounts,state.xuid)!='ready':
            auth_progress.close()
            account_guard.reject_message(accounts,state.gamertag)
            print('Authentication did not match the selected Windows accounts. No game session was started.',flush=True)
            account_guard.wait_before_exit()
            return 2
    if cache is not None:
        try:cache.save(state)
        except OSError:log_event('AUTH cache unavailable; session remains usable in memory')
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    LOG_PATH.write_text("", encoding="utf-8")
    log_event(
        f"AUTH gamertag={state.gamertag} xuid={state.xuid} "
        f"age_group={state.age_group} privileges={state.privileges}"
    )
    print(f"Apple TV bridge authenticated as {state.gamertag} ({state.xuid})")
    print(f"Listening on {args.host}:{args.port}; session cache uses Windows user protection when packaged.")
    thumbnail = load_legacy_thumbnail()
    with (
        BridgeServer((args.host, args.port), BridgeHandler, state) as server,
        ThreadingHTTPServer((args.host, WALLET_PORT), WalletHandler) as wallet_server,
        ThreadingHTTPServer(
            (args.host, THUMBNAIL_PORT), ThumbnailHandler
        ) as thumbnail_server,
    ):
        thumbnail_server.thumbnail = thumbnail
        diagnostics.event('bridge_listeners_ready', ports=[args.port,WALLET_PORT,THUMBNAIL_PORT])
        threading.Thread(target=wallet_server.serve_forever, daemon=True).start()
        threading.Thread(target=thumbnail_server.serve_forever, daemon=True).start()
        log_event(f"WALLET listening on http://{args.host}:{WALLET_PORT}/")
        log_event(f"THUMBNAIL listening on http://{args.host}:{THUMBNAIL_PORT}/")
        if args.launch:
            launcher_visible(False)
            launch_game()
            threading.Thread(target=monitor_accounts,args=(accounts,state.xuid,state.gamertag),daemon=False).start()
        if args.exit_with_game:
            threading.Thread(target=stop_after_game, args=(server,), daemon=True).start()
        server.serve_forever()
        wallet_server.shutdown()
        thumbnail_server.shutdown()
    return 0


if __name__ == "__main__":
    try:result=main()
    except Exception as error:
        diagnostics.event('launcher_exception', error_type=type(error).__name__, winerror=getattr(error,'winerror',None))
        import auth_progress
        auth_progress.close()
        if isinstance(error,auth_progress.SignInCancelled):raise SystemExit(0)
        if not getattr(sys,'frozen',False):raise
        account_guard.alert('The Xbox account check or connection could not complete ('+type(error).__name__+'). Please reopen Minecraft: Apple TV Edition to try again.')
        result=2
    raise SystemExit(result)
