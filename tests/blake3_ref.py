"""A short BLAKE3 written from the specification, used only to check the C code on many lengths."""
import struct

IV = [0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19]
PERM = [2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 8, 15, 14]
CHUNK_START, CHUNK_END, PARENT, ROOT = 1, 2, 4, 8
M = 0xFFFFFFFF


def _rot(x, n):
    return ((x >> n) | (x << (32 - n))) & M


def _g(v, a, b, c, d, x, y):
    v[a] = (v[a] + v[b] + x) & M
    v[d] = _rot(v[d] ^ v[a], 16)
    v[c] = (v[c] + v[d]) & M
    v[b] = _rot(v[b] ^ v[c], 12)
    v[a] = (v[a] + v[b] + y) & M
    v[d] = _rot(v[d] ^ v[a], 8)
    v[c] = (v[c] + v[d]) & M
    v[b] = _rot(v[b] ^ v[c], 7)


def _compress(cv, block, counter, block_len, flags):
    v = list(cv) + IV[:4] + [counter & M, counter >> 32, block_len, flags]
    m = list(block)
    for r in range(7):
        _g(v, 0, 4, 8, 12, m[0], m[1]); _g(v, 1, 5, 9, 13, m[2], m[3])
        _g(v, 2, 6, 10, 14, m[4], m[5]); _g(v, 3, 7, 11, 15, m[6], m[7])
        _g(v, 0, 5, 10, 15, m[8], m[9]); _g(v, 1, 6, 11, 12, m[10], m[11])
        _g(v, 2, 7, 8, 13, m[12], m[13]); _g(v, 3, 4, 9, 14, m[14], m[15])
        m = [m[i] for i in PERM]
    return [v[i] ^ v[i + 8] for i in range(8)] + [v[i + 8] ^ cv[i] for i in range(8)]


def _words(block):
    block = block.ljust(64, b"\0")
    return struct.unpack("<16I", block)


def _chunk(data, counter, root):
    cv = IV
    blocks = [data[i:i + 64] for i in range(0, len(data), 64)] or [b""]
    for i, block in enumerate(blocks):
        flags = (CHUNK_START if i == 0 else 0) | (CHUNK_END if i == len(blocks) - 1 else 0)
        if root and i == len(blocks) - 1:
            flags |= ROOT
        out = _compress(cv, _words(block), counter, len(block), flags)
        cv = out[:8]
    return out


def _node(data, counter, root):
    if len(data) <= 1024:
        return _chunk(data, counter, root)
    chunks = (len(data) + 1023) // 1024
    left = 1 << ((chunks - 1).bit_length() - 1)
    a = _node(data[:left * 1024], counter, False)[:8]
    b = _node(data[left * 1024:], counter + left, False)[:8]
    return _compress(IV, a + b, 0, 64, PARENT | (ROOT if root else 0))


def blake3(data):
    return struct.pack("<8I", *_node(bytes(data), 0, True)[:8])
