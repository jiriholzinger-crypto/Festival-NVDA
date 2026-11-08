//Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>
//64-bit port 2026 Jiří Holzinger: rewritten with the C++ standard library
//(no Boost), gapless double buffering, generation-based cancelling.

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
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Player
{
public:
Player();
~Player();
Player(const Player&)=delete;
Player& operator=(const Player&)=delete;

void init();
// samples: 16 bit PCM interleaved; generation: value of current_generation() when the text was queued
void play(unsigned int device,const short* samples,size_t num_frames,int channels,int sample_rate,int bookmark,unsigned long generation);
void stop();            // drop everything queued and stop the device
void close_device();    // stop and release the waveOut device
unsigned long current_generation() const {return generation.load();}
int get_bookmark();     // -1 = none
bool busy();            // something queued or playing

private:
struct Chunk
{
std::vector<short> data;
WAVEFORMATEX format;
unsigned int device;
int bookmark;
unsigned long generation;
WAVEHDR header;
bool written;
};

void loop();
bool open_device(const Chunk&);
void close_device_locked();
void submit(std::unique_ptr<Chunk>);
void reap(bool wait_all);

std::thread thread;
std::mutex queue_mutex;
std::deque<std::unique_ptr<Chunk> > queue;
std::deque<std::unique_ptr<Chunk> > inflight; // used by the audio thread only
std::atomic<unsigned long> generation;
std::atomic<int> bookmark;
std::atomic<int> active; // queued + playing chunks (for busy())
std::atomic<bool> close_requested;

std::mutex device_mutex;
HWAVEOUT device_handle;
WAVEFORMATEX device_format;
unsigned int device_id;

HANDLE wave_event;   // signalled by waveOut when a buffer is done
HANDLE queue_event;  // signalled when the queue changes
bool started;
};

#endif
