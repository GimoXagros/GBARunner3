// SPDX-License-Identifier: GPL-3.0-or-later
// Runs GBARunner3 as an NDS homebrew cartridge, NOT a GBA core.
#include "NDS.h"
#include "Args.h"
#include "ARMInterpreter.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace melonDS;
namespace melonDS {
void ProbeInstruction(ARMv5* c) {
    struct Snapshot {u32 pc,cpsr,op,r[15];};
    static std::array<Snapshot,256> ring{};
    static unsigned count=0;
    static u32 lastFrame=~0u;
    static unsigned instructionsThisFrame=0;
    const u32 frame=NDS::Current->NumFrames;
    if(frame!=lastFrame){lastFrame=frame;instructionsThisFrame=0;}
    const bool instructionLimit=++instructionsThisFrame>50000000u;
    Snapshot s{c->R[15]-((c->CPSR&32)?4u:8u),c->CPSR,c->CurInstr,{}};
    std::copy(c->R,c->R+15,s.r);ring[count++%ring.size()]=s;
    // Large low-address ITCM mirrors are intentional GBARunner dispatch
    // trampolines. Do not reject them solely because their address is large.
    if(instructionLimit || (s.pc>=0x10000000 && s.pc<0xFF000000) ||
       (s.op==0xE1200070 && (s.cpsr&31)!=0x10)) {
        std::ofstream f("invalid-pc-ring.csv");
        f<<"pc,cpsr,instruction";for(int i=0;i<15;++i)f<<",r"<<i;f<<'\n';
        for(unsigned i=count>ring.size()?count-ring.size():0;i<count;++i){
            auto& e=ring[i%ring.size()];f<<std::hex<<e.pc<<','<<e.cpsr<<','<<e.op;
            for(u32 r:e.r)f<<','<<r;f<<'\n';
        }
        throw std::runtime_error(instructionLimit
            ? "DS frame instruction budget exceeded; bounded ring saved (host guard, not a guest root-cause diagnosis)"
            : "DS CPU address guard or privileged BKPT; bounded ring saved (not a guest root-cause diagnosis)");
    }
}
}
class ProbeNDS : public NDS {
    std::ofstream io {"io.csv"};
    void observe(u32 a,u32 v,unsigned width) {
        if(a==0x04000208 || a==0x04000210 || a==0x04000000)
            io<<NumFrames<<','<<std::hex<<GetPC(0)<<','<<ARM9.CPSR<<','<<ARM9.CurInstr<<','<<a<<','<<v<<','<<width<<std::dec<<std::endl;
    }
public:
    explicit ProbeNDS(NDSArgs&& a):NDS(std::move(a)) {io<<"frame,pc,cpsr,instruction,address,value,width\n";}
    void ARM9Write32(u32 a,u32 v) override {observe(a,v,32);NDS::ARM9Write32(a,v);}
    void ARM9Write16(u32 a,u16 v) override {observe(a,v,16);NDS::ARM9Write16(a,v);}
    void ARM9Write8(u32 a,u8 v) override {observe(a,v,8);NDS::ARM9Write8(a,v);}
};
static std::vector<u8> readFile(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot read "+p.u8string());
    return {std::istreambuf_iterator<char>(f),{}};
}
template<class T> static void readBIOS(T& a,const std::filesystem::path& p) {
    auto b=readFile(p); if(b.size()!=a.size())throw std::runtime_error("Wrong BIOS length");
    std::copy(b.begin(),b.end(),a.begin());
}
static void screen(NDS& n,const std::string& name) {
    void *top,*bottom;
    if(!n.GPU.GetFramebuffers(&top,&bottom))throw std::runtime_error("No software framebuffer");
    // Uncompressed top-down BMP; no external image or UI dependency.
    u8 h[54]{}; auto put=[&](int off,u32 val){memcpy(h+off,&val,4);};
    h[0]='B';h[1]='M';put(2,54+256*384*4);put(10,54);put(14,40);
    put(18,256);put(22,u32(-384));h[26]=1;h[28]=32;put(34,256*384*4);
    std::ofstream f(name,std::ios::binary);f.write((char*)h,54);
    f.write((char*)top,256*192*4);f.write((char*)bottom,256*192*4);
}
int main(int argc,char** argv) try {
    if(argc==2 && std::string(argv[1])=="--selftest"){
        auto n=std::make_unique<NDS>();NDS::Current=n.get();n->Reset();
        auto& c=n->ARM9;
        c.CP15Write(0x600,0x3F); // enabled 4 GiB background region
        c.CP15Write(0x502,0x1);c.CP15Write(0x503,0x1);
        c.CP15Write(0x100,c.CP15Read(0x100)|1);
        bool ok=true;
        for(u32 addr:{0u,0x04000000u,0xFFFFC000u}){
            unsigned p=c.PU_PrivMap[addr>>12],u=c.PU_UserMap[addr>>12];
            bool pass=(p&7)==7 && (u&7)==0;ok&=pass;
            printf("MPU %08X priv=%02X user=%02X %s\n",addr,p,u,pass?"PASS":"FAIL");
        }
        c.CP15Write(0x3F00,0);
        c.CP15Write(0x3F20,0x02001010);
        c.CP15Write(0x3F30,0xE1A00000);
        c.CP15Write(0x3F10,0x09ED4010);
        bool tags=c.CP15Read(0x3F30)==0xE1A00000 && c.CP15Read(0x3F20)==0x02001010;
        ok&=tags;
        printf("Cache debug tag isolation: instruction=%08X dataTag=%08X %s\n",c.CP15Read(0x3F30),c.CP15Read(0x3F20),tags?"PASS":"FAIL");
        c.CP15Write(0x3F00,0x20);
        c.CP15Write(0x3F30,0xE1A01001);
        c.CP15Write(0x3F10,0x02000010);
        bool index=c.ICacheLookup(0x02000020)==0xE1A01001;
        ok&=index;printf("Cache debug nonzero index lookup: %s\n",index?"PASS":"FAIL");
        // A denied first STM word must not let later words write privileged
        // DS IO. In the observed BIOS failure the third word cleared DS IME.
        c.CP15Write(0x100,c.CP15Read(0x100)&~0x2000u);
        n->IME[0]=1;
        for(int i=2;i<=9;++i)c.R[i]=0;
        c.R[1]=0x04000200;c.R[15]=0x02001008;
        u32 old=c.CPSR;c.CPSR=0x10;c.UpdateMode(old,c.CPSR);
        c.CurInstr=0xE8A103FC; // stmia r1!, {r2-r9}
        bool trapped=false;
        try {ARMInterpreter::ARMInstrTable[((c.CurInstr>>4)&15)|((c.CurInstr>>16)&0xFF0)](&c);}
        catch(const InterpreterDataAbort&){trapped=true;}
        bool stm=trapped && n->IME[0]==1 && c.R[1]==0x04000200;
        ok&=stm;printf("STM abort: trapped=%d IME=%u base=%08X %s\n",trapped,n->IME[0],c.R[1],stm?"PASS":"FAIL");
        return ok?0:1;
    }
    if(argc!=4){std::cerr<<"Usage: gbar3-melonds-probe NDS_FILE SD_DIRECTORY BIOS_DIRECTORY\n";return 2;}
    std::cout.setf(std::ios::unitbuf);
    NDSArgs args;args.JIT=std::nullopt;
    auto bios=std::filesystem::u8path(argv[3]);
    readBIOS(*args.ARM9BIOS,bios/"biosnds9.bin");readBIOS(*args.ARM7BIOS,bios/"biosnds7.bin");
    auto fw=readFile(bios/"firmware.bin");args.Firmware=Firmware(fw.data(),fw.size());
    auto n=std::make_unique<ProbeNDS>(std::move(args));
    NDS::Current=n.get();
    auto rom=readFile(std::filesystem::u8path(argv[1]));
    NDSCart::NDSCartArgs ca;
    ca.SDCard=FATStorageArgs{"probe-dldi.img",256*1024*1024,false,std::filesystem::absolute(std::filesystem::u8path(argv[2])).u8string()};
    auto cart=NDSCart::ParseROM(rom.data(),rom.size(),nullptr,std::move(ca));
    if(!cart)throw std::runtime_error("Invalid NDS image");
    n->SetNDSCart(std::move(cart));n->Reset();n->SetupDirectBoot("gbar3.nds");n->Start();
    unsigned frame=0;
    std::ofstream trace("frames.csv");
    trace<<"frame,arm9_pc,arm9_lr,cpsr,arm7_pc,ds_vcount,ds_ie,ds_if,dispcnt,audio_peak\n";
    auto status=[&](int peak){
        char line[256];snprintf(line,sizeof(line),"%u,%08X,%08X,%08X,%08X,%u,%08X,%08X,%08X,%d",frame,n->GetPC(0),n->ARM9.R[14],n->ARM9.CPSR,n->GetPC(1),n->ARM9Read16(0x04000006),n->ARM9Read32(0x04000210),n->ARM9Read32(0x04000214),n->ARM9Read32(0x04000000),peak);
        trace<<line<<std::endl;std::cout<<line<<"\n";
    };
    std::cout<<"Ready. Commands: run FRAMES PRESSED_MASK_HEX; screen NAME.bmp; read ADDRESS_HEX LENGTH_HEX; quit. Key bits A=1 B=2 Select=4 Start=8.\n";
    status(0);
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream in(line);std::string cmd;in>>cmd;
        if(cmd=="quit")break;
        if(cmd=="run"){
            unsigned count=0,mask=0;in>>count>>std::hex>>mask;
            if(count>36000)throw std::runtime_error("Too many frames in one command");
            n->SetKeyMask(0xFFF^mask);
            int peak=0;
            for(unsigned i=0;i<count&&n->IsRunning();++i){
                n->RunFrame();++frame;
                s16 audio[4096];int ns;
                while((ns=n->SPU.ReadOutput(audio,2048))>0)for(int j=0;j<ns*2;++j)peak=std::max(peak,std::abs(int(audio[j])));
                if(frame%60==0){status(peak);peak=0;}
            }
            status(peak);std::cout<<"DONE\n";
        }else if(cmd=="state"){
            for(int i=0;i<16;++i)printf("r%d=%08X%c",i,n->ARM9.R[i],i%4==3?'\n':' ');
            printf("CPSR=%08X HALT=%u IME=%08X IE=%08X IF=%08X DTCM=%08X/%08X ITCMSIZE=%08X\n",n->ARM9.CPSR,n->ARM9.Halted,n->IME[0],n->IE[0],n->IF[0],n->ARM9.DTCMBase,n->ARM9.DTCMMask,n->ARM9.ITCMSize);
            printf("ABT sp=%08X lr=%08X spsr=%08X IRQ sp=%08X lr=%08X spsr=%08X\n",n->ARM9.R_ABT[0],n->ARM9.R_ABT[1],n->ARM9.R_ABT[2],n->ARM9.R_IRQ[0],n->ARM9.R_IRQ[1],n->ARM9.R_IRQ[2]);
            fflush(stdout);
        }else if(cmd=="screen"){
            std::string name;in>>name;screen(*n,name);std::cout<<"SAVED "<<name<<"\n";
        }else if(cmd=="read"){
            unsigned addr=0,len=0;in>>std::hex>>addr>>len;
            if(len>4096)throw std::runtime_error("Read too large");
            // Bus read is non-mutating; DTCM/ITCM read explicitly to avoid
            // affecting emulated CPU timing or protected-memory exceptions.
            for(unsigned i=0;i<len;++i){u32 a=addr+i;u8 b;
                if((a&n->ARM9.DTCMMask)==n->ARM9.DTCMBase)b=n->ARM9.DTCM[a&0x3FFF];
                else if(a<n->ARM9.ITCMSize)b=n->ARM9.ITCM[a&0x7FFF];
                else b=n->ARM9Read8(a);
                printf("%02X%s",b,(i%16==15)?"\n":" ");
            }printf("\n");fflush(stdout);
        }else if(!cmd.empty()){std::cout<<"Unknown command\n";}
    }
    n->Stop();n.reset();NDS::Current=nullptr;
    std::cout<<"STOPPED; DLDI folder synchronized\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
