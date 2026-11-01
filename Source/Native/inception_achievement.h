// Inception's server-side puzzle succeeds but its local-player lookup is null.
// Queue only that naturally generated completion, then deliver on the matching
// local player's next tick. No player pointer is retained across calls.
struct InceptionPending { unsigned long long ids[16]{},tick=0; size_t count=0; };
SRWLOCK g_inceptionLock=SRWLOCK_INIT;
InceptionPending g_inceptionPending{},g_breedingPending{};
using PlayerTick=void(__fastcall*)(void*);
PlayerTick g_originalInceptionTick=nullptr;
void* __fastcall ResolveInceptionPlayer(void* level) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    void* result=reinterpret_cast<void*(__fastcall*)(void*)>(base+0xBA3F80)(level);
    if(result)return result;
    auto p=static_cast<unsigned char*>(level);
    if(!p || p[0x1830]!=0)return nullptr;
    auto begin=*reinterpret_cast<unsigned char***>(p+0x30);
    auto end=*reinterpret_cast<unsigned char***>(p+0x38);
    auto count=(reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(begin))/sizeof(void*);
    if(!begin || count<1 || count>16)return nullptr;
    InceptionPending next{};next.tick=GetTickCount64();next.count=count;
    for(size_t i=0;i<count;++i)next.ids[i]=*reinterpret_cast<unsigned long long*>(begin[i]+0xDD0);
    AcquireSRWLockExclusive(&g_inceptionLock);g_inceptionPending=next;ReleaseSRWLockExclusive(&g_inceptionLock);
    return nullptr;
}
bool TakePendingCompletion(InceptionPending& pending,unsigned long long id,unsigned long long now) {
    bool deliver=false;
    AcquireSRWLockExclusive(&g_inceptionLock);
    if(pending.count && now-pending.tick<=500) {
        for(size_t i=0;i<pending.count;++i)if(pending.ids[i]==id){deliver=true;break;}
    }
    if(deliver || now-pending.tick>500)pending.count=0;
    ReleaseSRWLockExclusive(&g_inceptionLock);
    return deliver;
}
bool TakeInceptionCompletion(unsigned long long id,unsigned long long now) {
    return TakePendingCompletion(g_inceptionPending,id,now);
}
void __fastcall RouteBreedingAward(void* player,int achievement) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto p=static_cast<unsigned char*>(player);
    if(player && achievement==60 && !IsClientActor(p)) {
        InceptionPending pending{};pending.count=1;pending.tick=GetTickCount64();
        pending.ids[0]=*reinterpret_cast<unsigned long long*>(p+0xDD0);
        AcquireSRWLockExclusive(&g_inceptionLock);g_breedingPending=pending;ReleaseSRWLockExclusive(&g_inceptionLock);
        return;
    }
    reinterpret_cast<void(__fastcall*)(void*,int)>(base+0x7524A0)(player,achievement);
}
void __fastcall InceptionPlayerTick(void* player) {
    g_originalInceptionTick(player);
    auto p=static_cast<unsigned char*>(player);
    if(!IsClientActor(p))return;
    bool deliver=TakeInceptionCompletion(*reinterpret_cast<unsigned long long*>(p+0xDD0),GetTickCount64());
    bool breeding=TakePendingCompletion(g_breedingPending,*reinterpret_cast<unsigned long long*>(p+0xDD0),GetTickCount64());
    if(breeding) {
        auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        reinterpret_cast<void(__fastcall*)(void*,int)>(base+0x7524A0)(player,60);
    }
    if(deliver) {
        auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        reinterpret_cast<void(__fastcall*)(void*,int)>(base+0x7524A0)(player,58);
    }
}
void InstallInceptionRepair(unsigned char* base) {
    auto call=base+0xB39AA4;
    if(call[0]!=0xE8 || call+5+*reinterpret_cast<int*>(call+1)!=base+0xBA3F80)return;
    const unsigned char tick[]={0x48,0x8B,0xC4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xA8,0xA8,0xFD,0xFF,0xFF};
    if(std::memcmp(base+0x4AA600,tick,sizeof(tick)))return;
    // A near relay keeps the native CALL's instruction size and return address.
    unsigned char* relay=nullptr;
    uintptr_t origin=reinterpret_cast<uintptr_t>(call)&~uintptr_t(0xFFFF);
    for(uintptr_t distance=0x10000;distance<0x70000000 && !relay;distance+=0x10000)
        relay=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(origin+distance),4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!relay)return;
    void* target=reinterpret_cast<void*>(&ResolveInceptionPlayer);
    const unsigned char jump[]={0x49,0xBB,0,0,0,0,0,0,0,0,0x41,0xFF,0xE3};
    std::memcpy(relay,jump,sizeof(jump));std::memcpy(relay+2,&target,8);
    g_originalInceptionTick=reinterpret_cast<PlayerTick>(HookAchievementFunction(base+0x4AA600,sizeof(tick),reinterpret_cast<void*>(&InceptionPlayerTick)));
    if(!g_originalInceptionTick)return;
    DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
    *reinterpret_cast<int*>(call+1)=static_cast<int>(relay-(call+5));
    FlushInstructionCache(GetCurrentProcess(),relay,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),call,5);
    DWORD ignored;VirtualProtect(call,5,old,&ignored);
}

void InstallBreedingRepair(unsigned char* base) {
    if(!g_originalInceptionTick)return;
    auto call=base+0x991741;
    if(call[0]!=0xE8 || call+5+*reinterpret_cast<int*>(call+1)!=base+0x7524A0)return;
    unsigned char* relay=nullptr;
    uintptr_t origin=reinterpret_cast<uintptr_t>(call)&~uintptr_t(0xFFFF);
    for(uintptr_t distance=0x10000;distance<0x70000000 && !relay;distance+=0x10000)
        relay=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(origin+distance),4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!relay)return;
    void* target=reinterpret_cast<void*>(&RouteBreedingAward);
    const unsigned char jump[]={0x49,0xBB,0,0,0,0,0,0,0,0,0x41,0xFF,0xE3};
    std::memcpy(relay,jump,sizeof(jump));std::memcpy(relay+2,&target,8);
    DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
    *reinterpret_cast<int*>(call+1)=static_cast<int>(relay-(call+5));
    FlushInstructionCache(GetCurrentProcess(),relay,sizeof(jump));FlushInstructionCache(GetCurrentProcess(),call,5);
    DWORD ignored;VirtualProtect(call,5,old,&ignored);
}
