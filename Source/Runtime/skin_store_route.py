"""Resolve installed skin packs through the official pack-ID Store lookup."""
import json,re,uuid,shutil,time
import requests
from pathlib import Path
from live_marketplace import STORE,objects
from wallet_https_server import _legacy_document

def installed_packs(pack):
    result={}
    for manifest in (pack/'data/resource_packs').rglob('manifest.json'):
        d=json.loads(manifest.read_text(encoding='utf-8-sig'))
        if not any(x.get('type')=='skin_pack' for x in d.get('modules',[])):continue
        h=d['header'];pid=str(uuid.UUID(h['uuid']));names={h.get('name',''),manifest.parent.name}
        skins=manifest.parent/'skins.json'
        if skins.exists():
            s=json.loads(skins.read_text(encoding='utf-8-sig'));names.update([s.get('serialize_name',''),s.get('localization_name','')])
        translations={}
        lang=manifest.parent/'texts/en_US.lang'
        if lang.exists():
            translations=dict(line.split('=',1) for line in lang.read_text(encoding='utf-8-sig').splitlines() if '=' in line)
        for name in names-{'','Base','pack.name','PLACE_HOLDER'}:
            key=name if name.startswith('skinpack.') else 'skinpack.'+name
            result[key]={'uuid':pid,'name':translations.get(key,translations.get('pack.name',name))}
    return result

CACHE={}
def resolve(adapter,key):
    key,sep,runtime_uuid=key.partition('|')
    if not re.fullmatch(r'skinpack\.[A-Za-z0-9_.-]+',key):raise ValueError('Invalid skin key')
    if sep:runtime_uuid=str(uuid.UUID(runtime_uuid))
    packs=installed_packs(adapter.PACK)
    pack=packs.get(key)
    # Legacy built-in packs may have replacement manifests with different UUIDs.
    # The local game's selected pack identity is authoritative for this lookup.
    if pack and sep:pack=dict(pack,uuid=runtime_uuid)
    if not pack:
        if not sep:raise ValueError('Unknown installed skin pack')
        name=re.sub(r'(?<=[a-z])(?=[A-Z])',' ',key.removeprefix('skinpack.'))
        for lang in (adapter.PACK/'data/resource_packs').rglob('en_US.lang'):
            for line in lang.read_text(encoding='utf-8-sig').splitlines():
                if line.startswith(key+'='):name=line.split('=',1)[1]
        pack={'uuid':runtime_uuid,'name':name}
    for lang in (adapter.PACK/'data/resource_packs/vanilla/texts').glob('en_US.lang'):
        for line in lang.read_text(encoding='utf-8-sig').splitlines():
            if line.startswith(key+'='):pack=dict(pack,name=line.split('=',1)[1])
    cache_key=(key,pack['uuid'])
    if cache_key in CACHE and time.monotonic()-CACHE[cache_key][0]<300:return CACHE[cache_key][1]
    adapter.refresh()
    failed=False
    try:
        r=adapter.client.http.post(STORE+'/layout/pages/packId/'+pack['uuid'],headers=adapter.client.headers,json={},timeout=20)
        if r.status_code!=404:r.raise_for_status()
    except requests.RequestException:
        failed=True
    if failed or r.status_code==404:
        # A missing live listing is not permission to grant or sell the pack.
        oid=pack['uuid']
        item={'id':oid,'title':pack['name'],'description':"This skin pack is not currently available from the Marketplace.",'packType':'SkinPack','packIdentity':[{'type':'skinpack','uuid':pack['uuid'],'version':'1.0.0'}]}
        if failed:item['description']='The Marketplace could not be reached. Close this screen and try again.'
        page={}
    else:
        r.raise_for_status();page=r.json();oid=page['result']['id']
        item=next(x for x in objects(page) if x.get('id')==oid and x.get('contentType')=='MarketplaceDurableCatalog_V1.2')
        if not any(x.get('uuid')==pack['uuid'] for x in item.get('packIdentity',[])):raise ValueError('Skin identity mismatch')
        item=dict(item)
        if not item.get('description'):
            item['description']=next((x['description'] for x in objects(page) if x.get('type')=='itemDescriptionComp' and x.get('description')),'')
    doc=_legacy_document(item);doc['tags']=[oid,'skinpack'];doc['id']=oid
    # Use the adapter's dynamic gallery service, which understands new offers.
    url=item.get('thumbnail',{}).get('url','')
    if url.startswith('https://'):
        name=oid+'-9000.png';(adapter.CACHE/(name+'.url')).write_text(url,encoding='utf-8')
        url='http://127.0.0.1:32123/'+name
        doc['thumbnailUrl']=url;doc['images']=[{'imagePurpose':'Thumbnail','uri':url,'url':url}]
    else:
        name=oid+'-9001.png'
        shutil.copyfile(adapter.PACK/'data/resource_packs/vanilla/textures/gui/newgui/lock.png',adapter.CACHE/name)
        url='http://127.0.0.1:32123/'+name
        doc['thumbnailUrl']=url;doc['images']=[{'imagePurpose':'Thumbnail','uri':url,'url':url}]
    adapter.docs[oid]=doc;adapter.modern[oid]=item
    detail=json.loads(json.dumps(doc))
    for field in ('title','description'):detail[field]={'en-US':detail[field]}
    images=[]
    gallery=next((x for x in objects(page) if x.get('controlId')=='ImageGallery'),{})
    for entry in objects(gallery):
        u=entry.get('url','')
        if u.startswith('https://') and u not in images:images.append(u)
    for i,u in enumerate(images):
        name=f'{oid}-{9100+i}.png';(adapter.CACHE/(name+'.url')).write_text(u,encoding='utf-8')
        local='http://127.0.0.1:32123/'+name
        detail['images'].append({'imagePurpose':'screenshot','uri':local,'url':local})
    (adapter.DOCS/(oid+'.json')).write_text(json.dumps(detail),encoding='utf-8')
    print('Skin route',key,oid,'retry' if failed else ('available' if r.ok else 'unavailable'),flush=True)
    body=json.dumps({'count':1,'results':[{'score':1.0,'document':doc}]}).encode()
    if not failed:CACHE[cache_key]=(time.monotonic(),body)
    return body
