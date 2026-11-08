//Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>
//64-bit port and pitch support 2026 Jiří Holzinger

//This program is free software:
//you can redistribute it and/or modify it under the terms of
//the GNU General Public License as published by the Free Software Foundation,
//either version 2 of the License, or (at your option) any later version.
//This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//See the GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef FESTLIBAPI_INCLUDED
#define FESTLIBAPI_INCLUDED
#ifndef FESTLIBAPI
#define FESTLIBAPI extern "C" __declspec(dllimport)
#endif

// --- original API (unchanged signatures) ---
FESTLIBAPI int festlib_initialize(const wchar_t* path);       // 0 = OK, -1 = error
FESTLIBAPI void festlib_say_text(const wchar_t* text,int bookmark); // bookmark -1 = none
FESTLIBAPI void festlib_stop();
FESTLIBAPI void festlib_set_audio_device(unsigned int device); // waveOut id, 0xFFFFFFFF = WAVE_MAPPER
FESTLIBAPI void festlib_set_volume(int volume);  // 0..100
FESTLIBAPI int festlib_get_volume();
FESTLIBAPI int festlib_get_bookmark();           // -1 = none
FESTLIBAPI void festlib_set_rate(int rate);      // 0..100, 50 = normal
FESTLIBAPI int festlib_get_rate();
FESTLIBAPI int festlib_get_voice_count();
FESTLIBAPI const char* festlib_get_voice_name(int index);
FESTLIBAPI const char* festlib_get_current_voice_name();
FESTLIBAPI void festlib_set_voice(const char* name);

// --- new in the 64-bit version ---
FESTLIBAPI void festlib_set_pitch(int pitch);    // 0..100, 50 = voice default, 0/100 = one octave down/up
FESTLIBAPI int festlib_get_pitch();
FESTLIBAPI int festlib_is_speaking();            // 1 while text is queued, synthesized or played
FESTLIBAPI void festlib_terminate();             // stop speech and close the audio device
FESTLIBAPI int festlib_get_version();            // 0x00020100 = 2.1

// --- new in 2.1: let the caller play the audio ---
// When a callback is set, festlib does not open any audio device. The callback is
// called on the synthesis thread for every piece of audio (16 bit PCM, interleaved)
// with the bookmark given to festlib_say_text, and once more with samples=NULL and
// num_frames=0 when that text is finished (also if it produced no audio).
// Return 0 to continue, non-zero to abort the rest of that text.
// festlib_stop() drops queued texts; no end marker is sent for cancelled texts.
// Pass NULL to go back to the built-in waveOut player.
typedef int (__cdecl *festlib_audio_callback)(const short* samples,int num_frames,int channels,int sample_rate,int bookmark);
FESTLIBAPI void festlib_set_audio_callback(festlib_audio_callback cb);
#endif
