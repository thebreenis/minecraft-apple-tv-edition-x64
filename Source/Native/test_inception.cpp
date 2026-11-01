#include <windows.h>
#include <cstring>
#include <cassert>
#include <cstdio>
void* HookAchievementFunction(unsigned char*,size_t,void*){return nullptr;}
bool IsClientActor(unsigned char*){return true;}
#include "inception_achievement.h"
int main(){
 assert(!TakeInceptionCompletion(1,1000));
 g_inceptionPending={};g_inceptionPending.ids[0]=1;g_inceptionPending.count=1;g_inceptionPending.tick=1000;
 assert(!TakeInceptionCompletion(2,1100));assert(g_inceptionPending.count==1);
 assert(TakeInceptionCompletion(1,1100));assert(!TakeInceptionCompletion(1,1101));
 g_inceptionPending.count=1;g_inceptionPending.tick=1000;assert(!TakeInceptionCompletion(1,1501));assert(!g_inceptionPending.count);
 g_breedingPending={};g_breedingPending.ids[0]=7;g_breedingPending.count=1;g_breedingPending.tick=2000;
 assert(!TakeInceptionCompletion(7,2100));assert(g_breedingPending.count==1);
 assert(!TakePendingCompletion(g_breedingPending,8,2100));
 assert(TakePendingCompletion(g_breedingPending,7,2100));
 assert(!TakePendingCompletion(g_breedingPending,7,2101));
 g_breedingPending.count=1;g_breedingPending.tick=2000;assert(!TakePendingCompletion(g_breedingPending,7,2501));
 puts("Inception queue: absent, wrong player, matching player, exactly once, expiry passed");
}
