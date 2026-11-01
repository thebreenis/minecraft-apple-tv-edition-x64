"""Reconstruct the private game payload from two user-supplied archives.

Only authored changes and public asset URLs are distributed with this tool.
No decryption, account access, installation, or certificate changes occur here.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import urllib.request
import zipfile
from PIL import Image
from revert_cgbi_png import convert

PREFIX = 'Payload/minecraftappletv.app/'
SKIP = {'AppxSignature.p7x', 'AppxBlockMap.xml', '[Content_Types].xml',
        'AppxMetadata/CodeIntegrity.cat'}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def safe_target(root, name):
    name = name.replace('\\', '/')
    if ':' in name or name.startswith('/') or any(p in ('..', '') for p in name.split('/')):
        raise ValueError('Unsafe archive path: ' + name)
    target = (root / name).resolve()
    if not target.is_relative_to(root.resolve()):
        raise ValueError('Archive path escapes output')
    return target

def apply_edits(data, patch):
    if digest(data) != patch['before']:
        raise ValueError('Patch input mismatch: ' + patch['path'])
    chunks = []; previous = 0
    for start, end, replacement in patch['edits']:
        if start < previous or end < start or end > len(data):
            raise ValueError('Invalid patch bounds')
        chunks.extend((data[previous:start], bytes.fromhex(replacement)))
        previous = end
    result = b''.join(chunks) + data[previous:]
    if digest(result) != patch['after']:
        raise ValueError('Patch output mismatch: ' + patch['path'])
    return result

def apple_image(archive, name, temporary):
    src = temporary / 'input.png'; dst = temporary / 'output.png'
    src.write_bytes(archive.read(PREFIX + name))
    convert(src, dst)
    with Image.open(dst) as im:
        return im.convert('RGBA')

def cover(image, size):
    scale = max(size[0] / image.width, size[1] / image.height)
    result = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    x = (result.width-size[0])//2; y = (result.height-size[1])//2
    return result.crop((x,y,x+size[0],y+size[1]))

def generate_art(apple, stage):
    with tempfile.TemporaryDirectory(prefix='AppleTV-Art-') as td:
        temp = Path(td)
        wide = apple_image(apple, 'MC_AppleTV_Icon_Large_1280x768_Static.png', temp)
        background = apple_image(apple, 'MC_AppleTV_Icon_Large_1280x768_Background.png', temp)
        square = cover(background, (1280,1280))
        launch = apple_image(apple, 'LaunchImage.png', temp)
        title = apple_image(apple, 'data/resource_packs/vanilla/textures/gui/titleAppleTV.png', temp)
        logo = title.copy(); logo.thumbnail((1120,270), Image.Resampling.LANCZOS)
        square.alpha_composite(logo, ((1280-logo.width)//2,(1280-logo.height)//2))
        icons = stage/'UAP.Assets/minecraft/icons'
        sizes = {
            'Logo': {100:(44,44),200:(88,88),400:(176,176)},
            'MCLogo': {100:(150,150),200:(300,300),400:(600,600)},
            'MCSmallLogo': {100:(44,44),200:(88,88),400:(176,176)},
            'MCSquare71x71Logo': {100:(71,71),200:(142,142),400:(284,284)},
            'MCSquare310x310Logo': {100:(310,310)}, 'Square310x310Logo': {100:(310,310)},
            'MCStoreLogo': {400:(200,200)},
            'MCSquare44x44Logo': {100:(44,44),200:(44,44),400:(88,88)},
            'MCWide310x150Logo': {100:(310,150),200:(620,300),400:(1240,600)}}
        for stem, variants in sizes.items():
            for scale,size in variants.items():
                cover(wide if 'Wide' in stem else square,size).save(icons/f'{stem}.scale-{scale}.png',optimize=True)
        canvas=Image.new('RGBA',(1240,600),'white');launch.thumbnail(canvas.size,Image.Resampling.LANCZOS)
        canvas.alpha_composite(launch,((canvas.width-launch.width)//2,(canvas.height-launch.height)//2))
        canvas.save(icons/'MCSplashScreen.scale-200.png',optimize=True)
        title.save(stage/'data/resource_packs/vanilla/textures/gui/titleAppleTV.png',optimize=True)

def download_art(backend, stage):
    folder=stage/'data/resource_packs/vanilla/textures/ui/minecoin_offers';folder.mkdir(parents=True,exist_ok=True)
    Image.new('RGBA',(16,16),(255,255,255,255)).save(folder/'solid.png')
    for item in json.loads((backend/'CoinArtwork.json').read_text()):
        url=item['url']
        if not url.startswith(('https://xforgeassets001.xboxlive.com/','https://xforgeassets002.xboxlive.com/')):
            raise ValueError('Unexpected artwork host')
        print('Obtaining public Minecoin artwork:',item['amount'],flush=True)
        with urllib.request.urlopen(url,timeout=45) as response:
            data=response.read(8*1024*1024+1)
        if len(data)>8*1024*1024:raise ValueError('Artwork too large')
        if item.get('sha256') and digest(data)!=item['sha256']:raise ValueError('Coin artwork changed; update the builder')
        with Image.open(io.BytesIO(data)) as im:
            im.convert('RGBA').save(folder/f"{item['amount']}.png",'PNG',optimize=True)

def reconstruct(desktop, apple, backend, stage):
    config=json.loads((backend/'Build.json').read_text())
    for path,field in [(desktop,'DesktopSHA256'),(apple,'AppleTVSHA256')]:
        with path.open('rb') as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
        if actual.upper()!=config[field]:raise ValueError('Unsupported input checksum: '+path.name)
    if stage.exists():raise ValueError('Output staging folder must be new')
    with zipfile.ZipFile(desktop) as windows,zipfile.ZipFile(apple) as tv:
        info=plistlib.loads(tv.read(PREFIX+'Info.plist'))
        if info.get('CFBundleIdentifier')!='com.mojang.minecraftappletv':raise ValueError('Not the supported Apple TV package')
        stage.mkdir(parents=True);seen=set()
        for entry in windows.infolist():
            name=entry.filename.replace('\\','/')
            if entry.is_dir() or name in SKIP:continue
            if name.lower() in seen:raise ValueError('Duplicate archive path')
            seen.add(name.lower());target=safe_target(stage,name);target.parent.mkdir(parents=True,exist_ok=True)
            with windows.open(entry) as src,target.open('wb') as dst:shutil.copyfileobj(src,dst)
        for patch in json.loads((backend/'Patches.json').read_text()):
            target=safe_target(stage,patch['path']);target.write_bytes(apply_edits(target.read_bytes(),patch))
        generate_art(tv,stage)
    for src in (backend/'Payload').iterdir():
        if src.is_file():shutil.copy2(src,stage/src.name)
    download_art(backend,stage)
    print('Reconstruction completed; original inputs unchanged.',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for name in ['desktop','apple','backend','stage']:parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    try:reconstruct(args.desktop,args.apple,args.backend,args.stage)
    except Exception as error:
        print('Reconstruction failed:',error,flush=True)
        raise SystemExit(1)
