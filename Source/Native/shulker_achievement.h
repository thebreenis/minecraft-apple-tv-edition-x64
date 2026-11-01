// Repair local server -> client Shulker attacker provenance. Never award directly.
// Runtime actor ID is the identity carried by native effect packets. A short
// lifetime prevents a hit being reused after a later world/session transition.
struct ShulkerHit { unsigned long long actor=0, tick=0; bool valid=false; };
SRWLOCK g_shulkerHitLock=SRWLOCK_INIT;
ShulkerHit g_shulkerHits[16]{};
using NativeDamage=bool(__fastcall*)(void*,void*,int,bool);
using NativeEffect=void(__fastcall*)(void*,void*);
NativeDamage g_originalShulkerDamage=nullptr;
NativeEffect g_originalShulkerEffectAdd=nullptr,g_originalShulkerEffectUpdate=nullptr;
bool IsClientActor(unsigned char* actor) {
    auto level=*reinterpret_cast<unsigned char**>(actor+0x58);
    return level && level[0x1830]!=0;
}
bool __fastcall TrackShulkerDamage(void* actor,void* damage,int amount,bool flag) {
    bool accepted=g_originalShulkerDamage(actor,damage,amount,flag);
    auto p=static_cast<unsigned char*>(actor);
    if(!IsClientActor(p)) {
        auto id=*reinterpret_cast<unsigned long long*>(p+0xDD0);
        auto now=GetTickCount64();
        bool shulker=accepted && *reinterpret_cast<unsigned*>(p+0x1524)==0x40004C;
        AcquireSRWLockExclusive(&g_shulkerHitLock);
        ShulkerHit* slot=nullptr;
        for(auto& hit:g_shulkerHits) {
            if(hit.valid && hit.actor==id){slot=&hit;break;}
            if(!slot && (!hit.valid || now-hit.tick>2000))slot=&hit;
        }
        if(slot){slot->actor=id;slot->tick=now;slot->valid=shulker;}
        ReleaseSRWLockExclusive(&g_shulkerHitLock);
    }
    return accepted;
}
void SyncShulkerEffect(void* actor,void* effect) {
    auto p=static_cast<unsigned char*>(actor);
    if(*static_cast<unsigned*>(effect)!=24 || !IsClientActor(p))return;
    auto id=*reinterpret_cast<unsigned long long*>(p+0xDD0);
    auto now=GetTickCount64();bool matched=false;
    AcquireSRWLockShared(&g_shulkerHitLock);
    for(const auto& hit:g_shulkerHits)
        if(hit.valid && hit.actor==id && now-hit.tick<=2000){matched=true;break;}
    ReleaseSRWLockShared(&g_shulkerHitLock);
    if(matched)*reinterpret_cast<unsigned*>(p+0x1524)=0x40004C;
}
void __fastcall AddShulkerEffect(void* actor,void* effect) {
    SyncShulkerEffect(actor,effect);g_originalShulkerEffectAdd(actor,effect);
}
void __fastcall UpdateShulkerEffect(void* actor,void* effect) {
    SyncShulkerEffect(actor,effect);g_originalShulkerEffectUpdate(actor,effect);
}
void InstallShulkerSourceSync(unsigned char* base) {
    // Whole prologues, no RIP-relative instructions within copied bytes.
    const unsigned char damage[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x81,0xEC,0xB0,0,0,0};
    const unsigned char add[]={0x40,0x57,0x48,0x83,0xEC,0x60,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,0xFF};
    const unsigned char update[]={0x48,0x8B,0xC4,0x57,0x48,0x83,0xEC,0x60,0x48,0xC7,0x40,0xB8,0xFE,0xFF,0xFF,0xFF};
    if(std::memcmp(base+0xA47C20,damage,sizeof(damage)) ||
       std::memcmp(base+0xA246A0,add,sizeof(add)) ||
       std::memcmp(base+0xA247D0,update,sizeof(update)))return;
    g_originalShulkerDamage=reinterpret_cast<NativeDamage>(HookAchievementFunction(base+0xA47C20,sizeof(damage),reinterpret_cast<void*>(&TrackShulkerDamage)));
    g_originalShulkerEffectAdd=reinterpret_cast<NativeEffect>(HookAchievementFunction(base+0xA246A0,sizeof(add),reinterpret_cast<void*>(&AddShulkerEffect)));
    g_originalShulkerEffectUpdate=reinterpret_cast<NativeEffect>(HookAchievementFunction(base+0xA247D0,sizeof(update),reinterpret_cast<void*>(&UpdateShulkerEffect)));
}
