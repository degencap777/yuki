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

#include <time.h>
#include <limits.h>
#include <thread>
#include "config.h"

#include "Random.h"
#include "Utils.h"
#include "GTP.h"

Random& Random::get_Rng(void) {
    static thread_local Random s_rng{0};
    return s_rng;
}

Random::Random(uint64 seed) {
    if (seed == 0) {
        size_t thread_id =
            std::hash<std::thread::id>()(std::this_thread::get_id());
        seedrandom(cfg_rng_seed ^ (uint64)thread_id);
    } else {
        seedrandom(seed);
    }
}

// This is xoroshiro128+.
// Note that the last bit isn't entirely random, so don't use it,
// if possible.
uint64 Random::gen(void) {
    const uint64 s0 = m_s[0];
    uint64 s1 = m_s[1];
    const uint64 result = s0 + s1;

    s1 ^= s0;
    m_s[0] = Utils::rotl(s0, 55) ^ s1 ^ (s1 << 14);
    m_s[1] = Utils::rotl(s1, 36);

    return result;
}

uint16 Random::randuint16(const uint16 max) {
    return ((gen() >> 48) * max) >> 16;
}

uint32 Random::randuint32(const uint32 max) {
    return ((gen() >> 32) * (uint64)max) >> 32;
}

uint32 Random::randuint32() {
    return gen() >> 32;
}

void Random::seedrandom(uint64 seed) {
    // Magic values from Pierre L'Ecuyer,
    // "Tables of Linear Congruental Generators of different sizes and
    // good lattice structure"
    m_s[0] = (1181783497276652981ULL * seed);
    m_s[1] = (1181783497276652981ULL * m_s[0]);
}

float Random::randflt(void) {
    // We need a 23 bit mantissa + implicit 1 bit = 24 bit number
    // starting from a 64 bit random.
    constexpr float umax = 1.0f / (UINT32_C(1) << 24);
    uint32 num = gen() >> 40;
    return ((float)num) * umax;
}
