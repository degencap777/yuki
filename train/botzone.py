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


import gzip,shutil
import json


def read_from_botzone(file_name):
    file = open(file_name, 'r')
    jsfiles = []
    for line in file:
        jsfile = json.loads(line)
        jsfiles.append(jsfile)

    return jsfiles


def parse_traindata(train_data):
    new_data = []
    for line in train_data:
        log = line['log']
        move = []
        for var in log:
            #print(var)
            if '0' in var.keys():
                if 'response' in var['0'].keys():
                    move.append((0, var['0']['response']))
                else:
                    print(var)
                    break
            elif '1' in var.keys():
                if 'response' in var['1'].keys():
                    move.append((1, var['1']['response']))
                else:
                    print(var)
                    break

        move.append(log[-1]['output']['display']['winner'])
        outcome = log[-1]['output']['display']
        if 'x' in outcome.keys() and 'y' in outcome.keys() and 'winner' in outcome.keys() and (len(outcome.keys()) == 3):
            new_data.append(move)

    return new_data


def convert_to_table(new_data):
    table = [0] * 64
    table[28] = 1
    table[35] = 1
    table[27] = -1
    table[36] = -1


                

if __name__ == '__main__':
    train_data = read_from_botzone('./train_file/Reversi-2017-9/1-100.matches')
    new_data = parse_traindata(train_data)


