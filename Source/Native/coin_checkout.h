// Each card is bound to an allowlisted Microsoft Store consumable.
bool QueryStoreUi(const char*,const std::string&,std::string&);
void RefreshStoreUiStates();
volatile LONG g_coinProbeStarted = 0;
volatile LONG g_coinCheckoutBusy = 0;
volatile LONG g_coinPlatformReady = 0;
void StartCoinPlatformProbe();
std::string g_coinCheckoutStatus;
SRWLOCK g_coinPlatformLock = SRWLOCK_INIT;
const wchar_t* g_coinProductIds[5]{L"9NHGX9G9JF1X",L"9PD3R529P5XZ",L"9P063P6X637H",L"9P2XG2G6PPRC",L"9NJSBHH6FVRF"};
const unsigned g_coinAmounts[5]{320,1020,1720,3500,8800};
volatile LONG g_coinAvailableMask=0;
std::string g_coinPrices[5]{"Loading...","Loading...","Loading...","Loading...","Loading..."};
std::string CoinPlatformPrice(size_t index) {
    AcquireSRWLockShared(&g_coinPlatformLock);
    auto price=index<5 ? g_coinPrices[index] : "Unavailable";
    ReleaseSRWLockShared(&g_coinPlatformLock);
    return price;
}
std::string CoinCheckoutStatus() {
    AcquireSRWLockShared(&g_coinPlatformLock);auto text=g_coinCheckoutStatus;ReleaseSRWLockShared(&g_coinPlatformLock);return text;
}
void SetCoinCheckoutStatus(const char* text) {
    AcquireSRWLockExclusive(&g_coinPlatformLock);g_coinCheckoutStatus=text;ReleaseSRWLockExclusive(&g_coinPlatformLock);
}
void CoinProbeReport(const std::wstring& text) {
    std::array<std::string,7> ignored{};
    QueryBridge(L"GET",L"diag://coin-platform/"+text,L"",{},ignored);
}
bool g_coinRecoveryRetryAllowed=false; // Recovery runs on one background worker.
std::string g_coinAttempt; // Owned by the single checkout worker.
bool ReconcileWindowsCoins(const char* operation) {
    g_coinRecoveryRetryAllowed=false;
    using namespace winrt::Windows::ApplicationModel::Store;
    auto pending=CurrentApp::GetUnfulfilledConsumablesAsync().get();
    CoinProbeReport(L"pending?count="+std::to_wstring(pending.Size()));
    if(pending.Size()>16){SetCoinCheckoutStatus("Windows purchases need review.");return false;}
    std::string data=std::to_string(pending.Size())+"\n";
    for(auto item:pending) {
        data+=winrt::to_string(item.ProductId())+"|"+winrt::to_string(winrt::to_hstring(item.TransactionId()))+"\n";
    }
    if(std::strcmp(operation,"TOP2")==0) data=g_coinAttempt+"\n"+data;
    uint32_t size=static_cast<uint32_t>(data.size());
    std::string packet(reinterpret_cast<const char*>(&size),sizeof(size));packet+=data;
    std::string response;
    if(!QueryStoreUi(operation,packet,response)) {
        g_coinRecoveryRetryAllowed=true;
        SetCoinCheckoutStatus("Delivery check unavailable. Please do not buy again.");return false;
    }
    if(std::strcmp(operation,"TOP2")==0) data.erase(0,g_coinAttempt.size()+1);
    if(response=="keys-required") {
        SetCoinCheckoutStatus("Please wait... Checking Windows purchase delivery.");
        std::string tokens;
        if(!QueryStoreUi("TOK1","",tokens))throw std::runtime_error("Store token context unavailable");
        std::string values[3];size_t start=0;
        for(auto& value:values) {
            auto end=tokens.find('\n',start);
            if(end==std::string::npos || end==start)throw std::runtime_error("Invalid Store token context");
            value=tokens.substr(start,end-start);start=end+1;
        }
        auto context=winrt::Windows::Services::Store::StoreContext::GetDefault();
        auto collections=context.GetCustomerCollectionsIdAsync(winrt::to_hstring(values[0]),winrt::to_hstring(values[2])).get();
        auto purchase=context.GetCustomerPurchaseIdAsync(winrt::to_hstring(values[1]),winrt::to_hstring(values[2])).get();
        if(collections.empty() || purchase.empty())throw std::runtime_error("Windows Store keys unavailable");
        CoinProbeReport(L"keys?collections=1&purchase=1");
        data+=winrt::to_string(collections)+"\n"+winrt::to_string(purchase)+"\n";
        size=static_cast<uint32_t>(data.size());
        packet.assign(reinterpret_cast<const char*>(&size),sizeof(size));packet+=data;
        if(!QueryStoreUi("RDM1",packet,response)) {
            g_coinRecoveryRetryAllowed=true;
            SetCoinCheckoutStatus("Delivery check unavailable. Please do not buy again.");return false;
        }
    }
    if(response=="confirmed" || response=="acknowledge") {
        // The bridge has verified wallet credit. Acknowledge ONLY the exact
        // original transaction captured above, never an unrelated consumable.
        for(auto item:pending) {
            auto remaining=CurrentApp::GetUnfulfilledConsumablesAsync().get();
            for(auto current:remaining) {
                if(current.ProductId()==item.ProductId() && current.TransactionId()==item.TransactionId()) {
                    auto result=CurrentApp::ReportConsumableFulfillmentAsync(item.ProductId(),item.TransactionId()).get();
                    CoinProbeReport(L"fulfillment?result="+std::to_wstring(static_cast<int>(result)));
                }
            }
        }
        RefreshStoreUiStates();
        auto remaining=CurrentApp::GetUnfulfilledConsumablesAsync().get();
        g_coinRecoveryRetryAllowed=remaining.Size()!=0;
        SetCoinCheckoutStatus(remaining.Size()==0 ? "Minecoins added to your wallet." : "Coins credited. Windows receipt cleanup is pending.");
        return remaining.Size()==0;
    }
    if(response=="settled") {
        RefreshStoreUiStates();SetCoinCheckoutStatus("Your previous Minecoin purchase was delivered.");return pending.Size()==0;
    }
    if(response=="idle")return pending.Size()==0;
    if(response=="checkout-open") { SetCoinCheckoutStatus("Checkout was not completed. No payment has been confirmed."); return false; }
    g_coinRecoveryRetryAllowed=response=="pending" || response=="keys-required";
    SetCoinCheckoutStatus(response=="review" ? "Purchase delivery needs review. Please do not buy again." : "Purchase verification is pending. No payment has been confirmed. Please do not buy again.");
    return false;
}
bool ReconcileWindowsCoinsWithRetry(const char* operation) {
    for(unsigned attempt=0;attempt<3;++attempt) {
        if(ReconcileWindowsCoins(attempt==0 ? operation : "REC1"))return true;
        if(!g_coinRecoveryRetryAllowed || attempt==2)return false;
        Sleep(60000); // Background worker only; preserve the existing auth session.
    }
    return false;
}
DWORD WINAPI ProbeCoinPlatform(LPVOID) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        using namespace winrt::Windows::Services::Store;
        auto context=StoreContext::GetDefault();
        auto app=context.GetStoreProductForCurrentAppAsync().get();
        CoinProbeReport(L"app?error="+std::to_wstring(app.ExtendedError().value)+L"&id="+(app.Product() ? std::wstring(app.Product().StoreId()) : L"none"));
        auto kinds=winrt::single_threaded_vector<winrt::hstring>({L"Consumable",L"UnmanagedConsumable",L"Durable"});
        auto ids=winrt::single_threaded_vector<winrt::hstring>({L"9NHGX9G9JF1X",L"9PD3R529P5XZ",L"9P063P6X637H",L"9P2XG2G6PPRC",L"9NJSBHH6FVRF"});
        auto result=context.GetStoreProductsAsync(kinds,ids).get();
        CoinProbeReport(L"products?error="+std::to_wstring(result.ExtendedError().value)+L"&count="+std::to_wstring(result.Products().Size()));
        for (auto item:result.Products()) {
            auto p=item.Value();
            AcquireSRWLockExclusive(&g_coinPlatformLock);
            for (size_t i=0;i<5;++i) if(p.StoreId()==g_coinProductIds[i]) {
                g_coinPrices[i]=winrt::to_string(p.Price().FormattedPrice());
                InterlockedOr(&g_coinAvailableMask,1L<<i);
            }
            ReleaseSRWLockExclusive(&g_coinPlatformLock);
            CoinProbeReport(L"product?id="+std::wstring(p.StoreId())+L"&offer="+std::wstring(p.InAppOfferToken())+L"&kind="+std::wstring(p.ProductKind())+L"&price="+std::wstring(p.Price().FormattedPrice())+L"&currency="+std::wstring(p.Price().CurrencyCode()));
        }
        bool settled=ReconcileWindowsCoinsWithRetry("REC1");
        // Use the product identities already matched above, not collection keys
        // (which can include SKU suffixes). Receipt reconciliation still gates checkout.
        bool ready=result.ExtendedError().value==0 && g_coinAvailableMask!=0 && settled;
        InterlockedExchange(&g_coinPlatformReady,ready ? 1 : 0);
        CoinProbeReport(L"ready?mask="+std::to_wstring(g_coinAvailableMask)+L"&settled="+std::to_wstring(settled)+L"&ready="+std::to_wstring(ready));
        if(ready)SetCoinCheckoutStatus("Select a package to continue to Microsoft Store.");
        else if(settled)SetCoinCheckoutStatus("Microsoft Store packages unavailable. Select a package to retry.");
    } catch (const winrt::hresult_error& e) {
        CoinProbeReport(L"exception?error="+std::to_wstring(e.code().value));
        SetCoinCheckoutStatus("Windows delivery check unavailable. Please do not buy again.");
    } catch (...) { CoinProbeReport(L"exception?unknown=1");SetCoinCheckoutStatus("Windows delivery check unavailable. Please do not buy again."); }
    InterlockedExchange(&g_coinProbeStarted,0);
    return 0;
}

winrt::fire_and_forget OpenCoinCheckout(size_t index) {
    try {
        co_await winrt::resume_background();
        std::string response;
        auto pending=winrt::Windows::ApplicationModel::Store::CurrentApp::GetUnfulfilledConsumablesAsync().get();
        if(pending.Size()!=0) {
            SetCoinCheckoutStatus("A Windows purchase is still pending. Please wait for delivery.");
            InterlockedExchange(&g_coinCheckoutBusy,0);co_return;
        }
        std::string data=winrt::to_string(g_coinProductIds[index])+"\n0\n";
        uint32_t size=static_cast<uint32_t>(data.size());std::string observation(reinterpret_cast<const char*>(&size),sizeof(size));observation+=data;
        if (!QueryStoreUi("PRP3",observation,response) || response.rfind("ready:",0)!=0 || response.size()!=38) {
            SetCoinCheckoutStatus("Checkout unavailable. Previous purchases need review.");
            InterlockedExchange(&g_coinCheckoutBusy,0);co_return;
        }
        g_coinAttempt=response.substr(6);
        auto dispatcher=winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow().Dispatcher();
        co_await winrt::resume_foreground(dispatcher);
        auto message="Review the "+std::to_string(g_coinAmounts[index])+"-Minecoin purchase in Microsoft Store.";
        SetCoinCheckoutStatus(message.c_str());
        auto result=co_await winrt::Windows::Services::Store::StoreContext::GetDefault().RequestPurchaseAsync(g_coinProductIds[index]);
        auto status=result.Status();
        co_await winrt::resume_background();
        CoinProbeReport(L"checkout?status="+std::to_wstring(static_cast<int>(status))+L"&error="+std::to_wstring(result.ExtendedError().value));
        using Status=winrt::Windows::Services::Store::StorePurchaseStatus;
        if(status==Status::NotPurchased) {
            auto remaining=winrt::Windows::ApplicationModel::Store::CurrentApp::GetUnfulfilledConsumablesAsync().get();
            if(remaining.Size()!=0) {
                InterlockedExchange(&g_coinPlatformReady,0);
                SetCoinCheckoutStatus("Windows has a pending purchase. Select a package to check delivery.");
            } else {
                std::string cancelled=g_coinAttempt+"\n0\n";
                uint32_t n=static_cast<uint32_t>(cancelled.size());
                std::string packet(reinterpret_cast<const char*>(&n),sizeof(n));packet+=cancelled;
                bool saved=false;
                for(unsigned retry=0;retry<3 && !saved;++retry) saved=QueryStoreUi("CAN2",packet,response) && response=="cancelled";
                if(saved)SetCoinCheckoutStatus("Purchase cancelled. Select another package to continue.");
                else {InterlockedExchange(&g_coinPlatformReady,0);SetCoinCheckoutStatus("Cancellation could not be confirmed. Select a package to retry the check.");}
            }
            InterlockedExchange(&g_coinCheckoutBusy,0);co_return;
        }
        if(status==Status::Succeeded || status==Status::AlreadyPurchased) {
            SetCoinCheckoutStatus("Purchase received. Checking your Minecoin wallet...");
            if(!ReconcileWindowsCoinsWithRetry("TOP2"))InterlockedExchange(&g_coinPlatformReady,0);
            InterlockedExchange(&g_coinCheckoutBusy,0);
            co_return;
        }
        InterlockedExchange(&g_coinPlatformReady,0);
        SetCoinCheckoutStatus("Purchase needs verification. Please do not buy again.");
    } catch(const winrt::hresult_error& e) {
        InterlockedExchange(&g_coinPlatformReady,0);
        CoinProbeReport(L"checkout-exception?error="+std::to_wstring(e.code().value));
        SetCoinCheckoutStatus("Checkout needs verification. Please do not buy again.");
    } catch(...) { InterlockedExchange(&g_coinPlatformReady,0);SetCoinCheckoutStatus("Checkout needs verification. Please do not buy again."); }
    InterlockedExchange(&g_coinCheckoutBusy,0);
}
int __fastcall SmallCoinPurchasePress(void* callback) {
    if(g_coinProbeStarted || g_coinCheckoutBusy)return 0;
    auto tag=reinterpret_cast<uintptr_t*>(callback)[1];
    size_t index=(tag>=1 && tag<=5) ? tag-1 : 0; // Legacy purchase_coins still means smallest.
    if(!g_coinPlatformReady) { StartCoinPlatformProbe();return 0; }
    if(!(g_coinAvailableMask&(1L<<index))) {SetCoinCheckoutStatus("This package is unavailable in Microsoft Store.");return 0;}
    if(InterlockedCompareExchange(&g_coinCheckoutBusy,1,0)==0) OpenCoinCheckout(index);
    return 0;
}
void RegisterCoinPurchaseButtons(void* controller) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto registry=*reinterpret_cast<void**>(static_cast<unsigned char*>(controller)+0xc8);
    for(uintptr_t i=0;i<5;++i) {
        // Short native strings stay inline, avoiding cross-runtime allocation.
        std::string name="button.coin_"+std::to_string(i);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(base+0x70DC30)(registry,&name);
        auto id=reinterpret_cast<unsigned short(__fastcall*)(void*,void*)>(base+0x70DD20)(registry,&name);
        struct NativeFunction {uintptr_t storage[7]{};void* self;} value{};
        value.storage[0]=reinterpret_cast<uintptr_t>(base+0x17051E8);
        value.storage[1]=i+1;value.self=&value;
        reinterpret_cast<void(__fastcall*)(void*,unsigned short,void*)>(base+0x3DDCB0)(controller,id,&value);
    }
}
void StartCoinPlatformProbe() {
    if(g_coinCheckoutBusy || g_coinPlatformReady)return;
    if (InterlockedCompareExchange(&g_coinProbeStarted,1,0)==0) {
        SetCoinCheckoutStatus("Checking Microsoft Store and purchase delivery...");
        auto thread=CreateThread(nullptr,0,ProbeCoinPlatform,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
        else {InterlockedExchange(&g_coinProbeStarted,0);SetCoinCheckoutStatus("Store check could not start. Select a package to retry.");}
    }
}
