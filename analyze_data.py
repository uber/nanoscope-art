#!/usr/bin/env python

#
# Converts serialized timing metrics executed into rows and columns that are easy to analyze in excel.
#

import sys

def extractValue(text, prefix):
    assert(prefix in text)
    return int(text.split(prefix)[1].replace('\n', ''))

while True:
    first_row = dict()
    second_row = dict()

    first_line = sys.stdin.readline()
    if first_line == '':
        break

    assert("Disabled inlining of all methods? 0" in first_line)
    first_row['startup_value'] = extractValue(sys.stdin.readline(), "startup elapsed = ")
    first_row['transition_value'] = extractValue(sys.stdin.readline(), "elapsed = ")

    assert("Disabled inlining of all methods? 0" in sys.stdin.readline())
    second_row['startup_value'] = extractValue(sys.stdin.readline(), "startup elapsed = ")
    second_row['transition_value'] = extractValue(sys.stdin.readline(), "elapsed = ")

    assert("Disabled inlining of all methods? 1" in sys.stdin.readline())
    first_row['disabled_startup_value'] = extractValue(sys.stdin.readline(), "startup elapsed = ")
    first_row['disabled_transition_value'] = extractValue(sys.stdin.readline(), "elapsed = ")

    assert("Disabled inlining of all methods? 1" in sys.stdin.readline())
    second_row['disabled_startup_value'] = extractValue(sys.stdin.readline(), "startup elapsed = ")
    second_row['disabled_transition_value'] = extractValue(sys.stdin.readline(), "elapsed = ")

    print first_row
    print second_row


