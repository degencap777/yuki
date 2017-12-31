#!/usr/bin/env python3
#
#    This file is part of Yuki.
#    Copyright (C) 2017 Guofeng Dai
#
#    Yuki is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Yuki is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with Yuki.  If not, see <http://www.gnu.org/licenses/>.

import sys
import glob
import gzip
import random
import math
import multiprocessing as mp
import tensorflow as tf
from tfprocess import TFProcess

# 2 planes, 1 stm, 1 x 65 probs, 1 winner = 5 lines
DATA_ITEM_LINES = 2 + 1 + 1 + 1

BATCH_SIZE = 256

def remap_vertex(vertex, symmetry):
    """
        Remap a go board coordinate according to a symmetry.
    """
    assert vertex >= 0 and vertex < 64
    x = vertex % 8
    y = vertex // 8
    if symmetry >= 4:
        x, y = y, x
        symmetry -= 4
    if symmetry == 1 or symmetry == 3:
        x = 8 - x - 1
    if symmetry == 2 or symmetry == 3:
        y = 8 - y - 1
    return y * 8 + x

def apply_symmetry(plane, symmetry):
    """
        Applies one of 8 symmetries to the go board.

        The supplied go board can have 64 or 65 elements. The 65th
        element is pass will which get the identity mapping.
    """
    assert symmetry >= 0 and symmetry < 8
    work_plane = [0.0] * 64
    for vertex in range(0, 64):
        work_plane[vertex] = plane[remap_vertex(vertex, symmetry)]
    # Map back "pass"
    if len(plane) == 65:
        work_plane.append(plane[64])
    return work_plane

def convert_train_data(text_item):
    """
        Convert textual training data to python lists.

        Converts a set of 5 lines of text into a pythonic dataformat.
        [[plane_1],[plane_2]]
        [probabilities],...
        winner,...
    """
    planes = []
    for plane in range(0, 2):
        # 64 first bits are 16 hex chars
        hex_string = text_item[plane][0:16]
        integer = int(hex_string, 16)
        as_str = format(integer, '0>64b')
        
        assert len(as_str) == 64
        plane = [0.0 if digit == "0" else 1.0 for digit in as_str]
        planes.append(plane)
    stm = text_item[2][0]
    assert stm == "0" or stm == "1"
    if stm == "0":
        planes.append([1.0] * 361)
        planes.append([0.0] * 361)
    else:
        planes.append([0.0] * 361)
        planes.append([1.0] * 361)
    assert len(planes) == 4
    probabilities = []
    for val in text_item[3].split():
        float_val = float(val)
        # Work around a bug in leela-zero v0.3
        if math.isnan(float_val):
            return False, None
        probabilities.append(float_val)
    assert len(probabilities) == 65
    winner = float(text_item[4])
    assert winner == 1.0 or winner == -1.0
    # Get one of 8 symmetries
    symmetry = random.randrange(8)
    sym_planes = [apply_symmetry(plane, symmetry) for plane in planes]
    sym_probabilities = apply_symmetry(probabilities, symmetry)
    return True, (sym_planes, sym_probabilities, [winner])

class ChunkParser:
    def __init__(self, chunks):
        self.queue = mp.Queue(4096)
        # Start worker processes, leave 1 for TensorFlow
        workers = max(1, mp.cpu_count() - 1)
        print("Using {} worker processes.".format(workers))
        for _ in range(workers):
            mp.Process(target=self.task,
                       args=(chunks, self.queue)).start()

    def task(self, chunks, queue):
        while True:
            random.shuffle(chunks)
            for chunk in chunks:
                with gzip.open(chunk, 'r') as chunk_file:
                    file_content = chunk_file.readlines()
                    item_count = len(file_content) // DATA_ITEM_LINES
                    for item_idx in range(item_count):
                        pick_offset = item_idx * DATA_ITEM_LINES
                        item = file_content[pick_offset:pick_offset + DATA_ITEM_LINES]
                        str_items = [str(line, 'ascii') for line in item]
                        success, data = convert_train_data(str_items)
                        if success:
                            queue.put(data)

    def parse_chunk(self):
        while True:
            yield self.queue.get()

def get_chunks(data_prefix):
    return glob.glob(data_prefix + "*.gz")

def main(args):
    train_data_prefix = args.pop(0)

    chunks = get_chunks(train_data_prefix)
    print("Found {0} chunks".format(len(chunks)))

    if not chunks:
        return

    parser = ChunkParser(chunks)

    dataset = tf.data.Dataset.from_generator(
        parser.parse_chunk, output_types=(tf.float32, tf.float32, tf.float32))
    dataset = dataset.shuffle(65536)
    dataset = dataset.batch(BATCH_SIZE)
    dataset = dataset.prefetch(16)
    iterator = dataset.make_one_shot_iterator()
    next_batch = iterator.get_next()

    tfprocess = TFProcess(next_batch)
    if args:
        restore_file = args.pop(0)
        tfprocess.restore(restore_file)
    while True:
        tfprocess.process(BATCH_SIZE)

if __name__ == "__main__":
    main(sys.argv[1:])
    mp.freeze_support()
