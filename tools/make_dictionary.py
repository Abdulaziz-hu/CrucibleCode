#!/usr/bin/env python3
"""Expands a Hunspell dictionary (en_US.dic + en_US.aff, SCOWL based) into a plain word list
for CrucibleCode's built-in spell checker.

    python tools/make_dictionary.py /usr/share/hunspell/en_US.dic /usr/share/hunspell/en_US.aff resources/dict/en_US.txt

Words are lower-cased, one per line, sorted in byte order. Words Hunspell marks NOSUGGEST are written
with a trailing '!' so the checker accepts them but never proposes them as corrections.
"""
import re
import sys
from collections import defaultdict


def parse_aff(path):
    pfx, sfx = defaultdict(list), defaultdict(list)
    cross = {}
    nosuggest = None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if parts[0] == "NOSUGGEST":
                nosuggest = parts[1]
            elif parts[0] in ("PFX", "SFX"):
                kind, flag = parts[0], parts[1]
                table = pfx if kind == "PFX" else sfx
                if len(parts) == 4 and parts[2] in ("Y", "N"):      # header line
                    cross[(kind, flag)] = parts[2] == "Y"
                    continue
                strip = "" if parts[2] == "0" else parts[2]
                add = parts[3]
                cont = ""
                if "/" in add:
                    add, cont = add.split("/", 1)
                if add == "0":
                    add = ""
                cond = parts[4] if len(parts) > 4 else "."
                rx = re.compile(("^" + cond) if kind == "PFX" else (cond + "$"))
                table[flag].append((strip, add, rx, cont))
    return pfx, sfx, cross, nosuggest


def expand(word, flags, pfx, sfx, cross):
    out = {word}
    suffixed = []
    for fl in flags:
        for strip, add, rx, cont in sfx.get(fl, ()):
            if rx.search(word) and (not strip or word.endswith(strip)):
                base = word[: len(word) - len(strip)] if strip else word
                w = base + add
                out.add(w)
                suffixed.append((w, fl))
                for c in cont:                                        # continuation classes
                    for s2, a2, r2, _ in sfx.get(c, ()):
                        if r2.search(w) and (not s2 or w.endswith(s2)):
                            out.add((w[: len(w) - len(s2)] if s2 else w) + a2)
    for fl in flags:
        for strip, add, rx, cont in pfx.get(fl, ()):
            if rx.search(word) and (not strip or word.startswith(strip)):
                w = add + word[len(strip):]
                out.add(w)
                if cross.get(("PFX", fl), False):
                    for sw, sfl in suffixed:
                        if cross.get(("SFX", sfl), False):
                            out.add(add + sw[len(strip):])
    return out


def main(dic, aff, dest):
    pfx, sfx, cross, nosuggest = parse_aff(aff)
    words, hidden = set(), set()
    with open(dic, encoding="utf-8") as f:
        next(f)                                                        # entry count
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            token = line.split()[0]
            word, _, flags = token.partition("/")
            if not word or any(c.isdigit() for c in word):
                continue
            forms = expand(word, flags, pfx, sfx, cross)
            target = hidden if (nosuggest and nosuggest in flags) else words
            for w in forms:
                w = w.replace("\u2019", "'").lower()
                if re.fullmatch(r"[a-z][a-z'\-\.]*", w):
                    target.add(w)
    hidden -= words
    # One globally sorted (byte order) list: the checker binary-searches the raw resource without
    # loading it into per-word strings. A trailing '!' marks "accepted but never suggested".
    with open(dest, "w", encoding="utf-8", newline="\n") as f:
        for w in sorted(words | hidden):
            f.write(w + ("!" if w in hidden else "") + "\n")
    print(f"{len(words)} words (+{len(hidden)} not suggested) -> {dest}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
