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

#ifndef PLAYER_HPP
#define PLAYER_HPP
#include <windows.h>
#include <string>
#include <boost/utility.hpp>
#include <boost/thread.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/optional.hpp>
#include <boost/signal.hpp>
#include <EST_Wave.h>
#include "messagequeue.hpp"

class Player:private boost::noncopyable
{
public:
void play(unsigned int,const EST_Wave&,const boost::optional<int>&);
void init();
void free();
void stop();
void pause();
void resume();
typedef boost::signal<void()> stop_event;
stop_event stopped;
boost::optional<int> get_bookmark();
private:

boost::optional<int> bookmark;
boost::mutex bookmark_mutex;

class AudioData:private boost::noncopyable
{
public:
AudioData(unsigned int,const EST_Wave&,const boost::optional<int>&,HANDLE);
void play();
void pause();
void resume();
void stop();
bool started();
boost::optional<int> bookmark;
void wait(bool);
bool wait();
bool use_same_device(const AudioData&);
void cancel();
static void clear()
{
CloseHandle(event_handle);
event_handle=NULL;
}
private:
bool init();
void free(bool);
void internal_wait();
bool cancelled();
bool started_;
bool cancelled_;
static HANDLE event_handle;
static HWAVEOUT device_handle;
boost::scoped_ptr<WAVEHDR> header;
WAVEFORMATEX format;
std::string data;
unsigned int device_id;
static boost::mutex device_mutex;
boost::mutex cancel_mutex;
boost::mutex start_mutex;
HANDLE queue_event_handle;
};

void play_loop();
boost::scoped_ptr<boost::thread> audio_thread;
boost::optional<boost::shared_ptr<AudioData> > current_audio;
boost::mutex audio_mutex;
MessageQueue<boost::shared_ptr<AudioData> > audio_queue;
};

#endif