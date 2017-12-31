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

#ifndef KOSTATE_H_INCLUDED
#define KOSTATE_H_INCLUDED

#include <vector>

#include "FastState.h"
#include "FullBoard.h"

class KoState : public FastState {
public:
    void init_game(int size);
    void reset_game();

    void play_pass(void);
    void play_move(int color, int vertex);
    void play_move(int vertex);

private:
    std::vector<uint64> m_hash_history;
};

#endif
