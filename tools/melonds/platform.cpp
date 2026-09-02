// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal local-only frontend for the official melonDS core. No GUI, network,
// camera, microphone, or controller access. Software-renderer threads are real.
#include "Platform.h"
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>

namespace melonDS::Platform {
struct FileHandle { FILE* fp; };
std::string GetLocalFilePath(const std::string& p) { return p; }
FileHandle* OpenFile(const std::string& p, FileMode m) {
    const bool w=m&Write, r=m&Read, keep=m&Preserve;
    const bool exists=std::filesystem::exists(std::filesystem::u8path(p));
    if ((m&NoCreate) && !exists) return nullptr;
    std::string mode=w ? ((m&Append) ? "a" : ((keep&&exists) ? "r+" : "w")) : "r";
    if(w&&r&&mode.find('+')==std::string::npos) mode+='+';
    mode+=(m&Text) ? "t" : "b";
#ifdef _WIN32
    FILE* f=_wfopen(std::filesystem::u8path(p).c_str(), std::wstring(mode.begin(),mode.end()).c_str());
#else
    FILE* f=fopen(p.c_str(),mode.c_str());
#endif
    return f ? new FileHandle{f} : nullptr;
}
FileHandle* OpenLocalFile(const std::string& p,FileMode m) { return OpenFile(p,m); }
bool FileExists(const std::string& p) { return std::filesystem::exists(std::filesystem::u8path(p)); }
bool LocalFileExists(const std::string& p) { return FileExists(p); }
bool CloseFile(FileHandle* f) { if(!f)return false; auto ret=fclose(f->fp); delete f; return ret==0; }
bool CheckFileWritable(const std::string& p) { return CloseFile(OpenFile(p,FileMode(Write|Preserve))); }
bool CheckLocalFileWritable(const std::string& p) { return CheckFileWritable(p); }
bool IsEndOfFile(FileHandle* f) { return feof(f->fp)!=0; }
bool FileReadLine(char* s,int n,FileHandle* f) { return fgets(s,n,f->fp)!=nullptr; }
u64 FilePosition(FileHandle* f) { return ftello64(f->fp); }
bool FileSeek(FileHandle* f,s64 off,FileSeekOrigin o) { return fseeko64(f->fp,off,o==FileSeekOrigin::Start?SEEK_SET:o==FileSeekOrigin::Current?SEEK_CUR:SEEK_END)==0; }
void FileRewind(FileHandle* f) { rewind(f->fp); }
u64 FileRead(void* d,u64 s,u64 n,FileHandle* f) { return fread(d,s,n,f->fp); }
u64 FileWrite(const void* d,u64 s,u64 n,FileHandle* f) { return fwrite(d,s,n,f->fp); }
bool FileFlush(FileHandle* f) { return fflush(f->fp)==0; }
u64 FileWriteFormatted(FileHandle* f,const char* fmt,...) { va_list ap; va_start(ap,fmt); int n=vfprintf(f->fp,fmt,ap); va_end(ap); return n<0?0:n; }
u64 FileLength(FileHandle* f) { auto p=FilePosition(f); fseeko64(f->fp,0,SEEK_END); auto n=FilePosition(f); fseeko64(f->fp,p,SEEK_SET); return n; }
void Log(LogLevel l,const char* fmt,...) {
    static FILE* f=fopen("core.log","w");
    static unsigned long count=0;
    // Bound pathological unknown-register spam; keep the first evidence.
    if(!f || count++>100000) return;
    fprintf(f,"[%d] ",int(l)); va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap); fflush(f);
}
void SignalStop(StopReason r,void*) { fprintf(stderr,"melonDS stopped: %d\n",int(r)); }
struct Thread { std::thread t; };
Thread* Thread_Create(std::function<void()> f) { return new Thread{std::thread(std::move(f))}; }
void Thread_Wait(Thread* t) { if(t&&t->t.joinable()) t->t.join(); }
void Thread_Free(Thread* t) { Thread_Wait(t); delete t; }
struct Mutex { std::mutex m; };
Mutex* Mutex_Create(){return new Mutex;}
void Mutex_Free(Mutex* m){delete m;}
void Mutex_Lock(Mutex* m){m->m.lock();}
void Mutex_Unlock(Mutex* m){m->m.unlock();}
bool Mutex_TryLock(Mutex* m){return m->m.try_lock();}
struct Semaphore { std::mutex m; std::condition_variable cv; unsigned n=0; };
Semaphore* Semaphore_Create(){return new Semaphore;}
void Semaphore_Free(Semaphore* s){delete s;}
void Semaphore_Reset(Semaphore* s){std::lock_guard<std::mutex> l(s->m); s->n=0;}
void Semaphore_Wait(Semaphore* s){std::unique_lock<std::mutex> l(s->m);s->cv.wait(l,[&]{return s->n!=0;});--s->n;}
bool Semaphore_TryWait(Semaphore* s,int ms){std::unique_lock<std::mutex> l(s->m);if(!s->cv.wait_for(l,std::chrono::milliseconds(ms),[&]{return s->n!=0;}))return false;--s->n;return true;}
void Semaphore_Post(Semaphore* s,int n){{std::lock_guard<std::mutex> l(s->m);s->n+=n;}s->cv.notify_all();}
void Sleep(u64 us){std::this_thread::sleep_for(std::chrono::microseconds(us));}
u64 GetUSCount(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
u64 GetMSCount(){return GetUSCount()/1000;}
void WriteNDSSave(const u8*,u32,u32,u32,void*){}
void WriteGBASave(const u8*,u32,u32,u32,void*){}
void WriteFirmware(const Firmware&,u32,u32,void*){}
void WriteDateTime(int,int,int,int,int,int,void*){}
void MP_Begin(void*){}
void MP_End(void*){}
int MP_SendPacket(u8*,int,u64,void*){return 0;}
int MP_RecvPacket(u8*,u64*,void*){return 0;}
int MP_SendCmd(u8*,int,u64,void*){return 0;}
int MP_SendReply(u8*,int,u64,u16,void*){return 0;}
int MP_SendAck(u8*,int,u64,void*){return 0;}
int MP_RecvHostPacket(u8*,u64*,void*){return 0;}
u16 MP_RecvReplies(u8*,u64,u16,void*){return 0;}
int Net_SendPacket(u8*,int,void*){return 0;}
int Net_RecvPacket(u8*,void*){return 0;}
void Camera_Start(int,void*){}
void Camera_Stop(int,void*){}
void Camera_CaptureFrame(int,u32*,int,int,bool,void*){}
void Mic_Start(void*){}
void Mic_Stop(void*){}
int Mic_ReadInput(s16*,int,void*){return 0;}
AACDecoder* AAC_Init(){return nullptr;}
void AAC_DeInit(AACDecoder*){}
bool AAC_Configure(AACDecoder*,int,int){return false;}
bool AAC_DecodeFrame(AACDecoder*,const void*,int,void*,int){return false;}
bool Addon_KeyDown(KeyType,void*){return false;}
void Addon_RumbleStart(u32,void*){}
void Addon_RumbleStop(void*){}
float Addon_MotionQuery(MotionQueryType,void*){return 0;}
DynamicLibrary* DynamicLibrary_Load(const char*){return nullptr;}
void DynamicLibrary_Unload(DynamicLibrary*){}
void* DynamicLibrary_LoadFunction(DynamicLibrary*,const char*){return nullptr;}
}
