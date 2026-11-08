// festhost64.exe - runs festlib.dll (64-bit) in its own process for the NVDA
// Festival driver, so that a crash inside Festival cannot take NVDA down.
// Copyright (C) 2026 Jiří Holzinger. GNU GPL v2 or later.
//
// stdin: UTF-8 command lines
//   I <dir>        initialize             -> reply: 0 / -1
//   N              voice count            -> reply: count
//   n <i>          voice name             -> reply: name
//   C              current voice          -> reply: name
//   v <name>       set voice (no reply)
//   R <0..100>     rate      T <0..100> pitch      L <0..100> volume (no reply)
//   S <bm> <text>  speak text with bookmark (no reply)
//   X              stop (no reply)
//   Q              quit
// stdout: frames  [uint32 payload length][uint8 type][payload]
//   'r' reply      payload: UTF-8 text
//   'a' audio      payload: int32 bookmark, int32 sample rate, int32 channels, PCM16
//   'e' end        payload: int32 bookmark (text finished, also if silent)
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

typedef int (__cdecl *audio_cb_t)(const short*,int,int,int,int);
typedef int (*init_t)(const wchar_t*);
typedef void (*say_t)(const wchar_t*,int);
typedef void (*void_t)();
typedef void (*seti_t)(int);
typedef int (*geti_t)();
typedef const char* (*name_t)(int);
typedef const char* (*cname_t)();
typedef void (*setv_t)(const char*);
typedef void (*setcb_t)(audio_cb_t);

static HANDLE g_out;
static std::mutex g_out_mutex;
static bool g_out_broken=false;
static std::wstring g_dir;

static void write_frame(char type,const void* a,size_t alen,const void* b,size_t blen)
{
std::lock_guard<std::mutex> lock(g_out_mutex);
if(g_out_broken)
return;
uint32_t len=static_cast<uint32_t>(alen+blen);
unsigned char hdr[5];
std::memcpy(hdr,&len,4);
hdr[4]=static_cast<unsigned char>(type);
const void* parts[3]={hdr,a,b};
size_t sizes[3]={5,alen,blen};
for(int i=0;i<3;++i)
{
const char* p=static_cast<const char*>(parts[i]);
size_t left=sizes[i];
while(left>0)
{
DWORD w=0;
if(!WriteFile(g_out,p,static_cast<DWORD>(left>(1u<<20)?(1u<<20):left),&w,NULL)||w==0)
{
g_out_broken=true;
return;
}
p+=w;
left-=w;
}
}
}

static void reply(const std::string& s)
{
write_frame('r',s.data(),s.size(),nullptr,0);
}

static int __cdecl on_audio(const short* samples,int frames,int channels,int rate,int bookmark)
{
if(g_out_broken)
return 1;
if(!samples||frames<=0)
{
int32_t bm=bookmark;
write_frame('e',&bm,4,nullptr,0);
return 0;
}
int32_t h[3]={bookmark,rate,channels};
write_frame('a',h,sizeof(h),samples,static_cast<size_t>(frames)*channels*2);
return g_out_broken?1:0;
}

static std::wstring from_utf8(const std::string& s)
{
if(s.empty())
return std::wstring();
int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),NULL,0);
std::wstring w(static_cast<size_t>(n),L'\0');
MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),&w[0],n);
return w;
}

// Write the crash location to festhost-crash.log next to the executable, so the
// cause can be found; the driver restarts the host afterwards.
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep)
{
wchar_t path[MAX_PATH*2];
std::wstring file=g_dir.empty()?L"festhost-crash.log":g_dir+L"\\festhost-crash.log";
std::wcsncpy(path,file.c_str(),MAX_PATH*2-1);
path[MAX_PATH*2-1]=0;
FILE* f=_wfopen(path,L"a");
if(f)
{
void* addr=ep->ExceptionRecord->ExceptionAddress;
HMODULE mod=NULL;
char modname[MAX_PATH]="?";
if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(addr),&mod))
GetModuleFileNameA(mod,modname,MAX_PATH);
SYSTEMTIME t;
GetLocalTime(&t);
std::fprintf(f,"%04d-%02d-%02d %02d:%02d:%02d festhost64 crashed: exception 0x%08lx at %p (%s+0x%llx)\n",
t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,
static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),addr,modname,
static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(addr)-reinterpret_cast<uintptr_t>(mod)));
std::fclose(f);
}
return EXCEPTION_EXECUTE_HANDLER;
}

static bool read_line(HANDLE in,std::string& line,std::string& buf)
{
for(;;)
{
size_t p=buf.find('\n');
if(p!=std::string::npos)
{
line=buf.substr(0,p);
buf.erase(0,p+1);
if(!line.empty()&&line.back()=='\r')
line.pop_back();
return true;
}
char tmp[4096];
DWORD r=0;
if(!ReadFile(in,tmp,sizeof(tmp),&r,NULL)||r==0)
return false;
buf.append(tmp,r);
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int)
{
SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
SetUnhandledExceptionFilter(crash_filter);
HANDLE in=GetStdHandle(STD_INPUT_HANDLE);
g_out=GetStdHandle(STD_OUTPUT_HANDLE);
wchar_t exe[MAX_PATH];
GetModuleFileNameW(NULL,exe,MAX_PATH);
std::wstring dir(exe);
dir=dir.substr(0,dir.find_last_of(L"\\/"));
g_dir=dir;
SetDllDirectoryW(dir.c_str());
HMODULE h=LoadLibraryW((dir+L"\\festlib.dll").c_str());
if(!h)
{
reply("-2");
return 2;
}
#define F(T,n) T n=reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(h,#n)))
F(init_t,festlib_initialize); F(say_t,festlib_say_text); F(void_t,festlib_stop);
F(seti_t,festlib_set_rate); F(seti_t,festlib_set_pitch); F(seti_t,festlib_set_volume);
F(geti_t,festlib_get_voice_count); F(name_t,festlib_get_voice_name);
F(cname_t,festlib_get_current_voice_name); F(setv_t,festlib_set_voice);
F(setcb_t,festlib_set_audio_callback);
#undef F
if(!festlib_initialize||!festlib_set_audio_callback||!festlib_set_pitch)
{
reply("-3");
return 3;
}
festlib_set_audio_callback(on_audio);
std::string line,buf;
while(read_line(in,line,buf))
{
if(line.empty())
continue;
char c=line[0];
std::string arg=line.size()>2?line.substr(2):std::string();
switch(c)
{
case 'I':
{
std::wstring d=from_utf8(arg);
if(!d.empty())
g_dir=d;
reply(std::to_string(festlib_initialize(d.c_str())));
break;
}
case 'N': reply(std::to_string(festlib_get_voice_count())); break;
case 'n':
{
const char* s=festlib_get_voice_name(std::atoi(arg.c_str()));
reply(s?s:"");
break;
}
case 'C':
{
const char* s=festlib_get_current_voice_name();
reply(s?s:"");
break;
}
case 'v': festlib_set_voice(arg.c_str()); break;
case 'R': festlib_set_rate(std::atoi(arg.c_str())); break;
case 'T': festlib_set_pitch(std::atoi(arg.c_str())); break;
case 'L': festlib_set_volume(std::atoi(arg.c_str())); break;
case 'S':
{
size_t sp=arg.find(' ');
int bm=std::atoi(arg.substr(0,sp).c_str());
std::string text=sp==std::string::npos?std::string():arg.substr(sp+1);
festlib_say_text(from_utf8(text).c_str(),bm);
break;
}
case 'X': festlib_stop(); break;
#ifdef FESTHOST_TEST
case 'K': *static_cast<volatile int*>(nullptr)=0; break; // crash on purpose (test build only)
#endif
case 'Q':
festlib_stop();
ExitProcess(0);
default: break;
}
}
// NVDA went away: leave without running Festival's static destructors
festlib_stop();
ExitProcess(0);
return 0;
}
