// Achievement presentation only. No unlock/event/token behavior is changed.
volatile LONG g_liveAchievementSnapshot = 0;
using AchievementUnary = void(__fastcall*)(void*);
AchievementUnary g_originalAchievementCommit=nullptr;
AchievementUnary g_originalAchievementUpdate=nullptr;
AchievementUnary g_originalAchievementRegister=nullptr;

void __fastcall CommitAchievementSnapshot(void* service) {
    const bool completed=*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(service)+0x158)!=0;
    g_originalAchievementCommit(service);
    if(completed && *reinterpret_cast<int*>(static_cast<unsigned char*>(service)+0xFC)>0)
        InterlockedExchange(&g_liveAchievementSnapshot,1);
}
void __fastcall UpdateAchievementPresentation(void* controller) {
    if(!InterlockedCompareExchange(&g_liveAchievementSnapshot,0,0)) {
        // The native getter also starts the asynchronous fetch. Keep that side effect.
        auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        void* model=*reinterpret_cast<void**>(static_cast<unsigned char*>(controller)+0x3D8);
        reinterpret_cast<void*(__fastcall*)(void*)>(base+0x392F90)(model);
        *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(controller)+0x4DC)=0;
        return;
    }
    g_originalAchievementUpdate(controller);
}
bool AchievementPresentationReady(void* controller) {
    return InterlockedCompareExchange(&g_liveAchievementSnapshot,0,0)!=0 &&
        *reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(controller)+0x4DC)!=0;
}
void RegisterAchievementVisibility(void* controller,const char* name,uintptr_t tag) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    struct NativeFunction{uintptr_t storage[7]{};void* self;} value{},condition{};
    value.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D4A0);
    value.storage[1]=reinterpret_cast<uintptr_t>(controller)|tag;value.self=&value;
    condition.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D2A8);condition.self=&condition;
    unsigned key=0x811C9DC5;for(;*name;++name)key=(key^static_cast<unsigned char>(*name))*0x1000193;
    reinterpret_cast<void(__fastcall*)(void*,unsigned*,void*,void*)>(base+0x3DF530)(controller,&key,&value,&condition);
}
void __fastcall RegisterAchievementLoading(void* controller) {
    g_originalAchievementRegister(controller);
    RegisterAchievementVisibility(controller,"#appletv_achievements_ready",1);
    RegisterAchievementVisibility(controller,"#appletv_achievements_loading",2);
}
AchievementUnary HookAchievementFunction(unsigned char* target,size_t length,void* replacement) {
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,length+13,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!trampoline)return nullptr;
    std::memcpy(trampoline,target,length);
    unsigned char jump[13]{0x49,0xBB};void* continuation=target+length;
    std::memcpy(jump+2,&continuation,8);jump[10]=0x41;jump[11]=0xFF;jump[12]=0xE3;
    std::memcpy(trampoline+length,jump,13);
    DWORD old;if(!VirtualProtect(target,length,PAGE_EXECUTE_READWRITE,&old))return nullptr;
    std::memcpy(jump+2,&replacement,8);std::memcpy(target,jump,13);std::memset(target+13,0x90,length-13);
    FlushInstructionCache(GetCurrentProcess(),target,length);DWORD ignored;VirtualProtect(target,length,old,&ignored);
    return reinterpret_cast<AchievementUnary>(trampoline);
}
#include "shulker_achievement.h"
#include "inception_achievement.h"
void InstallAchievementLoading(unsigned char* base) {
    InstallShulkerSourceSync(base);
    InstallInceptionRepair(base);
    InstallBreedingRepair(base);
    // Native startup asked for only25. Ask for1000 before URL construction and
    // signing, retaining XSAPI's existing continuation handling. Current title
    // returns79 and no continuation at this size (verified live).
    if(*reinterpret_cast<DWORD*>(base+0x671850)==25) {
        DWORD old;if(VirtualProtect(base+0x671850,4,PAGE_EXECUTE_READWRITE,&old)) {
            *reinterpret_cast<DWORD*>(base+0x671850)=1000;
            FlushInstructionCache(GetCurrentProcess(),base+0x671850,4);
            DWORD ignored;VirtualProtect(base+0x671850,4,old,&ignored);
        }
    }
    g_originalAchievementCommit=HookAchievementFunction(base+0x66FD30,13,reinterpret_cast<void*>(&CommitAchievementSnapshot));
    g_originalAchievementUpdate=HookAchievementFunction(base+0x1E1270,19,reinterpret_cast<void*>(&UpdateAchievementPresentation));
    g_originalAchievementRegister=HookAchievementFunction(base+0x1E0370,18,reinterpret_cast<void*>(&RegisterAchievementLoading));
}

