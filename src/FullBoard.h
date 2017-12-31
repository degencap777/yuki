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

#ifndef FULLBOARD_H_INCLUDED
#define FULLBOARD_H_INCLUDED

#include "config.h"
#include "FastBoard.h"

class FullBoard : public FastBoard {
public:
    int remove_string(int i);
    int update_board(const int color, const int i, bool & capture);

    uint64 calc_hash(void);
    uint64 calc_ko_hash(void);
    uint64 get_hash(void) const;
    uint64 get_ko_hash(void) const;

    void reset_board(int size);
    void display_board(int lastmove = -1);

    uint64 m_hash;
    uint64 m_ko_hash;
};

#endif
