import struct, sys, os, collections
def walk(data, off, end, depth, cb):
    while off + 12 <= end:
        cid, dlen, clen = struct.unpack_from('<III', data, off)
        if clen < 12: break
        cb(cid, data, off, dlen, clen, depth)
        walk(data, off+dlen, off+clen, depth+1, cb)
        off += clen
def lzr_decompress(inp, outsize):
    """LZR (Lempel-Ziv-Radical), from pure3d/p3d/lzr.cpp of the SHR-era source.

    code > 15  -> match: count = code&15 (0 means 15 + 0xff-extension bytes),
                  offset = (code>>4) | next<<4, copied byte-wise from output-offset
    code <= 15 -> literal run of `code` bytes; code 0 means 15 + extension,
                  and the 15 are emitted before the extended count.
    """
    out = bytearray(); i = 0
    while len(out) < outsize:
        code = inp[i]; i += 1
        if code > 15:
            n = code & 15
            if n == 0:
                n += 15
                while inp[i] == 0:
                    n += 255; i += 1
                n += inp[i]; i += 1
            off = (code >> 4) | (inp[i] << 4); i += 1
            p = len(out) - off
            for _ in range(n):          # byte-wise: matches may overlap
                out.append(out[p]); p += 1
        else:
            n = code
            if n == 0:
                while inp[i] == 0:
                    n += 255; i += 1
                n += inp[i]; i += 1
                out += inp[i:i+15]; i += 15     # the implicit first 15 literals
            out += inp[i:i+n]; i += n
    return bytes(out)

def load(path):
    """Read a .p3d, transparently decompressing a P3DZ container.

    Pure3D::DATA_FILE            = 0xFF443350 -> bytes 'P3D\\xff'
    Pure3D::DATA_FILE_COMPRESSED = 0x5A443350 -> bytes 'P3DZ'
    (the _SWAP variants are the same words on a big endian host.)
    A compressed file is: 'P3DZ', u32 totalUncompressedSize, then LZR blocks
    { u32 compressedSize, u32 uncompressedSize, u8 data[compressedSize] };
    the concatenated output is the plain file, starting with 'P3D\\xff'.
    See tChunkFile::tChunkFile / tFileMem::SetCompressed in the SHR source.
    NOTE: neither shipped cement.rcf contains a single P3DZ file, so this path
    is source-faithful but untested on real data.
    """
    d = open(path,'rb').read()
    if d[:4] in (b'P3DZ', b'ZD3P'):
        ulen = struct.unpack_from('<I', d, 4)[0]
        out = bytearray(); off = 8
        while len(out) < ulen and off + 8 <= len(d):
            cl, ul = struct.unpack_from('<II', d, off); off += 8
            out += lzr_decompress(d[off:off+cl], ul); off += cl
        d = bytes(out)
    return d
if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'tree':
        d = load(sys.argv[2]); maxdepth = int(sys.argv[3]) if len(sys.argv)>3 else 99
        def cb(cid, data, off, dlen, clen, depth):
            if depth > maxdepth: return
            # try to read a p3d string (u8 len + chars) at start of data
            s = ''
            if dlen > 13:
                l = data[off+12]
                if 0 < l < 64 and off+13+l <= off+dlen:
                    t = data[off+13:off+13+l]
                    if all(32 <= c < 127 for c in t): s = t.decode()
            print('  '*depth + '%08x dlen=%d clen=%d %s' % (cid, dlen-12, clen, s))
        walk(d, 12, len(d), 0, cb)
    elif mode == 'hist':
        hist = collections.Counter(); parents = collections.defaultdict(set); files = collections.defaultdict(set)
        for path in sys.argv[2:]:
            try: d = load(path)
            except Exception as e: print('skip', path, e, file=sys.stderr); continue
            stack = []
            def cb(cid, data, off, dlen, clen, depth):
                del stack[depth:]
                par = stack[-1] if stack else 0
                stack.append(cid)
                hist[cid] += 1; parents[cid].add(par); files[cid].add(os.path.basename(path))
            walk(d, 12, len(d), 0, cb)
        for cid, n in sorted(hist.items()):
            fl = sorted(files[cid]); 
            print('%08x %7d parents=%s files=%d %s' % (cid, n, ','.join('%08x'%p for p in sorted(parents[cid])), len(fl), ' '.join(fl[:3])))
