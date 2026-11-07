#include "online.hpp"
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// Compile the real service's Vita branch on the desktop with SDK calls mocked.
// A query before all three initialization steps simulates the launch regression.
namespace {
std::atomic<bool> module_ready{false}, net_ready{false}, control_ready{false};
std::atomic<int> queries{0}, unsafe_queries{0}, state{0}, failure{0}, query_error{0};
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
reader::online::Update finish(reader::online::Service& service) {
    for(int i=0;i<2000;++i) {
        const auto update=service.poll();
        // Exercise concurrent status reads while the worker initializes.
        service.connection_state();
        if(update.done) return update;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Network worker timed out");
}
void startup(int fail_at) {
    failure=fail_at;queries=unsafe_queries=0;query_error=0;state=0;
    {
        reader::online::Service service(NOVELREADER_ASSETS,".");
        for(int frame=0;frame<120;++frame)
            check(service.connection_state()==-1,"Fresh launch reported initialized networking");
        check(queries==0,"Drawing offline Home called NetCtl before initialization");
        reader::online::Request request;request.source="novgo";
        check(service.start(request),"Could not start initialization test");
        const auto update=finish(service);
        check(!update.error.empty(),"Disconnected test must stop before HTTP");
        check(unsafe_queries==0,"Queried a missing module or uninitialized NetCtl");
        if(fail_at) {
            check(service.connection_state()==-1 && queries==0,"Failed initialization enabled status polling");
        } else {
            check(service.connection_state()==0,"Disconnected status was lost");
            state=SCE_NETCTL_STATE_CONNECTED;
            check(service.connection_state()==SCE_NETCTL_STATE_CONNECTED,"Connection change was not reflected");
            query_error=-1;
            check(service.connection_state()==-1,"Status query failure was presented as connected");
        }
    }
    check(!module_ready && !net_ready && !control_ready,"Network teardown left initialized resources");
}
}
extern "C" int sceSysmoduleLoadModule(unsigned short) {
    if(failure==1)return -1;
    module_ready=true;return 0;
}
extern "C" int sceSysmoduleUnloadModule(unsigned short) {module_ready=false;return 0;}
extern "C" int sceNetInit(SceNetInitParam* param) {
    if(!module_ready || failure==2 || !param->memory || param->size<=0)return -1;
    net_ready=true;return 0;
}
extern "C" int sceNetTerm() {net_ready=false;return 0;}
extern "C" int sceNetCtlInit() {
    if(!net_ready || failure==3)return -1;
    control_ready=true;return 0;
}
extern "C" int sceNetCtlTerm() {control_ready=false;return 0;}
extern "C" int sceNetCtlInetGetState(int* output) {
    ++queries;
    if(!module_ready || !net_ready || !control_ready){++unsafe_queries;return -1;}
    *output=state;return query_error;
}
int main() {
    try {
        for(int stage=0;stage<=3;++stage)startup(stage);
        std::cout<<"Vita network startup tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
