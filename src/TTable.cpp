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

#include <vector>

#include "Utils.h"
#include "TTable.h"

TTable* TTable::get_TT(void) {
    static TTable s_ttable;
    return &s_ttable;
}

TTable::TTable(int size) {
    LOCK(m_mutex, lock);
    m_buckets.resize(size);
}

void TTable::update(uint64 hash, const UCTNode * node) {
    LOCK(m_mutex, lock);

    unsigned int index = (unsigned int)hash;
    index %= m_buckets.size();

    /*
        update TT
    */
    m_buckets[index].m_hash       = hash;
    m_buckets[index].m_visits     = node->get_visits();
    m_buckets[index].m_eval_sum   = node->get_blackevals();
}

void TTable::sync(uint64 hash, UCTNode * node) {
    LOCK(m_mutex, lock);

    unsigned int index = (unsigned int)hash;
    index %= m_buckets.size();

    /*
        check for hash fail
    */
    if (m_buckets[index].m_hash != hash) {
        return;
    }

    /*
        valid entry in TT should have more info than tree
    */
    if (m_buckets[index].m_visits > node->get_visits()) {
        /*
            entry in TT has more info (new node)
        */
        node->set_visits(m_buckets[index].m_visits);
        node->set_blackevals(m_buckets[index].m_eval_sum);
    }
}
