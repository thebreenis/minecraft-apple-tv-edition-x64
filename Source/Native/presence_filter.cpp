#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <delayimp.h>
#include <string>
#include <vector>
#include <algorithm>

// Only URLMon's in-process presence writes are filtered. No credentials are
// changed, generated, persisted or logged; unknown authorization passes through.
namespace {
SRWLOCK tokenLock=SRWLOCK_INIT;
std::vector<std::wstring> nativeAuthorizations;
INIT_ONCE installOnce=INIT_ONCE_STATIC_INIT;

bool PresenceUrl(const std::wstring& url) {
    const std::wstring prefix=L"https://userpresence.xboxlive.com/users/xuid(";
    const std::wstring suffix=L")/devices/current/titles/current";
    if(url.size()<=prefix.size()+suffix.size() || url.compare(0,prefix.size(),prefix)!=0 ||
       url.compare(url.size()-suffix.size(),suffix.size(),suffix)!=0) return false;
    for(size_t i=prefix.size();i<url.size()-suffix.size();++i)
        if(url[i]<L'0' || url[i]>L'9') return false;
    return true;
}
std::wstring HeaderAuthorization(const wchar_t* text,DWORD length) {
    if(!text)return {};
    if(length==DWORD(-1))length=DWORD(wcslen(text));
    if(length>65536)return {};
    std::wstring all(text,length);
    for(size_t start=0;start<all.size();) {
        auto end=all.find(L'\n',start);if(end==std::wstring::npos)end=all.size();
        auto colon=all.find(L':',start);
        if(colon<end && colon-start==13 && _wcsnicmp(all.data()+start,L"Authorization",13)==0) {
            auto first=all.find_first_not_of(L" \t",colon+1);
            if(first==std::wstring::npos || first>=end)return {};
            while(end>first && (all[end-1]==L'\r'||all[end-1]==L' '||all[end-1]==L'\t'))--end;
            return all.substr(first,end-first);
        }
        start=end+1;
    }
    return {};
}
std::wstring QueryHeader(HINTERNET request,DWORD flag) {
    wchar_t buffer[16384]{};DWORD bytes=sizeof(buffer);
    if(!HttpQueryInfoW(request,flag|HTTP_QUERY_FLAG_REQUEST_HEADERS,buffer,&bytes,nullptr))return {};
    return std::wstring(buffer,bytes/sizeof(wchar_t));
}
bool ShouldBlock(HINTERNET request,LPCWSTR headers,DWORD length) {
    wchar_t url[2048]{};DWORD bytes=sizeof(url);
    if(!InternetQueryOptionW(request,INTERNET_OPTION_URL,url,&bytes) || !PresenceUrl(url))return false;
    if(QueryHeader(request,HTTP_QUERY_REQUEST_METHOD)!=L"POST")return false;
    auto auth=HeaderAuthorization(headers,length);
    if(auth.empty())auth=QueryHeader(request,HTTP_QUERY_AUTHORIZATION);
    if(auth.empty())return false;
    AcquireSRWLockShared(&tokenLock);
    bool matched=std::find(nativeAuthorizations.begin(),nativeAuthorizations.end(),auth)!=nativeAuthorizations.end();
    ReleaseSRWLockShared(&tokenLock);
    return matched;
}
}
void ReportWindowsPresenceSuppressed();
namespace {
BOOL WINAPI FilterSendEx(HINTERNET h,LPINTERNET_BUFFERSW in,LPINTERNET_BUFFERSW out,DWORD flags,DWORD_PTR context) {
    DWORD saved=GetLastError();bool blocked=false;
    try { blocked=ShouldBlock(h,in?in->lpcszHeader:nullptr,in?in->dwHeadersLength:0); } catch(...) {}
    if(blocked){ReportWindowsPresenceSuppressed();SetLastError(ERROR_INTERNET_OPERATION_CANCELLED);return FALSE;}
    SetLastError(saved);return HttpSendRequestExW(h,in,out,flags,context);
}
BOOL WINAPI FilterSend(HINTERNET h,LPCWSTR headers,DWORD length,LPVOID body,DWORD size) {
    DWORD saved=GetLastError();bool blocked=false;
    try { blocked=ShouldBlock(h,headers,length); } catch(...) {}
    if(blocked){ReportWindowsPresenceSuppressed();SetLastError(ERROR_INTERNET_OPERATION_CANCELLED);return FALSE;}
    SetLastError(saved);return HttpSendRequestW(h,headers,length,body,size);
}
BOOL CALLBACK InstallOnce(PINIT_ONCE,void*,void**) {
    // Runs at the first auth callback, outside DllMain/loader lock, before its
    // native helper can enqueue startup HTTP. Pin URLMon for the process life.
    auto module=LoadLibraryW(L"urlmon.dll");if(!module)return FALSE;
    auto base=reinterpret_cast<BYTE*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if(!dir.VirtualAddress)return FALSE;
    auto descriptor=reinterpret_cast<ImgDelayDescr*>(base+dir.VirtualAddress);
    unsigned installed=0;
    for(;descriptor->rvaDLLName;++descriptor) {
        if(!(descriptor->grAttrs & dlattrRva) || _stricmp(reinterpret_cast<char*>(base+descriptor->rvaDLLName),"wininet.dll"))continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->rvaINT);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+descriptor->rvaIAT);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData)->Name;
            void* replacement=nullptr;
            if(!strcmp(reinterpret_cast<char*>(name),"HttpSendRequestExW"))replacement=reinterpret_cast<void*>(&FilterSendEx);
            if(!strcmp(reinterpret_cast<char*>(name),"HttpSendRequestW"))replacement=reinterpret_cast<void*>(&FilterSend);
            if(!replacement)continue;
            DWORD old=0;
            if(!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))continue;
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function),replacement);
            DWORD ignored;VirtualProtect(&slots->u1.Function,sizeof(void*),old,&ignored);++installed;
        }
    }
    return installed==2;
}
}
void InstallPresenceWriteFilter(){InitOnceExecuteOnce(&installOnce,InstallOnce,nullptr,nullptr);}
void ObserveNativePresenceAuthorization(const std::wstring& value) {
    if(value.compare(0,6,L"XBL3.0")!=0)return;
    AcquireSRWLockExclusive(&tokenLock);
    if(std::find(nativeAuthorizations.begin(),nativeAuthorizations.end(),value)==nativeAuthorizations.end()) {
        if(nativeAuthorizations.size()>=8)nativeAuthorizations.erase(nativeAuthorizations.begin());
        nativeAuthorizations.push_back(value);
    }
    ReleaseSRWLockExclusive(&tokenLock);
}
