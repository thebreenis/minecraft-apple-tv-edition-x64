"""Official Windows 1.21.114 Marketplace restore requests; never initiate checkout."""
import json
import requests
ENDPOINT = 'https://entitlements.mktpl.minecraft-services.net/api/v1.0/transaction/redeem/xbox'

def access_tokens(fields):
    response=requests.post('https://20ca2.playfabapi.com/Client/LoginWithXbox',
        json={'TitleId':'20CA2','CreateAccount':False,'XboxToken':fields[0]},timeout=30)
    response.raise_for_status();entity=response.json()['data']['EntityToken']
    response=requests.post('https://20ca2.playfabapi.com/inventory/GetAccessTokens',
        headers={'X-EntityToken':entity['EntityToken']},
        json={'MarketplaceConfigInstance':'Secondary','TargetMarketplace':'onestore'},timeout=30)
    response.raise_for_status();tokens=response.json()['data']['AccessTokens']
    return {'collections':tokens['Collections']['Token'],'purchase':tokens['Purchase']['Token'],
            'user':'anonymous','xuid':fields[2]}

def summary(response):
    response.raise_for_status()
    result=response.json().get('result')
    if not isinstance(result,dict) or any(not isinstance(result.get(k),list)
            for k in ('failed','succeeded','transactionIds')):
        raise ValueError('Unexpected fulfillment response')
    return {k:len(result[k]) for k in ('failed','succeeded','transactionIds')}

def redeem_windows(client,fields,context,keys):
    if context['xuid']!=fields[2]:raise ValueError('Store context account changed')
    if any(not isinstance(keys.get(k),str) or not 100<len(keys[k])<32768
           for k in ('collections','purchase')):raise ValueError('Native Store keys missing')
    receipt={'access_token':context['collections'],'actor':'','collections_token':keys['collections'],
             'inventory':'','purchase_token':keys['purchase'],'sandbox':'RETAIL'}
    payload={'customTags':{'BuildPlat':7,'ClientId':client.session_id,'CorrelationId':'',
                'DeviceSessionId':client.session_id,'Seq':1,'TitleId':'20ca2','Xuid':fields[2],'editionType':'vanilla'},
             'purchaseInfo':{'active':True,'correlationId':'','passSubscription':False,
                'platformPurchaseId':'','productSku':'','purchasePath':-1,
                'receipt':json.dumps(receipt,separators=(',',':')),'renewal':False}}
    return summary(client.http.put(ENDPOINT.removesuffix('/xbox'),headers=client.headers,json=payload,timeout=45))

def redeem(client, fields):
    token, xuid = fields[0], fields[2]
    if not token or not xuid.isdecimal():
        raise ValueError('Existing Xbox session unavailable')
    payload = {
        'CustomTags': {'BuildPlat': 7, 'ClientId': client.session_id,
            'DeviceSessionId': client.session_id, 'Seq': 1, 'TitleId': '20ca2',
            'Xuid': xuid, 'editionType': 'vanilla'},
        'Redemption': {'MarketplaceData': {'userId': xuid,
            'xboxAuthentication': {'XboxToken': token}}},
    }
    response = client.http.post(ENDPOINT, headers=client.headers, json=payload, timeout=45)
    return summary(response)
