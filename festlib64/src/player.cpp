//Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>
//64-bit port 2026 Jiří Holzinger

//This program is free software:
//you can redistribute it and/or modify it under the terms of
//the GNU General Public License as published by the Free Software Foundation,
//either version 2 of the License, or (at your option) any later version.
//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with this program. If not, see <http://www.gnu.org/licenses/>.

#include "player.hpp"
#include <cstring>

namespace
{
const size_t max_inflight=3;
}

Player::Player():
generation(0),
bookmark(-1),
active(0),
close_requested(false),
device_handle(NULL),
device_id(WAVE_MAPPER),
wave_event(CreateEventW(NULL,FALSE,FALSE,NULL)),
queue_event(CreateEventW(NULL,FALSE,FALSE,NULL)),
started(false)
{
std::memset(&device_format,0,sizeof(device_format));
}

Player::~Player()
{
// The player lives for the whole life of the process (see festlib.cpp),
// the thread is never joined from DllMain.
if(thread.joinable())
thread.detach();
}

void Player::init()
{
if(started)
return;
started=true;
thread=std::thread(&Player::loop,this);
}

void Player::play(unsigned int device,const short* samples,size_t num_frames,int channels,int sample_rate,int bookmark_,unsigned long gen)
{
if(gen!=generation.load()||num_frames==0||channels<=0||sample_rate<=0)
return;
std::unique_ptr<Chunk> c(new Chunk);
c->data.assign(samples,samples+num_frames*static_cast<size_t>(channels));
std::memset(&c->format,0,sizeof(c->format));
c->format.wFormatTag=WAVE_FORMAT_PCM;
c->format.nChannels=static_cast<WORD>(channels);
c->format.wBitsPerSample=16;
c->format.nSamplesPerSec=static_cast<DWORD>(sample_rate);
c->format.nBlockAlign=static_cast<WORD>(channels*2);
c->format.nAvgBytesPerSec=c->format.nSamplesPerSec*c->format.nBlockAlign;
c->format.cbSize=0;
c->device=device;
c->bookmark=bookmark_;
c->generation=gen;
std::memset(&c->header,0,sizeof(c->header));
c->written=false;
{
std::lock_guard<std::mutex> lock(queue_mutex);
if(gen!=generation.load())
return;
queue.push_back(std::move(c));
++active;
}
SetEvent(queue_event);
}

void Player::stop()
{
{
std::lock_guard<std::mutex> lock(queue_mutex);
++generation;
active-=static_cast<int>(queue.size());
queue.clear();
}
{
std::lock_guard<std::mutex> lock(device_mutex);
if(device_handle)
waveOutReset(device_handle);
}
SetEvent(queue_event);
}

void Player::close_device()
{
close_requested=true;
stop();
}

int Player::get_bookmark()
{
return bookmark.load();
}

bool Player::busy()
{
return active.load()>0;
}

bool Player::open_device(const Chunk& c)
{
std::lock_guard<std::mutex> lock(device_mutex);
if(device_handle)
return true;
HWAVEOUT h=NULL;
WAVEFORMATEX fmt=c.format;
if(waveOutOpen(&h,c.device,&fmt,reinterpret_cast<DWORD_PTR>(wave_event),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
{
// fall back to the default device if the selected one disappeared
if(c.device==WAVE_MAPPER||waveOutOpen(&h,WAVE_MAPPER,&fmt,reinterpret_cast<DWORD_PTR>(wave_event),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
return false;
}
device_handle=h;
device_format=c.format;
device_id=c.device;
return true;
}

void Player::close_device_locked()
{
std::lock_guard<std::mutex> lock(device_mutex);
if(device_handle)
{
waveOutReset(device_handle);
waveOutClose(device_handle);
device_handle=NULL;
}
}

void Player::reap(bool wait_all)
{
while(!inflight.empty())
{
Chunk& f=*inflight.front();
if(f.written&&!(f.header.dwFlags&WHDR_DONE))
{
if(!wait_all)
break;
WaitForSingleObject(wave_event,100);
continue;
}
{
std::lock_guard<std::mutex> lock(device_mutex);
if(device_handle&&(f.header.dwFlags&WHDR_PREPARED))
waveOutUnprepareHeader(device_handle,&f.header,sizeof(WAVEHDR));
}
inflight.pop_front();
--active;
}
}

void Player::submit(std::unique_ptr<Chunk> c)
{
struct drop_guard
{
std::atomic<int>& a;
bool keep;
~drop_guard() {if(!keep) --a;}
} guard{active,false};
bool same_format;
{
std::lock_guard<std::mutex> lock(device_mutex);
same_format=device_handle&&(device_id==c->device)&&
(device_format.nChannels==c->format.nChannels)&&
(device_format.nSamplesPerSec==c->format.nSamplesPerSec);
if(!device_handle)
same_format=true;
}
if(!same_format)
{
reap(true);
close_device_locked();
}
if(!open_device(*c))
return;
c->header.lpData=reinterpret_cast<LPSTR>(c->data.data());
c->header.dwBufferLength=static_cast<DWORD>(c->data.size()*sizeof(short));
c->header.dwFlags=0;
{
std::lock_guard<std::mutex> lock(device_mutex);
if(c->generation!=generation.load())
return;
if(waveOutPrepareHeader(device_handle,&c->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
return;
if(waveOutWrite(device_handle,&c->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
{
waveOutUnprepareHeader(device_handle,&c->header,sizeof(WAVEHDR));
return;
}
c->written=true;
}
inflight.push_back(std::move(c));
guard.keep=true;
}

void Player::loop()
{
for(;;)
{
for(;;)
{
std::unique_ptr<Chunk> c;
{
std::lock_guard<std::mutex> lock(queue_mutex);
if(queue.empty()||inflight.size()>=max_inflight)
break;
c=std::move(queue.front());
queue.pop_front();
}
if(c->generation!=generation.load())
{
--active;
continue;
}
submit(std::move(c));
}
if(close_requested.exchange(false))
{
reap(true);
close_device_locked();
}
reap(false);
if(!inflight.empty())
bookmark=inflight.front()->bookmark;
bool queue_empty;
{
std::lock_guard<std::mutex> lock(queue_mutex);
queue_empty=queue.empty();
}
if(inflight.empty())
{
if(queue_empty)
WaitForSingleObject(queue_event,INFINITE);
}
else if(queue_empty||inflight.size()>=max_inflight)
{
HANDLE handles[2]={wave_event,queue_event};
WaitForMultipleObjects(2,handles,FALSE,INFINITE);
}
}
}
