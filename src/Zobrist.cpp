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

#include "config.h"

#include "Random.h"
#include "Zobrist.h"

std::array<std::array<uint64, FastBoard::MAXSQ>,     4> Zobrist::zobrist;
std::array<uint64, 5>                                   Zobrist::zobrist_pass;

void Zobrist::init_zobrist(Random & rng) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < FastBoard::MAXSQ; j++) {
			//用两个32位合成64位随机数
            Zobrist::zobrist[i][j]  = ((uint64)rng.randuint32()) << 32;
            Zobrist::zobrist[i][j] ^= (uint64)rng.randuint32();
        }
    }

    for (int i = 0; i < 5; i++) {
        Zobrist::zobrist_pass[i]  = ((uint64)rng.randuint32()) << 32;
        Zobrist::zobrist_pass[i] ^= (uint64)rng.randuint32();
    }
}
