//Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>

//This program is free software:
//you can redistribute it and/or modify it under the terms of
//the GNU General Public License as published by the Free Software Foundation,
//either version 2 of the License, or (at your option) any later version.
//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <windows.h>
#define FESTLIBAPI extern "C" __declspec(dllexport)
#include "api.hpp"
#include "player.hpp"
#include <EST_String.h>
#include <EST_Wave.h>
#include <siod.h>
#include <EST_Token.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <boost/utility.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/signal.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <string>
#include <stdexcept>
#include <eh.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <utility>
#include <map>
#include <fstream>
#include <tinyxml.h>

using std::FILE;
using std::freopen;
using std::fclose;
using std::cerr;
using std::cout;
using std::streambuf;
using std::ofstream;
using std::exception;
using std::runtime_error;
using std::find;
using std::floor;
using boost::noncopyable;
using boost::optional;
using boost::none;
using boost::condition_variable;
using std::string;
using std::wstring;
using std::pair;
using std::map;
using boost::wregex;
using boost::regex_replace;
using boost::format;
using boost::function;
using boost::scoped_ptr;
using boost::thread;
using boost::mutex;
using boost::signal;
using boost::shared_ptr;
using boost::weak_ptr;
using boost::enable_shared_from_this;
using std::vector;
using boost::bind;
using boost::scoped_array;

#define FESTIVAL_HEAP_SIZE 3000000

void festival_initialize(int load_init_files,int heap_size);
int festival_eval_command(const EST_String &expr);
int festival_text_to_wave(const EST_String &text,EST_Wave &wave);
void festival_tidy_up();

EST_Wave* wave(LISP);
int wave_p(LISP);

extern const char* festival_libdir;

void se_translator(unsigned int code,EXCEPTION_POINTERS* p)
{
throw runtime_error((format("0x%x") % code).str());
}

void invalid_parameter_handler(const wchar_t*,const wchar_t*,const wchar_t*,unsigned int,uintptr_t)
{
return;
}

class Festival:private noncopyable
{
public:
Festival();
~Festival()
{
free();
}
void init(const string&);
void say_text(const wstring&,const optional<int>&);
vector<string> get_voices();
optional<int> get_bookmark()
{
return player.get_bookmark();
}
optional<string> get_voice();
void set_voice(const string&);
void set_rate(int);
int get_rate();
void set_volume(int);
int get_volume();

unsigned int device;

typedef signal<void()> stop_event;
stop_event stopped;
void stop();
private:
void free();
bool initialized;
bool freed;
string festival_libdir_;
string parent_dir;

class command:private noncopyable
{
public:
command():cancelled_(false) {}
virtual ~command() {}
void cancel();
bool cancelled();
private:
mutex cancel_mutex;
bool cancelled_;
};

class say_command:public command
{
public:
say_command(unsigned int device_,const wstring& text_,int volume_,const optional<int>& bookmark_):
device(device_),
text(text_),
volume(volume_),
bookmark(bookmark_)
{}

void speak();
typedef map<string,pair<optional<int>, optional<vector<pair<wregex, wstring > > > > > voice_settings_type;
static voice_settings_type voice_settings;
private:
wstring text;
int volume;
unsigned int device;
optional<int> bookmark;
};

friend say_command;

class change_voice_command:public command,public enable_shared_from_this<change_voice_command>
{
public:
change_voice_command(const string& name_):
name(name_)
{}
typedef signal<void()> voice_change_event;
static voice_change_event voice_changed;
static shared_ptr<change_voice_command> create(const string&);
static optional<string> get_voice();
static void init();
const string name;
void set_voice_1();
void set_voice_2();
static optional<string> get_voice_1();
static optional<string> current_voice;
static mutex info_mutex;
static weak_ptr<change_voice_command> changing;
};

friend change_voice_command;

static Player player;

void set_rate_1(int);
int get_rate_1();
void set_rate_2(int);
mutex info_mutex;
vector<string> voice_list;
int current_rate;
int current_volume;
scoped_ptr<thread> tts_thread;
MessageQueue<function<void()> > tts_queue;
void tts_loop();
condition_variable tts_started_event;
bool tts_started;
bool tts_ready;
};

Festival& get_tts();

Player Festival::player;
Festival::say_command::voice_settings_type Festival::say_command::voice_settings;

optional<string> Festival::change_voice_command::current_voice;
mutex Festival::change_voice_command::info_mutex;
weak_ptr<Festival::change_voice_command> Festival::change_voice_command::changing;
Festival::change_voice_command::voice_change_event Festival::change_voice_command::voice_changed;

Festival::Festival():
initialized(false),
freed(false),
device(WAVE_MAPPER),
current_volume(100),
tts_started(false),
tts_ready(false)
{
}

void Festival::init(const string& parent_dir_)
{
if(!initialized)
{
TiXmlDocument doc(parent_dir_+"\\festsettings.xml");
if(doc.LoadFile())
{
TiXmlHandle hDoc(&doc);
TiXmlElement* e1=hDoc.FirstChildElement("settings").FirstChildElement("voices").FirstChildElement("voice").ToElement();
if(e1)
{
say_command::voice_settings.clear();
for(;e1;e1=e1->NextSiblingElement("voice"))
{
const char* name_=e1->Attribute("name");
if(!name_)
continue;
string name(name_);
optional<int> cp;
int cp_=CP_UTF8;
if(e1->QueryIntAttribute("cp",&cp_)==TIXML_SUCCESS)
cp=cp_;
vector<pair<wregex,wstring> > rules_;
TiXmlElement* e2=TiXmlHandle(e1).FirstChildElement("replace").FirstChildElement("item").ToElement();
if(e2)
{
for(;e2;e2=e2->NextSiblingElement("item"))
{
const char* txt=NULL;
TiXmlElement* e=NULL;
int len=0;
e=e2->FirstChildElement("what");
if(!e)
continue;
txt=e->GetText();
if(!txt)
continue;
len=MultiByteToWideChar(CP_UTF8,0,txt,-1,NULL,0);
if(len==0)
continue;
scoped_array<wchar_t> pwtxt1(new wchar_t[len]);
if(MultiByteToWideChar(CP_UTF8,0,txt,-1,pwtxt1.get(),len)==0)
continue;
wstring rgx_str(pwtxt1.get());
wstring fmt_str;
e=e2->FirstChildElement("with");
if(e)
{
txt=e->GetText();
if(!txt)
txt="";
}
else txt="";
if(string(txt).length()>0)
{
len=MultiByteToWideChar(CP_UTF8,0,txt,-1,NULL,0);
if(len==0)
continue;
scoped_array<wchar_t> pwtxt2(new wchar_t[len]);
if(MultiByteToWideChar(CP_UTF8,0,txt,-1,pwtxt2.get(),len)==0)
continue;
fmt_str=pwtxt2.get();
}
try
{
wregex r(rgx_str);
rules_.push_back(pair<wregex,wstring>(r,fmt_str));
}
catch(...) {}
}
}
optional<vector<pair<wregex,wstring> > > rules;
if(!rules_.empty())
rules=rules_;
if(cp||rules)
{
say_command::voice_settings_type::mapped_type vs(cp,rules);
say_command::voice_settings[name]=vs;
}
}
}
}
parent_dir=parent_dir_;
festival_libdir_=parent_dir+"\\festival\\lib";
festival_libdir=festival_libdir_.c_str();
player.init();
tts_thread.reset(new thread(&Festival::tts_loop,this));
mutex::scoped_lock lock(info_mutex);
if(!tts_started)
tts_started_event.wait(lock);
initialized=true;
}
bool result;
{
mutex::scoped_lock lock(info_mutex);
result=tts_ready;
}
if(!result)
{
free();
throw runtime_error("cannot initialize Festival");
}
}

void Festival::free()
{
if(initialized&&(!freed))
{
tts_queue.stop();
stop();
tts_thread->join();
player.free();
freed=true;
}
}

void Festival::say_text(const wstring& text,const optional<int>& bookmark)
{
shared_ptr<say_command> cmd(new say_command(device,text,current_volume,bookmark));
stopped.connect(stop_event::slot_type(&say_command::cancel,cmd).track(cmd));
tts_queue.push(bind(&say_command::speak,cmd));
}

optional<string> Festival::change_voice_command::get_voice_1()
{
if(!festival_eval_command("(set! tmp_var (format nil current-voice))"))
return none;
LISP var;
var=siod_get_lval("tmp_var",NULL);
if(!STRINGP(var))
return none;
return string(get_c_string(var));
}

void Festival::change_voice_command::set_voice_1()
{
string cmd=(format("(voice.select \'%1%)") % name).str();
festival_eval_command(cmd.c_str());
}

void Festival::set_rate_1(int rate)
{
string cmd=(format("(set-voice-rate %1%)") % rate).str();
festival_eval_command(cmd.c_str());
}

int Festival::get_rate_1()
{
if(!festival_eval_command("(set! tmp_var (get-voice-rate))"))
return 50;
LISP var;
var=siod_get_lval("tmp_var",NULL);
if(NFLONUMP(var))
return 50;
return floor(get_c_double(var)+0.5);
}

void Festival::stop()
{
stopped();
player.stop();
}

void Festival::tts_loop()
{
bool initializing=true;
string log_file_name=parent_dir+"\\festival.log";
#ifdef REDIRECT_STDERR
_invalid_parameter_handler old_iph=_set_invalid_parameter_handler(invalid_parameter_handler);
FILE* stderr_file=NULL;
FILE old_stdout=*stdout;
stderr_file=freopen(log_file_name.c_str(),"wt",stderr);
if(stderr_file)
*stdout=*stderr_file;
#else
streambuf* cerr_buf=cerr.rdbuf();
ofstream log_file;
log_file.exceptions(0);
log_file.open(log_file_name.c_str());
if(log_file.is_open())
cerr.rdbuf(log_file.rdbuf());
#endif
streambuf* cout_buf=cout.rdbuf();
cout.rdbuf(cerr.rdbuf());
_se_translator_function old_se_translator=_set_se_translator(se_translator);
try
{
festival_initialize(1,FESTIVAL_HEAP_SIZE);
if(!festival_eval_command("(require 'festlib_helper)"))
throw runtime_error("cannot load festlib_helper");
if(festival_eval_command("(set! tmp_var (mapcar (lambda (x) (format nil x)) (voice-list)))"))
{
LISP l1;
l1=siod_get_lval("tmp_var",NULL);
EST_StrList l2;
siod_list_to_strlist(l1,l2);
EST_StrList::Entries it(l2);
while(it.has_more_elements())
{
mutex::scoped_lock lock(info_mutex);
voice_list.push_back(static_cast<const char*>(it.current()));
it.next();
}
}
change_voice_command::init();
set_rate_2(50);
initializing=false;
{
mutex::scoped_lock lock(info_mutex);
tts_started=true;
tts_ready=!voice_list.empty();
tts_started_event.notify_one();
}
while(optional<function<void()> > f_=tts_queue.get())
{
try
{
(*f_)();
}
catch(exception& e)
{
cerr << "caught exception: " << e.what() << "\n";
}
}
festival_tidy_up();
}
catch(exception& e)
{
cerr << "caught exception: " << e.what() << "\n";
if(initializing)
{
mutex::scoped_lock lock(info_mutex);
tts_started=true;
tts_ready=false;
tts_started_event.notify_one();
}
}
_set_se_translator(old_se_translator);
#ifdef REDIRECT_STDERR
if(stderr_file)
{
cerr.flush();
cout.flush();
fclose(stderr_file);
*stdout=old_stdout;
}
_set_invalid_parameter_handler(old_iph);
#else
if(log_file.is_open())
{
cerr.flush();
cout.flush();
log_file.close();
cerr.rdbuf(cerr_buf);
}
#endif
cout.rdbuf(cout_buf);
}

void Festival::change_voice_command::init()
{
optional<string> v=get_voice_1();
mutex::scoped_lock lock(info_mutex);
current_voice=v;
}

void Festival::set_volume(int volume)
{
if(volume<0)
{
current_volume=0;
return;
}
if(volume>100)
{
current_volume=100;
return;
}
current_volume=volume;
}

int Festival::get_volume()
{
return current_volume;
}

void Festival::change_voice_command::set_voice_2()
{
if(cancelled()||(name==get_voice_1()))
{
mutex::scoped_lock lock(info_mutex);
if(changing.lock()==shared_from_this())
changing.reset();
return;
}
set_voice_1();
get_tts().set_rate_2(50);
optional<string> voice=get_voice_1();
mutex::scoped_lock lock(info_mutex);
current_voice=voice;
if(changing.lock()==shared_from_this())
changing.reset();
}

void Festival::set_voice(const string& name)
{
{
mutex::scoped_lock lock(info_mutex);
if(find(voice_list.begin(),voice_list.end(),name)==voice_list.end())
throw runtime_error("voice not found");
}
shared_ptr<change_voice_command> cmd=change_voice_command::create(name);
tts_queue.push(bind(&Festival::change_voice_command::set_voice_2,cmd));
}

shared_ptr<Festival::change_voice_command> Festival::change_voice_command::create(const string& name)
{
shared_ptr<change_voice_command> cmd(new change_voice_command(name));
voice_changed();
voice_changed.connect(voice_change_event::slot_type(&change_voice_command::cancel,cmd).track(cmd));
mutex::scoped_lock lock(info_mutex);
changing=cmd;
return cmd;
}

optional<string> Festival::change_voice_command::get_voice()
{
optional<string> voice;
shared_ptr<change_voice_command> tmp;
{
mutex::scoped_lock lock(info_mutex);
tmp=changing.lock();
}
if(tmp&&(!tmp->cancelled()))
voice=tmp->name;
else
{
mutex::scoped_lock lock(info_mutex);
voice=current_voice;
}
return voice;
}

optional<string> Festival::get_voice()
{
return change_voice_command::get_voice();
}

void Festival::set_rate_2(int rate)
{
set_rate_1(rate);
int rate_=get_rate_1();
mutex::scoped_lock lock(info_mutex);
current_rate=rate_;
}

void Festival::set_rate(int rate)
{
int rate_=rate;
if(rate<0)
rate_=0;
if(rate>100)
rate_=100;
{
mutex::scoped_lock lock(info_mutex);
current_rate=rate_;
}
tts_queue.push(bind(&Festival::set_rate_2,this,rate_));
}

int Festival::get_rate()
{
int rate;
mutex::scoped_lock lock(info_mutex);
rate=current_rate;
return rate;
}

vector<string> Festival::get_voices()
{
mutex::scoped_lock lock(info_mutex);
vector<string> v=voice_list;
return v;
}

void Festival::command::cancel()
{
mutex::scoped_lock lock(cancel_mutex);
cancelled_=true;
}

bool Festival::command::cancelled()
{
mutex::scoped_lock lock(cancel_mutex);
return cancelled_;
}

void Festival::say_command::speak()
{
if(cancelled())
return;
int cp=CP_UTF8;
if(festival_eval_command("(set! tmp_var (format nil current-voice))"))
{
LISP v=siod_get_lval("tmp_var",NULL);
if(STRINGP(v))
{
const char* name=get_c_string(v);
voice_settings_type::const_iterator it=voice_settings.find(name);
if(it!=voice_settings.end())
{
if(it->second.first)
cp=*(it->second.first);
if(it->second.second)
{
vector<pair<wregex, wstring> > rules=*(it->second.second);
for(vector<pair<wregex, wstring> >::const_iterator it=rules.begin();it!=rules.end();++it)
{
text=regex_replace(text,it->first,it->second);
}
}
}
}
}
DWORD flags=0;
if(cp!=CP_UTF8)
{
flags=WC_COMPOSITECHECK|WC_SEPCHARS;
}
int n=WideCharToMultiByte(cp,flags,text.c_str(),-1,NULL,0,NULL,NULL);
if(n<=1)
return;
scoped_array<char> new_text(new char[n]);
n=WideCharToMultiByte(cp,flags,text.c_str(),-1,new_text.get(),n,NULL,NULL);
if(n==0)
return;
if(text.length()>150)
{
EST_String quoted_text(quote_string(new_text.get(),"\"","\\",1));
string cmd=(format("(set-synth-text %1%)") % static_cast<const char*>(quoted_text)).str();
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
EST_Wave* pwav=wave(v);;
pwav->rescale(volume/100.0);
Festival::player.play(device,*pwav,bookmark);
}
festival_eval_command("(clear-synth-text)");
}
else
{
EST_Wave wav;
if(!festival_text_to_wave(new_text.get(),wav))
{
//if(!cancelled())
//MessageBeep(-1);
return;
}
if(cancelled())
return;
wav.rescale(volume/100.0);
Festival::player.play(device,wav,bookmark);
}
}

Festival& get_tts()
{
static Festival tts;
return tts;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)
{
return TRUE;
}

int festlib_initialize(const wchar_t* wpath)
{
int size=WideCharToMultiByte(CP_ACP,0,wpath,-1,NULL,0,NULL,NULL);
if(size==0)
return -1;
scoped_array<char> path(new char[size]);
if(WideCharToMultiByte(CP_ACP,0,wpath,-1,path.get(),size,NULL,NULL)==0)
return -1;
int result=0;
try
{
get_tts().init(path.get());
}
catch(...)
{
result=-1;
}
return result;
}

void festlib_say_text(const wchar_t* text,int bookmark)
{
optional<int> b;
if(bookmark!=-1)
b=bookmark;
get_tts().say_text(text,b);
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
optional<int> bookmark=get_tts().get_bookmark();
if(bookmark)
return *bookmark;
else return -1;
}

void festlib_set_rate(int r)
{
get_tts().set_rate(r);
}

int festlib_get_rate()
{
return get_tts().get_rate();
}

int festlib_get_voice_count()
{
return get_tts().get_voices().size();
}

const char* festlib_get_voice_name(int index)
{
static string name;
vector<string> voices=get_tts().get_voices();
if(index>=voices.size())
return NULL;
name=voices[index];
return name.c_str();
}

void festlib_set_voice(const char* name)
{
get_tts().set_voice(name);
}

const char* festlib_get_current_voice_name()
{
static string name;
optional<string> current=get_tts().get_voice();
if(!current)
return NULL;
name=*current;
return name.c_str();
}