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

#include <assert.h>
#include <cctype>
#include <string>
#include <sstream>
#include <algorithm>
#include <array>

#include "config.h"

#include "KoState.h"
#include "GameState.h"
#include "FullBoard.h"
#include "UCTSearch.h"
#include "Zobrist.h"
#include "Random.h"
#include "Utils.h"

void GameState::init_game(int size) {
    KoState::init_game(size);

    game_history.clear();
    game_history.emplace_back(std::make_shared<KoState>(*this));

    m_timecontrol.set_boardsize(board.get_boardsize());
    m_timecontrol.reset_clocks();

    return;
};

void GameState::reset_game() {
    KoState::reset_game();

    game_history.clear();
    game_history.emplace_back(std::make_shared<KoState>(*this));

    m_timecontrol.reset_clocks();
}

bool GameState::forward_move(void) {
    if (game_history.size() > m_movenum + 1) {
        m_movenum++;
        *(static_cast<KoState*>(this)) = *game_history[m_movenum];
        return true;
    } else {
        return false;
    }
}

bool GameState::undo_move(void) {
    if (m_movenum > 0) {
        m_movenum--;

        // don't actually delete it!
        //game_history.pop_back();

        // this is not so nice, but it should work
        *(static_cast<KoState*>(this)) = *game_history[m_movenum];

        // This also restores hashes as they're part of state
        return true;
    } else {
        return false;
    }
}

void GameState::rewind(void) {
    *(static_cast<KoState*>(this)) = *game_history[0];
    m_movenum = 0;
}

void GameState::play_move(int vertex) {
    play_move(board.get_to_move(), vertex);
}

void GameState::play_pass() {
    play_move(get_to_move(), FastBoard::PASS);
}

void GameState::play_move(int color, int vertex) {
    if (vertex != FastBoard::PASS && vertex != FastBoard::RESIGN) {
        KoState::play_move(color, vertex);
    } else {
        KoState::play_pass();
        if (vertex == FastBoard::RESIGN) {
            std::rotate(rbegin(m_lastmove), rbegin(m_lastmove) + 1,
                        rend(m_lastmove));
            m_lastmove[0] = vertex;
            m_last_was_capture = false;
        }
    }

    // cut off any leftover moves from navigating
    game_history.resize(m_movenum);
    game_history.emplace_back(std::make_shared<KoState>(*this));
}

bool GameState::play_textmove(std::string color, std::string vertex) {
    int who;
    int column, row;
    int boardsize = board.get_boardsize();

    if (color == "w" || color == "white") {
        who = FullBoard::WHITE;
    } else if (color == "b" || color == "black") {
        who = FullBoard::BLACK;
    } else return false;

    if (vertex.size() < 2) return 0;
    if (!std::isalpha(vertex[0])) return 0;
    if (!std::isdigit(vertex[1])) return 0;
    if (vertex[0] == 'i') return 0;

    if (vertex[0] >= 'A' && vertex[0] <= 'Z') {
        if (vertex[0] < 'I') {
            column = 25 + vertex[0] - 'A';
        } else {
            column = 25 + (vertex[0] - 'A')-1;
        }
    } else {
        if (vertex[0] < 'i') {
            column = vertex[0] - 'a';
        } else {
            column = (vertex[0] - 'a')-1;
        }
    }

    std::string rowstring(vertex);
    rowstring.erase(0, 1);
    std::istringstream parsestream(rowstring);

    parsestream >> row;
    row--;

    if (row >= boardsize) return false;
    if (column >= boardsize) return false;

    int move = board.get_vertex(column, row);

    play_move(who, move);

    return true;
}

void GameState::stop_clock(int color) {
    m_timecontrol.stop(color);
}

void GameState::start_clock(int color) {
    m_timecontrol.start(color);
}

void GameState::display_state() {
    FastState::display_state();

    m_timecontrol.display_times();
}

TimeControl& GameState::get_timecontrol() {
    return m_timecontrol;
}

void GameState::set_timecontrol(int maintime, int byotime,
                                int byostones, int byoperiods) {
    TimeControl timecontrol(board.get_boardsize(), maintime, byotime,
                            byostones, byoperiods);

    m_timecontrol = timecontrol;
}

void GameState::set_timecontrol(TimeControl tmc) {
    m_timecontrol = tmc;
}

void GameState::adjust_time(int color, int time, int stones) {
    m_timecontrol.adjust_time(color, time, stones);
}

void GameState::anchor_game_history(void) {
    m_movenum = 0;
    game_history.clear();
    game_history.emplace_back(std::make_shared<KoState>(*this));
}

void GameState::trim_game_history(int lastmove) {
    m_movenum = lastmove - 1;
    game_history.resize(lastmove);
}
