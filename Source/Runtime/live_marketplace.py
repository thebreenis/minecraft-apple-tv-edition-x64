"""Existing-session Minecraft commerce access. No credentials written to disk."""
import json
import uuid
from pathlib import Path
import requests
from probe_playfab_from_bridge import get_bridge_fields

ROOT = Path(__file__).parent
FREE_ID = 'c8d1696b-87cb-4904-9952-2be30ef5d763'
ENTITLEMENTS = 'https://entitlements.mktpl.minecraft-services.net/api/v1.0'
STORE = 'https://store.mktpl.minecraft-services.net/api/v1.0'

def objects(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from objects(child)
    elif isinstance(value, list):
        for child in value:
            yield from objects(child)

def minecoin_balance(inventory):
    """An explicit empty currency collection represents an unfunded wallet.

    Missing data or partial/duplicate Minecoin stacks must not become zero.
    """
    rows=inventory.get('virtualCurrencyBalances')
    if not isinstance(rows,list):raise ValueError('Missing currency balances')
    if not rows:return 0
    coins=[row for row in rows if row.get('type')=='Minecoin']
    totals=[row.get('amount') for row in coins if row.get('currencyStack')=='Total']
    if len(totals)!=1 or type(totals[0]) is not int or totals[0]<0:
        raise ValueError('No authoritative wallet balance')
    return totals[0]


class Marketplace:
    def __init__(self):
        self.http = requests.Session()
        r = self.http.post('https://20ca2.playfabapi.com/Client/LoginWithXbox', json={
            'TitleId':'20CA2', 'CreateAccount':False, 'XboxToken':get_bridge_fields()[0]}, timeout=30)
        r.raise_for_status()
        ticket = r.json()['data']['SessionTicket']
        self.session_id = str(uuid.uuid4())
        r = self.http.post('https://authorization.franchise.minecraft-services.net/api/v1.0/session/start', json={
            'user':{'language':'en','languageCode':'en-US','regionCode':'US','token':ticket,'tokenType':'PlayFab'},
            'device':{'applicationType':'MinecraftPE','memory':'8589934592','id':self.session_id,
                'gameVersion':'1.26.52','platform':'Windows10','playFabTitleId':'20CA2','storePlatform':'uwp.store','type':'Windows10'}}, timeout=30)
        r.raise_for_status()
        self.headers = {'Authorization':r.json()['result']['authorizationHeader'], 'session-id':self.session_id}

    def inventory(self):
        r = self.http.get(ENTITLEMENTS+'/player/inventory', headers=self.headers, timeout=30)
        r.raise_for_status()
        return r.json()['result']['inventory']

    def detail(self, item_id):
        r = self.http.post(STORE+'/layout/pages/ItemDetail_'+item_id, headers=self.headers, json={}, timeout=30)
        r.raise_for_status()
        return r.json()

    def acquire_wilderness_free(self):
        # Deliberately no generic purchase method. Only the user's exact free item.
        detail = self.detail(FREE_ID)
        item = next(x for x in objects(detail) if x.get('id') == FREE_ID and 'price' in x)
        price = item['price']
        if price.get('listPrice') != 0 or price.get('virtualCurrencyType') != 'Minecoin':
            raise RuntimeError('Refusing acquisition: live item is not free')
        before = self.inventory()
        owned = [x for x in before['entitlements'] if FREE_ID in x.get('offerIds', [])]
        if not owned:
            r = self.http.post(ENTITLEMENTS+'/transaction/virtual', headers={**self.headers,
                'inventoryversion':before['inventoryVersion'], 'inventoryetag':before['inventoryVersion']},
                json={'OfferId':FREE_ID,'StoreId':'','VirtualCurrency':{'Amount':'0','Type':'Minecoin'},
                    'CustomTags':{'BuildPlat':'UWP','ClientId':'AppleTVCompatibility','CorrelationId':uuid.uuid4().hex,
                        'DeviceSessionId':self.session_id,'Seq':1,'TitleId':'20ca2','editionType':'vanilla'}}, timeout=45)
            print('Free transaction HTTP', r.status_code)
            if not r.ok:
                print('Service error', r.text[:1500])
            r.raise_for_status()
            (ROOT/'wilderness-free-transaction.json').write_text(json.dumps(r.json(), indent=2))
        after = self.inventory()
        owned = [x for x in after['entitlements'] if FREE_ID in x.get('offerIds', [])]
        if not owned:
            raise RuntimeError('Free item not confirmed in account inventory')
        if before['virtualCurrencyBalances'] != after['virtualCurrencyBalances']:
            raise RuntimeError('Balance changed; stop further acquisition work')
        (ROOT/'live-inventory-sanitized.json').write_text(json.dumps(after, indent=2))
        (ROOT/('live-owned-ItemDetail_'+FREE_ID+'.json')).write_text(json.dumps(self.detail(FREE_ID), indent=2))
        print('Confirmed owned', FREE_ID, 'balances unchanged', after['virtualCurrencyBalances'])
        return after

if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--acquire-wilderness-free', action='store_true')
    args=parser.parse_args()
    client=Marketplace()
    if args.acquire_wilderness_free:
        client.acquire_wilderness_free()
    else:
        inv=client.inventory()
        (ROOT/'live-inventory-sanitized.json').write_text(json.dumps(inv, indent=2))
        print('Balances',inv['virtualCurrencyBalances'],'entitlements',len(inv['entitlements']))
