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

#ifndef MESSAGEQUEUE_HPP
#define MESSAGEQUEUE_HPP
#include <deque>
#include <boost/utility.hpp>
#include <boost/thread.hpp>
#include <boost/optional.hpp>
#include <windows.h>

template<class T> class MessageQueue:private boost::noncopyable
{
public:
MessageQueue():stopped(false),
event_handle(CreateEvent(NULL,TRUE,FALSE,NULL))
{}
~MessageQueue()
{
CloseHandle(event_handle);
}
const HANDLE event_handle;
void push(const T&);
boost::optional<T> get();
void clear();
bool empty();
void stop();
private:
boost::mutex guard;
boost::condition_variable queue_not_empty;
std::deque<T> messages;
bool stopped;
};

template<class T> void MessageQueue<T>::push(const T& message)
{
boost::mutex::scoped_lock lock(guard);
if(stopped) return;
bool was_empty=messages.empty();
messages.push_back(message);
if(was_empty)
{
queue_not_empty.notify_one();
SetEvent(event_handle);
}
}

template<class T> void MessageQueue<T>::stop()
{
boost::mutex::scoped_lock lock(guard);
stopped=true;
queue_not_empty.notify_one();
ResetEvent(event_handle);
}

template<class T> boost::optional<T> MessageQueue<T>::get()
{
boost::mutex::scoped_lock lock(guard);
if(stopped)
return boost::optional<T>();
if(messages.empty()) queue_not_empty.wait(lock);
if(stopped)
return boost::optional<T>();
T message=messages.front();
messages.pop_front();
if(messages.empty())
ResetEvent(event_handle);
return boost::optional<T>(message);
}

template<class T> void MessageQueue<T>::clear()
{
boost::mutex::scoped_lock lock(guard);
messages.clear();
ResetEvent(event_handle);
}

template<class T> bool MessageQueue<T>::empty()
{
boost::mutex::scoped_lock lock(guard);
return messages.empty();
}
#endif