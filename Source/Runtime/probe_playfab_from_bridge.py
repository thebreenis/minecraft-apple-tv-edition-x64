"""Probe Minecraft PlayFab with the in-memory bridge credential; print no token."""

import json
import socket
import struct

import requests


HOST = "127.0.0.1"
PORT = 32115
PLAYFAB_URL = "https://20CA2.playfabapi.com/Client/LoginWithXbox"


def read_exact(stream, size: int) -> bytes:
    value = stream.read(size)
    if len(value) != size:
        raise ConnectionError("bridge disconnected")
    return value


def get_bridge_fields() -> list[str]:
    fields = [b"POST", b"https://20CA2.playfabapi.com/", b"", b""]
    with socket.create_connection((HOST, PORT), timeout=5) as client:
        stream = client.makefile("rwb", buffering=0)
        stream.write(b"ATVB1")
        stream.write(struct.pack("<IIII", *(len(value) for value in fields)))
        for value in fields:
            stream.write(value)
        status = struct.unpack("<I", read_exact(stream, 4))[0]
        sizes = struct.unpack("<IIIIIII", read_exact(stream, 28))
        values = [read_exact(stream, size).decode("utf-8") for size in sizes]
        if status:
            raise RuntimeError(values[0])
        return values


def main() -> None:
    authorization, _signature, *_identity = get_bridge_fields()
    response = requests.post(
        PLAYFAB_URL,
        headers={"Content-Type": "application/json"},
        json={
            "CreateAccount": True,
            "XboxToken": authorization,
            "InfoRequestParameters": {
                "GetUserInventory": True,
                "GetUserVirtualCurrency": True,
            },
        },
        timeout=30,
    )
    try:
        payload = response.json()
    except ValueError:
        payload = {"nonJsonBody": response.text[:500]}
    safe = {
        "httpStatus": response.status_code,
        "code": payload.get("code"),
        "status": payload.get("status"),
        "error": payload.get("error"),
        "errorCode": payload.get("errorCode"),
        "errorMessage": payload.get("errorMessage"),
        "dataKeys": sorted((payload.get("data") or {}).keys()),
    }
    print(json.dumps(safe, indent=2))


if __name__ == "__main__":
    main()
