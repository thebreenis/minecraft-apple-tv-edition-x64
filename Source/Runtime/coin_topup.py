"""Durable checkout/recovery journal. This module cannot initiate a payment."""
import json
import threading
import time
import uuid
from pathlib import Path
from live_marketplace import Marketplace, minecoin_balance
from probe_playfab_from_bridge import get_bridge_fields
from windows_coin_redemption import redeem,redeem_windows,access_tokens

from runtime_paths import STATE as ROOT
JOURNAL=ROOT/'coin-topup-current.json'
LOCK=threading.Lock()
PRODUCT='9NHGX9G9JF1X'
OFFER='minecraft.win10.minecoin.200'
PACKS={
    PRODUCT:(320,OFFER),
    '9PD3R529P5XZ':(1020,'minecraft.win10.minecoin.600'),
    '9P063P6X637H':(1720,'minecraft.win10.minecoin.1000'),
    '9P2XG2G6PPRC':(3500,'minecraft.win10.minecoin.2000'),
    '9NJSBHH6FVRF':(8800,'minecraft.win10.minecoin.5000'),
}
POLL_SECONDS=45
RETRY_SECONDS=60
MAX_ATTEMPTS=3
WINDOWS_CONTEXT=None # Short-lived publisher tokens stay only in memory.

def balance(inv):
    return minecoin_balance(inv)

def save(record):
    temp=JOURNAL.with_suffix('.tmp')
    temp.write_text(json.dumps(record,indent=2),encoding='utf-8')
    temp.replace(JOURNAL)

def parse_pending(data):
    """Bounded native observation: count newline, then product|transaction newline."""
    lines=data.decode('ascii').splitlines()
    count=int(lines[0])
    if not 0<=count<=16 or len(lines)!=count+1:raise ValueError('Invalid pending list')
    pending=[]
    for line in lines[1:]:
        product,transaction=line.split('|')
        transaction=str(uuid.UUID(transaction.strip('{}')))
        if not product or len(product)>128:raise ValueError('Invalid product')
        pending.append({'product':product,'transaction':transaction})
    if len({p['transaction'] for p in pending})!=len(pending):raise ValueError('Duplicate receipt')
    return pending

def confirm(record, after):
    record.update(status='confirmed',balanceAfter=after,confirmedAt=time.time(),
                  reconciliation='Official Minecraft Xbox redemption endpoint')
    record.pop('errorType',None)
    save(record)
    return b'confirmed'

def reconcile(record,pending,keys=None):
    if record.get('status')=='confirmed':
        # Completed transactions never call the redemption service again.
        if not pending:
            if not record.get('nativeFulfilled'):
                record['nativeFulfilled']=True;save(record)
            return b'settled'
        if len(pending)==1 and pending[0]==record.get('nativeReceipt'):
            if record.get('xuid')!=get_bridge_fields()[2]:return b'review'
            return b'acknowledge'
        return b'review'
    if record.get('status') not in ('prepared','awaiting receipt','redeeming','requires reconciliation'):
        return b'review' if pending else b'idle'
    pack=PACKS.get(record.get('product'))
    if pack is None or record.get('coins')!=pack[0]:return b'review'
    coins,offer=pack
    if not pending and record.get('officialAttemptStarted'):
        fields=get_bridge_fields()
        if record.get('xuid')!=fields[2]:return b'review'
        after=balance(Marketplace().inventory())
        if after==record['balanceBefore']+coins:
            record['nativeFulfilled']=True
            return confirm(record,after)
        return b'pending' if after==record['balanceBefore'] else b'review'
    if len(pending)!=1 or pending[0]['product']!=offer:
        if not pending and record['status']=='prepared':
            return b'checkout-open' # A background observation is not a payment result.
        return b'pending' if not pending else b'review'
    receipt=pending[0]
    if record.get('nativeReceipt') not in (None,receipt):return b'review'
    fields=get_bridge_fields()
    if record.get('xuid') not in (None,fields[2]):return b'review'
    # Bind recovery to this account and the native transaction before networking.
    record.update(nativeReceipt=receipt,xuid=fields[2]);save(record)
    client=Marketplace()
    current=balance(client.inventory());expected=record['balanceBefore']+coins
    if record.get('officialAttemptStarted') and current==expected:
        return confirm(record,current)
    if current!=record['balanceBefore']:return b'review'
    attempts=record.get('officialAttempts',0)
    if attempts>=MAX_ATTEMPTS:return b'review'
    if attempts and time.time()-record['officialAttemptStarted']<RETRY_SECONDS:return b'pending'
    if keys is None or WINDOWS_CONTEXT is None or time.time()-WINDOWS_CONTEXT['created']>300:
        return b'keys-required'
    if WINDOWS_CONTEXT['xuid']!=fields[2]:return b'review'
    record.update(status='redeeming',officialAttempts=attempts+1,officialAttemptStarted=time.time())
    save(record) # A crash/timeout cannot erase a possibly submitted request.
    try:
        outcome=redeem_windows(client,fields,WINDOWS_CONTEXT,keys)
        record['officialResult']=outcome;save(record)
        # The official client also reconciles the Xbox collection. Only follow
        # an explicit empty Windows result, never a failed or uncertain request.
        if not any(outcome.values()) and balance(client.inventory())==record['balanceBefore']:
            outcome=redeem(client,fields)
            record['officialXboxResult']=outcome;save(record)
        deadline=time.monotonic()+POLL_SECONDS
        while True:
            after=balance(client.inventory());record['balanceAfter']=after
            if after==expected:
                return confirm(record,after)
            if after!=record['balanceBefore']:raise ValueError('Unexpected wallet change')
            if outcome['failed'] or time.monotonic()>=deadline:break
            time.sleep(3)
        record.update(status='requires reconciliation',errorType='WalletCreditNotConfirmed');save(record)
        return b'pending'
    except Exception as error:
        record.update(status='requires reconciliation',errorType=type(error).__name__);save(record)
        return b'pending'

def handle(operation,pending=None,keys=None,product=PRODUCT,attempt=None):
    global WINDOWS_CONTEXT
    with LOCK:
        record=json.loads(JOURNAL.read_text(encoding='utf-8')) if JOURNAL.exists() else {}
        if operation=='TOK1':
            fields=get_bridge_fields()
            if record.get('xuid') not in (None,fields[2]):raise ValueError('Account changed')
            WINDOWS_CONTEXT=access_tokens(fields)
            WINDOWS_CONTEXT['created']=time.time()
            return ('\n'.join(WINDOWS_CONTEXT[k] for k in ('collections','purchase','user'))+'\n').encode()
        if operation in ('ARM1','PRP1','PRP2','PRP3'):
            if product not in PACKS:raise ValueError('Unknown coin package')
            if operation not in ('PRP2','PRP3') and product!=PRODUCT:raise ValueError('Legacy package mismatch')
            # Only the new protocol supplies a fresh native pending-receipt check.
            allowed=(None,'cancelled','confirmed') if operation in ('PRP1','PRP2','PRP3') and pending==[] else (None,'cancelled')
            if operation in ('PRP1','PRP2','PRP3') and pending!=[]:raise ValueError('Windows purchase still pending')
            if record.get('status') not in allowed:raise ValueError('Previous checkout requires review')
            fields=get_bridge_fields();m=Marketplace()
            current_balance=balance(m.inventory())
            if record:
                history=JOURNAL.parent/'coin-topup-history';history.mkdir(exist_ok=True)
                attempt=uuid.UUID(record['attempt']).hex
                archive=history/(attempt+'.json')
                if archive.exists():
                    if json.loads(archive.read_text(encoding='utf-8'))!=record:raise ValueError('Purchase history mismatch')
                else:
                    with archive.open('x',encoding='utf-8') as f:json.dump(record,f,indent=2)
            record={'attempt':uuid.uuid4().hex,'product':product,'coins':PACKS[product][0],'status':'prepared',
                    'xuid':fields[2],'balanceBefore':current_balance,'started':time.time()}
            save(record)
            return ('ready:'+record['attempt']).encode() if operation=='PRP3' else b'ready'
        if operation in ('CAN2','TOP2'):
            if attempt!=record.get('attempt'):raise ValueError('Stale checkout response')
            if record.get('xuid')!=get_bridge_fields()[2]:raise ValueError('Account changed')
            if pending is None:raise ValueError('Native receipt observation required')
            if operation=='CAN2':
                if pending or record.get('nativeReceipt') or record.get('officialAttemptStarted') or record.get('status') not in ('prepared','cancelled'):
                    raise ValueError('Cannot cancel a possibly paid transaction')
                record.update(status='cancelled',checkoutResult='NotPurchased',cancelledAt=time.time());save(record)
                return b'cancelled'
            if record.get('status')!='prepared':raise ValueError('Checkout already completed')
            record.update(status='awaiting receipt',checkoutResult='SucceededOrAlreadyPurchased');save(record)
            return reconcile(record,pending,keys)
        if operation in ('REC1','TOP1','RDM1'):
            if pending is None:raise ValueError('Native pending observation required')
            if operation=='TOP1' and record.get('status')=='prepared':
                record.update(status='awaiting receipt',checkoutResult='legacy-success');save(record)
            return reconcile(record,pending,keys)
        if record.get('product') not in PACKS:raise ValueError('No prepared checkout')
        if operation=='CAN1':
            if record['status']!='prepared':raise ValueError('Cannot cancel submitted redemption')
            record['status']='cancelled';save(record)
            return b'cancelled'
        raise ValueError('Unknown checkout operation')
