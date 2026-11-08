//Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>
//64-bit port and pitch support 2026 Jiří Holzinger
//  - builds as a 64-bit (x64) DLL with MSVC or MinGW-w64/llvm-mingw
//  - Boost, thread_safe_signals and TinyXML replaced by the C++17 standard
//    library and TinyXML-2
//  - new functions: festlib_set_pitch, festlib_get_pitch, festlib_is_speaking,
//    festlib_terminate, festlib_get_version
//  - the rate (and pitch) is kept when the voice changes
//  - optional audio callback (festlib_set_audio_callback): the caller plays the
//    audio itself, e.g. through NVDA's nvwave.WavePlayer

//This program is free software:
//you can redistribute it and/or modify it under the terms of
//the GNU General Public License as published by the Free Software Foundation,
//either version 2 of the License, or (at your option) any later version.
//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <objbase.h>
#define FESTLIBAPI extern "C" __declspec(dllexport)
#include "api.hpp"
#include "player.hpp"

#include <festival.h>
#include <EST_String.h>
#include <EST_Wave.h>
#include <EST_Token.h>
#include <siod.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <eh.h>
#endif

#include "tinyxml2.h"

#undef FESTIVAL_HEAP_SIZE
#define FESTIVAL_HEAP_SIZE 3000000
#define TTS_THREAD_STACK_SIZE (16*1024*1024)
#define FESTLIB_VERSION 0x00020100

namespace
{

std::atomic<festlib_audio_callback> audio_callback(nullptr);

#ifdef _MSC_VER
void se_translator(unsigned int code,EXCEPTION_POINTERS*)
{
char buf[32];
std::snprintf(buf,sizeof(buf),"SEH 0x%x",code);
throw std::runtime_error(buf);
}

void invalid_parameter_handler(const wchar_t*,const wchar_t*,const wchar_t*,unsigned int,uintptr_t)
{
}
#endif

std::wstring utf8_to_wide(const char* txt)
{
if(!txt||!*txt)
return std::wstring();
int len=MultiByteToWideChar(CP_UTF8,0,txt,-1,NULL,0);
if(len<=0)
return std::wstring();
std::vector<wchar_t> buf(static_cast<size_t>(len));
if(MultiByteToWideChar(CP_UTF8,0,txt,-1,buf.data(),len)==0)
return std::wstring();
return std::wstring(buf.data());
}

std::string lisp_string_var(const char* name,bool* ok)
{
LISP v=siod_get_lval(name,NULL);
if(!STRINGP(v))
{
*ok=false;
return std::string();
}
*ok=true;
return std::string(get_c_string(v));
}

struct VoiceSettings
{
std::optional<int> cp;
std::vector<std::pair<std::wregex,std::wstring> > rules;
};

class Festival
{
public:
Festival();
Festival(const Festival&)=delete;
Festival& operator=(const Festival&)=delete;

void init(const std::string& parent_dir);
void say_text(const std::wstring& text,int bookmark);
void stop();
void terminate();
std::vector<std::string> get_voices();
std::optional<std::string> get_voice();
void set_voice(const std::string& name);
void set_rate(int);
int get_rate();
void set_pitch(int);
int get_pitch();
void set_volume(int);
int get_volume();
int get_bookmark() {return player.get_bookmark();}
bool is_speaking();

std::atomic<unsigned int> device;

private:
void load_settings(const std::string& file);
void push(std::function<void()> f);
static unsigned __stdcall thread_proc(void* arg);
void tts_loop();
void speak(const std::wstring& text,int volume,int bookmark,unsigned int device,unsigned long gen);
std::optional<std::string> query_voice();
void apply_rate(int);
void apply_pitch(int);
void change_voice(const std::string& name,unsigned long voice_gen);

Player player;
std::map<std::string,VoiceSettings> voice_settings;
std::vector<std::string> settings_warnings;
std::string parent_dir;
std::string festival_libdir_;

std::mutex init_mutex;
bool initialized;
bool tts_started;
bool tts_ready;
std::condition_variable tts_started_cv;

std::mutex info_mutex;
std::vector<std::string> voice_list;
std::optional<std::string> current_voice;
std::optional<std::string> pending_voice;
std::atomic<unsigned long> voice_generation;
std::atomic<int> current_rate;
std::atomic<int> current_pitch;
std::atomic<int> current_volume;

std::mutex queue_mutex;
std::condition_variable queue_cv;
std::deque<std::function<void()> > tts_queue;
std::atomic<int> tts_busy;   // queued + running commands that produce speech
};

Festival::Festival():
device(WAVE_MAPPER),
initialized(false),
tts_started(false),
tts_ready(false),
voice_generation(0),
current_rate(50),
current_pitch(50),
current_volume(100),
tts_busy(0)
{
}

void Festival::load_settings(const std::string& file)
{
tinyxml2::XMLDocument doc;
if(doc.LoadFile(file.c_str())!=tinyxml2::XML_SUCCESS)
return;
tinyxml2::XMLElement* root=doc.FirstChildElement("settings");
if(!root)
return;
tinyxml2::XMLElement* voices=root->FirstChildElement("voices");
if(!voices)
return;
voice_settings.clear();
for(tinyxml2::XMLElement* e1=voices->FirstChildElement("voice");e1;e1=e1->NextSiblingElement("voice"))
{
const char* name=e1->Attribute("name");
if(!name)
continue;
VoiceSettings vs;
int cp=CP_UTF8;
if(e1->QueryIntAttribute("cp",&cp)==tinyxml2::XML_SUCCESS)
vs.cp=cp;
tinyxml2::XMLElement* replace=e1->FirstChildElement("replace");
if(replace)
{
for(tinyxml2::XMLElement* e2=replace->FirstChildElement("item");e2;e2=e2->NextSiblingElement("item"))
{
tinyxml2::XMLElement* what=e2->FirstChildElement("what");
if(!what||!what->GetText())
continue;
std::wstring rgx_str=utf8_to_wide(what->GetText());
if(rgx_str.empty())
continue;
std::wstring fmt_str;
tinyxml2::XMLElement* with=e2->FirstChildElement("with");
if(with&&with->GetText())
fmt_str=utf8_to_wide(with->GetText());
try
{
vs.rules.emplace_back(std::wregex(rgx_str,std::regex_constants::ECMAScript),fmt_str);
}
catch(...)
{
settings_warnings.push_back(std::string("festsettings.xml: regular expression not supported, rule skipped in voice ")+name+": "+what->GetText());
}
}
}
if(vs.cp||!vs.rules.empty())
voice_settings[name]=std::move(vs);
}
}

void Festival::init(const std::string& parent_dir_)
{
std::unique_lock<std::mutex> lock(init_mutex);
if(!initialized)
{
parent_dir=parent_dir_;
load_settings(parent_dir+"\\festsettings.xml");
festival_libdir_=parent_dir+"\\festival\\lib";
festival_libdir=festival_libdir_.c_str();
player.init();
uintptr_t h=_beginthreadex(NULL,TTS_THREAD_STACK_SIZE,&Festival::thread_proc,this,STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
if(h==0)
throw std::runtime_error("cannot start the TTS thread");
CloseHandle(reinterpret_cast<HANDLE>(h));
tts_started_cv.wait(lock,[this]{return tts_started;});
initialized=true;
}
if(!tts_ready)
throw std::runtime_error("cannot initialize Festival");
}

unsigned __stdcall Festival::thread_proc(void* arg)
{
static_cast<Festival*>(arg)->tts_loop();
return 0;
}

void Festival::push(std::function<void()> f)
{
{
std::lock_guard<std::mutex> lock(queue_mutex);
tts_queue.push_back(std::move(f));
}
queue_cv.notify_one();
}

void Festival::tts_loop()
{
// COM on the synthesis thread: an audio callback (NVDA's WASAPI player) may
// create or reopen audio devices from this thread.
HRESULT com_hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);
bool initializing=true;
std::string log_file_name=parent_dir+"\\festival.log";
std::streambuf* cerr_buf=std::cerr.rdbuf();
std::streambuf* cout_buf=std::cout.rdbuf();
std::ofstream log_file;
log_file.open(log_file_name.c_str());
if(log_file.is_open())
std::cerr.rdbuf(log_file.rdbuf());
std::cout.rdbuf(std::cerr.rdbuf());
for(const auto& w:settings_warnings)
std::cerr << w << "\n";
#ifdef _MSC_VER
_invalid_parameter_handler old_iph=_set_invalid_parameter_handler(invalid_parameter_handler);
_se_translator_function old_se_translator=_set_se_translator(se_translator);
#endif
try
{
festival_initialize(1,FESTIVAL_HEAP_SIZE);
if(!festival_eval_command("(require 'festlib_helper)"))
throw std::runtime_error("cannot load festlib_helper");
if(festival_eval_command("(set! tmp_var (mapcar (lambda (x) (format nil \"%s\" x)) (voice-list)))"))
{
LISP l1=siod_get_lval("tmp_var",NULL);
EST_StrList l2;
siod_list_to_strlist(l1,l2);
std::lock_guard<std::mutex> lock(info_mutex);
for(EST_Litem* p=l2.head();p!=0;p=p->next())
voice_list.push_back(static_cast<const char*>(l2(p)));
}
{
std::optional<std::string> v=query_voice();
std::lock_guard<std::mutex> lock(info_mutex);
current_voice=v;
}
apply_rate(50);
apply_pitch(50);
initializing=false;
{
std::lock_guard<std::mutex> lock(init_mutex);
tts_started=true;
{
std::lock_guard<std::mutex> lock2(info_mutex);
tts_ready=!voice_list.empty();
}
}
tts_started_cv.notify_all();
std::cerr << "festlib: initialized, " << voice_list.size() << " voices\n";
std::cerr.flush();
for(;;)
{
std::function<void()> f;
{
std::unique_lock<std::mutex> lock(queue_mutex);
queue_cv.wait(lock,[this]{return !tts_queue.empty();});
f=std::move(tts_queue.front());
tts_queue.pop_front();
}
try
{
f();
}
catch(std::exception& e)
{
std::cerr << "caught exception: " << e.what() << "\n";
}
catch(...)
{
std::cerr << "caught unknown exception\n";
}
std::cerr.flush();
}
}
catch(std::exception& e)
{
std::cerr << "caught exception: " << e.what() << "\n";
}
catch(...)
{
std::cerr << "caught unknown exception during initialization\n";
}
if(initializing)
{
{
std::lock_guard<std::mutex> lock(init_mutex);
tts_started=true;
tts_ready=false;
}
tts_started_cv.notify_all();
}
#ifdef _MSC_VER
_set_se_translator(old_se_translator);
_set_invalid_parameter_handler(old_iph);
#endif
std::cerr.flush();
std::cout.flush();
std::cerr.rdbuf(cerr_buf);
std::cout.rdbuf(cout_buf);
if(SUCCEEDED(com_hr))
CoUninitialize();
}

std::optional<std::string> Festival::query_voice()
{
if(!festival_eval_command("(set! tmp_var (format nil \"%s\" current-voice))"))
return std::nullopt;
bool ok;
std::string s=lisp_string_var("tmp_var",&ok);
if(!ok)
return std::nullopt;
return s;
}

void Festival::apply_rate(int rate)
{
char cmd[64];
std::snprintf(cmd,sizeof(cmd),"(set-voice-rate %d)",rate);
festival_eval_command(cmd);
}

void Festival::apply_pitch(int pitch)
{
char cmd[64];
std::snprintf(cmd,sizeof(cmd),"(set-voice-pitch %d)",pitch);
festival_eval_command(cmd);
}

void Festival::say_text(const std::wstring& text,int bookmark)
{
unsigned long gen=player.current_generation();
int volume=current_volume.load();
unsigned int dev=device.load();
++tts_busy;
push([this,text,volume,bookmark,dev,gen]
{
struct busy_guard {std::atomic<int>& b; ~busy_guard(){--b;}} guard{tts_busy};
speak(text,volume,bookmark,dev,gen);
});
}

void Festival::speak(const std::wstring& text_,int volume,int bookmark,unsigned int dev,unsigned long gen)
{
bool aborted=false;
auto cancelled=[this,gen,&aborted]{return aborted||gen!=player.current_generation();};
if(cancelled())
return;
// In callback mode every text ends with an end marker (samples=NULL, num_frames=0),
// also when it produced no audio, unless it was cancelled.
struct end_marker
{
std::function<bool()> cancelled;
int bookmark;
~end_marker()
{
festlib_audio_callback cb=audio_callback.load();
if(cb&&!cancelled())
cb(nullptr,0,0,0,bookmark);
}
} marker{cancelled,bookmark};
std::wstring text=text_;
int cp=CP_UTF8;
std::optional<std::string> voice=query_voice();
if(voice)
{
auto it=voice_settings.find(*voice);
if(it!=voice_settings.end())
{
if(it->second.cp)
cp=*(it->second.cp);
for(const auto& rule:it->second.rules)
{
try
{
text=std::regex_replace(text,rule.first,rule.second);
}
catch(...) {}
}
}
}
DWORD flags=0;
if(cp!=CP_UTF8)
flags=WC_COMPOSITECHECK|WC_SEPCHARS;
int n=WideCharToMultiByte(cp,flags,text.c_str(),-1,NULL,0,NULL,NULL);
if(n<=1)
return;
std::vector<char> new_text(static_cast<size_t>(n));
if(WideCharToMultiByte(cp,flags,text.c_str(),-1,new_text.data(),n,NULL,NULL)==0)
return;
auto play=[&](EST_Wave& w)
{
if(cancelled()||w.num_samples()<=0)
return;
w.rescale(volume/100.0);
const int channels=w.num_channels();
const size_t frames=static_cast<size_t>(w.num_samples());
std::vector<short> pcm(frames*static_cast<size_t>(channels));
for(size_t i=0;i<frames;++i)
for(int c=0;c<channels;++c)
pcm[i*channels+c]=w.a_no_check(static_cast<int>(i),c);
festlib_audio_callback cb=audio_callback.load();
if(cb)
{
if(cb(pcm.data(),static_cast<int>(frames),channels,w.sample_rate(),bookmark)!=0)
aborted=true;
}
else
player.play(dev,pcm.data(),frames,channels,w.sample_rate(),bookmark,gen);
};
if(text.length()>150)
{
EST_String quoted_text(quote_string(new_text.data(),"\"","\\",1));
std::string cmd=std::string("(set-synth-text ")+static_cast<const char*>(quoted_text)+")";
if(!festival_eval_command(cmd.c_str()))
return;
while(true)
{
if(!festival_eval_command("(set! tmp_var (synth-next-part))"))
break;
if(cancelled())
break;
LISP v=siod_get_lval("tmp_var",NULL);
if(NULLP(v))
break;
if(SYMBOLP(v))
continue;
if(!wave_p(v))
break;
play(*wave(v));
}
festival_eval_command("(clear-synth-text)");
}
else
{
EST_Wave wav;
if(!festival_text_to_wave(new_text.data(),wav))
return;
play(wav);
}
}

void Festival::stop()
{
player.stop();
}

void Festival::terminate()
{
// Festival itself cannot be shut down and started again in one process
// (SIOD keeps global state bound to the TTS thread), so the engine stays
// loaded; only the speech is stopped and the audio device released.
player.close_device();
}

bool Festival::is_speaking()
{
return tts_busy.load()>0||player.busy();
}

void Festival::change_voice(const std::string& name,unsigned long voice_gen)
{
if(voice_gen==voice_generation.load())
{
std::optional<std::string> before=query_voice();
if(!before||*before!=name)
{
std::string cmd=std::string("(festlib-select-voice '")+name+")";
festival_eval_command(cmd.c_str());
// keep the user's rate and pitch for the new voice
apply_rate(current_rate.load());
apply_pitch(current_pitch.load());
}
}
std::optional<std::string> voice=query_voice();
std::lock_guard<std::mutex> lock(info_mutex);
current_voice=voice;
if(voice_gen==voice_generation.load())
pending_voice.reset();
}

void Festival::set_voice(const std::string& name)
{
unsigned long g;
{
std::lock_guard<std::mutex> lock(info_mutex);
if(std::find(voice_list.begin(),voice_list.end(),name)==voice_list.end())
throw std::runtime_error("voice not found");
g=++voice_generation;
pending_voice=name;
}
push([this,name,g]{change_voice(name,g);});
}

std::optional<std::string> Festival::get_voice()
{
std::lock_guard<std::mutex> lock(info_mutex);
if(pending_voice)
return pending_voice;
return current_voice;
}

std::vector<std::string> Festival::get_voices()
{
std::lock_guard<std::mutex> lock(info_mutex);
return voice_list;
}

void Festival::set_rate(int rate)
{
rate=std::max(0,std::min(100,rate));
current_rate=rate;
push([this,rate]{apply_rate(rate);});
}

int Festival::get_rate()
{
return current_rate.load();
}

void Festival::set_pitch(int pitch)
{
pitch=std::max(0,std::min(100,pitch));
current_pitch=pitch;
push([this,pitch]{apply_pitch(pitch);});
}

int Festival::get_pitch()
{
return current_pitch.load();
}

void Festival::set_volume(int volume)
{
current_volume=std::max(0,std::min(100,volume));
}

int Festival::get_volume()
{
return current_volume.load();
}

// Created once and intentionally never destroyed: the worker threads must
// not be joined from DllMain/static destructors during process exit.
Festival& get_tts()
{
static Festival* tts=new Festival;
return *tts;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID)
{
return TRUE;
}

int festlib_initialize(const wchar_t* wpath)
{
if(!wpath)
return -1;
int size=WideCharToMultiByte(CP_ACP,0,wpath,-1,NULL,0,NULL,NULL);
if(size==0)
return -1;
std::vector<char> path(static_cast<size_t>(size));
if(WideCharToMultiByte(CP_ACP,0,wpath,-1,path.data(),size,NULL,NULL)==0)
return -1;
try
{
get_tts().init(path.data());
}
catch(...)
{
return -1;
}
return 0;
}

void festlib_say_text(const wchar_t* text,int bookmark)
{
if(!text)
return;
try {get_tts().say_text(text,bookmark);} catch(...) {}
}

void festlib_stop()
{
get_tts().stop();
}

void festlib_set_audio_device(unsigned int dev)
{
get_tts().device=dev;
}

void festlib_set_volume(int v)
{
get_tts().set_volume(v);
}

int festlib_get_volume()
{
return get_tts().get_volume();
}

int festlib_get_bookmark()
{
return get_tts().get_bookmark();
}

void festlib_set_rate(int r)
{
try {get_tts().set_rate(r);} catch(...) {}
}

int festlib_get_rate()
{
return get_tts().get_rate();
}

void festlib_set_pitch(int p)
{
try {get_tts().set_pitch(p);} catch(...) {}
}

int festlib_get_pitch()
{
return get_tts().get_pitch();
}

int festlib_get_voice_count()
{
return static_cast<int>(get_tts().get_voices().size());
}

const char* festlib_get_voice_name(int index)
{
static thread_local std::string name;
std::vector<std::string> voices=get_tts().get_voices();
if(index<0||static_cast<size_t>(index)>=voices.size())
return NULL;
name=voices[static_cast<size_t>(index)];
return name.c_str();
}

void festlib_set_voice(const char* name)
{
if(!name)
return;
try {get_tts().set_voice(name);} catch(...) {}
}

const char* festlib_get_current_voice_name()
{
static thread_local std::string name;
std::optional<std::string> current=get_tts().get_voice();
if(!current)
return NULL;
name=*current;
return name.c_str();
}

int festlib_is_speaking()
{
return get_tts().is_speaking()?1:0;
}

void festlib_terminate()
{
get_tts().terminate();
}

void festlib_set_audio_callback(festlib_audio_callback cb)
{
audio_callback=cb;
}

int festlib_get_version()
{
return FESTLIB_VERSION;
}
