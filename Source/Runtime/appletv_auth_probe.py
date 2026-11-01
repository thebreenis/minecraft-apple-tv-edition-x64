"""Minimal, non-persistent probe for Minecraft Apple TV's Xbox auth protocol."""

import argparse
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid
import webbrowser
from urllib.parse import urlsplit

import requests
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (
    Prehashed,
    decode_dss_signature,
)


DEVICE_AUTH_URL = "https://device.auth.xboxlive.com/device/authenticate"
TITLE_AUTH_URL = "https://title.auth.xboxlive.com/title/authenticate"
USER_AUTH_URL = "https://user.auth.xboxlive.com/user/authenticate"
XSTS_AUTH_URL = "https://xsts.auth.xboxlive.com/xsts/authorize"
MSA_CONNECT_URL = "https://login.live.com/oauth20_connect.srf"
MSA_TOKEN_URL = "https://login.live.com/oauth20_token.srf"
XBOX_AUTH_RELYING_PARTY = "http://auth.xboxlive.com"
XBOX_LIVE_RELYING_PARTY = "http://xboxlive.com"
APPLE_TV_TITLE_ID = 0x6D646E7B
# XBLMSADeviceClient::msaAppID constructs this exact value from the app's
# CFBundleIdentifier using the format string "ios-app://%@".
DEFAULT_MSA_CLIENT_ID = "ios-app://com.mojang.minecraftappletv"
MSA_SCOPE = "service::user.auth.xboxlive.com::MBI_SSL"
WINDOWS_EPOCH_OFFSET_SECONDS = 11_644_473_600


def open_account_selection_browser(url: str) -> None:
    """Use the user's normal default browser and existing browser session."""
    webbrowser.open(url)



def base64url(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def filetime_now() -> int:
    return (time.time_ns() // 100 + WINDOWS_EPOCH_OFFSET_SECONDS * 10_000_000)


class ProofKey:
    def __init__(self):
        self.private_key = ec.generate_private_key(ec.SECP256R1())

    def jwk(self) -> dict:
        numbers = self.private_key.public_key().public_numbers()
        return {
            "crv": "P-256",
            "alg": "ES256",
            "use": "sig",
            "kty": "EC",
            "x": base64url(numbers.x.to_bytes(32, "big")),
            "y": base64url(numbers.y.to_bytes(32, "big")),
        }

    def sign_request(
        self,
        method: str,
        url: str,
        body: bytes,
        authorization: str = "",
        timestamp: int | None = None,
    ) -> str:
        timestamp = filetime_now() if timestamp is None else timestamp
        parsed = urlsplit(url)
        path_and_query = parsed.path
        if parsed.query:
            path_and_query += "?" + parsed.query

        fields = (
            struct.pack(">I", 1),
            b"\x00",
            struct.pack(">Q", timestamp),
            b"\x00",
            method.upper().encode("ascii"),
            b"\x00",
            path_and_query.encode("utf-8"),
            b"\x00",
            authorization.encode("utf-8"),
            b"\x00",
            body,
            b"\x00",
        )
        digest = hashes.Hash(hashes.SHA256())
        digest.update(b"".join(fields))
        der_signature = self.private_key.sign(
            digest.finalize(), ec.ECDSA(Prehashed(hashes.SHA256()))
        )
        r, s = decode_dss_signature(der_signature)
        signature = (
            struct.pack(">I", 1)
            + struct.pack(">Q", timestamp)
            + r.to_bytes(32, "big")
            + s.to_bytes(32, "big")
        )
        return base64.b64encode(signature).decode("ascii")


def compact_json(value: dict) -> bytes:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def signed_post(
    session: requests.Session,
    proof_key: ProofKey,
    url: str,
    payload: dict,
    timeout: float,
) -> requests.Response:
    body = compact_json(payload)
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "x-xbl-contract-version": "1",
        "Cache-Control": "no-store, must-revalidate, no-cache",
        "Signature": proof_key.sign_request("POST", url, body),
    }
    return session.post(url, data=body, headers=headers, timeout=timeout)


def require_token(response: requests.Response, stage: str) -> dict:
    if not response.ok:
        print(f"{stage} failed:")
        print_response(response)
        raise RuntimeError(f"{stage} returned HTTP {response.status_code}")
    payload = response.json()
    if not payload.get("Token"):
        raise RuntimeError(f"{stage} returned no token")
    return payload


def request_device_token(
    session: requests.Session,
    proof_key: ProofKey,
    device_type: str,
    device_version: str,
    timeout: float,
) -> requests.Response:
    device_id = "{" + str(uuid.uuid4()).upper() + "}"
    return signed_post(
        session,
        proof_key,
        DEVICE_AUTH_URL,
        {
            "Properties": {
                "AuthMethod": "ProofOfPossession",
                "Id": device_id,
                "DeviceType": device_type,
                "SerialNumber": device_id,
                "Version": device_version,
                "ProofKey": proof_key.jwk(),
            },
            "RelyingParty": XBOX_AUTH_RELYING_PARTY,
            "TokenType": "JWT",
        },
        timeout,
    )


def acquire_msa_token(
    session: requests.Session,
    client_id: str,
    timeout: float,
    *,
    open_browser: bool = False,
) -> str:
    response = session.post(
        MSA_CONNECT_URL,
        data={
            "client_id": client_id,
            "scope": MSA_SCOPE,
            "response_type": "device_code",
        },
        timeout=timeout,
    )
    response.raise_for_status()
    device_flow = response.json()
    user_code = device_flow["user_code"]
    verification_url = device_flow.get("verification_url") or device_flow.get(
        "verification_uri"
    )
    interval = max(float(device_flow.get("interval", 5)), 1.0)
    deadline = time.monotonic() + float(device_flow.get("expires_in", 900))

    print("\nMicrosoft sign-in required")
    print(f"Open: {verification_url}")
    print(f"Enter code: {user_code}")
    print("Waiting for sign-in; no password is stored. Packaged sessions use Windows-protected storage.\n", flush=True)
    if getattr(__import__('sys'),'frozen',False):
        import auth_progress
        auth_progress.begin(user_code, verification_url)

    if open_browser:
        if os.name == "nt":
            try:
                subprocess.run(
                    ["clip.exe"],
                    input=user_code,
                    text=True,
                    check=True,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                print("The sign-in code has been copied to the clipboard.")
            except Exception:
                pass
        open_account_selection_browser(verification_url)

    while time.monotonic() < deadline:
        import auth_progress
        auth_progress.check_cancelled()
        time.sleep(interval)
        auth_progress.check_cancelled()
        poll = session.post(
            MSA_TOKEN_URL,
            data={
                "grant_type": "device_code",
                "device_code": device_flow["device_code"],
                "client_id": client_id,
            },
            timeout=timeout,
        )
        payload = poll.json()
        if poll.ok and payload.get("access_token"):
            auth_progress.status("Sign-in received. Verifying Xbox account...")
            return payload["access_token"]
        error = payload.get("error")
        if error == "authorization_pending":
            continue
        if error == "slow_down":
            interval += 5
            continue
        raise RuntimeError(
            f"Microsoft sign-in failed: {error or poll.status_code}: "
            f"{payload.get('error_description', '')}"
        )
    raise TimeoutError("Microsoft device sign-in expired")


def request_user_token(
    session: requests.Session,
    proof_key: ProofKey,
    rps_ticket: str,
    timeout: float,
) -> requests.Response:
    return signed_post(
        session,
        proof_key,
        USER_AUTH_URL,
        {
            "Properties": {
                "AuthMethod": "RPS",
                "SiteName": "user.auth.xboxlive.com",
                "RpsTicket": rps_ticket,
                "ProofKey": proof_key.jwk(),
            },
            "RelyingParty": XBOX_AUTH_RELYING_PARTY,
            "TokenType": "JWT",
        },
        timeout,
    )


def request_title_token(
    session: requests.Session,
    proof_key: ProofKey,
    rps_ticket: str,
    device_token: str,
    timeout: float,
    *,
    url: str = TITLE_AUTH_URL,
    include_proof_key: bool = True,
) -> requests.Response:
    properties = {
        "DeviceToken": device_token,
        "AuthMethod": "RPS",
        "SiteName": "user.auth.xboxlive.com",
        "RpsTicket": rps_ticket,
    }
    if include_proof_key:
        properties["ProofKey"] = proof_key.jwk()
    return signed_post(
        session,
        proof_key,
        url,
        {
            "Properties": properties,
            "RelyingParty": XBOX_AUTH_RELYING_PARTY,
            "TokenType": "JWT",
        },
        timeout,
    )


def find_title_token(
    session: requests.Session,
    proof_key: ProofKey,
    msa_access_token: str,
    device_token: str,
    timeout: float,
) -> dict:
    variants = [
        ("sdk-path / d= / proof", TITLE_AUTH_URL, "d=" + msa_access_token, True),
        ("sdk-path / raw / proof", TITLE_AUTH_URL, msa_access_token, True),
        ("sdk-path / d= / no-proof", TITLE_AUTH_URL, "d=" + msa_access_token, False),
        (
            "legacy-root / d= / proof",
            "https://title.auth.xboxlive.com",
            "d=" + msa_access_token,
            True,
        ),
        (
            "legacy-root / raw / no-proof",
            "https://title.auth.xboxlive.com",
            msa_access_token,
            False,
        ),
    ]
    for label, url, ticket, include_proof_key in variants:
        response = request_title_token(
            session,
            proof_key,
            ticket,
            device_token,
            timeout,
            url=url,
            include_proof_key=include_proof_key,
        )
        print(f"Title variant [{label}]: HTTP {response.status_code}")
        if response.ok and response.json().get("Token"):
            return response.json()
        diagnostic_headers = {
            key: value
            for key, value in response.headers.items()
            if key.lower().startswith(("x-xbl", "xerr", "ms-cv", "x-ms"))
        }
        if diagnostic_headers:
            print(f"  diagnostics={diagnostic_headers}")
    raise RuntimeError("all title-auth variants were rejected")


def request_xsts_token(
    session: requests.Session,
    proof_key: ProofKey,
    user_token: str,
    device_token: str,
    title_token: str,
    timeout: float,
    *,
    relying_party: str = XBOX_LIVE_RELYING_PARTY,
) -> requests.Response:
    return signed_post(
        session,
        proof_key,
        XSTS_AUTH_URL,
        {
            "Properties": {
                # This 2017 Apple SDK uses the legacy field name SandboxId.
                # The spelling is confirmed in its XSTS JSON builder at
                # 0x1011c5760; modern examples commonly use "Sandbox".
                "SandboxId": "RETAIL",
                "UserTokens": [user_token],
                "DeviceToken": device_token,
                "TitleToken": title_token,
            },
            "RelyingParty": relying_party,
            "TokenType": "JWT",
        },
        timeout,
    )


def request_achievements(
    session: requests.Session,
    proof_key: ProofKey,
    xsts_payload: dict,
    timeout: float,
) -> requests.Response:
    claims = xsts_payload["DisplayClaims"]["xui"][0]
    xuid = claims["xid"]
    user_hash = claims["uhs"]
    authorization = f"XBL3.0 x={user_hash};{xsts_payload['Token']}"
    # XSAPI's get_achievements_for_title_id always includes titleId.  Without
    # it the service accepts the request but returns an empty generic result.
    url = (
        f"https://achievements.xboxlive.com/users/xuid({xuid})/achievements"
        f"?titleId={APPLE_TV_TITLE_ID}"
    )
    headers = {
        "Authorization": authorization,
        "Accept": "application/json",
        "x-xbl-contract-version": "2",
        "Signature": proof_key.sign_request(
            "GET", url, b"", authorization=authorization
        ),
    }
    return session.get(url, headers=headers, timeout=timeout)


def run_full_probe(
    device_type: str, device_version: str, client_id: str, timeout: float
) -> int:
    session = requests.Session()
    proof_key = ProofKey()

    device = require_token(
        request_device_token(
            session, proof_key, device_type, device_version, timeout
        ),
        "Device authentication",
    )
    print("Device authentication: OK")

    print(f"Testing OAuth client identity: {client_id}")
    msa_access_token = acquire_msa_token(session, client_id, timeout)
    print("Microsoft account authentication: OK")
    rps_ticket = "d=" + msa_access_token

    user = require_token(
        request_user_token(session, proof_key, rps_ticket, timeout),
        "Xbox user authentication",
    )
    print("Xbox user authentication: OK")

    title = find_title_token(
        session,
        proof_key,
        msa_access_token,
        device["Token"],
        timeout,
    )
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
    claims = xsts.get("DisplayClaims", {}).get("xui", [{}])[0]
    print(
        "XSTS authorization: OK "
        f"(gamertag={claims.get('gtg', '<not returned>')}, "
        f"xuid={claims.get('xid', '<not returned>')})"
    )

    achievements = request_achievements(session, proof_key, xsts, timeout)
    print(f"Achievement query: HTTP {achievements.status_code}")
    try:
        achievement_payload = achievements.json()
    except ValueError:
        print(achievements.text[:4000])
        return 1
    titles = sorted(
        {
            (
                item.get("titleAssociations", [{}])[0].get("id"),
                item.get("titleAssociations", [{}])[0].get("name"),
            )
            for item in achievement_payload.get("achievements", [])
            if item.get("titleAssociations")
        }
    )
    print(f"Achievements returned: {len(achievement_payload.get('achievements', []))}")
    print(f"Title associations: {titles}")
    return 0 if achievements.ok else 1


def print_response(response: requests.Response) -> None:
    print(f"HTTP {response.status_code}")
    diagnostic_headers = {
        key: value
        for key, value in response.headers.items()
        if key.lower().startswith(("x-xbl", "xerr", "ms-cv", "x-ms"))
    }
    if diagnostic_headers:
        print("Diagnostic headers:")
        print(json.dumps(diagnostic_headers, indent=2))
    try:
        payload = response.json()
    except ValueError:
        print(response.text[:4000])
        return

    # A successful token proves the exchange worked, but printing the token itself
    # would add no diagnostic value and could leak it into logs.
    if "Token" in payload:
        payload["Token"] = f"<redacted: {len(payload['Token'])} characters>"
    print(json.dumps(payload, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--device-type",
        default="AppleTV",
        help="device type reported to Xbox (default: AppleTV)",
    )
    parser.add_argument(
        "--device-version",
        default="10.1",
        help="tvOS version reported to Xbox (default: the app's original SDK era)",
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--client-id",
        default=DEFAULT_MSA_CLIENT_ID,
        help="legacy Microsoft OAuth client ID to test",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="perform interactive Microsoft login and test Apple TV achievements",
    )
    args = parser.parse_args()

    if args.full:
        return run_full_probe(
            args.device_type,
            args.device_version,
            args.client_id,
            args.timeout,
        )

    session = requests.Session()
    proof_key = ProofKey()
    response = request_device_token(
        session,
        proof_key,
        args.device_type,
        args.device_version,
        args.timeout,
    )
    print_response(response)
    return 0 if response.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
