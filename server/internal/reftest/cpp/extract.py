#!/usr/bin/env python3
"""Pull original functions out of the C++ sources, verbatim, for the reference drivers.

    extract.py <file> <signature start> [--body]

prints the function that starts with the given text (up to its matching closing brace); with --body,
only what is between its outer braces. The drivers paste these into minimal stand-in types, so the
golden values come from the original code, not from a transcription of it.
"""
import sys

def extract(path, start, body):
    src = open(path, encoding='utf-8', errors='replace').read()
    i = src.find(start)
    if i < 0:
        sys.exit(f"extract: {start!r} not in {path}")
    j = src.index('{', i)
    depth, k = 0, j
    while True:
        c = src[k]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    return src[j + 1:k] if body else src[i:k + 1]

if __name__ == '__main__':
    body = '--body' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--body']
    print(f"// --- from {args[0].split('src/')[-1]}: {args[1]}")
    print(extract(args[0], args[1], body))
