#include <windows.h>
#include <cstring>
#include <cassert>
#include <cstdio>
void* HookAchievementFunction(unsigned char*,size_t,void*){return nullptr;}
#include "shulker_achievement.h"
bool accepted=true;
bool __fastcall FakeDamage(void*,void*,int,bool){return accepted;}
int main(){
 unsigned char server[0x1600]{},client[0x1600]{},sl[0x1900]{},cl[0x1900]{};
 *reinterpret_cast<void**>(server+0x58)=sl;*reinterpret_cast<void**>(client+0x58)=cl;cl[0x1830]=1;
 *reinterpret_cast<unsigned long long*>(server+0xDD0)=1;*reinterpret_cast<unsigned long long*>(client+0xDD0)=1;
 *reinterpret_cast<unsigned*>(server+0x1524)=0x40004C;auto& result=*reinterpret_cast<unsigned*>(client+0x1524);unsigned effect=24;
 g_originalShulkerDamage=FakeDamage;
 result=1;SyncShulkerEffect(client,&effect);assert(result==1);
 TrackShulkerDamage(server,nullptr,1,false);SyncShulkerEffect(client,&effect);assert(result==0x40004C);
 result=1;effect=1;SyncShulkerEffect(client,&effect);assert(result==1);effect=24;
 *reinterpret_cast<unsigned long long*>(client+0xDD0)=2;SyncShulkerEffect(client,&effect);assert(result==1);*reinterpret_cast<unsigned long long*>(client+0xDD0)=1;
 for(auto& h:g_shulkerHits)h.tick=GetTickCount64()-2001;SyncShulkerEffect(client,&effect);assert(result==1);
 TrackShulkerDamage(server,nullptr,1,false);accepted=false;TrackShulkerDamage(server,nullptr,1,false);SyncShulkerEffect(client,&effect);assert(result==1);
 accepted=true;TrackShulkerDamage(server,nullptr,1,false);*reinterpret_cast<unsigned*>(server+0x1524)=1;TrackShulkerDamage(server,nullptr,1,false);SyncShulkerEffect(client,&effect);assert(result==1);
 puts("7 source synchronization checks passed");
}
