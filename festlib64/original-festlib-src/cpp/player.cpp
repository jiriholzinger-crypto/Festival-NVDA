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

#include "player.hpp"

using boost::shared_ptr;
using boost::mutex;
using boost::optional;
using boost::none;
using boost::thread;

HANDLE Player::AudioData::event_handle(NULL);

HWAVEOUT Player::AudioData::device_handle(NULL);
mutex Player::AudioData::device_mutex;

Player::AudioData::AudioData(unsigned int device_id_,const EST_Wave& wav,const boost::optional<int>& bookmark_,HANDLE handle):
data(reinterpret_cast<const char*>(wav.values().memory()),2*wav.num_samples()*wav.num_channels()),
header(new WAVEHDR),
device_id(device_id_),
bookmark(bookmark_),
cancelled_(false),
started_(false),
queue_event_handle(handle)
{
format.wFormatTag=WAVE_FORMAT_PCM;
format.nChannels=wav.num_channels();
format.wBitsPerSample=16;
format.nSamplesPerSec=wav.sample_rate();
format.nBlockAlign=format.nChannels*2;
format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
format.cbSize=0;
header->lpData=const_cast<char*>(data.c_str());
header->dwBufferLength=data.length();
header->dwFlags=0;
}

void Player::AudioData::cancel()
{
mutex::scoped_lock lock(cancel_mutex);
cancelled_=true;
}

bool Player::AudioData::cancelled()
{
mutex::scoped_lock lock(cancel_mutex);
return cancelled_;
}

bool Player::AudioData::use_same_device(const Player::AudioData& other)
{
return ((device_id==other.device_id)&&
(format.nChannels==other.format.nChannels)&&
(format.nSamplesPerSec==other.format.nSamplesPerSec));
}

bool Player::AudioData::started()
{
return started_;
}

bool Player::AudioData::init()
{
if(!event_handle)
event_handle=CreateEvent(NULL,FALSE,FALSE,NULL);
mutex::scoped_lock lock(device_mutex);
if(!device_handle)
{
if(waveOutOpen(&device_handle,device_id,&format,reinterpret_cast<DWORD_PTR>(event_handle),NULL,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
return false;
}
if(waveOutPrepareHeader(device_handle,header.get(),sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
{
waveOutClose(device_handle);
device_handle=NULL;
return false;
}
return true;
}

void Player::AudioData::free(bool close_device)
{
mutex::scoped_lock lock(device_mutex);
if(device_handle)
{
if(header->dwFlags&WHDR_PREPARED)
{
waveOutUnprepareHeader(device_handle,header.get(),sizeof(WAVEHDR));
}
if(close_device)
{
waveOutClose(device_handle);
device_handle=NULL;
}
}
}

void Player::AudioData::internal_wait()
{
while(!(header->dwFlags&WHDR_DONE))
{
WaitForSingleObject(event_handle,INFINITE);
}
}

bool Player::AudioData::wait()
{
if(!started())
return true;
HANDLE handles[]={queue_event_handle,event_handle};
while(!(header->dwFlags&WHDR_DONE))
{
DWORD result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
if(result==WAIT_OBJECT_0)
return false;
}
free(true);
return true;
}

void Player::AudioData::play()
{
if(cancelled())
return;
if(!init())
return;
{
mutex::scoped_lock lock(device_mutex);
started_=(waveOutWrite(device_handle,header.get(),sizeof(WAVEHDR))==MMSYSERR_NOERROR);
}
if(!started_)
free(true);
}

void Player::AudioData::stop()
{
mutex::scoped_lock lock(device_mutex);
if(device_handle)
waveOutReset(device_handle);
}

void Player::AudioData::pause()
{
mutex::scoped_lock lock(device_mutex);
if(device_handle)
waveOutPause(device_handle);
}

void Player::AudioData::resume()
{
mutex::scoped_lock lock(device_mutex);
if(device_handle)
waveOutRestart(device_handle);
}

void Player::AudioData::wait(bool close_device)
{
if(started_)
{
internal_wait();
}
free(close_device);
}

void Player::play(unsigned int device_id,const EST_Wave& wav,const optional<int>& bookmark)
{
shared_ptr<AudioData> audio(new AudioData(device_id,wav,bookmark,audio_queue.event_handle));
stopped.connect(stop_event::slot_type(&AudioData::cancel,audio).track(audio));
audio_queue.push(audio);
}

void Player::play_loop()
{
while(optional<shared_ptr<AudioData> > audio_=audio_queue.get())
{
shared_ptr<AudioData> audio=*audio_;
if(current_audio)
{
if(audio->use_same_device(**current_audio))
{
audio->play();
(*current_audio)->wait(false);
}
else
{
(*current_audio)->wait(true);
audio->play();
}
}
else
{
audio->play();
}
if(audio->started())
{
{
mutex::scoped_lock lock(audio_mutex);
current_audio=audio;
}
{
mutex::scoped_lock lock(bookmark_mutex);
bookmark=(*current_audio)->bookmark;
}
(*current_audio)->wait();
}
else
{
{
mutex::scoped_lock lock(audio_mutex);
current_audio=none;
}
{
mutex::scoped_lock lock(bookmark_mutex);
bookmark=none;
}
}
}
if(current_audio)
{
(*current_audio)->stop();
(*current_audio)->wait(true);
{
mutex::scoped_lock lock(audio_mutex);
current_audio=none;
}
{
mutex::scoped_lock lock(bookmark_mutex);
bookmark=none;
}
}
}

void Player::stop()
{
stopped();
mutex::scoped_lock lock(audio_mutex);
if(current_audio)
(*current_audio)->stop();
}

void Player::pause()
{
mutex::scoped_lock lock(audio_mutex);
if(current_audio)
(*current_audio)->pause();
}

void Player::resume()
{
mutex::scoped_lock lock(audio_mutex);
if(current_audio)
(*current_audio)->resume();
}

void Player::init()
{
audio_thread.reset(new thread(&Player::play_loop,this));
}

void Player::free()
{
audio_queue.stop();
stop();
audio_thread->join();
AudioData::clear();
}

optional<int> Player::get_bookmark()
{
optional<int> result;
mutex::scoped_lock lock(bookmark_mutex);
result=bookmark;
return result;
}