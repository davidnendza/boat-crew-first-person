#!/usr/bin/env python3
"""Who calls a given function in Boat Crew's GameAssembly.dll?

Scans the game's compiled C# (the PE section named "il2cpp") for direct calls (E8/E9) whose target is a given
address, and names the calling method using an Il2CppInspector C# dump of the same build. This is how the crew
attribute effects in crew-stats.md were found: give it each attribute getter's address and it lists the
calculations that read it.

    python callers.py --game "<folder with GameAssembly.dll>" --dump "<Il2CppInspector --cs-out folder>" \
        --list "Attribute_Live"                     # find addresses by method name (regex)
    python callers.py --game ... --dump ... HEC_Live=0x954BF0 GRT_Live=0x955070   # who calls these

Addresses are RVAs (the dump's addresses minus 0x180000000). They change with every game update, so re-dump first.

Blind spots: a field read directly (bypassing its getter), virtual and delegate calls, and compiler-generated
nested classes (a "_recoilNoise_d__203" is still GunMount code). Attribute fields below offset 0x80 use a 1-byte
displacement and need a different byte pattern than the one in find_field_readers().
"""
import argparse, bisect, collections, glob, os, re, struct, sys


def load_dump(folder):
    """(start, end, 'Class.method') for every method and property getter in the dump."""
    spans = []
    for f in glob.glob(os.path.join(folder, '**', '*.cs'), recursive=True):
        cls_stack = []
        for line in open(f, encoding='utf-8', errors='replace'):
            m = re.match(r'(\t+)(?:public |private |internal |protected |static |abstract |sealed |partial )*(?:class|struct|interface) (\w+)', line)
            if m:
                depth = len(m.group(1))
                cls_stack = [c for c in cls_stack if c[0] < depth] + [(depth, m.group(2))]
                continue
            m = re.search(r'(\w+)\([^;]*\)[^;]*;\s*// 0x0000000(1[0-9A-F]{8})-0x0000000(1[0-9A-F]{8})', line)
            if m and cls_stack:
                spans.append((int(m.group(2), 16) - 0x180000000, int(m.group(3), 16) - 0x180000000, '.'.join(c[1] for c in cls_stack) + '.' + m.group(1)))
            m = re.match(r'\t+(?:public |private |internal )*[\w<>\[\],. ]+ (\w+) \{ .*// 0x0000000(1[0-9A-F]{8})-0x0000000(1[0-9A-F]{8})', line)
            if m and cls_stack:
                spans.append((int(m.group(2), 16) - 0x180000000, int(m.group(3), 16) - 0x180000000, '.'.join(c[1] for c in cls_stack) + '.get_' + m.group(1)))
    spans.sort()
    return spans


def load_code(game):
    d = open(os.path.join(game, 'GameAssembly.dll'), 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    sizeopt = struct.unpack_from('<H', d, pe + 20)[0]
    st = pe + 24 + sizeopt
    for i in range(nsec):
        n, vs, va, rs, rp = struct.unpack_from('<8sIIII', d, st + i * 40)
        if n.startswith(b'il2cpp'):                       # the game's compiled C# lives here, not in .text
            return va, d[rp:rp + min(vs, rs)]
    sys.exit('no il2cpp section in GameAssembly.dll')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True, help='folder that holds GameAssembly.dll')
    ap.add_argument('--dump', required=True, help='Il2CppInspector C# output folder (per-namespace layout)')
    ap.add_argument('--list', metavar='REGEX', help='print methods whose name matches, with their RVAs, and exit')
    ap.add_argument('targets', nargs='*', help='name=0xRVA pairs to find callers of')
    a = ap.parse_args()

    spans = load_dump(a.dump)
    if a.list:
        rx = re.compile(a.list)
        for s, e, name in spans:
            if rx.search(name):
                print(f'0x{s:X}  {name}')
        return
    starts = [s[0] for s in spans]

    def owner(rva):
        i = bisect.bisect_right(starts, rva) - 1
        while i >= 0 and rva - spans[i][0] < 0x20000:
            if spans[i][0] <= rva < spans[i][1]:
                return spans[i][2]
            i -= 1
        return None

    va, code = load_code(a.game)
    calls = [(m.start(), struct.unpack_from('<i', code, m.start() + 1)[0]) for m in re.finditer(rb'[\xE8\xE9]', code) if m.start() + 5 <= len(code)]
    for t in a.targets:
        name, rva = t.split('=')
        rva = int(rva, 16)
        hits = collections.Counter(owner(va + i) for i, rel in calls if va + i + 5 + rel == rva)
        hits.pop(None, None)
        print(f'== {name}  (rva 0x{rva:X}): {sum(hits.values())} call sites in {len(hits)} methods')
        for who, n in hits.most_common(40):
            print(f'   {who}  x{n}')


if __name__ == '__main__':
    main()
