#!/usr/bin/env python3
"""The game data for the original Xbox, with names FATX takes.

usage: fatx_data.py SRC DST [DISC]

Copies SRC (the data folder) to DST, only what changed, and makes every file and folder name fit
FATX: 42 characters at most (a map's thumbnail, the game's, is its name with .png for .map: as long).

A name that's too long is shortened so the game still shows it the same: a map's or a world's name
is what's after its author's prefix (Author_Name.map shows as "Name"), so the author is cut to a
few letters first (DrHammer_Pirates of the Mushroom Kingdom.map: Dr_Pirates of the Mushroom
Kingdom.map); if that isn't enough, the name's end is trimmed, with ~1, ~2... to keep it unique.
The text files that name a renamed file by its old name get the new one.

DISC (optional): the folder the data was deployed to before (the disc's): its data files that
aren't in DST any more (an old long name) are removed, as deploying only adds files.

Also prints the longest path, from the data folder: installed on the Xbox, the install folder comes
before it (F:\\homebrew\\Super Mario War\\data\\...).
"""
import os
import shutil
import sys

FATX_NAME = 42
TEXT = ('.txt', '.tour', '.ini', '.cfg')


def limit_for(rel_dir, name):
    #(a map's thumbnail is its name with .png for .map: as long, so 42 covers both)
    return FATX_NAME


def shorten(name, limit, taken):
    """`name` cut to `limit` characters, shown the same where it can be"""
    if len(name) <= limit:
        return name
    stem, ext = os.path.splitext(name)
    #(the author's prefix first: the part shown is after it)
    if '_' in stem:
        author, shown = stem.split('_', 1)
        for keep in (3, 2, 1):
            candidate = author[:keep] + '_' + shown + ext
            if len(candidate) <= limit and candidate.lower() not in taken:
                return candidate
    #(then the end trimmed, unique)
    for n in range(0, 100):
        mark = '' if n == 0 else '~%d' % n
        room = limit - len(ext) - len(mark)
        candidate = stem[:room].rstrip() + mark + ext
        if candidate.lower() not in taken:
            return candidate
    raise SystemExit('fatx_data: no short name for ' + name)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    disc = sys.argv[3] if len(sys.argv) > 3 else None
    renames = {}     # old relative path -> new relative path
    stems = {}       # old stem -> new stem (for the text files)
    wanted = set()   # the relative paths DST has (new names)
    copied = 0
    longest = ''
    #(first the new names, all of them: the text files are rewritten with all of them known)
    plan = []        # (source file, new relative path)
    for root, dirs, files in os.walk(src):
        rel_root = os.path.relpath(root, src)
        rel_root = '' if rel_root == '.' else rel_root
        new_root = renames.get(rel_root, rel_root)
        taken = set()
        dirs.sort()
        files.sort()
        #(folders too: their new names for what's in them)
        for d in dirs:
            short = shorten(d, FATX_NAME, taken)
            taken.add(short.lower())
            if short != d:
                renames[os.path.join(rel_root, d)] = os.path.join(new_root, short)
            else:
                renames.setdefault(os.path.join(rel_root, d), os.path.join(new_root, d))
        for f in files:
            short = shorten(f, limit_for(rel_root, f), taken)
            taken.add(short.lower())
            new_rel = os.path.join(new_root, short)
            if short != f:
                renames[os.path.join(rel_root, f)] = new_rel
                stems[os.path.splitext(f)[0]] = os.path.splitext(short)[0]
            wanted.add(os.path.normcase(new_rel))
            if len(new_rel) > len(longest):
                longest = new_rel
            plan.append((os.path.join(root, f), new_rel))
    #(then the copies: what changed)
    for s, new_rel in plan:
        t = os.path.join(dst, new_rel)
        text = s.lower().endswith(TEXT) and stems
        st = os.stat(s)
        if not text and os.path.exists(t):
            tt = os.stat(t)
            if tt.st_size == st.st_size and int(tt.st_mtime) >= int(st.st_mtime):
                continue
        os.makedirs(os.path.dirname(t), exist_ok=True)
        if text:
            #(a text file: the old names it mentions, the new ones)
            data = open(s, 'rb').read()
            for old, new in stems.items():
                data = data.replace(old.encode('utf-8', 'surrogateescape'), new.encode('utf-8', 'surrogateescape'))
            if os.path.exists(t) and open(t, 'rb').read() == data:
                continue
            open(t, 'wb').write(data)
            shutil.copystat(s, t)
        else:
            shutil.copy2(s, t)
        copied += 1
    #(what DST has that the data doesn't (renamed since, removed): gone; the same in the disc's folder)
    removed = 0
    for base in [dst] + ([os.path.join(disc, 'data')] if disc else []):
        if not os.path.isdir(base):
            continue
        for root, dirs, files in os.walk(base, topdown=False):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), base)
                if os.path.normcase(rel) not in wanted:
                    os.remove(os.path.join(root, f))
                    removed += 1
            if root != base and not os.listdir(root):
                os.rmdir(root)
    real = {old: new for old, new in renames.items() if os.path.basename(old) != os.path.basename(new)}
    print('fatx_data: %d copied, %d removed, %d renamed for FATX' % (copied, removed, len(real)))
    for old, new in sorted(real.items()):
        print('fatx_data:   %s -> %s' % (old.replace(os.sep, '/'), os.path.basename(new)))
    print('fatx_data: the longest path, from the data folder: %d characters (%s)' % (len('data/' + longest), 'data/' + longest.replace(os.sep, '/')))


if __name__ == '__main__':
    main()
