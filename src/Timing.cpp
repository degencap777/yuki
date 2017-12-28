/*
    This file is part of Yuki.
    Copyright (C) 2017 Guofeng Dai

    Yuki is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Yuki is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yuki.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ctime>
#include <cstdlib>

#include "config.h"
#include "Timing.h"


int Time::timediff (Time start, Time end) {
    int diff;

#ifdef GETTICKCOUNT
    diff = ((end.m_time-start.m_time)+5)/10;
#elif (defined(GETTIMEOFDAY))
    diff = ((end.m_time.tv_sec-start.m_time.tv_sec)*100
           + (end.m_time.tv_usec-start.m_time.tv_usec)/10000);
#else
    diff = (100*(int) difftime (end.m_time, start.m_time));
#endif

    return (abs(diff));
}

Time::Time(void) {

#if defined (GETTICKCOUNT)
    m_time = (int)(GetTickCount());
#elif defined(GETTIMEOFDAY)
    struct timeval tmp;
    gettimeofday(&tmp, NULL);
    m_time = tmp;
#else
    m_time = time (0);
#endif

}
