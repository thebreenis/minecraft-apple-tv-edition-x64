#include "presence_filter.cpp"
#include <cassert>
void ReportWindowsPresenceSuppressed(){}
int main(){
 assert(PresenceUrl(L"https://userpresence.xboxlive.com/users/xuid(123)/devices/current/titles/current"));
 assert(!PresenceUrl(L"https://userpresence.xboxlive.com/users/batch"));
 assert(!PresenceUrl(L"https://userpresence.xboxlive.com/users/xuid(123)?level=all"));
 assert(!PresenceUrl(L"https://userpresence.xboxlive.com.evil/users/xuid(123)/devices/current/titles/current"));
 assert(!PresenceUrl(L"https://userpresence.xboxlive.com/users/xuid()/devices/current/titles/current"));
 assert(!PresenceUrl(L"https://userpresence.xboxlive.com/users/xuid(123)/devices/current/titles/current?other=1"));
 assert(HeaderAuthorization(L"X-Test: 1\r\nAuthorization: native-test \r\n",DWORD(-1))==L"native-test");
 assert(HeaderAuthorization(L"X-Authorization: wrong\r\n",DWORD(-1)).empty());
 assert(HeaderAuthorization(L"authorization: apple-test\r\n",DWORD(-1))==L"apple-test");
 ObserveNativePresenceAuthorization(L"XBL3.0 native-test");
 assert(nativeAuthorizations.size()==1);
 ObserveNativePresenceAuthorization(L"XBL3.0 native-test");assert(nativeAuthorizations.size()==1);
 assert(std::find(nativeAuthorizations.begin(),nativeAuthorizations.end(),L"XBL3.0 apple-test")==nativeAuthorizations.end());
 return 0;
}
