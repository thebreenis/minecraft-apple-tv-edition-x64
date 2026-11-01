#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <intrin.h>
#include <unknwn.h>
#undef small
#include <system_error>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>
#include <winrt/Windows.ApplicationModel.Store.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

void InstallPresenceWriteFilter();
void ObserveNativePresenceAuthorization(const std::wstring& value);

struct XINPUT_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XINPUT_STATE {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};

namespace {

constexpr uintptr_t kAuthHelperRva = 0x00E42AF0;
constexpr uintptr_t kWalletBaseUrlRva = 0x01927C90;
constexpr uintptr_t kWalletUrlResolverRva = 0x0069A500;
constexpr uintptr_t kWalletUrlLoadRva = kWalletUrlResolverRva + 0xCC;
constexpr uintptr_t kWalletLoadRva = 0x00672E60;
constexpr uintptr_t kWalletReadyJumpRva = 0x00672EB8;
constexpr uintptr_t kWalletCompletionRva = 0x0069F320;
constexpr uintptr_t kWalletGetServiceRva = 0x000DB8E0;
constexpr uintptr_t kWalletTaskCleanupRva = 0x000D4570;
constexpr uintptr_t kWalletParseRva = 0x006A3010;
constexpr uintptr_t kWalletPreprocessRva = 0x006A36F0;
constexpr uintptr_t kWalletContextRva = 0x00699F90;
constexpr uintptr_t kJsonParseRva = 0x00E68960;
constexpr uintptr_t kCatalogInitRva = 0x006C7AA0;
constexpr uintptr_t kCatalogExecuteRva = 0x0060D320;
constexpr uintptr_t kCatalogParseRva = 0x0060ECC0;
constexpr uintptr_t kCatalogNotifyRva = 0x0060E9A0;
constexpr uintptr_t kCatalogSelectRva = 0x006C91D0;
constexpr uintptr_t kCatalogApplyManifestRva = 0x006C9470;
constexpr uintptr_t kCatalogManifestCallbackVtableRva = 0x017304E0;
constexpr uintptr_t kOfferPurchasableRva = 0x006C5040;
constexpr uintptr_t kThumbnailCallbackRva = 0x006C8C40;
constexpr uintptr_t kThumbnailDecodeCallbackRva = 0x006C8D70;
constexpr uintptr_t kThumbnailTextureCallbackRva = 0x006C88F0;
constexpr uintptr_t kThumbnailLoadRva = 0x005F0020;
constexpr uintptr_t kThumbnailApplyCallRva = 0x006C8C5F;
constexpr uintptr_t kThumbnailIdentityLoopRva = 0x006C8C9F;
constexpr uintptr_t kStringAssignRva = 0x000DC860;
constexpr uintptr_t kStringMapFindRva = 0x000DEC30;
constexpr uintptr_t kOfferFactoryFindReturnRva = 0x006C8746;
constexpr uint16_t kBridgePort = 32115;
constexpr bool kUseNativeThumbnails = true;
constexpr char kMagic[] = "ATVB1";
constexpr size_t kHookSize = 13;
constexpr size_t kWalletResolverHookSize = 14;
constexpr size_t kWalletLoadHookSize = 19;
constexpr size_t kWalletCompletionHookSize = 19;
constexpr size_t kCatalogInitHookSize = 18;
constexpr size_t kCatalogExecuteHookSize = 18;
constexpr size_t kOfferPurchasableHookSize = 18;
constexpr size_t kThumbnailCallbackHookSize = 16;
constexpr size_t kThumbnailDecodeCallbackHookSize = 18;
constexpr size_t kThumbnailTextureCallbackHookSize = 17;
constexpr size_t kThumbnailLoadHookSize = 19;
constexpr size_t kStringMapFindHookSize = 14;

using AuthHelper = void* (__fastcall *)(
    void*, void*, const std::wstring*, const std::wstring*,
    const std::wstring*, const std::vector<unsigned char>*, bool, bool);
using RealXInputGetState = DWORD (WINAPI *)(DWORD, XINPUT_STATE*);

std::array<unsigned char, kHookSize> g_original{};
unsigned char* g_target = nullptr;
SRWLOCK g_hookLock = SRWLOCK_INIT;
RealXInputGetState g_realXInputGetState = nullptr;
using RealGetAddrInfo = int (WSAAPI *)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
RealGetAddrInfo g_realGetAddrInfo = nullptr;
using RealConnect = int (WSAAPI *)(SOCKET, const sockaddr*, int);
RealConnect g_realConnect = nullptr;
std::array<unsigned char, kHookSize> g_connectOriginal{};
unsigned char* g_connectTarget = nullptr;
SRWLOCK g_connectLock = SRWLOCK_INIT;
using RealWinHttpOpen = HINTERNET (WINAPI *)(
    LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
RealWinHttpOpen g_realWinHttpOpen = nullptr;
volatile LONG g_walletGlobalPatched = 0;
volatile LONG g_walletServerStatus = 0;
volatile LONG g_walletResolverArmed = 0;
volatile LONG g_thumbnailServerArmed = 0;
std::array<unsigned char, kWalletResolverHookSize> g_walletResolverOriginal{};
unsigned char* g_walletResolverTarget = nullptr;
SRWLOCK g_walletResolverLock = SRWLOCK_INIT;
std::array<unsigned char, kWalletLoadHookSize> g_walletLoadOriginal{};
unsigned char* g_walletLoadTarget = nullptr;
SRWLOCK g_walletLoadLock = SRWLOCK_INIT;
std::array<unsigned char, kWalletCompletionHookSize> g_walletCompletionOriginal{};
unsigned char* g_walletCompletionTarget = nullptr;
std::array<unsigned char, kCatalogInitHookSize> g_catalogInitOriginal{};
unsigned char* g_catalogInitTarget = nullptr;
SRWLOCK g_catalogInitLock = SRWLOCK_INIT;
std::array<unsigned char, kCatalogExecuteHookSize> g_catalogExecuteOriginal{};
unsigned char* g_catalogExecuteTarget = nullptr;
SRWLOCK g_catalogExecuteLock = SRWLOCK_INIT;
void* g_catalogManager = nullptr;
volatile LONG g_catalogRetryScheduled = 0;
std::array<unsigned char, kOfferPurchasableHookSize>
    g_offerPurchasableOriginal{};
unsigned char* g_offerPurchasableTarget = nullptr;
SRWLOCK g_offerPurchasableLock = SRWLOCK_INIT;
std::array<unsigned char, kThumbnailCallbackHookSize>
    g_thumbnailCallbackOriginal{};
unsigned char* g_thumbnailCallbackTarget = nullptr;
SRWLOCK g_thumbnailCallbackLock = SRWLOCK_INIT;
std::array<unsigned char, kThumbnailDecodeCallbackHookSize>
    g_thumbnailDecodeCallbackOriginal{};
unsigned char* g_thumbnailDecodeCallbackTarget = nullptr;
SRWLOCK g_thumbnailDecodeCallbackLock = SRWLOCK_INIT;
std::array<unsigned char, kThumbnailTextureCallbackHookSize>
    g_thumbnailTextureCallbackOriginal{};
unsigned char* g_thumbnailTextureCallbackTarget = nullptr;
SRWLOCK g_thumbnailTextureCallbackLock = SRWLOCK_INIT;
std::array<unsigned char, kThumbnailLoadHookSize> g_thumbnailLoadOriginal{};
unsigned char* g_thumbnailLoadTarget = nullptr;
SRWLOCK g_thumbnailLoadLock = SRWLOCK_INIT;
SRWLOCK g_thumbnailFinalizeQueueLock = SRWLOCK_INIT;
std::vector<void*> g_thumbnailFinalizeQueue;
std::unordered_set<std::string> g_queuedThumbnailUrls;
volatile LONG g_thumbnailFinalizeWorkerArmed = 0;
volatile LONG g_offerDiagnosticCount = 0;
volatile LONG g_thumbnailCallbacksCompleted = 0;
volatile LONG g_offerKeyDiagnosticCount = 0;
std::array<unsigned char, kStringMapFindHookSize> g_stringMapFindOriginal{};
unsigned char* g_stringMapFindTarget = nullptr;
SRWLOCK g_stringMapFindLock = SRWLOCK_INIT;

struct StringLayout {
    union {
        char small[16];
        char* pointer;
    } storage;
    size_t size;
    size_t capacity;
};

static_assert(sizeof(StringLayout) == 32, "unexpected MSVC string layout");

bool WriteAll(SOCKET socket, const void* buffer, size_t size) {
    auto cursor = static_cast<const char*>(buffer);
    while (size) {
        int sent = send(socket, cursor, static_cast<int>(size), 0);
        if (sent <= 0) return false;
        cursor += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool ReadAll(SOCKET socket, void* buffer, size_t size) {
    auto cursor = static_cast<char*>(buffer);
    while (size) {
        int received = recv(socket, cursor, static_cast<int>(size), 0);
        if (received <= 0) return false;
        cursor += received;
        size -= static_cast<size_t>(received);
    }
    return true;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

bool ReadPackagedUtf8(const wchar_t* name, std::wstring& result) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ReadPackagedUtf8), &module)) {
        return false;
    }
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t* separator = wcsrchr(path, L'\\');
    if (!separator) return false;
    *(separator + 1) = L'\0';
    if (wcslen(path) + wcslen(name) >= MAX_PATH) return false;
    wcscat_s(path, name);

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= 16 * 1024 * 1024;
    std::string bytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                      &read, nullptr) && read == bytes.size();
    }
    CloseHandle(file);
    if (!ok) return false;
    int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLength <= 0) return false;
    result.resize(static_cast<size_t>(wideLength));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), result.data(), wideLength) == wideLength;
}

std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

struct WstringLayout {
    union { wchar_t small[8]; wchar_t* pointer; } storage;
    size_t size;
    size_t capacity;
};

static_assert(sizeof(WstringLayout) == 32, "unexpected MSVC wstring layout");

bool ReplaceExistingWstring(void* address, const std::string& utf8) {
    auto value = static_cast<WstringLayout*>(address);
    std::wstring replacement = Wide(utf8);
    if (replacement.size() > value->capacity) {
        // VS 2015+ std::wstring has a stable ABI and uses the shared release
        // CRT allocator. Let the string grow instead of silently retaining the
        // Windows identity token when the Apple TV token exceeds its buffer.
        reinterpret_cast<std::wstring*>(address)->assign(replacement);
        return true;
    }
    wchar_t* destination = value->capacity < 8 ? value->storage.small : value->storage.pointer;
    if (!destination) return false;
    std::memcpy(destination, replacement.data(), replacement.size() * sizeof(wchar_t));
    destination[replacement.size()] = L'\0';
    value->size = replacement.size();
    return true;
}

size_t WstringCapacity(void* address) {
    return static_cast<WstringLayout*>(address)->capacity;
}

bool QueryBridge(
    const std::wstring& method,
    const std::wstring& url,
    const std::wstring& headers,
    const std::vector<unsigned char>& body,
    std::array<std::string, 7>& response) {
    if (method == L"DIAG") return true;
    static INIT_ONCE winsockOnce = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&winsockOnce, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }, nullptr, nullptr);

    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return false;
    DWORD timeout = 2500;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kBridgePort);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(socket);
        return false;
    }

    std::array<std::string, 3> text{Utf8(method), Utf8(url), Utf8(headers)};
    uint32_t sizes[4]{
        static_cast<uint32_t>(text[0].size()), static_cast<uint32_t>(text[1].size()),
        static_cast<uint32_t>(text[2].size()), static_cast<uint32_t>(body.size())};
    bool ok = WriteAll(socket, kMagic, sizeof(kMagic) - 1) &&
        WriteAll(socket, sizes, sizeof(sizes));
    for (const auto& field : text) ok = ok && WriteAll(socket, field.data(), field.size());
    ok = ok && WriteAll(socket, body.data(), body.size());

    uint32_t status = 1;
    uint32_t responseSizes[7]{};
    ok = ok && ReadAll(socket, &status, sizeof(status)) &&
        ReadAll(socket, responseSizes, sizeof(responseSizes));
    for (size_t index = 0; ok && index < response.size(); ++index) {
        if (responseSizes[index] > 16 * 1024 * 1024) { ok = false; break; }
        response[index].resize(responseSizes[index]);
        ok = ReadAll(socket, response[index].data(), response[index].size());
    }
    closesocket(socket);
    return ok && status == 0;
}

DWORD WINAPI WalletServerThread(LPVOID) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        InterlockedExchange(&g_walletServerStatus, -1);
        return 0;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        InterlockedExchange(&g_walletServerStatus, -2);
        return 0;
    }
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(32117);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        InterlockedExchange(&g_walletServerStatus, -3);
        closesocket(listener);
        return 0;
    }
    if (listen(listener, 8) != 0) {
        InterlockedExchange(&g_walletServerStatus, -4);
        closesocket(listener);
        return 0;
    }
    InterlockedExchange(&g_walletServerStatus, 1);

    constexpr char body[] =
        "{\"Balance\":{\"currencies\":[{\"currencyType\":\"minecoin\",\"amount\":0}]},"
        "\"EntitlementReceipt\":\"\",\"Inventory\":{\"items\":[]}}";
    std::string response =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
        std::to_string(sizeof(body) - 1) + "\r\n\r\n" + body;
    constexpr unsigned char thumbnail[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xB5, 0x1C, 0x0C,
        0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41,
        0x54, 0x78, 0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00,
        0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xE3, 0x66,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0xAE, 0x42, 0x60, 0x82,
    };
    std::string thumbnailResponse =
        "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
        std::to_string(sizeof(thumbnail)) + "\r\n\r\n";
    thumbnailResponse.append(
        reinterpret_cast<const char*>(thumbnail), sizeof(thumbnail));
    for (;;) {
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        char request[4096]{};
        recv(client, request, sizeof(request), 0);
        const bool isThumbnail =
            std::strstr(request, "/marketplace-thumbnail.png") != nullptr;
        const std::string& selectedResponse =
            isThumbnail ? thumbnailResponse : response;
        WriteAll(client, selectedResponse.data(), selectedResponse.size());
        shutdown(client, SD_BOTH);
        closesocket(client);
        InterlockedExchange(&g_walletServerStatus, 2);
        std::array<std::string, 7> ignored{};
        QueryBridge(
            L"DIAG",
            isThumbnail ? L"diag://thumbnail-local-hit"
                        : L"diag://wallet-local-hit",
            L"", {}, ignored);
    }
}

void InstallJump(bool hook);
void InstallCatalogInitJump(bool hook);

void InstallPermanentJump(unsigned char* target, void* destination) {
    if (!target || !destination) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, kHookSize, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    unsigned char patch[kHookSize]{0x49, 0xBB};
    std::memcpy(patch + 2, &destination, sizeof(destination));
    patch[10] = 0x41;
    patch[11] = 0xFF;
    patch[12] = 0xE3;
    std::memcpy(target, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), target, kHookSize);
    DWORD ignored = 0;
    VirtualProtect(target, kHookSize, oldProtect, &ignored);
}

bool PatchWalletResolverLiteral(unsigned char* executable) {
    constexpr char localUrl[] = "https://127.0.0.1:32116/";
    unsigned char* replacement = nullptr;

    // The resolver uses a seven-byte RIP-relative LEA. Reserve a tiny data page
    // close enough to the executable for its signed 32-bit displacement.
    const uintptr_t base = reinterpret_cast<uintptr_t>(executable);
    for (uintptr_t address = base + 0x01B00000;
         address < base + 0x70000000;
         address += 0x10000) {
        replacement = static_cast<unsigned char*>(VirtualAlloc(
            reinterpret_cast<void*>(address), 0x1000,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (replacement) break;
    }
    if (!replacement) return false;
    std::memcpy(replacement, localUrl, sizeof(localUrl));

    unsigned char* instruction = executable + kWalletUrlLoadRva;
    const intptr_t displacement = replacement - (instruction + 7);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;

    const int32_t relative = static_cast<int32_t>(displacement);
    DWORD oldProtect = 0;
    if (!VirtualProtect(instruction, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    const unsigned char opcode[3]{0x48, 0x8D, 0x15};
    std::memcpy(instruction, opcode, sizeof(opcode));
    std::memcpy(instruction + 3, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), instruction, 7);
    DWORD ignored = 0;
    VirtualProtect(instruction, 7, oldProtect, &ignored);
    return true;
}

void InstallWalletResolverJump(bool hook);
void InstallWalletLoadJump(bool hook);
void InstallWalletCompletionJump(bool hook);

DWORD WINAPI ArmWalletResolverAfterSignIn(LPVOID) {
    Sleep(2000);
    // Do not inject wallet JSON into the token-request service object.
    // The native authentication result must remain untouched here.
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://wallet-injection-disabled", L"", {}, ignored);
    return 0;
}

using CatalogInit = void (__fastcall *)(void*);
using CatalogExecute = void (__fastcall *)(void*);
using CatalogParse = void (__fastcall *)(void*, void*);
using CatalogNotify = void (__fastcall *)(void*);
using CatalogSelect = void* (__fastcall *)(void*, void*);
using CatalogApplyManifest = void (__fastcall *)(void*, void*);
using JsonParse = void* (__fastcall *)(void*, const std::wstring*);
void ReleaseJsonValue(void* implementation);
DWORD WINAPI RetryCatalogAfterSignIn(LPVOID);
void InstallCatalogExecuteJump(bool hook);
void InstallOfferPurchasableJump(bool hook);
void InstallThumbnailCallbackJump(bool hook);
void InstallThumbnailDecodeCallbackJump(bool hook);
void InstallThumbnailTextureCallbackJump(bool hook);
void InstallThumbnailLoadJump(bool hook);

using OfferPurchasable = bool (__fastcall *)(void*);

bool __fastcall HookedOfferPurchasable(void* offer) {
    AcquireSRWLockExclusive(&g_offerPurchasableLock);
    InstallOfferPurchasableJump(false);
    const bool purchasable =
        reinterpret_cast<OfferPurchasable>(g_offerPurchasableTarget)(offer);
    InstallOfferPurchasableJump(true);
    ReleaseSRWLockExclusive(&g_offerPurchasableLock);

    const LONG diagnosticIndex = InterlockedIncrement(&g_offerDiagnosticCount);
    if (diagnosticIndex <= 4 && offer) {
        auto* bytes = static_cast<unsigned char*>(offer);
        const int offerTier = *reinterpret_cast<int*>(bytes + 0xF4);
        const int managerTier = g_catalogManager
            ? *reinterpret_cast<int*>(
                  static_cast<unsigned char*>(g_catalogManager) + 0x518)
            : -999;
        const bool identityFlag = bytes[0x151] != 0;
        const uintptr_t identityBegin =
            *reinterpret_cast<uintptr_t*>(bytes + 0x158);
        const uintptr_t identityEnd =
            *reinterpret_cast<uintptr_t*>(bytes + 0x160);
        const size_t identityCount = identityEnd >= identityBegin
            ? (identityEnd - identityBegin) / sizeof(uintptr_t) : 0;
        unsigned int identityVersion = 0;
        uintptr_t identityHandle = 0;
        unsigned int identityReady = 0;
        std::wstring thumbnail = L"<invalid>";
        auto* thumbnailLayout = reinterpret_cast<StringLayout*>(bytes + 0x60);
        if (thumbnailLayout->size < 2048 &&
            thumbnailLayout->capacity >= thumbnailLayout->size) {
            const char* thumbnailData = thumbnailLayout->capacity < 16
                ? thumbnailLayout->storage.small
                : thumbnailLayout->storage.pointer;
            if (thumbnailData) {
                thumbnail = Wide(std::string(
                    thumbnailData, thumbnailData + thumbnailLayout->size));
            }
        }
        if (identityCount != 0) {
            auto* identity = *reinterpret_cast<unsigned char**>(identityBegin);
            if (identity) {
                identityVersion =
                    *reinterpret_cast<unsigned int*>(identity + 0x90);
                identityHandle =
                    *reinterpret_cast<uintptr_t*>(identity + 0xA8);
                identityReady = identity[0xD8];
            }
        }
        std::array<std::string, 7> ignored{};
        QueryBridge(
            L"DIAG",
            L"diag://offer-gate?index=" + std::to_wstring(diagnosticIndex) +
                L"&purchasable=" + std::to_wstring(purchasable ? 1 : 0) +
                L"&offerTier=" + std::to_wstring(offerTier) +
                L"&managerTier=" + std::to_wstring(managerTier) +
                L"&identityFlag=" + std::to_wstring(identityFlag ? 1 : 0) +
                L"&identityCount=" + std::to_wstring(identityCount) +
                L"&identityReady=" + std::to_wstring(identityReady) +
                L"&identityHandle=" + std::to_wstring(identityHandle) +
                L"&identityVersion=" + std::to_wstring(identityVersion) +
                L"&thumbnail=" + thumbnail,
            L"", {}, ignored);
    }
    return purchasable;
}

void InstallOfferPurchasableJump(bool hook) {
    if (!g_offerPurchasableTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_offerPurchasableTarget, kOfferPurchasableHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kOfferPurchasableHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedOfferPurchasable);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kOfferPurchasableHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_offerPurchasableTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_offerPurchasableTarget,
                    g_offerPurchasableOriginal.data(),
                    g_offerPurchasableOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_offerPurchasableTarget,
                          kOfferPurchasableHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_offerPurchasableTarget, kOfferPurchasableHookSize,
                   oldProtect, &ignored);
}

using ThumbnailCallback = void (__fastcall *)(void*, void*, void*, void*);

void InstallStringMapFindJump(bool hook);
using StringMapFind = void* (__fastcall *)(void*, void*, const StringLayout*);

void* __fastcall HookedStringMapFind(
    void* map, void* outputIterator, const StringLayout* key) {
    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (executable && _ReturnAddress() ==
            executable + kOfferFactoryFindReturnRva) {
        // 6C86F0 normally reuses an offer from manager+0x58 when its key is
        // found.  The legacy cache is incorrectly resolving every translated
        // UUID to the first offer.  Return this map's end iterator only at that
        // exact call site, preserving all other unordered_map lookups and
        // allowing the native miss path to construct and cache one offer per
        // catalog row.
        *reinterpret_cast<void**>(outputIterator) =
            *reinterpret_cast<void**>(
                static_cast<unsigned char*>(map) + 8);
        return outputIterator;
    }

    AcquireSRWLockExclusive(&g_stringMapFindLock);
    InstallStringMapFindJump(false);
    void* result = reinterpret_cast<StringMapFind>(g_stringMapFindTarget)(
        map, outputIterator, key);
    InstallStringMapFindJump(true);
    ReleaseSRWLockExclusive(&g_stringMapFindLock);
    return result;
}

void InstallStringMapFindJump(bool hook) {
    if (!g_stringMapFindTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_stringMapFindTarget, kStringMapFindHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kStringMapFindHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedStringMapFind);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kStringMapFindHookSize; ++index)
            patch[index] = 0x90;
        std::memcpy(g_stringMapFindTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_stringMapFindTarget, g_stringMapFindOriginal.data(),
                    g_stringMapFindOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_stringMapFindTarget,
                          kStringMapFindHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_stringMapFindTarget, kStringMapFindHookSize,
                   oldProtect, &ignored);
}

void __fastcall HookedThumbnailCallback(
    void* self, void* result, void* third, void* fourth) {
    // 6C8C40 decrements manager->pending[index].  A catalog retry can replace
    // that counter while the synthetic thumbnail completions are draining.
    // Never let the legacy completion path wrap it to UINT_MAX: zero means the
    // query has already completed, and one gives this callback a safe final
    // decrement while still running the native offer-registration code.
    if (self && g_catalogManager) {
        const int index = *reinterpret_cast<int*>(
            static_cast<unsigned char*>(self) + 8);
        auto* manager = static_cast<unsigned char*>(g_catalogManager);
        const auto countersBegin =
            *reinterpret_cast<uintptr_t*>(manager + 0x4D0);
        const auto countersEnd =
            *reinterpret_cast<uintptr_t*>(manager + 0x4D8);
        if (index >= 0 &&
            countersBegin + (static_cast<size_t>(index) + 1) * sizeof(uint32_t)
                <= countersEnd) {
            auto* counter = reinterpret_cast<volatile LONG*>(
                countersBegin + static_cast<size_t>(index) * sizeof(uint32_t));
            if (*counter <= 0) InterlockedExchange(counter, 1);
        }
    }

    AcquireSRWLockExclusive(&g_thumbnailCallbackLock);
    InstallThumbnailCallbackJump(false);
    reinterpret_cast<ThumbnailCallback>(g_thumbnailCallbackTarget)(
        self, result, third, fourth);
    InstallThumbnailCallbackJump(true);
    ReleaseSRWLockExclusive(&g_thumbnailCallbackLock);

    size_t offerCount = 0;
    size_t registeredCount = 0;
    if (g_catalogManager) {
        auto* manager = static_cast<unsigned char*>(g_catalogManager);
        const auto begin = *reinterpret_cast<uintptr_t*>(manager + 0xB0);
        const auto end = *reinterpret_cast<uintptr_t*>(manager + 0xB8);
        if (end >= begin) offerCount = (end - begin) / sizeof(uintptr_t);
        // manager+0x160 is the native std::list/map populated at 6C8D3F.
        // Its node count lives at +0x10 (manager+0x170).
        registeredCount =
            *reinterpret_cast<size_t*>(manager + 0x170);
    }
    const LONG completed = InterlockedIncrement(&g_thumbnailCallbacksCompleted);
    if (completed == 1 || completed % 25 == 0) {
        std::array<std::string, 7> ignored{};
        QueryBridge(
            L"DIAG",
            L"diag://thumbnail-callback-progress?completed=" +
                std::to_wstring(completed) + L"&offers=" +
                std::to_wstring(offerCount) + L"&registered=" +
                std::to_wstring(registeredCount),
            L"", {}, ignored);
    }
}

void InstallThumbnailCallbackJump(bool hook) {
    if (!g_thumbnailCallbackTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_thumbnailCallbackTarget, kThumbnailCallbackHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kThumbnailCallbackHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedThumbnailCallback);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kThumbnailCallbackHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_thumbnailCallbackTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_thumbnailCallbackTarget,
                    g_thumbnailCallbackOriginal.data(),
                    g_thumbnailCallbackOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_thumbnailCallbackTarget,
                          kThumbnailCallbackHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_thumbnailCallbackTarget, kThumbnailCallbackHookSize,
                   oldProtect, &ignored);
}

void __fastcall HookedThumbnailDecodeCallback(
    void* self, void* result, void* third, void* fourth) {
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://thumbnail-decode-callback-enter", L"", {}, ignored);
    AcquireSRWLockExclusive(&g_thumbnailDecodeCallbackLock);
    InstallThumbnailDecodeCallbackJump(false);
    reinterpret_cast<ThumbnailCallback>(g_thumbnailDecodeCallbackTarget)(
        self, result, third, fourth);
    InstallThumbnailDecodeCallbackJump(true);
    ReleaseSRWLockExclusive(&g_thumbnailDecodeCallbackLock);
    QueryBridge(L"DIAG", L"diag://thumbnail-decode-callback-exit", L"", {}, ignored);
}

void InstallThumbnailDecodeCallbackJump(bool hook) {
    if (!g_thumbnailDecodeCallbackTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_thumbnailDecodeCallbackTarget,
                        kThumbnailDecodeCallbackHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kThumbnailDecodeCallbackHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedThumbnailDecodeCallback);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kThumbnailDecodeCallbackHookSize; ++index)
            patch[index] = 0x90;
        std::memcpy(g_thumbnailDecodeCallbackTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_thumbnailDecodeCallbackTarget,
                    g_thumbnailDecodeCallbackOriginal.data(),
                    g_thumbnailDecodeCallbackOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_thumbnailDecodeCallbackTarget,
                          kThumbnailDecodeCallbackHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_thumbnailDecodeCallbackTarget,
                   kThumbnailDecodeCallbackHookSize, oldProtect, &ignored);
}

void __fastcall HookedThumbnailTextureCallback(
    void* self, void* result, void* third, void* fourth) {
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://thumbnail-texture-callback-enter", L"", {}, ignored);
    AcquireSRWLockExclusive(&g_thumbnailTextureCallbackLock);
    InstallThumbnailTextureCallbackJump(false);
    reinterpret_cast<ThumbnailCallback>(g_thumbnailTextureCallbackTarget)(
        self, result, third, fourth);
    InstallThumbnailTextureCallbackJump(true);
    ReleaseSRWLockExclusive(&g_thumbnailTextureCallbackLock);
    QueryBridge(L"DIAG", L"diag://thumbnail-texture-callback-exit", L"", {}, ignored);
}

void InstallThumbnailTextureCallbackJump(bool hook) {
    if (!g_thumbnailTextureCallbackTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_thumbnailTextureCallbackTarget,
                        kThumbnailTextureCallbackHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kThumbnailTextureCallbackHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedThumbnailTextureCallback);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kThumbnailTextureCallbackHookSize; ++index)
            patch[index] = 0x90;
        std::memcpy(g_thumbnailTextureCallbackTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_thumbnailTextureCallbackTarget,
                    g_thumbnailTextureCallbackOriginal.data(),
                    g_thumbnailTextureCallbackOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_thumbnailTextureCallbackTarget,
                          kThumbnailTextureCallbackHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_thumbnailTextureCallbackTarget,
                   kThumbnailTextureCallbackHookSize, oldProtect, &ignored);
}

struct OwnedThumbnailCallback {
    alignas(16) unsigned char wrapper[0x40]{};
};

DWORD WINAPI FinalizeQueuedThumbnails(LPVOID) {
    // The catalog callback constructs all offers in one frame.  Let that frame
    // return before completing its captured callbacks, matching the timing of
    // the original asynchronous image service and avoiding re-entrant teardown.
    Sleep(750);
    for (;;) {
        std::vector<void*> captures;
        AcquireSRWLockExclusive(&g_thumbnailFinalizeQueueLock);
        captures.swap(g_thumbnailFinalizeQueue);
        ReleaseSRWLockExclusive(&g_thumbnailFinalizeQueueLock);
        if (captures.empty()) break;

        std::array<std::string, 7> ignored{};
        QueryBridge(
            L"DIAG",
            L"diag://thumbnail-bypass-batch?count=" +
                std::to_wstring(captures.size()),
            L"", {}, ignored);
        for (void* saved : captures) {
            auto* owned = static_cast<OwnedThumbnailCallback*>(saved);
            void* target = *reinterpret_cast<void**>(owned->wrapper + 0x38);
            auto vtable = *reinterpret_cast<void***>(target);
            auto* capture = static_cast<unsigned char*>(target) + 8;
            auto* managerForCallback = *reinterpret_cast<unsigned char**>(capture);
            const int queryIndex = *reinterpret_cast<int*>(capture + 8);
            auto* counters = *reinterpret_cast<unsigned int**>(managerForCallback + 0x4D0);
            if (vtable[2] == reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + 0x6CCFD0 &&
                (g_thumbnailCallbacksCompleted == 0 || g_thumbnailCallbacksCompleted == 124)) {
                auto* query = *reinterpret_cast<unsigned char**>(capture + 0x50);
                QueryBridge(L"DIAG", L"diag://query-completion-state?index=" +
                    std::to_wstring(queryIndex) + L"&counter=" + std::to_wstring(counters[queryIndex]) +
                    L"&queryOffers=" + std::to_wstring((
                    *reinterpret_cast<uintptr_t*>(query + 0x68) -
                    *reinterpret_cast<uintptr_t*>(query + 0x60)) / 8), L"", {}, ignored);
            }
            using Invoke = void (__fastcall *)(void*, void*);
            reinterpret_cast<Invoke>(vtable[2])(target, nullptr);
            const LONG completed = InterlockedIncrement(&g_thumbnailCallbacksCompleted);
            if (completed == 1 || completed % 25 == 0) {
                auto* manager = static_cast<unsigned char*>(g_catalogManager);
                const auto begin = *reinterpret_cast<uintptr_t*>(manager + 0xB0);
                const auto end = *reinterpret_cast<uintptr_t*>(manager + 0xB8);
                QueryBridge(L"DIAG", L"diag://owned-thumbnail-complete?count=" +
                    std::to_wstring(completed) + L"&offers=" +
                    std::to_wstring((end - begin) / sizeof(uintptr_t)), L"", {}, ignored);
            }
            using Destroy = void (__fastcall *)(void*, bool);
            reinterpret_cast<Destroy>(vtable[4])(target, target != owned->wrapper);
            delete owned;
        }
    }
    InterlockedExchange(&g_thumbnailFinalizeWorkerArmed, 0);
    // Close the small race where a producer queued work between the final
    // empty check and clearing the armed flag.
    AcquireSRWLockShared(&g_thumbnailFinalizeQueueLock);
    const bool pending = !g_thumbnailFinalizeQueue.empty();
    ReleaseSRWLockShared(&g_thumbnailFinalizeQueueLock);
    if (pending &&
        InterlockedCompareExchange(&g_thumbnailFinalizeWorkerArmed, 1, 0) == 0) {
        HANDLE thread = CreateThread(
            nullptr, 0, &FinalizeQueuedThumbnails, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return 0;
}

using ThumbnailLoad = void (__fastcall *)(void*, StringLayout*, void*);

void __fastcall HookedThumbnailLoad(
    void* service, StringLayout* url, void* callback) {
    static const std::string prefix =
        "http://127.0.0.1:32117/marketplace-thumbnail.png?id=";
    std::string value;
    if (url && url->size < 4096 && url->capacity >= url->size) {
        const char* data = url->capacity < 16
            ? url->storage.small : url->storage.pointer;
        if (data) value.assign(data, data + url->size);
    }
    if (!kUseNativeThumbnails && value.rfind(prefix, 0) == 0 && callback) {
        void* target = *reinterpret_cast<void**>(
            static_cast<unsigned char*>(callback) + 0x38);
        if (target) {
            // std::function stores the vtable-bearing allocation in target;
            // the lambda capture used by 6C8C40 begins eight bytes later.
            void* capture = static_cast<unsigned char*>(target) + 8;
            // The 1.1.5 parser leaves offer.productId (the string at offset 0)
            // empty for these translated modern rows.  The native registration
            // map is keyed by that string, so 125 offers otherwise collapse to
            // one entry.  Recover the exact product UUID already embedded in
            // our per-offer thumbnail URL and copy it using the game's own
            // std::string assignment routine.
            auto executableBase = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
            auto callbackVtable = *reinterpret_cast<void***>(target);
            const bool queryCallback = callbackVtable[2] == executableBase + 0x6CCFD0;
            void* offer = *reinterpret_cast<void**>(
                static_cast<unsigned char*>(capture) + (queryCallback ? 0x58 : 0x50));
            const std::string productId = value.substr(prefix.size());
            if (offer && !productId.empty()) {
                auto* key = static_cast<StringLayout*>(offer);
                std::string source = productId;
                auto executable = reinterpret_cast<unsigned char*>(
                    GetModuleHandleW(nullptr));
                using StringAssign = void (__fastcall *)(
                    StringLayout*, const StringLayout*, size_t, size_t);
                reinterpret_cast<StringAssign>(
                    executable + kStringAssignRva)(
                        key, reinterpret_cast<const StringLayout*>(&source),
                        0, static_cast<size_t>(-1));
                const LONG keyIndex =
                    InterlockedIncrement(&g_offerKeyDiagnosticCount);
                if (keyIndex <= 4) {
                    std::string actual;
                    if (key->size < 4096 && key->capacity >= key->size) {
                        const char* keyData = key->capacity < 16
                            ? key->storage.small : key->storage.pointer;
                        if (keyData) actual.assign(keyData, keyData + key->size);
                    }
                    std::array<std::string, 7> ignored{};
                    QueryBridge(
                        L"DIAG",
                        L"diag://offer-key?index=" +
                            std::to_wstring(keyIndex) + L"&size=" +
                            std::to_wstring(key->size) + L"&pointer=" +
                            std::to_wstring(
                                reinterpret_cast<uintptr_t>(offer)) +
                            L"&value=" +
                            std::wstring(actual.begin(), actual.end()),
                        L"", {}, ignored);
                }
            }
            AcquireSRWLockExclusive(&g_thumbnailFinalizeQueueLock);
            // Every request owns a completion, including repeated Store opens.
            // Deduplicating by URL strands the new query's pending counter.
            const bool firstForUrl = true;
            if (firstForUrl) {
                auto* owned = new OwnedThumbnailCallback{};
                auto vtable = *reinterpret_cast<void***>(target);
                using Clone = void* (__fastcall *)(void*, void*);
                void* clone = reinterpret_cast<Clone>(vtable[0])(target, owned->wrapper);
                *reinterpret_cast<void**>(owned->wrapper + 0x38) = clone;
                if (clone) g_thumbnailFinalizeQueue.push_back(owned);
                else delete owned;
            }
            ReleaseSRWLockExclusive(&g_thumbnailFinalizeQueueLock);
            if (!firstForUrl) return;
            if (InterlockedCompareExchange(
                    &g_thumbnailFinalizeWorkerArmed, 1, 0) == 0) {
                HANDLE thread = CreateThread(
                    nullptr, 0, &FinalizeQueuedThumbnails,
                    nullptr, 0, nullptr);
                if (thread) CloseHandle(thread);
            }
            return;
        }
    }

    AcquireSRWLockExclusive(&g_thumbnailLoadLock);
    InstallThumbnailLoadJump(false);
    reinterpret_cast<ThumbnailLoad>(g_thumbnailLoadTarget)(
        service, url, callback);
    InstallThumbnailLoadJump(true);
    ReleaseSRWLockExclusive(&g_thumbnailLoadLock);
}

void InstallThumbnailLoadJump(bool hook) {
    if (!g_thumbnailLoadTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_thumbnailLoadTarget, kThumbnailLoadHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kThumbnailLoadHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedThumbnailLoad);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kThumbnailLoadHookSize; ++index)
            patch[index] = 0x90;
        std::memcpy(g_thumbnailLoadTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_thumbnailLoadTarget, g_thumbnailLoadOriginal.data(),
                    g_thumbnailLoadOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_thumbnailLoadTarget,
                          kThumbnailLoadHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_thumbnailLoadTarget, kThumbnailLoadHookSize,
                   oldProtect, &ignored);
}

void AddCatalogTaskReference(void* self) {
    if (!self) return;
    auto* control = static_cast<unsigned char*>(self) - 0x10;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(control + 8));
}

void ReleaseCatalogTaskReference(void* self) {
    if (!self) return;
    auto* control = static_cast<unsigned char*>(self) - 0x10;
    auto vtable = *reinterpret_cast<void***>(control);
    if (InterlockedExchangeAdd(
            reinterpret_cast<volatile LONG*>(control + 8), -1) == 1) {
        using Destroy = void (__fastcall *)(void*);
        reinterpret_cast<Destroy>(vtable[0])(control);
        if (InterlockedExchangeAdd(
                reinterpret_cast<volatile LONG*>(control + 12), -1) == 1) {
            reinterpret_cast<Destroy>(vtable[1])(control);
        }
    }
}

struct CatalogWork {
    void* self{};
    alignas(16) unsigned char callback[0x40]{};
};

bool CloneCatalogCallback(void* self, CatalogWork* work) {
    if (!self || !work) return false;
    auto* sourceWrapper = static_cast<unsigned char*>(self) + 0x2C0;
    void* source = *reinterpret_cast<void**>(sourceWrapper + 0x38);
    if (!source) return false;
    auto vtable = *reinterpret_cast<void***>(source);
    using Clone = void* (__fastcall *)(void*, void*);
    void* clone = reinterpret_cast<Clone>(vtable[0])(
        source, work->callback);
    *reinterpret_cast<void**>(work->callback + 0x38) = clone;
    return clone != nullptr;
}

void DestroySavedCatalogCallback(CatalogWork* work) {
    if (!work) return;
    void* target = *reinterpret_cast<void**>(work->callback + 0x38);
    if (!target) return;
    auto vtable = *reinterpret_cast<void***>(target);
    using Destroy = void (__fastcall *)(void*, bool);
    reinterpret_cast<Destroy>(vtable[4])(
        target, target != work->callback);
    *reinterpret_cast<void**>(work->callback + 0x38) = nullptr;
}

bool InvokeSavedCatalogCallback(CatalogWork* work, void* result) {
    if (!work) return false;
    void* target = *reinterpret_cast<void**>(work->callback + 0x38);
    if (!target) return false;
    auto vtable = *reinterpret_cast<void***>(target);
    using Invoke = void (__fastcall *)(void*, void*);
    reinterpret_cast<Invoke>(vtable[2])(target, result);
    DestroySavedCatalogCallback(work);
    return true;
}

std::wstring CatalogTaskString(void* self, size_t offset) {
    if (!self) return {};
    auto* value = reinterpret_cast<StringLayout*>(
        static_cast<unsigned char*>(self) + offset);
    if (value->size > 4096 || value->capacity < value->size) return L"<invalid>";
    const char* data = value->capacity < 16
        ? value->storage.small
        : value->storage.pointer;
    if (!data) return L"<null>";
    std::string copy(data, data + value->size);
    return Wide(copy);
}

DWORD WINAPI CompleteLocalCatalog(LPVOID parameter) {
    auto* work = static_cast<CatalogWork*>(parameter);
    void* self = work ? work->self : nullptr;
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://catalog-local-inject", L"", {}, ignored);

    std::wstring document;
    void* jsonValue = nullptr;
    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const std::wstring contentType = self
        ? CatalogTaskString(static_cast<unsigned char*>(self) + 0x108, 0x40)
        : std::wstring{};
    std::wstring catalogFile = L"LegacyEmptyCatalog.json";
    if (contentType == L"QueryManifest_V1.1.5") {
        catalogFile = L"LegacyManifestCatalog.json";
    } else if (contentType == L"MarketplaceDurableCatalog_V1.1") {
        catalogFile = L"LegacyOffersCatalog.json";
        auto* query = static_cast<unsigned char*>(self) + 0x108;
        auto rowName = CatalogTaskString(query, 0x158);
        // Both navigation and home rows route by category searchCriteria; rowName is display text.
        const auto criteriaBegin = *reinterpret_cast<uintptr_t*>(query + 0xC0);
        const auto criteriaEnd = *reinterpret_cast<uintptr_t*>(query + 0xC8);
        if (criteriaBegin && criteriaEnd >= criteriaBegin + 0x20) {
            rowName = CatalogTaskString(reinterpret_cast<void*>(criteriaBegin), 0);
        }
        if (rowName == L"skinpacks" || rowName == L"texturepacks" ||
            rowName == L"worlds" || rowName == L"mashups" ||
            rowName == L"free" || rowName == L"new") {
            catalogFile = L"LegacyCategory-" + rowName + L".json";
        }
        // Create World asks for [mashup, worldtemplate] Store suggestions.
        // Never replace this query with the entire commerce catalog. Usable
        // installed templates are enumerated independently by the native pack
        // repository; purchases/catalog metadata alone do not make one usable.
        if (rowName == L"mashup" || rowName == L"worldtemplate") {
            catalogFile = L"LegacyEmptyCatalog.json";
        }
    }
    if (self && executable && ReadPackagedUtf8(catalogFile.c_str(), document)) {
        reinterpret_cast<JsonParse>(executable + kJsonParseRva)(
            &jsonValue, &document);
    }
    if (jsonValue) {
        // CatalogParse consumes the same JSON wrapper normally supplied by the
        // retired XForge HTTP task. CatalogNotify then runs the request's
        // already-installed completion callback with the parsed result vector.
        reinterpret_cast<CatalogParse>(executable + kCatalogParseRva)(
            self, &jsonValue);
        auto* result = static_cast<unsigned char*>(self) + 0x300;
        const auto declaredCount =
            *reinterpret_cast<unsigned int*>(result);
        const auto begin = *reinterpret_cast<uintptr_t*>(result + 8);
        const auto end = *reinterpret_cast<uintptr_t*>(result + 16);
        const auto parsedRows = end >= begin ? (end - begin) / 0x1C0 : 0;
        if (contentType == L"MarketplaceDurableCatalog_V1.1" &&
            executable && begin && parsedRows) {
            // The retired 1.1.5 parser successfully retains our UUID tag but
            // leaves documentId/productId empty in its 0x1C0-byte row.  Offer
            // lookup at 6C86F0 is keyed by row+0x28, so repairing it later in
            // the thumbnail callback is too late: every row has already reused
            // the first blank-key offer.  Promote the first UUID tag into both
            // legacy ID fields before invoking the catalog consumer.
            using StringAssign = void (__fastcall *)(
                StringLayout*, const StringLayout*, size_t, size_t);
            auto assign = reinterpret_cast<StringAssign>(
                executable + kStringAssignRva);
            for (size_t rowIndex = 0; rowIndex < parsedRows; ++rowIndex) {
                auto* row = reinterpret_cast<unsigned char*>(
                    begin + rowIndex * 0x1C0);
                const auto tagsBegin =
                    *reinterpret_cast<uintptr_t*>(row + 0xC8);
                const auto tagsEnd =
                    *reinterpret_cast<uintptr_t*>(row + 0xD0);
                if (tagsBegin && tagsEnd >= tagsBegin + sizeof(StringLayout)) {
                    auto* source = reinterpret_cast<const StringLayout*>(
                        tagsBegin);
                    assign(reinterpret_cast<StringLayout*>(row + 0x08),
                           source, 0, static_cast<size_t>(-1));
                    assign(reinterpret_cast<StringLayout*>(row + 0x28),
                           source, 0, static_cast<size_t>(-1));
                }
            }
            for (size_t rowIndex = 0;
                 rowIndex < parsedRows && rowIndex < 4; ++rowIndex) {
                auto* row = reinterpret_cast<unsigned char*>(
                    begin + rowIndex * 0x1C0);
                std::array<std::string, 7> keyIgnored{};
                QueryBridge(
                    L"DIAG",
                    L"diag://catalog-row-key?index=" +
                        std::to_wstring(rowIndex + 1) + L"&value=" +
                        CatalogTaskString(row, 0x28),
                    L"", {}, keyIgnored);
            }
        }
        size_t firstTagCount = 0;
        size_t firstCustomLength = 0;
        std::wstring firstTag;
        if (begin && end >= begin + 0x1C0) {
            const auto tagsBegin = *reinterpret_cast<uintptr_t*>(begin + 0xC8);
            const auto tagsEnd = *reinterpret_cast<uintptr_t*>(begin + 0xD0);
            if (tagsEnd >= tagsBegin) firstTagCount = (tagsEnd - tagsBegin) / 0x20;
            firstCustomLength = *reinterpret_cast<size_t*>(begin + 0x190);
            if (firstTagCount) {
                const auto tagLength = *reinterpret_cast<size_t*>(tagsBegin + 0x10);
                const auto tagCapacity = *reinterpret_cast<size_t*>(tagsBegin + 0x18);
                const char* tagData = tagCapacity >= 0x10
                    ? *reinterpret_cast<const char**>(tagsBegin)
                    : reinterpret_cast<const char*>(tagsBegin);
                if (tagData && tagLength < 256) {
                    firstTag.assign(tagData, tagData + tagLength);
                }
            }
        }
        void* selected = reinterpret_cast<CatalogSelect>(
            executable + kCatalogSelectRva)(nullptr, result);
        long long selectedIndex = -1;
        const auto selectedAddress = reinterpret_cast<uintptr_t>(selected);
        if (selectedAddress >= begin && selectedAddress < end) {
            selectedIndex = static_cast<long long>(
                (selectedAddress - begin) / 0x1C0);
        }
        QueryBridge(
            L"DIAG",
            L"diag://catalog-result?count=" + std::to_wstring(declaredCount) +
                L"&rows=" + std::to_wstring(parsedRows) +
                L"&firstTags=" + std::to_wstring(firstTagCount) +
                L"&firstTag=" + firstTag +
                L"&firstCustom=" + std::to_wstring(firstCustomLength) +
                L"&selected=" + std::to_wstring(selectedIndex),
            L"", {}, ignored);
        // Parsed catalog rows retain shared references into the JSON DOM. The
        // normal HTTP task owns that root until task teardown; keep this local
        // root alive for the (shorter) lifetime of the game process as well.
        // Releasing it here leaves every non-empty result with dangling
        // shared_ptr control blocks and crashes in their first vector move.
        // Execute is called by the native task scheduler. Populate its result
        // before returning, then let the scheduler call Notify exactly once.
        // An independent worker raced scheduler teardown and delivered a second
        // callback after Notify had already published an empty result.
        if (g_catalogManager) {
            auto* manager = static_cast<unsigned char*>(g_catalogManager);
            const auto queryBegin =
                *reinterpret_cast<uintptr_t*>(manager + 0x98);
            const auto queryEnd =
                *reinterpret_cast<uintptr_t*>(manager + 0xA0);
            const auto offerBegin =
                *reinterpret_cast<uintptr_t*>(manager + 0xB0);
            const auto offerEnd =
                *reinterpret_cast<uintptr_t*>(manager + 0xB8);
            const auto queryCount = queryEnd >= queryBegin
                ? (queryEnd - queryBegin) / 0x78 : 0;
            const auto offerCount = offerEnd >= offerBegin
                ? (offerEnd - offerBegin) / sizeof(uintptr_t) : 0;
            QueryBridge(
                L"DIAG",
                L"diag://catalog-manager?byte4c8=" +
                    std::to_wstring(manager[0x4C8]) +
                    L"&ready4c9=" + std::to_wstring(manager[0x4C9]) +
                    L"&initialized4ca=" + std::to_wstring(manager[0x4CA]) +
                    L"&pending=" + std::to_wstring(
                        *reinterpret_cast<unsigned int*>(manager + 0x4E8)) +
                    L"&queries=" + std::to_wstring(queryCount) +
                    L"&offers=" + std::to_wstring(offerCount),
                L"", {}, ignored);
        }
        QueryBridge(L"DIAG", L"diag://catalog-local-complete", L"", {}, ignored);
        delete work;
        return 0;
    }

    QueryBridge(L"DIAG", L"diag://catalog-local-fallback", L"", {}, ignored);
    DestroySavedCatalogCallback(work);
    delete work;
    return 0;
}

void __fastcall HookedCatalogExecute(void* self) {
    std::string serializedQuery;
    using SerializeQuery = void* (__fastcall *)(void*, void*);
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    reinterpret_cast<SerializeQuery>(base + 0x60C9A0)(
        static_cast<unsigned char*>(self) + 0x108, &serializedQuery);
    std::array<std::string, 7> queryIgnored{};
    QueryBridge(L"DIAG", L"diag://catalog-query-cache-key?value=" + Wide(serializedQuery), L"", {}, queryIgnored);
    std::array<std::string, 7> ignored{};
    auto* query = static_cast<unsigned char*>(self) + 0x108;
    QueryBridge(
        L"DIAG",
        L"diag://catalog-local-execute?q0=" + CatalogTaskString(query, 0x00) +
            L"&q20=" + CatalogTaskString(query, 0x20) +
            L"&q40=" + CatalogTaskString(query, 0x40) +
            L"&q60=" + CatalogTaskString(query, 0x60) +
            L"&q80=" + CatalogTaskString(query, 0x80) +
            L"&qa0=" + CatalogTaskString(query, 0xA0) +
            L"&qd8=" + CatalogTaskString(query, 0xD8) +
            L"&qf8=" + CatalogTaskString(query, 0xF8) +
            L"&q118=" + CatalogTaskString(query, 0x118) +
            L"&q138=" + CatalogTaskString(query, 0x138) +
            L"&q158=" + CatalogTaskString(query, 0x158) +
            L"&q190=" + CatalogTaskString(query, 0x190) +
            L"&mode=" + std::to_wstring(
                *reinterpret_cast<unsigned int*>(
                    static_cast<unsigned char*>(self) + 0x3A0)),
        L"", {}, ignored);

    // The task already owns its completion callback when Execute is entered.
    // Keep it alive ourselves and do not start the retired XForge request: its
    // eventual failure otherwise races the locally supplied successful result.
    // Complete on a worker so this Execute frame has returned before the
    // callback is allowed to release or re-enter the task.
    auto* work = new (std::nothrow) CatalogWork{};
    if (!work) return;
    work->self = self;
    CompleteLocalCatalog(work);
}

void InstallCatalogExecuteJump(bool hook) {
    if (!g_catalogExecuteTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_catalogExecuteTarget, kCatalogExecuteHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kCatalogExecuteHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedCatalogExecute);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kCatalogExecuteHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_catalogExecuteTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_catalogExecuteTarget, g_catalogExecuteOriginal.data(),
                    g_catalogExecuteOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_catalogExecuteTarget,
                          kCatalogExecuteHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_catalogExecuteTarget, kCatalogExecuteHookSize,
                   oldProtect, &ignored);
}

void __fastcall HookedCatalogInit(void* self) {
    g_catalogManager = self;
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://catalog-init-enter", L"", {}, ignored);

    AcquireSRWLockExclusive(&g_catalogInitLock);
    InstallCatalogInitJump(false);
    reinterpret_cast<CatalogInit>(g_catalogInitTarget)(self);
    InstallCatalogInitJump(true);
    ReleaseSRWLockExclusive(&g_catalogInitLock);

    QueryBridge(L"DIAG", L"diag://catalog-init-exit", L"", {}, ignored);
    // Local catalog completion no longer depends on network sign-in timing.
    // A timed retry invalidates query objects retained by thumbnail callbacks.
}

void InstallCatalogInitJump(bool hook) {
    if (!g_catalogInitTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_catalogInitTarget, kCatalogInitHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kCatalogInitHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedCatalogInit);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kCatalogInitHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_catalogInitTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_catalogInitTarget, g_catalogInitOriginal.data(),
                    g_catalogInitOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_catalogInitTarget,
                          kCatalogInitHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_catalogInitTarget, kCatalogInitHookSize,
                   oldProtect, &ignored);
}

DWORD WINAPI RetryCatalogAfterSignIn(LPVOID) {
    return 0; // Local completion owns the query lifecycle; never reset it on a timer.
    // The constructor's first manifest request is made before the bridged Xbox
    // identity exists. Give the rest of sign-in a moment to settle, then clear
    // the one-shot byte and run the game's own initializer again.
    Sleep(3000);
    void* manager = g_catalogManager;
    if (!manager) {
        InterlockedExchange(&g_catalogRetryScheduled, 0);
        return 0;
    }
    // A Store opened after sign-in may already have completed successfully.
    // Reinitializing here discards its offers and the per-URL completion cache
    // then suppresses the second batch.
    if (*reinterpret_cast<size_t*>(static_cast<unsigned char*>(manager) + 0x170)) {
        return 0;
    }
    *reinterpret_cast<volatile unsigned char*>(
        static_cast<unsigned char*>(manager) + 0x4CA) = 0;
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://catalog-init-retry", L"", {}, ignored);
    HookedCatalogInit(manager);
    return 0;
}

using WalletResolver = std::string* (__fastcall *)(void*, std::string*, bool);
void* g_storeWallet = nullptr;
void ApplyLiveWallet(void* wallet);

std::string* __fastcall HookedWalletResolver(
    void* self, std::string* result, bool flag) {
    auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) -
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    AcquireSRWLockExclusive(&g_walletResolverLock);
    InstallWalletResolverJump(false);
    auto original = reinterpret_cast<WalletResolver>(g_walletResolverTarget);
    std::string* resolved = original(self, result, flag);
    InstallWalletResolverJump(true);
    ReleaseSRWLockExclusive(&g_walletResolverLock);
    // This caller supplies WalletService::context at +0x50. Keep the URL and
    // authentication flow unchanged; refresh the actual wallet service only.
    if (caller == 0x69A371 && self) {
        ApplyLiveWallet(static_cast<unsigned char*>(self) - 0x50);
    }
    return resolved;
}

void InstallWalletResolverJump(bool hook) {
    if (!g_walletResolverTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_walletResolverTarget, kWalletResolverHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kWalletResolverHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedWalletResolver);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        patch[13] = 0x90;
        std::memcpy(g_walletResolverTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_walletResolverTarget, g_walletResolverOriginal.data(),
                    g_walletResolverOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_walletResolverTarget,
                          kWalletResolverHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_walletResolverTarget, kWalletResolverHookSize,
                   oldProtect, &ignored);
}

using WalletLoad = void* (__fastcall *)(void*, void*, const std::string*);
using WalletPreprocess = void (__fastcall *)(void*, void*);
using WalletContext = void* (__fastcall *)(void*, std::string*);
using WalletParse = void (__fastcall *)(void*, void*, void*);
using WalletGetService = void (__fastcall *)(void*, void*);
using WalletTaskCleanup = void (__fastcall *)(void*);

struct SharedService {
    void* object;
    void* control;
};

void ReleaseSharedService(SharedService& shared) {
    if (!shared.control) return;
    auto* control = static_cast<unsigned char*>(shared.control);
    if (InterlockedExchangeAdd(
            reinterpret_cast<volatile LONG*>(control + 8), -1) == 1) {
        auto vtable = *reinterpret_cast<void***>(shared.control);
        using Destroy = void (__fastcall *)(void*);
        reinterpret_cast<Destroy>(vtable[0])(shared.control);
        if (InterlockedExchangeAdd(
                reinterpret_cast<volatile LONG*>(control + 12), -1) == 1) {
            reinterpret_cast<Destroy>(vtable[1])(shared.control);
        }
    }
    shared.object = nullptr;
    shared.control = nullptr;
}

void ReleaseJsonValue(void* implementation) {
    if (!implementation) return;
    auto vtable = *reinterpret_cast<void***>(implementation);
    using Release = void (__fastcall *)(void*, bool);
    reinterpret_cast<Release>(vtable[0xC0 / sizeof(void*)])(
        implementation, true);
}

void* __fastcall HookedWalletLoad(
    void* self, void* taskResult, const std::string* requestUrl) {
    constexpr char walletUrl[] =
        "https://minecraft-wallet.commerce.gameservices.xboxlive.com/";
    const bool isWallet = requestUrl && *requestUrl == walletUrl;
    // AppContainer traffic can be blocked even with a loopback exemption.
    // Using the host's LAN address reaches this same in-process responder via
    // the private-network capability already present in the package.
    const std::string localUrl(
        "https://minecraft-wallet.commerce.gameservices.xboxlive.com/");

    if (isWallet && self) {
        auto state = static_cast<unsigned char*>(self);
        std::wstring diagnostic = L"diag://wallet-load-state?inProgress=" +
            std::to_wstring(state[0x20]) + L"&service=" +
            std::to_wstring(*reinterpret_cast<uintptr_t*>(state + 0x28) != 0) +
            L"&owner=" +
            std::to_wstring(*reinterpret_cast<uintptr_t*>(state + 0x38) != 0);
        std::array<std::string, 7> ignored{};
        QueryBridge(L"DIAG", diagnostic, L"", {}, ignored);
        // A failed request in this build never clears the one-shot busy byte,
        // so subsequent Store opens return before creating a network task.
        state[0x20] = 0;
        auto* serviceWrapper = *reinterpret_cast<unsigned char**>(state + 0x28);
        if (serviceWrapper) {
            auto* serviceImplementation =
                *reinterpret_cast<unsigned char**>(serviceWrapper + 0x10);
            if (serviceImplementation) {
                serviceImplementation[0xF8] = 1;
                QueryBridge(
                    L"DIAG", L"diag://commerce-http-service-ready",
                    L"", {}, ignored);
            }
        }
    }

    AcquireSRWLockExclusive(&g_walletLoadLock);
    InstallWalletLoadJump(false);
    auto original = reinterpret_cast<WalletLoad>(g_walletLoadTarget);
    void* returned = original(
        self, taskResult, isWallet ? &localUrl : requestUrl);
    InstallWalletLoadJump(true);
    ReleaseSRWLockExclusive(&g_walletLoadLock);

    if (!isWallet) return returned;

    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!executable) return returned;

    constexpr wchar_t emptyWallet[] =
        L"{\"Balance\":{\"currencies\":[{\"currencyType\":\"minecoin\",\"amount\":0}]},"
        L"\"EntitlementReceipt\":\"\"," 
        L"\"Inventory\":{\"items\":[]}}";
    std::wstring document(emptyWallet);
    void* jsonValue = nullptr;
    reinterpret_cast<JsonParse>(executable + kJsonParseRva)(
        &jsonValue, &document);
    if (!jsonValue) return returned;

    std::string context;
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://wallet-response-before-parse", L"", {}, ignored);
    reinterpret_cast<WalletParse>(executable + kWalletParseRva)(
        self, &jsonValue, &context);
    ReleaseJsonValue(jsonValue);

    QueryBridge(L"DIAG", L"diag://wallet-response-injected", L"", {}, ignored);
    return returned;
}

bool ReadLiveWallet(std::wstring& document) {
    SOCKET connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connection == INVALID_SOCKET) return false;
    DWORD timeout = 15000;
    setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(32121);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uint32_t size = 0;
    bool ok = connect(connection, reinterpret_cast<sockaddr*>(&address),
                      sizeof(address)) == 0 &&
        WriteAll(connection, "WAL1", 4) && ReadAll(connection, &size, sizeof(size)) &&
        size > 0 && size < 4 * 1024 * 1024;
    std::string bytes;
    if (ok) {
        bytes.resize(size);
        ok = ReadAll(connection, bytes.data(), size);
    }
    closesocket(connection);
    if (ok) document = Wide(bytes);
    return ok && !document.empty();
}

void RefreshStoreUiStates();

void ApplyLiveWallet(void* wallet) {
    g_storeWallet = wallet;
    RefreshStoreUiStates();
    std::wstring document;
    if (!ReadLiveWallet(document)) return;
    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    void* value = nullptr;
    reinterpret_cast<JsonParse>(executable + kJsonParseRva)(&value, &document);
    if (!value) return;
    std::string context;
    reinterpret_cast<WalletContext>(executable + kWalletContextRva)(
        static_cast<unsigned char*>(wallet) + 0x50, &context);
    reinterpret_cast<WalletParse>(executable + kWalletParseRva)(wallet, &value, &context);
    ReleaseJsonValue(value);
    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://wallet-live-refresh?balance=" +
        std::to_wstring(*reinterpret_cast<int*>(static_cast<unsigned char*>(wallet) + 0x68)), L"", {}, ignored);
}

int __fastcall HookedWalletCompletion(void* owner, void* task) {
    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!executable) return 3;

    SharedService service{};
    reinterpret_cast<WalletGetService>(executable + kWalletGetServiceRva)(
        owner, &service);
    if (!service.object) {
        reinterpret_cast<WalletTaskCleanup>(
            executable + kWalletTaskCleanupRva)(task);
        ReleaseSharedService(service);
        return 3;
    }

    std::wstring document;
    if (!ReadLiveWallet(document)) {
        reinterpret_cast<WalletTaskCleanup>(executable + kWalletTaskCleanupRva)(task);
        ReleaseSharedService(service);
        return 3;
    }
    void* jsonValue = nullptr;
    reinterpret_cast<JsonParse>(executable + kJsonParseRva)(
        &jsonValue, &document);

    int result = 3;
    if (jsonValue) {
        reinterpret_cast<WalletPreprocess>(
            executable + kWalletPreprocessRva)(service.object, &jsonValue);
        std::string context;
        reinterpret_cast<WalletContext>(executable + kWalletContextRva)(
            static_cast<unsigned char*>(service.object) + 0x50, &context);
        reinterpret_cast<WalletParse>(executable + kWalletParseRva)(
            service.object, &jsonValue, &context);
        ReleaseJsonValue(jsonValue);
        result = 1;
    }

    reinterpret_cast<WalletTaskCleanup>(
        executable + kWalletTaskCleanupRva)(task);
    ReleaseSharedService(service);

    std::array<std::string, 7> ignored{};
    QueryBridge(L"DIAG", L"diag://wallet-live-response-applied", L"", {}, ignored);
    return result;
}

void InstallWalletLoadJump(bool hook) {
    if (!g_walletLoadTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_walletLoadTarget, kWalletLoadHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kWalletLoadHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedWalletLoad);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kWalletLoadHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_walletLoadTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_walletLoadTarget, g_walletLoadOriginal.data(),
                    g_walletLoadOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_walletLoadTarget,
                          kWalletLoadHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_walletLoadTarget, kWalletLoadHookSize,
                   oldProtect, &ignored);
}

void InstallWalletCompletionJump(bool hook) {
    if (!g_walletCompletionTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_walletCompletionTarget, kWalletCompletionHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kWalletCompletionHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedWalletCompletion);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        for (size_t index = 13; index < kWalletCompletionHookSize; ++index) {
            patch[index] = 0x90;
        }
        std::memcpy(g_walletCompletionTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_walletCompletionTarget, g_walletCompletionOriginal.data(),
                    g_walletCompletionOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_walletCompletionTarget,
                          kWalletCompletionHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_walletCompletionTarget, kWalletCompletionHookSize,
                   oldProtect, &ignored);
}

int WSAAPI HookedGetAddrInfo(PCSTR node, PCSTR service, const ADDRINFOA* hints,
                             PADDRINFOA* result) {
    constexpr char walletHost[] =
        "minecraft-wallet.commerce.gameservices.xboxlive.com";
    constexpr char catalogHost[] = "mccommerceproxy.azurewebsites.net";
    if (node && (_stricmp(node, walletHost) == 0 ||
                 _stricmp(node, catalogHost) == 0)) {
        int status = g_realGetAddrInfo("127.0.0.1", service, hints, result);
        std::array<std::string, 7> ignored{};
        QueryBridge(L"DIAG", L"diag://commerce-dns-redirect", L"", {}, ignored);
        return status;
    }
    return g_realGetAddrInfo(node, service, hints, result);
}

void InstallDnsHook() {
    auto image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return;
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
        const char* library = reinterpret_cast<const char*>(image + imports->Name);
        if (_stricmp(library, "WS2_32.dll") != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + imports->OriginalFirstThunk);
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), "getaddrinfo") != 0) continue;
            DWORD oldProtect = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) return;
            g_realGetAddrInfo = reinterpret_cast<RealGetAddrInfo>(addresses->u1.Function);
            addresses->u1.Function = reinterpret_cast<uintptr_t>(&HookedGetAddrInfo);
            DWORD ignored = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(uintptr_t), oldProtect, &ignored);
            return;
        }
    }
}

int WSAAPI HookedConnect(SOCKET socket, const sockaddr* name, int nameLength);

void InstallConnectJump(bool hook) {
    if (!g_connectTarget) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_connectTarget, kHookSize,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        unsigned char patch[kHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedConnect);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41;
        patch[11] = 0xFF;
        patch[12] = 0xE3;
        std::memcpy(g_connectTarget, patch, sizeof(patch));
    } else {
        std::memcpy(g_connectTarget, g_connectOriginal.data(),
                    g_connectOriginal.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_connectTarget, kHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_connectTarget, kHookSize, oldProtect, &ignored);
}

int WSAAPI HookedConnect(SOCKET socket, const sockaddr* name, int nameLength) {
    bool redirectedRequest = false;
    sockaddr_in redirected{};
    const sockaddr* destination = name;
    int destinationLength = nameLength;
    if (name && name->sa_family == AF_INET &&
        nameLength >= static_cast<int>(sizeof(sockaddr_in))) {
        redirected = *reinterpret_cast<const sockaddr_in*>(name);
        if (ntohs(redirected.sin_port) == 443 &&
            ntohl(redirected.sin_addr.s_addr) == 0x7F000001) {
            redirected.sin_addr.s_addr = htonl(0x7F000001);
            destination = reinterpret_cast<const sockaddr*>(&redirected);
            destinationLength = sizeof(redirected);
            redirectedRequest = true;
        }
    }
    AcquireSRWLockExclusive(&g_connectLock);
    InstallConnectJump(false);
    int result = g_realConnect(socket, destination, destinationLength);
    InstallConnectJump(true);
    ReleaseSRWLockExclusive(&g_connectLock);
    if (redirectedRequest) {
        std::array<std::string, 7> ignored{};
        QueryBridge(L"DIAG", L"diag://commerce-connect-redirect", L"", {}, ignored);
    }
    return result;
}

void InstallConnectHook() {
    HMODULE winsock = GetModuleHandleW(L"WS2_32.dll");
    if (!winsock) return;
    g_connectTarget = reinterpret_cast<unsigned char*>(
        GetProcAddress(winsock, "connect"));
    if (!g_connectTarget) return;
    g_realConnect = reinterpret_cast<RealConnect>(g_connectTarget);
    std::memcpy(g_connectOriginal.data(), g_connectTarget,
                g_connectOriginal.size());
    InstallConnectJump(true);
}

HINTERNET WINAPI HookedWinHttpOpen(
    LPCWSTR userAgent, DWORD, LPCWSTR, LPCWSTR, DWORD flags) {
    return g_realWinHttpOpen(
        userAgent, WINHTTP_ACCESS_TYPE_NAMED_PROXY,
        L"127.0.0.1:32118", WINHTTP_NO_PROXY_BYPASS, flags);
}

void InstallWinHttpHook() {
    auto image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return;
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
        const char* library = reinterpret_cast<const char*>(image + imports->Name);
        if (_stricmp(library, "WINHTTP.dll") != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + imports->OriginalFirstThunk);
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                image + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(imported->Name),
                            "WinHttpOpen") != 0) continue;
            DWORD oldProtect = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(uintptr_t),
                                PAGE_READWRITE, &oldProtect)) return;
            g_realWinHttpOpen = reinterpret_cast<RealWinHttpOpen>(
                addresses->u1.Function);
            addresses->u1.Function = reinterpret_cast<uintptr_t>(
                &HookedWinHttpOpen);
            DWORD ignored = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(uintptr_t),
                           oldProtect, &ignored);
            return;
        }
    }
}

void PatchWalletUrlInMemory() {
    static volatile LONG patched = 0;
    if (InterlockedCompareExchange(&patched, 1, 0) != 0) return;
    constexpr char oldUrl[] = "https://minecraft-wallet.commerce.gameservices.xboxlive.com/";
    constexpr char newUrl[] = "http://127.0.0.1:32116/";
    auto image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    size_t imageSize = nt->OptionalHeader.SizeOfImage;
    for (size_t offset = 0; offset + sizeof(oldUrl) <= imageSize; ++offset) {
        if (std::memcmp(image + offset, oldUrl, sizeof(oldUrl) - 1) != 0) continue;
        DWORD oldProtect = 0;
        if (!VirtualProtect(image + offset, sizeof(oldUrl), PAGE_READWRITE, &oldProtect)) return;
        std::memset(image + offset, 0, sizeof(oldUrl));
        std::memcpy(image + offset, newUrl, sizeof(newUrl) - 1);
        DWORD ignored = 0;
        VirtualProtect(image + offset, sizeof(oldUrl), oldProtect, &ignored);
        return;
    }
}

DWORD WINAPI PatchInitializedWalletUrl(LPVOID) {
    constexpr char oldUrl[] =
        "https://minecraft-wallet.commerce.gameservices.xboxlive.com/";
    constexpr char newUrl[] = "http://127.0.0.1:32116/";
    auto image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return 0;
    auto value = reinterpret_cast<StringLayout*>(image + kWalletBaseUrlRva);

    // The EXE's CRT initializer constructs this global std::string after DLL
    // initialization. Wait for that exact value, then replace the initialized
    // object before Marketplace code caches it elsewhere.
    for (unsigned attempt = 0; attempt < 15000; ++attempt) {
        if (value->size == sizeof(oldUrl) - 1 &&
            value->capacity >= value->size) {
            const char* current = value->capacity < 16
                ? value->storage.small
                : value->storage.pointer;
            if (current &&
                std::memcmp(current, oldUrl, sizeof(oldUrl) - 1) == 0) {
                reinterpret_cast<std::string*>(value)->assign(newUrl);
                InterlockedExchange(&g_walletGlobalPatched, 1);
                return 1;
            }
        }
        Sleep(1);
    }
    InterlockedExchange(&g_walletGlobalPatched, -1);
    return 0;
}

DWORD ClearWindowsTitlePresence(void* self, AuthHelper original, const std::string& expectedXuid) {
    // Keep WAM's real account reference and sign-out watcher, but close only
    // the Windows-title presence created by its bootstrap token acquisition.
    struct NativeResult {
        std::wstring fields[9];
        IUnknown* tokenResult = nullptr;
        std::error_code error;
        std::string message;
        ~NativeResult() { if (tokenResult) tokenResult->Release(); }
    } value;
    static_assert(offsetof(NativeResult,error)==0x128 && sizeof(NativeResult)==0x158);
    std::wstring method=L"DELETE", path=L"/users/xuid("+Wide(expectedXuid)+L")/devices/current/titles/current";
    std::wstring url=L"https://userpresence.xboxlive.com"+path, empty;
    std::vector<unsigned char> body;
    original(self,&value,&method,&url,&empty,&body,false,false);
    DWORD status=0;
    if (!value.error && value.fields[2]==Wide(expectedXuid) && !value.fields[0].empty() && !value.fields[1].empty()) {
        auto session=WinHttpOpen(L"MinecraftAppleTVCompat/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if (session) {
            WinHttpSetTimeouts(session,5000,5000,5000,5000);
            auto connection=WinHttpConnect(session,L"userpresence.xboxlive.com",INTERNET_DEFAULT_HTTPS_PORT,0);
            auto request=connection ? WinHttpOpenRequest(connection,L"DELETE",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE) : nullptr;
            if (request) {
                std::wstring headers=L"Authorization: "+value.fields[0]+L"\r\nSignature: "+value.fields[1]+L"\r\nx-xbl-contract-version: 3\r\n";
                if (WinHttpSendRequest(request,headers.c_str(),DWORD(-1),nullptr,0,0,0) && WinHttpReceiveResponse(request,nullptr)) {
                    DWORD size=sizeof(status);
                    WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
                }
                WinHttpCloseHandle(request);
            }
            if(connection)WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
        }
    }
    std::array<std::string,7> ignored{};
    QueryBridge(L"GET",L"diag://windows-presence-close?http="+std::to_wstring(status)+L"&nativeError="+std::to_wstring(value.error.value()),L"",{},ignored);
    return status;
}

ULONGLONG g_windowsPresenceCloseDue = 0; // guarded by g_hookLock

void* __fastcall HookedAuthHelper(
    void* self, void* result, const std::wstring* method, const std::wstring* url,
    const std::wstring* headers, const std::vector<unsigned char>* body,
    bool prompt, bool forceRefresh) {
    InstallPresenceWriteFilter();
    AcquireSRWLockExclusive(&g_hookLock);
    InstallJump(false);
    auto original = reinterpret_cast<AuthHelper>(g_target);
    const bool bootstrap=method && url && *method==L"GET" && (*url==L"https://xboxlive.com" || *url==L"https://xboxlive.com/");
    const std::wstring accountOnlyUrl=L"https://profile.xboxlive.com";
    void* returned = original(self, result, method, bootstrap ? &accountOnlyUrl : url, headers, body, prompt, forceRefresh);
    if (result && !*reinterpret_cast<int32_t*>(static_cast<unsigned char*>(result)+0x128))
        ObserveNativePresenceAuthorization(*static_cast<std::wstring*>(result));
    if (method && url && *method==L"GET" && (*url==L"https://xboxlive.com" || *url==L"https://xboxlive.com/")) {
        // WAM can publish Windows presence asynchronously even when the
        // bootstrap asks for profile.xboxlive.com. Keep the native account
        // reference for sign-in, then close only its Windows-title presence.
        // Do not postpone an already pending cleanup on repeated bootstraps.
        const auto startupDue=GetTickCount64()+5000;
        if (!g_windowsPresenceCloseDue || startupDue<g_windowsPresenceCloseDue)
            g_windowsPresenceCloseDue=startupDue;
    }
    if (g_windowsPresenceCloseDue && GetTickCount64()>=g_windowsPresenceCloseDue && self &&
        *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+0x180)) {
        std::array<std::string,7> identity{};
        if (QueryBridge(L"GET",L"https://xboxlive.com",L"",{},identity)) {
            auto status=ClearWindowsTitlePresence(self,original,identity[2]);
            // Reassert after token refresh/resume as well as startup. Use the
            // live callback's self, never a retained/dangling provider pointer.
            g_windowsPresenceCloseDue=GetTickCount64()+
                ((status>=200 && status<300) ? 120000 : 60000);
        }
    }
    InstallJump(true);
    ReleaseSRWLockExclusive(&g_hookLock);

    if (!method || !url || !headers || !body) return returned;
    if (url->find(L".playfabapi.com") != std::wstring::npos) {
        std::array<std::string, 7> ignored{};
        QueryBridge(L"DIAG", L"diag://playfab-bridge-routing", L"", {}, ignored);
    }
    if (url->find(L"minecraft-wallet.commerce.gameservices.xboxlive.com") !=
            std::wstring::npos ||
        url->find(L"mccommerceproxy.azurewebsites.net") !=
            std::wstring::npos) {
        auto bytes = static_cast<unsigned char*>(result);
        // Commerce uses a separate request path from achievements.  Record the
        // native Windows identity result before replacing it so we can tell
        // whether that path is aborting on an uninitialized provider even
        // though the Apple TV bridge has valid credentials ready.
        const int32_t originalError =
            *reinterpret_cast<const int32_t*>(bytes + 0x128);
        const size_t originalMessageLength =
            *reinterpret_cast<const size_t*>(bytes + 0x148);
        const bool nativeProviderPresent =
            self && *reinterpret_cast<void**>(
                static_cast<unsigned char*>(self) + 0x180) != nullptr;
        std::array<std::string, 7> localIdentity{};
        bool exactSigned = QueryBridge(
            *method, *url, *headers, *body, localIdentity);
        if (!exactSigned) {
            QueryBridge(
                L"GET", L"https://xboxlive.com", L"", {}, localIdentity);
        }
        for (size_t index = 0; index < localIdentity.size(); ++index) {
            ReplaceExistingWstring(bytes + index * 0x20, localIdentity[index]);
        }
        *reinterpret_cast<int32_t*>(bytes + 0x128) = 0;
        std::array<std::string, 7> ignored{};
        std::wstring diagnostic = L"diag://commerce-exact-sign?nativeError=" +
            std::to_wstring(originalError) + L"&nativeMessage=" +
            std::to_wstring(originalMessageLength) + L"&nativeProvider=" +
            std::to_wstring(nativeProviderPresent) + L"&ok=" +
            std::to_wstring(exactSigned) + L"&token=" +
            std::to_wstring(localIdentity[0].size()) + L"&signature=" +
            std::to_wstring(localIdentity[1].size());
        QueryBridge(L"DIAG", diagnostic, L"", {}, ignored);
        return returned;
    }
    std::array<std::string, 7> bridge{};
    if (!QueryBridge(*method, *url, *headers, *body, bridge)) return returned;

    // XSAPI's Windows sign-in bootstrap requests a token/signature for the
    // placeholder GET https://xboxlive.com, but then reuses that signature on
    // its presence-validation POST. Proof-of-possession signatures cover the
    // exact method, URL, and body, so sign the request that XSAPI really sends.
    if (*method == L"GET" &&
        (*url == L"https://xboxlive.com" || *url == L"https://xboxlive.com/")) {
        std::wstring presenceUrl =
            L"https://userpresence.xboxlive.com/users/xuid(" + Wide(bridge[2]) +
            L")/devices/current/titles/current";
        constexpr char presenceJson[] = "{\"state\":\"active\"}";
        std::vector<unsigned char> presenceBody(
            presenceJson, presenceJson + sizeof(presenceJson) - 1);
        std::array<std::string, 7> presenceBridge{};
        if (QueryBridge(L"POST", presenceUrl, L"", presenceBody, presenceBridge)) {
            bridge[1] = presenceBridge[1];
        }
    }

    // xbox_live_result<T> begins with token_and_signature_result. In this
    // Minecraft/XSAPI build the first fields are token, signature, XUID,
    // gamertag, and user hash, each an MSVC std::wstring (32 bytes).
    auto bytes = static_cast<unsigned char*>(result);
    static volatile LONG reportedCapacities = 0;
    if (InterlockedCompareExchange(&reportedCapacities, 1, 0) == 0) {
        std::wstring diagnostic = L"diag://wstring-capacities?token=" +
            std::to_wstring(WstringCapacity(bytes + 0x00)) + L"&signature=" +
            std::to_wstring(WstringCapacity(bytes + 0x20)) + L"&xuid=" +
            std::to_wstring(WstringCapacity(bytes + 0x40)) + L"&gamertag=" +
            std::to_wstring(WstringCapacity(bytes + 0x60)) + L"&tokenNeeded=" +
            std::to_wstring(bridge[0].size()) + L"&signatureNeeded=" +
            std::to_wstring(bridge[1].size()) + L"&walletGlobal=" +
            std::to_wstring(g_walletGlobalPatched) + L"&walletServer=" +
            std::to_wstring(g_walletServerStatus);
        std::array<std::string, 7> ignored{};
        QueryBridge(L"DIAG", diagnostic, L"", {}, ignored);
    }
    ReplaceExistingWstring(bytes + 0x00, bridge[0]);
    ReplaceExistingWstring(bytes + 0x20, bridge[1]);
    ReplaceExistingWstring(bytes + 0x40, bridge[2]);
    ReplaceExistingWstring(bytes + 0x60, bridge[3]);
    ReplaceExistingWstring(bytes + 0x80, bridge[4]);
    ReplaceExistingWstring(bytes + 0xA0, bridge[5]);
    ReplaceExistingWstring(bytes + 0xC0, bridge[6]);

    // In this XSAPI build the payload occupies 0x128 bytes. The following
    // std::error_code begins with its integer value. The Windows identity
    // provider can fail even though the Apple TV bridge supplied a complete,
    // valid result, so promote that bridged result to success.
    *reinterpret_cast<int32_t*>(bytes + 0x128) = 0;
    if (InterlockedCompareExchange(&g_catalogRetryScheduled, 1, 0) == 0) {
        HANDLE retryThread = CreateThread(
            nullptr, 0, &RetryCatalogAfterSignIn, nullptr, 0, nullptr);
        if (retryThread) CloseHandle(retryThread);
    }
    if (InterlockedCompareExchange(&g_walletResolverArmed, 1, 0) == 0) {
        HANDLE thread = CreateThread(
            nullptr, 0, &ArmWalletResolverAfterSignIn, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    if (InterlockedCompareExchange(&g_thumbnailServerArmed, 1, 0) == 0) {
        HANDLE thread = CreateThread(
            nullptr, 0, &WalletServerThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return returned;
}

void InstallJump(bool hook) {
    if (!g_target) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_target, kHookSize, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    if (hook) {
        // mov r11, imm64; jmp r11 -- exactly 13 bytes, matching the safe prologue.
        unsigned char patch[kHookSize]{0x49, 0xBB};
        void* destination = reinterpret_cast<void*>(&HookedAuthHelper);
        std::memcpy(patch + 2, &destination, sizeof(destination));
        patch[10] = 0x41; patch[11] = 0xFF; patch[12] = 0xE3;
        std::memcpy(g_target, patch, sizeof(patch));
    } else {
        std::memcpy(g_target, g_original.data(), g_original.size());
    }
    FlushInstructionCache(GetCurrentProcess(), g_target, kHookSize);
    DWORD ignored = 0;
    VirtualProtect(g_target, kHookSize, oldProtect, &ignored);
}

#include "store_ui.h"

void __fastcall OpenInstalledSkinStore(void* model, void* skinKey) {
    auto key=Utf8(CatalogTaskString(skinKey,0));
    if(key.empty() || key.size()>128)return;
    auto inspectBase=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto inspectServices=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(model)+0x18);
    auto info=reinterpret_cast<unsigned char*(__fastcall*)(void*,void*)>(inspectBase+0x4F7F00)(*reinterpret_cast<void**>(inspectServices+0x1B8),skinKey);
    if(info) {
        const char* hex="0123456789abcdef";std::string uuid;
        for(int i=0;i<16;++i){if(i==4 || i==6 || i==8 || i==10)uuid+='-';auto c=info[0x70+(i<8?7-i:23-i)];uuid+=hex[c>>4];uuid+=hex[c&15];}
        key+='|';key+=uuid;
    }
    uint32_t size=static_cast<uint32_t>(key.size());
    std::string request(reinterpret_cast<const char*>(&size),4);request+=key;
    std::string response;
    if(!QueryStoreUi("SKN1",request,response))return;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    // Retain one parsed row per pack/offer identity; repeated visits reuse it.
    // A temporary unavailable placeholder must not mask a later live listing.
    static std::unordered_map<std::string,unsigned char*> parsedRows;
    unsigned char* row=nullptr;
    auto tagOffset=response.find("\"tags\": [\"");
    if(tagOffset==std::string::npos)return;
    auto cacheKey=key+response.substr(tagOffset+10,36);
    auto cached=parsedRows.find(cacheKey);
    if(cached!=parsedRows.end())row=cached->second;
    else {
    auto document=Wide(response);void* json=nullptr;
    reinterpret_cast<JsonParse>(base+kJsonParseRva)(&json,&document);
    if(!json)return;
    // The catalog parser only accesses the result vector at300..320. Keep its
    // owner alive alongside JSON, just as the normal legacy catalog adapter.
    auto task=new unsigned char[0x400]{};
    reinterpret_cast<CatalogParse>(base+kCatalogParseRva)(task,&json);
    auto begin=*reinterpret_cast<uintptr_t*>(task+0x308);
    auto end=*reinterpret_cast<uintptr_t*>(task+0x310);
    if(!begin || end-begin!=0x1C0)return;
    row=reinterpret_cast<unsigned char*>(begin);
    auto tags=*reinterpret_cast<uintptr_t*>(row+0xC8);
    if(!tags)return;
    using Assign=void(__fastcall*)(void*,const void*,size_t,size_t);
    auto assign=reinterpret_cast<Assign>(base+kStringAssignRva);
    assign(row+8,reinterpret_cast<void*>(tags),0,size_t(-1));
    assign(row+0x28,reinterpret_cast<void*>(tags),0,size_t(-1));
    parsedRows.emplace(cacheKey,row);
    }
    auto services=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(model)+0x18);
    auto manager=*reinterpret_cast<void**>(services+0x1D0);
    auto offer=reinterpret_cast<void*(__fastcall*)(void*,void*)>(base+0x6C86F0)(manager,row);
    if(!offer)return;
    // Use the guarded catalog detail screen for every installed skin type.
    // Do not call the old upsell screen or a purchase handler.
    void* shared[2]{};
    reinterpret_cast<void(__fastcall*)(void*,void*)>(base+0x3B2AA0)(shared,offer);
    RefreshStoreUiStates();
    reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(base+0x6C4560)(offer,*reinterpret_cast<void**>(services+0x1C8),static_cast<unsigned char*>(offer)+0xA0);
    auto navigation=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(model)+0x20);
    navigation=*reinterpret_cast<unsigned char**>(navigation+0x30);
    reinterpret_cast<void(__fastcall*)(void*,void*,int)>(base+0x3BF590)(*reinterpret_cast<void**>(navigation+0x278),shared,0);
    // Release this temporary shared_ptr; the manager and screen retain theirs.
    auto control=static_cast<unsigned char*>(shared[1]);
    if(control && InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+8))==0) {
        auto table=*reinterpret_cast<void***>(control);
        reinterpret_cast<void(__fastcall*)(void*)>(table[0])(control);
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+12))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(table[1])(control);
    }
}

void __fastcall LocalDetailExecute(void* self) {
    const auto id = CatalogTaskString(self, 0xC8);
    std::array<std::string, 7> ignored{};
    std::wstring document;
    if (id.size() != 36 || id.find_first_not_of(L"0123456789abcdef-") != std::wstring::npos) return;
    // Account state is refreshed by the wallet path, not by every preview.
    std::string liveDocument;
    if (QueryStoreUi("DET1", Utf8(id), liveDocument)) { document = Wide(liveDocument); TrackStorePreview(Utf8(id), liveDocument); }
    else if (!ReadPackagedUtf8((L"LegacyDetail-" + id + L".json").c_str(), document)) return;
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    void* json = nullptr;
    reinterpret_cast<JsonParse>(base + kJsonParseRva)(&json, &document);
    if (!json) return;
    using Parse = void (__fastcall *)(void*, void*);
    reinterpret_cast<Parse>(base + 0x60A290)(self, &json);
    QueryBridge(L"DIAG", L"diag://detail-local-parsed?id=" + CatalogTaskString(self, 0x170) +
        L"&titleLanguages=" + std::to_wstring(*reinterpret_cast<size_t*>(static_cast<unsigned char*>(self) + 0x1A0)), L"", {}, ignored);
    // Keep the JSON owner alive, matching the catalog workaround, until native ownership is mapped.
}

int* __fastcall AvailableScreenshotGrid(void* callback, int* dimensions) {
    auto* controller = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(callback) + 8);
    auto* offer = *reinterpret_cast<unsigned char**>(controller + 0x480);
    size_t screenshots = 0;
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (offer) {
        void* iterator = nullptr;
        using Find = void* (__fastcall *)(void*, void*, void*);
        reinterpret_cast<Find>(base + kStringMapFindRva)(offer + 0x170, &iterator, base + 0x19286B0);
        if (iterator && iterator != *reinterpret_cast<void**>(offer + 0x178)) {
            auto entry = static_cast<unsigned char*>(iterator);
            auto begin = *reinterpret_cast<uintptr_t*>(entry + 0x30);
            auto end = *reinterpret_cast<uintptr_t*>(entry + 0x38);
            if (end >= begin) screenshots = (end - begin) / 0x48;
        }
    }
    const int available = static_cast<int>(screenshots) + 1; // Cover artwork is the first slot.
    auto& first = *reinterpret_cast<int*>(controller + 0x494);
    if (first < 0 || first >= available) first = 0;
    const int slots = *reinterpret_cast<int*>(controller + 0x49C);
    dimensions[0] = slots < available - first ? slots : available - first;
    dimensions[1] = 1;
    return dimensions;
}

void InitializeHook() {
    auto executable = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!executable) return;
    // The legacy final callback normally applies decoded thumbnail pixels
    // before registering the offer.  Local translated offers deliberately use
    // a placeholder and bypass the retired decoder, so that call has no image
    // result and would dereference an empty string.  The remainder of the
    // callback is independent and performs the native offer registration.
    {
        unsigned char* applyCall = executable + kThumbnailApplyCallRva;
        DWORD oldProtect = 0;
        if (!kUseNativeThumbnails && VirtualProtect(applyCall, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memset(applyCall, 0x90, 5);
            FlushInstructionCache(GetCurrentProcess(), applyCall, 5);
            DWORD ignored = 0;
            VirtualProtect(applyCall, 5, oldProtect, &ignored);
        }
    }
    // The following loop indexes thumbnail-derived identity records.  Those
    // records are absent when the retired decoder is bypassed; jump directly
    // to the native offer-map insertion that follows the loop.
    {
        unsigned char* identityLoop = executable + kThumbnailIdentityLoopRva;
        const unsigned char patch[5]{0xE9, 0x9B, 0x00, 0x00, 0x00};
        DWORD oldProtect = 0;
        if (!kUseNativeThumbnails && VirtualProtect(identityLoop, sizeof(patch), PAGE_EXECUTE_READWRITE,
                           &oldProtect)) {
            std::memcpy(identityLoop, patch, sizeof(patch));
            FlushInstructionCache(GetCurrentProcess(), identityLoop,
                                  sizeof(patch));
            DWORD ignored = 0;
            VirtualProtect(identityLoop, sizeof(patch), oldProtect, &ignored);
        }
    }
    g_walletLoadTarget = executable + kWalletLoadRva;
    std::memcpy(g_walletLoadOriginal.data(), g_walletLoadTarget,
                g_walletLoadOriginal.size());
    // Wallet loading also consults the legacy Windows identity provider's
    // ready flag. The bridged Apple TV identity is valid but invisible there;
    // skip only that six-byte conditional branch while retaining the null,
    // owner, and busy-state checks around it.
    {
        unsigned char* readyJump = executable + kWalletReadyJumpRva;
        DWORD oldProtect = 0;
        if (VirtualProtect(readyJump, 6, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memset(readyJump, 0x90, 6);
            FlushInstructionCache(GetCurrentProcess(), readyJump, 6);
            DWORD ignored = 0;
            VirtualProtect(readyJump, 6, oldProtect, &ignored);
        }
    }
    g_walletCompletionTarget = executable + kWalletCompletionRva;
    std::memcpy(g_walletCompletionOriginal.data(), g_walletCompletionTarget,
                g_walletCompletionOriginal.size());
    g_walletResolverTarget = executable + kWalletUrlResolverRva;
    std::memcpy(g_walletResolverOriginal.data(), g_walletResolverTarget,
                g_walletResolverOriginal.size());
    InstallWalletResolverJump(true);
    g_catalogInitTarget = executable + kCatalogInitRva;
    std::memcpy(g_catalogInitOriginal.data(), g_catalogInitTarget,
                g_catalogInitOriginal.size());
    InstallCatalogInitJump(true);
    g_catalogExecuteTarget = executable + kCatalogExecuteRva;
    std::memcpy(g_catalogExecuteOriginal.data(), g_catalogExecuteTarget,
                g_catalogExecuteOriginal.size());
    InstallCatalogExecuteJump(true);
    g_offerPurchasableTarget = executable + kOfferPurchasableRva;
    std::memcpy(g_offerPurchasableOriginal.data(), g_offerPurchasableTarget,
                g_offerPurchasableOriginal.size());
    InstallOfferPurchasableJump(true);
    // Preserve native offer reuse now that parsed rows carry unique product IDs.
    // Synthetic thumbnail completion has no texture object. Preserve the query
    // callback's native bookkeeping but omit only its texture-application call.
    unsigned char* queryTextureApply = executable + 0x6CCFE6;
    DWORD queryProtection = 0;
    if (!kUseNativeThumbnails && VirtualProtect(queryTextureApply, 5, PAGE_EXECUTE_READWRITE, &queryProtection)) {
        std::memset(queryTextureApply, 0x90, 5);
        FlushInstructionCache(GetCurrentProcess(), queryTextureApply, 5);
        DWORD ignoredProtection = 0;
        VirtualProtect(queryTextureApply, 5, queryProtection, &ignoredProtection);
    }
    g_thumbnailCallbackTarget = executable + kThumbnailCallbackRva;
    std::memcpy(g_thumbnailCallbackOriginal.data(), g_thumbnailCallbackTarget,
                g_thumbnailCallbackOriginal.size());
    InstallThumbnailCallbackJump(true);
    g_thumbnailDecodeCallbackTarget =
        executable + kThumbnailDecodeCallbackRva;
    std::memcpy(g_thumbnailDecodeCallbackOriginal.data(),
                g_thumbnailDecodeCallbackTarget,
                g_thumbnailDecodeCallbackOriginal.size());
    InstallThumbnailDecodeCallbackJump(true);
    g_thumbnailTextureCallbackTarget =
        executable + kThumbnailTextureCallbackRva;
    std::memcpy(g_thumbnailTextureCallbackOriginal.data(),
                g_thumbnailTextureCallbackTarget,
                g_thumbnailTextureCallbackOriginal.size());
    InstallThumbnailTextureCallbackJump(true);
    g_thumbnailLoadTarget = executable + kThumbnailLoadRva;
    std::memcpy(g_thumbnailLoadOriginal.data(), g_thumbnailLoadTarget,
                g_thumbnailLoadOriginal.size());
    InstallThumbnailLoadJump(true);
    g_target = executable + kAuthHelperRva;
    std::memcpy(g_original.data(), g_target, g_original.size());
    InstallJump(true);
    {
        auto target = executable + 0x609A20;
        DWORD old = 0;
        if (VirtualProtect(target, 19, PAGE_EXECUTE_READWRITE, &old)) {
            unsigned char patch[19]{0x49, 0xBB};
            auto fn = &LocalDetailExecute;
            std::memcpy(patch + 2, &fn, sizeof(fn));
            patch[10] = 0x41; patch[11] = 0xFF; patch[12] = 0xE3;
            std::memset(patch + 13, 0x90, 6);
            std::memcpy(target, patch, 19);
            FlushInstructionCache(GetCurrentProcess(), target, 19);
            DWORD ignored = 0;
            VirtualProtect(target, 19, old, &ignored);
        }
    }
    // A modern skin pack has no legacy installed preview object. When that
    // lookup fails, open the normal artwork/description dialog instead of
    // silently returning. Existing native skin previews keep their own path.
    {
        auto branch = executable + 0x38C34A;
        DWORD old = 0;
        if (VirtualProtect(branch, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *reinterpret_cast<int32_t*>(branch) = 0x38C38F - 0x38C34E;
            FlushInstructionCache(GetCurrentProcess(), branch, 4);
            DWORD ignored = 0;
            VirtualProtect(branch, 4, old, &ignored);
        }
    }
    // Modern worlds and mash-ups lack legacy panorama/installed-preview data.
    // Open the shared guarded catalog detail screen instead of legacy upsells.
    for (auto rva : {0x38C3B7, 0x38C447}) {
        auto target = executable + rva;
        DWORD old = 0;
        if (VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &old)) {
            target[0] = 0xE9;
            *reinterpret_cast<int32_t*>(target + 1) = 0x38C38F - (rva + 5);
            target[5] = target[6] = 0x90;
            FlushInstructionCache(GetCurrentProcess(), target, 7);
            DWORD ignored; VirtualProtect(target, 7, old, &ignored);
        }
    }
    // Do not create loading placeholders for screenshot slots with no image.
    {
        auto callback = executable + 0x170D558;
        DWORD old = 0;
        if (VirtualProtect(callback, sizeof(void*), PAGE_READWRITE, &old)) {
            *reinterpret_cast<void**>(callback) = reinterpret_cast<void*>(&AvailableScreenshotGrid);
            DWORD ignored = 0;
            VirtualProtect(callback, sizeof(void*), old, &ignored);
        }
    }
    InstallStoreUiBindings(executable);
    {
        auto target=executable+0x38D430;DWORD old;
        if(VirtualProtect(target,20,PAGE_EXECUTE_READWRITE,&old)) {
            unsigned char patch[20]{0x49,0xBB};void* function=reinterpret_cast<void*>(&OpenInstalledSkinStore);
            std::memcpy(patch+2,&function,8);patch[10]=0x41;patch[11]=0xFF;patch[12]=0xE3;
            std::memset(patch+13,0x90,7);std::memcpy(target,patch,20);
            FlushInstructionCache(GetCurrentProcess(),target,20);DWORD ignored;VirtualProtect(target,20,old,&ignored);
        }
    }
    InstallStoreOwnershipPresentation(executable);
    InstallDnsHook();
    InstallConnectHook();
    // Use normal WinHTTP routing; the experimental LAN proxy is no longer active.
}

void LoadRealXInput() {
    if (g_realXInputGetState) return;
    wchar_t path[MAX_PATH]{};
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length + 15 >= MAX_PATH) return;
    std::memcpy(path + length, L"\\XInputUap.dll", 15 * sizeof(wchar_t));
    HMODULE module = LoadLibraryW(path);
    if (module) {
        g_realXInputGetState = reinterpret_cast<RealXInputGetState>(
            GetProcAddress(module, "XInputGetState"));
    }
}

} // namespace

void ReportWindowsPresenceSuppressed() {
    std::array<std::string,7> ignored{};
    QueryBridge(L"GET",L"diag://windows-presence-write-suppressed",L"",{},ignored);
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(
    DWORD userIndex, XINPUT_STATE* state) noexcept {
    LoadRealXInput();
    return g_realXInputGetState
        ? g_realXInputGetState(userIndex, state)
        : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) InitializeHook();
    return TRUE;
}




