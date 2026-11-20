"""Original pixel-art cat in a three-quarter view (turned to the viewer's right).
32x24 grid -> 12-frame transparent GIF (blink + moving whiskers) plus a contact sheet."""
import sys
from PIL import Image

W, H, SCALE = 32, 24, 6
OUT = sys.argv[1]

COLORS = {
    'B': (0x2E, 0x5B, 0xC4),  # fur
    'D': (0x1B, 0x2C, 0x6B),  # outline, eyes, whiskers
    'L': (0x6C, 0x94, 0xEB),  # inner ear, tail tip
    'W': (255, 255, 255),     # face, bib, paws
    'P': (255, 166, 192),     # cheeks
    'N': (229, 96, 127),      # nose
    'Y': (255, 210, 63),      # sparkles
}
KEYS = ['.'] + list(COLORS)
IDX = {k: i for i, k in enumerate(KEYS)}
PAL = [0, 0, 0]
for k in KEYS[1:]:
    PAL += list(COLORS[k])
PAL += [0] * (768 - len(PAL))

SIL = {
    2: [(7, 8), (18, 19)],
    3: [(7, 9), (17, 19)],
    4: [(7, 20)],
    5: [(6, 21)],
    6: [(6, 22)], 7: [(6, 22)], 8: [(6, 22)],
    9: [(6, 23)], 10: [(6, 23)],
    11: [(7, 22)],
    12: [(9, 21)],
    13: [(10, 20)],
    14: [(4, 5), (8, 21)],
    15: [(4, 5), (7, 22)],
    16: [(3, 4), (7, 22)], 17: [(3, 4), (7, 22)], 18: [(3, 4), (7, 22)],
    19: [(3, 5), (7, 22)],
    20: [(4, 22)],
    21: [(7, 22)],
}
FACE = {6: (12, 20), 7: (9, 22), 8: (9, 22), 9: (9, 23), 10: (9, 23), 11: (10, 22), 12: (11, 20)}
BIB = {14: (13, 19), 15: (14, 18), 16: (15, 17)}
PAWS = [(12, 20), (13, 20), (14, 20), (15, 20), (12, 21), (13, 21), (14, 21), (15, 21),
        (18, 20), (19, 20), (20, 20), (21, 20), (18, 21), (19, 21), (20, 21), (21, 21)]

# near side (viewer's left) has long whiskers, far side (right) has short ones
WL = {
    0: [(9, 1, 5), (11, 1, 6)],
    1: [(9, 3, 5), (8, 1, 2), (7, 0, 0), (11, 3, 6), (10, 1, 2)],
    2: [(9, 3, 5), (10, 1, 2), (11, 0, 0), (11, 3, 6), (12, 1, 2), (13, 0, 0)],
}
WR = {
    0: [(9, 24, 27), (11, 23, 27)],
    1: [(9, 24, 25), (8, 26, 27), (11, 23, 25), (10, 26, 27)],
    2: [(9, 24, 25), (10, 26, 27), (11, 23, 25), (12, 26, 27)],
}
SPARK = {
    0: [(2, 3), (27, 2), (29, 18)],
    1: [(1, 15), (26, 4), (30, 8)],
}

SEQ = [
    ('open', 0, 0, 700),
    ('open', 1, 0, 110),
    ('open', 0, 0, 110),
    ('open', 2, 0, 110),
    ('open', 0, 0, 110),
    ('half', 1, 1, 70),
    ('closed', 2, 1, 90),
    ('half', 1, 1, 70),
    ('open', 0, 1, 110),
    ('open', 2, 0, 110),
    ('open', 1, 0, 110),
    ('open', 0, 0, 110),
]


def span_set(rows):
    s = set()
    for r, spans in rows.items():
        for a, b in spans:
            for c in range(a, b + 1):
                s.add((c, r))
    return s


def render(eye, wh, sp):
    g = [['.'] * W for _ in range(H)]
    sil = span_set(SIL)

    def put(c, r, k):
        if 0 <= c < W and 0 <= r < H:
            g[r][c] = k

    for c, r in sil:
        put(c, r, 'B')
    for r, (a, b) in FACE.items():
        for c in range(a, b + 1):
            put(c, r, 'W')
    for r, (a, b) in BIB.items():
        for c in range(a, b + 1):
            put(c, r, 'W')
    for c, r in PAWS:
        put(c, r, 'W')
    for c, r in [(8, 3), (8, 4), (18, 3)]:        # inner ears
        put(c, r, 'L')
    put(4, 14, 'L'); put(5, 14, 'L')              # tail tip
    put(15, 5, 'D'); put(16, 5, 'D')              # forehead mark
    for c, r in sil:                              # outline
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (c + dc, r + dr) not in sil:
                put(c + dc, r + dr, 'D')
    for c, r in [(9, 10), (10, 10), (11, 10), (10, 11), (11, 11), (18, 11)]:   # cheeks
        put(c, r, 'P')
    put(21, 10, 'N'); put(22, 10, 'N')            # nose, near the leading edge
    for c, r in [(21, 11), (20, 12), (19, 12)]:   # mouth
        put(c, r, 'D')
    # eyes: near eye 2 px wide, far eye 1 px wide (foreshortened)
    if eye == 'open':
        for r in (7, 8, 9):
            put(12, r, 'D'); put(13, r, 'D'); put(19, r, 'D')
        put(13, 7, 'W')
    elif eye == 'half':
        for r in (8, 9):
            put(12, r, 'D'); put(13, r, 'D'); put(19, r, 'D')
    else:
        for c in (11, 12, 13, 14, 19, 20):
            put(c, 9, 'D')
    for r, a, b in WL[wh]:
        for c in range(a, b + 1):
            put(c, r, 'D')
    for r, a, b in WR[wh]:
        for c in range(a, b + 1):
            put(c, r, 'D')
    for c, r in SPARK[sp]:
        put(c, r, 'Y')

    img = Image.new('P', (W, H), 0)
    img.putpalette(PAL)
    px = img.load()
    for r in range(H):
        for c in range(W):
            px[c, r] = IDX[g[r][c]]
    big = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
    big.putpalette(PAL)
    return big


frames = [render(e, w, s) for e, w, s, _ in SEQ]
durs = [d for *_, d in SEQ]
frames[0].save(f'{OUT}/cat34.gif', save_all=True, append_images=frames[1:], duration=durs,
               loop=0, disposal=2, transparency=0, optimize=False)

im = Image.open(f'{OUT}/cat34.gif')
cols, rows, w, h = 4, 3, W * 3, H * 3
sheet = Image.new('RGB', (cols * w + 10 * (cols + 1), rows * h + 10 * (rows + 1)), (0xF3, 0xF3, 0xF3))
for i in range(im.n_frames):
    im.seek(i)
    f = im.convert('RGBA').resize((w, h), Image.NEAREST)
    sheet.paste(f, (10 + (i % cols) * (w + 10), 10 + (i // cols) * (h + 10)), f)
sheet.save(f'{OUT}/sheet34.png')
print('frames', im.n_frames, im.size)
