"""Temporary HTTPS replacement for Minecraft's retired legacy wallet host."""

import argparse
import json
import socket
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# The local Python installation globally injects pip's client-side truststore
# wrapper into ssl.SSLContext.  That wrapper tries to verify a peer certificate
# even for server sockets, so restore the standard-library TLS context here.
try:
    from pip._vendor import truststore

    truststore.extract_from_ssl()
except (ImportError, AttributeError):
    pass


WALLET = {
    "Balance": {"currencies": []},
    "EntitlementReceipt": "",
    "Inventory": {"items": []},
}

EMPTY_LEGACY_CATALOG = {
    "count": 0,
    "results": [],
}


def _modern_items() -> list[dict]:
    from runtime_paths import CATALOG
    cache_path = CATALOG
    if not cache_path.exists():
        return []
    payload = json.loads(cache_path.read_text(encoding="utf-8"))
    items: dict[str, dict] = {}

    def walk(value: object) -> None:
        if isinstance(value, dict):
            item_id = value.get("id")
            if (
                isinstance(item_id, str)
                and value.get("contentType") == "MarketplaceDurableCatalog_V1.2"
                and isinstance(value.get("title"), str)
            ):
                items[item_id] = value
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    walk(payload)
    return list(items.values())


def _legacy_document(item: dict) -> dict:
    thumbnail = item.get("thumbnail") or {}
    original_thumbnail_url = (
        thumbnail.get("url", "") if isinstance(thumbnail, dict) else ""
    )
    thumbnail_url = (
        "http://127.0.0.1:32117/marketplace-thumbnail.png?id=" + item["id"]
        if original_thumbnail_url else ""
    )
    creator_page = item.get("creatorPage", "")
    creator_id = creator_page.split("!", 1)[-1] if "!" in creator_page else creator_page
    price_info = item.get("price") or {}
    price = price_info.get("listPrice", 0) if isinstance(price_info, dict) else 0
    start_date = item.get("startDate") or "2017-01-01T00:00:00Z"
    legacy_pack_type = {
        "SkinPack": "skinpack",
        "ResourcePack": "resourcepack",
        "WorldTemplate": "worldtemplate",
        # A mash-up contains a world template plus one or more companion packs.
        "MashUp": "worldtemplate",
    }.get(item.get("packType"), "")
    image = {
        "imagePurpose": "Thumbnail",
        "uri": thumbnail_url,
        "url": thumbnail_url,
    }
    return {
        "productId": item["id"],
        "documentId": item["id"],
        "title": item.get("title", ""),
        "description": item.get("description", ""),
        "contentType": "MarketplaceDurableCatalog_V1.1",
        "scids": ["00000000-0000-0000-0000-00006d646e7b"],
        "platforms": ["appletv.store", "uwp.store", "uwp.store.mobile"],
        "tags": list(item.get("tags") or []) + [
            item["id"], legacy_pack_type
        ],
        "visibility": "Public",
        "creatorId": creator_id,
        "creatorName": item.get("creatorName", ""),
        "creatorGamertag": item.get("creatorName", ""),
        "thumbnailUrl": thumbnail_url,
        "images": [image] if thumbnail_url else [],
        "contents": [],
        "creationDate": start_date,
        "startDate": start_date,
        "lastModifiedDate": start_date,
        "version": "1.1.5",
        "minClientVersion": "1.1.5",
        "clientVersion": "1.1.5",
        "price": price,
        "custom": {
            "creatorName": item.get("creatorName", ""),
            "price": price,
            "packType": legacy_pack_type,
            "purchasable": True,
            "storePriority": 0,
            "minPerformanceTier": 0,
            "rating": item.get("rating") or {},
            "packIdentity": item.get("packIdentity") or [],
        },
    }


def _legacy_query_manifest() -> dict:
    """Bootstrap the Store rows expected by the 1.1.5 catalog client."""
    return {
        "productId": "mctestdefault",
        "documentId": "mctestdefault",
        "title": "Minecraft Store",
        "description": "Legacy Marketplace query manifest",
        "contentType": "QueryManifest_V1.1.5",
        "scids": ["00000000-0000-0000-0000-00006d646e7b"],
        "platforms": ["appletv.store", "uwp.store", "uwp.store.mobile"],
        "tags": ["mctestdefault"],
        "visibility": "Public",
        "creatorId": "Microsoft",
        "creatorName": "Minecraft",
        "thumbnailUrl": "",
        "images": [],
        "contents": [],
        "creationDate": "2017-01-01T00:00:00Z",
        "lastModifiedDate": "2017-01-01T00:00:00Z",
        "version": "1.1.5",
        "minClientVersion": "1.1.5",
        "clientVersion": "1.1.5",
        "price": 0,
        "custom": {
            "queryOrder": ["Featured"],
            "queries": [
                {
                    "searchCriteria": [],
                    "tags": "",
                    "sort": "storePriority",
                    "itemOrder": [],
                    "isNavButtonQuery": False,
                    "rowName": "Featured",
                    "storePriority": 0,
                    "featured": True,
                    "purchasable": True,
                    "panorama": False,
                }
            ],
        },
    }


_SUPPORTED_PACK_TYPES = {"SkinPack", "ResourcePack", "WorldTemplate", "MashUp"}
_PACK_TYPE_ORDER = {"ResourcePack": 0, "WorldTemplate": 1, "SkinPack": 2, "MashUp": 3}
_SUPPORTED_ITEMS = [
    item for item in _modern_items()
    if item.get("packType") in _SUPPORTED_PACK_TYPES
]
_SUPPORTED_ITEMS.sort(key=lambda item: _PACK_TYPE_ORDER[item["packType"]])

LEGACY_DOCUMENTS = [
    _legacy_query_manifest(),
    *[_legacy_document(item) for item in _SUPPORTED_ITEMS],
]
LEGACY_BY_ID = {item["productId"]: item for item in LEGACY_DOCUMENTS}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _reply(self) -> None:
        host = self.headers.get("Host", "").split(":", 1)[0].lower()
        is_wallet = (
            host == "minecraft-wallet.commerce.gameservices.xboxlive.com"
            or self.path.rstrip("/") == ""
        )
        request_body = b""
        length = int(self.headers.get("Content-Length", "0"))
        if length:
            request_body = self.rfile.read(length)
        if is_wallet:
            payload = WALLET
        elif self.path.rstrip("/").endswith("/v1/catalog/items/search"):
            requested_top = len(LEGACY_DOCUMENTS)
            try:
                requested_top = int(json.loads(request_body or b"{}").get("top", requested_top))
            except (ValueError, TypeError, AttributeError):
                pass
            documents = LEGACY_DOCUMENTS[: max(0, requested_top)]
            payload = {
                "count": len(LEGACY_DOCUMENTS),
                "results": [{"score": 1.0, "document": item} for item in documents],
            }
        elif "/v1/catalog/items/" in self.path:
            product_id = self.path.rstrip("/").rsplit("/", 1)[-1].split("?", 1)[0]
            payload = LEGACY_BY_ID.get(product_id, {})
        else:
            payload = EMPTY_LEGACY_CATALOG
        body = json.dumps(payload, separators=(",", ":")).encode()
        print(f"{self.command} host={host} path={self.path}", flush=True)
        for name in (
            "Accept",
            "Authorization",
            "Content-Type",
            "User-Agent",
            "Signature",
            "x-xbl-contract-version",
        ):
            value = self.headers.get(name)
            if value:
                if name.lower() in {"authorization", "signature"}:
                    value = f"<{len(value)} chars>"
                print(f"  {name}: {value}", flush=True)
        if request_body:
            print(f"  Body: {request_body[:8192].decode('utf-8', 'replace')}", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        self._reply()

    def do_POST(self) -> None:
        self._reply()

    def log_message(self, format: str, *args: object) -> None:
        print(format % args, flush=True)


class DiagnosticHTTPSServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], handler, context: ssl.SSLContext):
        self.tls_context = context
        super().__init__(server_address, handler)
        self.socket = context.wrap_socket(self.socket, server_side=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.verify_mode = ssl.CERT_NONE
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    server = DiagnosticHTTPSServer(("0.0.0.0", 443), Handler, context)
    print(
        f"Legacy HTTPS compatibility service listening on 0.0.0.0:443 "
        f"with {len(LEGACY_DOCUMENTS)} translated Marketplace items",
        flush=True,
    )
    server.serve_forever()


if __name__ == "__main__":
    main()
