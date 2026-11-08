// Small console test for festlib.dll (x64)
#include <windows.h>
#include <cstdio>
#include <string>
typedef int (*init_t)(const wchar_t*);
typedef void (*say_t)(const wchar_t*,int);
typedef void (*voidf_t)();
typedef int (*intf_t)();
typedef void (*seti_t)(int);
typedef const char* (*name_t)(int);
typedef const char* (*cname_t)();
typedef void (*setv_t)(const char*);
#define GET(T,n) T n=(T)GetProcAddress(h,#n); if(!n){printf("missing %s\n",#n);return 1;}
int wmain(int argc,wchar_t** argv)
{
  if(argc<2){printf("usage: test_dll <dir with festlib.dll and festival\\lib>\n");return 1;}
  std::wstring dir=argv[1];
  SetDllDirectoryW(dir.c_str());
  HMODULE h=LoadLibraryW((dir+L"\\festlib.dll").c_str());
  if(!h){printf("LoadLibrary failed %lu\n",GetLastError());return 1;}
  GET(init_t,festlib_initialize) GET(say_t,festlib_say_text) GET(intf_t,festlib_get_voice_count)
  GET(name_t,festlib_get_voice_name) GET(cname_t,festlib_get_current_voice_name) GET(setv_t,festlib_set_voice)
  GET(seti_t,festlib_set_pitch) GET(intf_t,festlib_get_pitch) GET(seti_t,festlib_set_rate) GET(intf_t,festlib_get_rate)
  GET(intf_t,festlib_is_speaking) GET(intf_t,festlib_get_bookmark) GET(voidf_t,festlib_stop) GET(intf_t,festlib_get_version)
  GET(voidf_t,festlib_terminate)
  printf("version %x\n",festlib_get_version());
  DWORD t0=GetTickCount();
  int r=festlib_initialize(dir.c_str());
  printf("initialize -> %d (%lu ms)\n",r,GetTickCount()-t0);
  if(r!=0) return 2;
  int n=festlib_get_voice_count();
  for(int i=0;i<n;i++) printf("voice %d: %s\n",i,festlib_get_voice_name(i));
  printf("current: %s\n",festlib_get_current_voice_name());
  if(argc>=3)
  {
    // single voice mode for audio capture: argv[2]=voice, argv[3]=text
    char vname[128]; WideCharToMultiByte(CP_ACP,0,argv[2],-1,vname,128,NULL,NULL);
    festlib_set_voice(vname);
    for(int p=20;p<=80;p+=30)
    {
      festlib_set_pitch(p);
      t0=GetTickCount();
      festlib_say_text(argc>=4?argv[3]:L"Hello world.",p);
      Sleep(50);
      while(festlib_is_speaking()&&GetTickCount()-t0<30000) Sleep(20);
      printf("%s pitch %d done %lu ms\n",vname,p,GetTickCount()-t0);
    }
    return 0;
  }
  const char* voices[]={"kal_diphone","czech_machac"};
  const wchar_t* texts[]={L"Hello, this is a test of the sixty four bit Festival library.",L"Dobrý den, toto je zkouška českého hlasu."};
  for(int v=0;v<2;v++)
  {
    festlib_set_voice(voices[v]);
    printf("set voice %s -> current reported %s\n",voices[v],festlib_get_current_voice_name());
    for(int p=20;p<=80;p+=30)
    {
      festlib_set_pitch(p);
      t0=GetTickCount();
      festlib_say_text(texts[v],100+p);
      Sleep(50);
      while(festlib_is_speaking()&&GetTickCount()-t0<30000) Sleep(20);
      printf("  pitch %d: done in %lu ms, bookmark %d, pitch reads %d, rate %d\n",p,GetTickCount()-t0,festlib_get_bookmark(),festlib_get_pitch(),festlib_get_rate());
    }
  }
  // long text path (>150 chars) + stop
  std::wstring longtext;
  for(int i=0;i<10;i++) longtext+=L"This is sentence number one of a rather long paragraph that is spoken in parts. ";
  festlib_set_voice("kal_diphone");
  festlib_set_rate(70);
  t0=GetTickCount();
  festlib_say_text(longtext.c_str(),7);
  Sleep(1500);
  festlib_stop();
  while(festlib_is_speaking()&&GetTickCount()-t0<30000) Sleep(20);
  printf("long text stopped, idle after %lu ms, voice %s rate %d\n",GetTickCount()-t0,festlib_get_current_voice_name(),festlib_get_rate());
  festlib_terminate();
  printf("OK\n");
  return 0;
}
