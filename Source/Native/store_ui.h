// Included inside the proxy's private namespace. Presentation never grants licenses.
#include "coin_checkout.h"
struct StoreUiState { bool owned = false; bool free = false; char compatibility = 'U'; bool pending = false; bool failed = false; ULONGLONG doneUntil = 0; bool soundPending = false; };
volatile LONG g_confirmedStoreBalance = -1;
SRWLOCK g_storeUiLock = SRWLOCK_INIT;
std::unordered_map<std::string, StoreUiState> g_storeUiStates;
std::string g_storeGamertag;
std::string g_storeGamerPicture, g_storeGamerPictureSource;
template<uintptr_t OriginalRva, bool Source>
void* __fastcall CacheStorePicture(void* callback, void* result) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    using Original = void* (__fastcall *)(void*,void*);
    reinterpret_cast<Original>(base+OriginalRva)(callback,result);
    const auto& text = *static_cast<std::string*>(result);
    AcquireSRWLockExclusive(&g_storeUiLock);
    (Source ? g_storeGamerPictureSource : g_storeGamerPicture) = text;
    ReleaseSRWLockExclusive(&g_storeUiLock);
    return result;
}

bool QueryStoreUi(const char* operation, const std::string& id, std::string& response) {
    SOCKET connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection == INVALID_SOCKET) return false;
    DWORD timeout = (std::strcmp(operation, "CLM1") == 0 || std::strcmp(operation, "BUY1") == 0 || std::strcmp(operation,"TOP1")==0 || std::strcmp(operation,"TOP2")==0 || std::strcmp(operation,"REC1")==0 || std::strcmp(operation,"RDM1")==0 || std::strcmp(operation,"TOK1")==0) ? 180000 : 45000;
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(32122);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uint32_t length = 0;
    bool ok = connect(connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
        WriteAll(connection, operation, 4) && (id.empty() || WriteAll(connection, id.data(), id.size())) &&
        ReadAll(connection, &length, 4) && length > 0 && length < 4 * 1024 * 1024;
    if (ok) { response.resize(length); ok = ReadAll(connection, response.data(), length); }
    closesocket(connection);
    return ok;
}

void ApplyStoreUiStates(const std::string& response) {
    std::unordered_map<std::string, StoreUiState> parsed;
    std::string gamertag;
    size_t start = 0;
    while (start < response.size()) {
        auto end = response.find('\n', start);
        if (end == std::string::npos) break;
        auto line = response.substr(start, end - start);
        if (line.rfind("BAL|",0)==0) InterlockedExchange(&g_confirmedStoreBalance, static_cast<LONG>(std::strtol(line.c_str()+4,nullptr,10)));
        if (line.rfind("TAG|",0)==0) gamertag=line.substr(4);
        if (line.size() == 42 && line[36] == '|' && line[38] == '|' && line[40] == '|')
            parsed[line.substr(0,36)] = {line[37]=='1', line[39]=='1', line[41]};
        start = end + 1;
    }
    AcquireSRWLockExclusive(&g_storeUiLock);
    if (!gamertag.empty()) g_storeGamertag=gamertag;
    for (auto& entry : parsed) {
        auto previous = g_storeUiStates.find(entry.first);
        if (previous != g_storeUiStates.end()) { entry.second.pending = previous->second.pending; entry.second.doneUntil = previous->second.doneUntil; entry.second.soundPending = previous->second.soundPending; }
    }
    g_storeUiStates.swap(parsed);
    ReleaseSRWLockExclusive(&g_storeUiLock);
}

void RefreshStoreUiStates() {
    std::string response;
    if (QueryStoreUi("ALL1", "", response)) ApplyStoreUiStates(response);
}
// Refresh header data asynchronously: no network wait in a render callback.
volatile LONG g_headerWalletRefreshPending = 0;
volatile LONG64 g_headerWalletRefreshDue = 0;
DWORD WINAPI RefreshHeaderWallet(LPVOID) {
    std::string response;
    bool ok = QueryStoreUi("ALL1", "", response);
    if (ok) ApplyStoreUiStates(response);
    InterlockedExchange64(&g_headerWalletRefreshDue, GetTickCount64() + (ok ? 60000 : 5000));
    InterlockedExchange(&g_headerWalletRefreshPending, 0);
    return 0;
}
void EnsureHeaderWalletRefresh() {
    if (GetTickCount64() < static_cast<ULONGLONG>(InterlockedCompareExchange64(&g_headerWalletRefreshDue,0,0))) return;
    if (InterlockedCompareExchange(&g_headerWalletRefreshPending,1,0)) return;
    HANDLE thread=CreateThread(nullptr,0,RefreshHeaderWallet,nullptr,0,nullptr);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&g_headerWalletRefreshPending,0);
}
void AssignNativeString(void*,const std::string&);
void* __fastcall LiveHeaderWallet(void* callback,void* result) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    // Preserve native string construction; replace only its displayed value.
    reinterpret_cast<void*(__fastcall*)(void*,void*)>(base+0x262760)(callback,result);
    EnsureHeaderWalletRefresh();
    LONG balance=InterlockedCompareExchange(&g_confirmedStoreBalance,0,0);
    AssignNativeString(result,balance>=0 ? std::to_string(balance) : "...");
    return result;
}

void* __fastcall LiveStoreTitle(void* callback,void* result) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto controller=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(callback)+8);
    auto offer=*reinterpret_cast<void**>(controller+0x480);
    std::memset(result,0,32);*reinterpret_cast<size_t*>(static_cast<unsigned char*>(result)+24)=15;
    if(offer) {
        auto title=reinterpret_cast<void*(__fastcall*)(void*)>(base+0x6C4740)(offer);
        AssignNativeString(result,Utf8(CatalogTaskString(title,0)));
    }
    return result;
}

std::string StoreOfferId(void* callback) {
    if (!callback) return {};
    auto controller = reinterpret_cast<unsigned char*>(*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback) + 8) & ~uintptr_t(1));
    if (!controller) return {};
    auto offer = *reinterpret_cast<void**>(controller + 0x480);
    return Utf8(CatalogTaskString(offer, 0));
}

StoreUiState StoreState(const std::string& id) {
    AcquireSRWLockShared(&g_storeUiLock);
    auto found = g_storeUiStates.find(id);
    StoreUiState result = found == g_storeUiStates.end() ? StoreUiState{} : found->second;
    ReleaseSRWLockShared(&g_storeUiLock);
    return result;
}

void AssignNativeString(void* result, const std::string& text) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    using Assign = void (__fastcall *)(void*, const void*, size_t, size_t);
    reinterpret_cast<Assign>(base + kStringAssignRva)(result, &text, 0, static_cast<size_t>(-1));
}

constexpr const char* kPaidTestOffer = "ddd2bcc0-0996-4031-993a-0f541266c3dc";
bool PaidTestOffer(const std::string& id) { return id == kPaidTestOffer; }
DWORD WINAPI ReportStoreSound(LPVOID result) {
    std::string response;
    QueryStoreUi("SND1", std::string(36, result ? '1' : '0'), response);
    return 0;
}
void PlayClaimSuccess(void* callback) {
    bool played = false;
    // Native UI sound route: screen model -> client instance -> sound player.
    __try {
        auto controller = reinterpret_cast<unsigned char*>(*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback) + 8) & ~uintptr_t(1));
        auto model = *reinterpret_cast<unsigned char**>(controller + 0x3D8);
        auto scene = *reinterpret_cast<unsigned char**>(model + 0x20);
        auto client = *reinterpret_cast<unsigned char**>(scene + 0x30);
        auto player = *reinterpret_cast<void**>(client + 0x188);
        struct SmallString { char text[16]; size_t length; size_t capacity; } name{"ui.levelup",10,15};
        using Play = void (__fastcall *)(void*, void*, float, float);
        auto table = *reinterpret_cast<uintptr_t**>(player);
        reinterpret_cast<Play>(table[2])(player,&name,1.0f,1.0f);
        played = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    HANDLE report = CreateThread(nullptr,0,ReportStoreSound,reinterpret_cast<void*>(static_cast<uintptr_t>(played ? 1 : 0)),0,nullptr);
    if (report) CloseHandle(report);
}
StoreUiState StorePresentationState(void* callback) {
    auto id = StoreOfferId(callback);
    bool play = false;
    AcquireSRWLockExclusive(&g_storeUiLock);
    auto& value = g_storeUiStates[id];
    if (value.soundPending) { value.soundPending = false; value.doneUntil = GetTickCount64() + 1800; play = true; }
    auto result = value;
    ReleaseSRWLockExclusive(&g_storeUiLock);
    if (g_storeWallet && g_confirmedStoreBalance >= 0) *reinterpret_cast<int*>(static_cast<unsigned char*>(g_storeWallet) + 0x68) = g_confirmedStoreBalance;
    if (play) PlayClaimSuccess(callback);
    return result;
}

winrt::fire_and_forget LaunchProjectPage() {
    try { co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(L"https://github.com/thebreenis/minecraft-apple-tv-edition-x64")); }
    catch (...) { }
}
int __fastcall OpenProjectPage(void*) { LaunchProjectPage(); return 1; }
bool g_profileHelpVisible = false;
int __fastcall OpenProfileHelp(void*) { g_profileHelpVisible=true; return 1; }
int __fastcall CloseProfileHelp(void*) { g_profileHelpVisible=false; return 1; }
bool __fastcall StoreActionCoins(void* callback);
void* __fastcall StoreActionLabel(void* callback, void* result) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto purpose = *reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback)+8) & 7;
    if (purpose) {
        std::string text;
        if (purpose == 2) { EnsureHeaderWalletRefresh(); text = g_confirmedStoreBalance >= 0 ? std::to_string(g_confirmedStoreBalance) : "..."; if (g_storeWallet && g_confirmedStoreBalance >= 0) *reinterpret_cast<int*>(static_cast<unsigned char*>(g_storeWallet) + 0x68) = g_confirmedStoreBalance; }
        else if (purpose == 6) text=CoinPlatformPrice(*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback)+8)>>3);
        else if (purpose == 7) text=CoinCheckoutStatus();
        else if (purpose >= 3) {
            AcquireSRWLockShared(&g_storeUiLock);
            text=purpose==3 ? g_storeGamertag : purpose==4 ? g_storeGamerPicture : g_storeGamerPictureSource;
            ReleaseSRWLockShared(&g_storeUiLock);
        } else {
            auto state = StorePresentationState(callback);
            text=state.doneUntil > GetTickCount64() ? "Done!" : "Please Wait...";
        }
        // This additional native binding belongs only to the wait popup.
        std::memset(result,0,32);
        *reinterpret_cast<size_t*>(static_cast<unsigned char*>(result)+24)=15;
        AssignNativeString(result,text);
        return result;
    }
    using Original = void* (__fastcall *)(void*, void*);
    reinterpret_cast<Original>(base + 0x2D10E0)(callback, result);
    auto state = StorePresentationState(callback);
    std::string label;
    if (state.pending || state.doneUntil > GetTickCount64()) label = "Please Wait...";
    else if (StoreActionCoins(callback)) label = "Please Wait...";
    else if (!state.owned && PaidTestOffer(StoreOfferId(callback))) label = "Buy for 160 Minecoins";
    else if (!state.owned && state.compatibility == 'S') label = "Coming Soon";
    else if (!state.owned && state.compatibility == 'N') label = "Unavailable";
    else if (!state.owned && state.free) label = "Free";
    else if (state.compatibility == 'I') label = "Incompatible";
    else if (state.compatibility == 'U') label = "Incompatible";
    else if (state.owned) label = "Download";
    if (!label.empty()) AssignNativeString(result, label);
    return result;
}

bool AchievementPresentationReady(void* controller);
bool __fastcall StoreActionEnabled(void* callback) {
    auto tagged=*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback)+8);
    if((tagged&7)==1 || (tagged&7)==2) {
        bool ready=AchievementPresentationReady(reinterpret_cast<void*>(tagged&~uintptr_t(7)));
        return (tagged&7)==1 ? ready : !ready;
    }
    if (*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(callback)+8)==0xFE) return g_profileHelpVisible;
    auto state = StorePresentationState(callback);
    return !state.pending && state.doneUntil <= GetTickCount64() && ((!state.owned && (state.free || PaidTestOffer(StoreOfferId(callback)))) || (state.owned && state.compatibility == 'C'));
}
// This obsolete coin binding now drives the preview-loading overlay; detail
// action labels no longer contain coin icons.
std::unordered_map<std::string, std::pair<size_t, ULONGLONG>> g_previewExpected;
void TrackStorePreview(const std::string& id, const std::string& document) {
    size_t count = 0, pos = 0;
    while ((pos = document.find("\"imagePurpose\":\"screenshot\"", pos)) != std::string::npos) { ++count; ++pos; }
    AcquireSRWLockExclusive(&g_storeUiLock);
    g_previewExpected[id] = {count, GetTickCount64()};
    ReleaseSRWLockExclusive(&g_storeUiLock);
}
bool __fastcall StoreActionCoins(void* callback) {
    auto id = StoreOfferId(callback);
    auto state = StorePresentationState(callback);
    if (state.pending || state.doneUntil > GetTickCount64()) return true;
    AcquireSRWLockShared(&g_storeUiLock);
    auto it = g_previewExpected.find(id);
    auto expected = it == g_previewExpected.end() ? std::pair<size_t, ULONGLONG>{0,0} : it->second;
    ReleaseSRWLockShared(&g_storeUiLock);
    if (!expected.first || GetTickCount64() - expected.second > 30000) return false;
    auto controller = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(callback) + 8);
    auto offer = *reinterpret_cast<unsigned char**>(controller + 0x480);
    if (!offer) return false;
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    void* iterator = nullptr;
    using Find = void* (__fastcall *)(void*, void*, void*);
    reinterpret_cast<Find>(base + kStringMapFindRva)(offer + 0x170, &iterator, base + 0x19286B0);
    size_t loaded = 0;
    if (iterator && iterator != *reinterpret_cast<void**>(offer + 0x178)) {
        auto entry = static_cast<unsigned char*>(iterator);
        auto begin = *reinterpret_cast<uintptr_t*>(entry + 0x30);
        auto end = *reinterpret_cast<uintptr_t*>(entry + 0x38);
        if (end >= begin) loaded = (end - begin) / 0x48;
    }
    return loaded < expected.first;
}

void* __fastcall StoreDescription(void* callback, void* result) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    using Original = void* (__fastcall *)(void*, void*);
    reinterpret_cast<Original>(base + 0x2D0F70)(callback, result);
    auto id = StoreOfferId(callback);
    auto state = StoreState(id);
    std::string prefix;
    if (state.owned) prefix = "Owned\n";
    if (state.compatibility == 'I') prefix += "Requires Minecraft 1.26.50.\n";
    if (state.compatibility == 'S') prefix += "Coming Soon. Not available to claim yet.\n";
    if (state.compatibility == 'N') prefix += "No verified purchase price is available.\n";
    if (state.failed) prefix += "Claim failed. Please try again.\n";
    if (!prefix.empty()) AssignNativeString(result, prefix + "\n" + Utf8(CatalogTaskString(result, 0)));
    return result;
}

DWORD WINAPI ClaimStoreFree(LPVOID parameter) {
    auto id = static_cast<std::string*>(parameter);
    std::string response;
    bool ok = QueryStoreUi(PaidTestOffer(*id) ? "BUY1" : "CLM1", *id, response);
    if (ok) ApplyStoreUiStates(response);
    AcquireSRWLockExclusive(&g_storeUiLock);
    auto& state = g_storeUiStates[*id]; state.pending = false; state.failed = !ok; state.soundPending = ok;
    ReleaseSRWLockExclusive(&g_storeUiLock);
    delete id;
    return 0;
}

int __fastcall StoreActionPress(void* callback) {
    const auto id = StoreOfferId(callback);
    auto state = StoreState(id);
    if (id.size() != 36 || state.pending) return 0;
    if (!state.owned && (state.free || PaidTestOffer(id))) {
        AcquireSRWLockExclusive(&g_storeUiLock); g_storeUiStates[id].pending = true; ReleaseSRWLockExclusive(&g_storeUiLock);
        auto parameter = new std::string(id);
        HANDLE thread = CreateThread(nullptr, 0, ClaimStoreFree, parameter, 0, nullptr);
        if (thread) CloseHandle(thread);
        else { delete parameter; AcquireSRWLockExclusive(&g_storeUiLock); g_storeUiStates[id].pending = false; ReleaseSRWLockExclusive(&g_storeUiLock); }
    } else if (state.owned && state.compatibility == 'C') {
        auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        using Original = int (__fastcall *)(void*);
        return reinterpret_cast<Original>(base + 0x2D1260)(callback);
    }
    // UI testing does not authorize a paid transaction. Never invoke purchase here.
    return 0;
}

using StoreRegister = void (__fastcall *)(void*);
StoreRegister g_originalStoreRegister = nullptr;
StoreRegister g_originalCoinRegister = nullptr;
void RegisterTaggedStoreText(void* controller, const char* name, uintptr_t purpose) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    struct NativeFunction { uintptr_t storage[7]{}; void* self; } value{}, condition{};
    value.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D708);
    value.storage[1]=(purpose&7)==6 ? purpose : reinterpret_cast<uintptr_t>(controller)|purpose;
    value.self=&value;
    condition.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D2A8);
    condition.self=&condition;
    unsigned int key = 0x811C9DC5;
    for (; *name; ++name) key=(key^static_cast<unsigned char>(*name))*0x1000193;
    using Register = void (__fastcall *)(void*,unsigned int*,void*,void*);
    reinterpret_cast<Register>(base+0x3DF3D0)(controller,&key,&value,&condition);
}
void __fastcall RegisterStoreWaitText(void* controller) {
    g_originalStoreRegister(controller);
    RegisterTaggedStoreText(controller,"#store_wait_text",1);
}
void __fastcall RegisterCoinHeader(void* controller) {
    g_originalCoinRegister(controller);
    RegisterCoinPurchaseButtons(controller);
    StartCoinPlatformProbe();
    RegisterTaggedStoreText(controller,"#coin_balance",2);
    RegisterTaggedStoreText(controller,"#coin_gamertag",3);
    RegisterTaggedStoreText(controller,"#gamertag",3);
    RegisterTaggedStoreText(controller,"#gamer_pic_texture",4);
    RegisterTaggedStoreText(controller,"#gamer_pic_file_source",5);
    const char* prices[]{"#coin_price_320","#coin_price_1020","#coin_price_1720","#coin_price_3500","#coin_price_8800"};
    for (uintptr_t i=0;i<5;++i) RegisterTaggedStoreText(controller,prices[i],(i<<3)|6);
    RegisterTaggedStoreText(controller,"#coin_checkout_status",7);
}
void InstallCoinHeaderBinding(unsigned char* base) {
    auto target=base+0x264100;
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!trampoline) return;
    // First20 bytes are whole non-RIP-relative instructions.
    std::memcpy(trampoline,target,20);
    unsigned char jump[13]{0x49,0xBB};void* continuation=target+20;
    std::memcpy(jump+2,&continuation,8);jump[10]=0x41;jump[11]=0xFF;jump[12]=0xE3;
    std::memcpy(trampoline+20,jump,13);
    g_originalCoinRegister=reinterpret_cast<StoreRegister>(trampoline);
    DWORD old;if (!VirtualProtect(target,20,PAGE_EXECUTE_READWRITE,&old)) return;
    void* replacement=reinterpret_cast<void*>(&RegisterCoinHeader);
    std::memcpy(jump+2,&replacement,8);std::memcpy(target,jump,13);std::memset(target+13,0x90,7);
    FlushInstructionCache(GetCurrentProcess(),target,20);
    DWORD ignored;VirtualProtect(target,20,old,&ignored);
}
void InstallStoreWaitBinding(unsigned char* base) {
    // First18 bytes are whole instructions with no RIP-relative operands.
    auto target=base+0x2CFEF0;
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!trampoline) return;
    std::memcpy(trampoline,target,18);
    unsigned char jump[13]{0x49,0xBB};
    void* continuation=target+18;
    std::memcpy(jump+2,&continuation,8);jump[10]=0x41;jump[11]=0xFF;jump[12]=0xE3;
    std::memcpy(trampoline+18,jump,13);
    g_originalStoreRegister=reinterpret_cast<StoreRegister>(trampoline);
    DWORD old;
    if (!VirtualProtect(target,18,PAGE_EXECUTE_READWRITE,&old)) return;
    void* replacement=reinterpret_cast<void*>(&RegisterStoreWaitText);
    std::memcpy(jump+2,&replacement,8);std::memcpy(target,jump,13);std::memset(target+13,0x90,5);
    FlushInstructionCache(GetCurrentProcess(),target,18);
    DWORD ignored;VirtualProtect(target,18,old,&ignored);
}
// Settings-local state is reset on each controller construction; no account APIs run.
StoreRegister g_originalProfileRegister = nullptr;
void __fastcall RegisterProfileHelp(void* controller) {
    g_originalProfileRegister(controller);
    g_profileHelpVisible=false;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    struct NativeFunction { uintptr_t storage[7]{};void* self; } value{},condition{};
    value.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D4A0);value.storage[1]=0xFE;value.self=&value;
    condition.storage[0]=reinterpret_cast<uintptr_t>(base+0x170D2A8);condition.self=&condition;
    unsigned int key=0x811C9DC5;for(const char* n="#appletv_profile_help";*n;++n)key=(key^static_cast<unsigned char>(*n))*0x1000193;
    reinterpret_cast<void(__fastcall*)(void*,unsigned int*,void*,void*)>(base+0x3DF530)(controller,&key,&value,&condition);
}
void InstallProfileHelp(unsigned char* base) {
    auto target=base+0x23B4B0;
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!trampoline) return;
    // First 22 bytes are complete, non-RIP-relative instructions.
    std::memcpy(trampoline,target,22);
    unsigned char jump[13]{0x49,0xBB}; void* continuation=target+22;
    std::memcpy(jump+2,&continuation,8);jump[10]=0x41;jump[11]=0xFF;jump[12]=0xE3;
    std::memcpy(trampoline+22,jump,13);
    g_originalProfileRegister=reinterpret_cast<StoreRegister>(trampoline);
    DWORD old;if(!VirtualProtect(target,22,PAGE_EXECUTE_READWRITE,&old))return;
    void* replacement=reinterpret_cast<void*>(&RegisterProfileHelp);
    std::memcpy(jump+2,&replacement,8);std::memcpy(target,jump,13);std::memset(target+13,0x90,9);
    FlushInstructionCache(GetCurrentProcess(),target,22);DWORD ignored;VirtualProtect(target,22,old,&ignored);
}
#include "achievement_ui.h"
void InstallStoreUiBindings(unsigned char* base) {
    InstallAchievementLoading(base);
    InstallProfileHelp(base);
    InstallStoreWaitBinding(base);
    InstallCoinHeaderBinding(base);
    // Layout review of current verified coin offers. Do not start the retired
    // platform listing fetch, nor its coin-controller polling/error dialog.
    // Retain common Store controller updates (identity/balance/navigation).
    DWORD coinOld;
    if (VirtualProtect(base+0x263FF0,1,PAGE_EXECUTE_READWRITE,&coinOld)) {
        base[0x263FF0]=0xC3;
        DWORD ignored;VirtualProtect(base+0x263FF0,1,coinOld,&ignored);
        FlushInstructionCache(GetCurrentProcess(),base+0x263FF0,1);
    }
    if (VirtualProtect(base+0x264030,5,PAGE_EXECUTE_READWRITE,&coinOld)) {
        base[0x264030]=0xE9;
        int32_t delta=0x2692E0-(0x264030+5);
        std::memcpy(base+0x264031,&delta,4);
        DWORD ignored;VirtualProtect(base+0x264030,5,coinOld,&ignored);
        FlushInstructionCache(GetCurrentProcess(),base+0x264030,5);
    }
    const std::pair<uintptr_t, void*> bindings[] = {
        // #coin_balance in Store home/category, detail and other Store headers.
        {0x1712F20, reinterpret_cast<void*>(&LiveHeaderWallet)}, // live main/category controller
        {0x1704C90, reinterpret_cast<void*>(&LiveHeaderWallet)},
        {0x170D408, reinterpret_cast<void*>(&LiveHeaderWallet)},
        {0x1710118, reinterpret_cast<void*>(&LiveHeaderWallet)},
        {0x1701FE8, reinterpret_cast<void*>(&OpenProjectPage)},
        {0x17024B8, reinterpret_cast<void*>(&OpenProfileHelp)},
        {0x1702480, reinterpret_cast<void*>(&CloseProfileHelp)},
        {0x170D440, reinterpret_cast<void*>(&LiveStoreTitle)},
        {0x17051F8, reinterpret_cast<void*>(&SmallCoinPurchasePress)},
        {0x1713118, reinterpret_cast<void*>(&CacheStorePicture<0x30C440,false>)},
        {0x1713150, reinterpret_cast<void*>(&CacheStorePicture<0x30C3E0,true>)},
        {0x170D718, reinterpret_cast<void*>(&StoreActionLabel)},
        {0x170D4B0, reinterpret_cast<void*>(&StoreActionEnabled)},
        {0x170D6A8, reinterpret_cast<void*>(&StoreActionCoins)},
        {0x170D478, reinterpret_cast<void*>(&StoreDescription)},
        {0x170D360, reinterpret_cast<void*>(&StoreActionPress)}
    };
    for (auto binding : bindings) {
        DWORD old;
        if (VirtualProtect(base + binding.first, sizeof(void*), PAGE_READWRITE, &old)) {
            *reinterpret_cast<void**>(base + binding.first) = binding.second;
            DWORD ignored; VirtualProtect(base + binding.first, sizeof(void*), old, &ignored);
        }
    }
}

extern "C" bool StoreOwnershipThunk(void*);
extern "C" bool StoreOwnershipPresentation(void* pointer, uintptr_t returnAddress) {
    auto offer = static_cast<unsigned char*>(pointer);
    auto caller = returnAddress - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Only listing label/coin-visibility queries use account ownership. Actual
    // content licensing and download predicates keep the native receipt checks.
    if (caller == 0x31062F || caller == 0x310752 || caller == 0x310F63 ||
        caller == 0x3111BF || caller == 0x315418 || caller == 0x315619) {
        auto state = StoreState(Utf8(CatalogTaskString(offer, 0)));
        if (state.owned) return true;
        if (state.compatibility == 'S' || state.compatibility == 'N') {
            if (caller == 0x310752 || caller == 0x3111BF || caller == 0x315619) return true;
            AssignNativeString(offer + 0x40, state.compatibility == 'S' ? "Coming Soon" : "Unavailable");
        }
    }
    if (!offer || !offer[0x151]) return false;
    auto begin = *reinterpret_cast<unsigned char***>(offer + 0x158);
    auto end = *reinterpret_cast<unsigned char***>(offer + 0x160);
    if (begin == end) return false;
    for (auto current = begin; current != end; ++current) {
        auto identity = *current;
        if (!identity[0xD8] || !*reinterpret_cast<void**>(identity + 0xA8) ||
            !*reinterpret_cast<unsigned int*>(identity + 0x90)) return false;
    }
    return true;
}

void InstallStoreOwnershipPresentation(unsigned char* base) {
    auto target = base + 0x6C4FE0;
    DWORD old;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return;
    unsigned char patch[16]{0x49,0xBB};
    auto destination = &StoreOwnershipThunk;
    std::memcpy(patch+2, &destination, sizeof(destination));
    patch[10]=0x41;patch[11]=0xFF;patch[12]=0xE3;
    std::memset(patch+13,0x90,3);
    std::memcpy(target,patch,16);
    FlushInstructionCache(GetCurrentProcess(),target,16);
    DWORD ignored;VirtualProtect(target,16,old,&ignored);
}

