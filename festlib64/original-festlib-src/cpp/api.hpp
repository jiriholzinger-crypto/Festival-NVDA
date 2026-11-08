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

#ifndef FESTLIBAPI_INCLUDED
#define FESTLIBAPI_INCLUDED
#ifdef FESTLIBAPI
#else
#define FESTLIBAPI extern "C" __declspec(dllimport)
#endif
FESTLIBAPI int festlib_initialize(const wchar_t*);
FESTLIBAPI void festlib_say_text(const wchar_t*,int);
FESTLIBAPI void festlib_stop();
FESTLIBAPI void festlib_set_audio_device(unsigned int);
FESTLIBAPI void festlib_set_volume(int);
FESTLIBAPI int festlib_get_volume();
FESTLIBAPI int festlib_get_bookmark();
FESTLIBAPI void festlib_set_rate(int);
FESTLIBAPI int festlib_get_rate();
FESTLIBAPI int festlib_get_voice_count();
FESTLIBAPI const char* festlib_get_voice_name(int);
FESTLIBAPI const char* festlib_get_current_voice_name();
FESTLIBAPI void festlib_set_voice(const char*);
#endif