r'''
<MIT License>
Copyright (c) 2013  Marek Majkowski <marek@popcount.org>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
</MIT License>


SipHash-2-4 implementation, following the 'hashlib' API:

>>> key = b'0123456789ABCDEF'
>>> SipHash_2_4(key, b'a').hexdigest()
b'864c339cb0dc0fac'
>>> SipHash_2_4(key, b'a').digest()
b'\x86L3\x9c\xb0\xdc\x0f\xac'
>>> SipHash_2_4(key, b'a').hash()
12398370950267227270
>>> SipHash_2_4(key).update(b'a').hash()
12398370950267227270

>>> key = b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f'
>>> SipHash_2_4(key, b'').hash()
8246050544436514353
>>> SipHash_2_4(key, b'').hexdigest()
b'310e0edd47db6f72'

'''
import struct
import binascii

def _doublesipround(v, m):
    '''
    Internal helper. Xors 'm' to 'v3', runs two rounds of siphash on
    vector 'v' and xors 'm' to 'v0'.

    >>> _doublesipround((1,2,3,4),0)
    (9263201270060220426, 2307743542053503000, 5255419393243893904, 10208987565802066018)
    >>> _doublesipround((1,2,3,4),0xff)
    (11557575153743626750, 2307780510495171325, 7519994316568162407, 5442382437785464174)
    >>> _doublesipround((0,0,0,0),0)
    (0, 0, 0, 0)
    >>> _doublesipround((0,0,0,0),0xff)
    (2368684213854535680, 36416423977725, 2305811110491594975, 15626573430810475768)
    '''
    a, b, c, d = v
    d ^= m

    e = (a + b) & 0xffffffffffffffff
    i = (((b & 0x7ffffffffffff) << 13) | (b >> 51)) ^ e
    f = c + d
    j = ((((d) << 16) | (d >> 48)) ^ f ) & 0xffffffffffffffff
    h = (f + i) & 0xffffffffffffffff

    k = ((e << 32) | (e >> 32)) + j
    l = (((i & 0x7fffffffffff) << 17) | (i >> 47)) ^ h
    o = (((j << 21) | (j >> 43)) ^ k) & 0xffffffffffffffff

    p = (k + l) & 0xffffffffffffffff
    q = (((l & 0x7ffffffffffff) << 13) | (l >> 51)) ^ p
    r = ((h << 32) | (h >> 32)) + o
    s = (((o << 16) | (o >> 48)) ^ r) & 0xffffffffffffffff
    t = (r + q) & 0xffffffffffffffff
    u = (((p << 32) | (p >> 32)) + s) & 0xffffffffffffffff

    return (u ^ m,
            (((q & 0x7fffffffffff) << 17) | (q >> 47)) ^ t,
            ((t & 0xffffffff) << 32) | (t >> 32),
            (((s & 0x7ffffffffff) << 21) | (s >> 43)) ^ u)


_zeroes = b'\x00\x00\x00\x00\x00\x00\x00\x00'
_oneQ = struct.Struct('<Q')
_twoQ = struct.Struct('<QQ')
_oneQout = struct.Struct(">Q")


class SipHash_2_4(object):
    r'''
    >>> SipHash_2_4(b'0123456789ABCDEF', b'a').hash()
    12398370950267227270
    >>> SipHash_2_4(b'0123456789ABCDEF', b'').hash()
    3627314469837380007
    >>> SipHash_2_4(b'FEDCBA9876543210', b'').hash()
    2007056766899708634
    >>> SipHash_2_4(b'FEDCBA9876543210').update(b'').update(b'').hash()
    2007056766899708634
    >>> SipHash_2_4(b'FEDCBA9876543210', b'a').hash()
    6581475155582014123
    >>> SipHash_2_4(b'FEDCBA9876543210').update(b'a').hash()
    6581475155582014123
    >>> SipHash_2_4(b'FEDCBA9876543210').update(b'a').update(b'').hash()
    6581475155582014123
    >>> SipHash_2_4(b'FEDCBA9876543210').update(b'').update(b'a').hash()
    6581475155582014123

    >>> a = SipHash_2_4(b'FEDCBA9876543210').update(b'a')
    >>> a.hash()
    6581475155582014123
    >>> b = a.copy()
    >>> a.hash(), b.hash()
    (6581475155582014123, 6581475155582014123)
    >>> a.update(b'a') and None
    >>> a.hash(), b.hash()
    (3258273892680892829, 6581475155582014123)
    '''
    digest_size = 16
    block_size = 64

    s = b''
    b = 0

    def __init__(self, secret, s=b''):
        k0 = (secret[1] << 32) + secret[0]
        k1 = (secret[3] << 32) + secret[2]
        self.v = (0x736f6d6570736575 ^ k0,
                  0x646f72616e646f6d ^ k1,
                  0x6c7967656e657261 ^ k0,
                  0x7465646279746573 ^ k1)
        self.update(s)

    def update(self, s):
        s = self.s + s
        lim = (len(s)//8)*8
        v = self.v
        off = 0

        for off in range(0, lim, 8):
            m, = _oneQ.unpack_from(s, off)

            # print 'v0 %016x' % v[0]
            # print 'v1 %016x' % v[1]
            # print 'v2 %016x' % v[2]
            # print 'v3 %016x' % v[3]
            # print 'compress %016x' % m

            v = _doublesipround(v, m)
        self.v = v
        self.b += lim
        self.s = s[lim:]
        return self

    def hash(self):
        l = len(self.s)
        assert l < 8

        b = (((self.b + l) & 0xff) << 56)
        b |= _oneQ.unpack_from(self.s+_zeroes)[0]
        v = self.v

        # print 'v0 %016x' % v[0]
        # print 'v1 %016x' % v[1]
        # print 'v2 %016x' % v[2]
        # print 'v3 %016x' % v[3]
        # print 'padding %016x' % b

        v = _doublesipround(v, b)

        # print 'v0 %016x' % v0
        # print 'v1 %016x' % v1
        # print 'v2 %016x' % v2
        # print 'v3 %016x' % v3

        v = list(v)
        v[2] ^= 0xff
        v = _doublesipround(_doublesipround(v, 0), 0)
        return v[0] ^ v[1] ^ v[2] ^ v[3]

    def digest(self):
        return _oneQout.pack(self.hash())

    def hexdigest(self):
        return binascii.hexlify(self.digest())

    def copy(self):
        n = SipHash_2_4(_zeroes * 2)
        n.v, n.s, n.b = self.v, self.s, self.b
        return n


siphash24 = SipHash_2_4
SipHash24 = SipHash_2_4


if __name__ == "__main__":
    # Test vectors as per spec
    vectors = [c.encode('utf-8') for c in [
        "310e0edd47db6f72", "fd67dc93c539f874", "5a4fa9d909806c0d", "2d7efbd796666785",
        "b7877127e09427cf", "8da699cd64557618", "cee3fe586e46c9cb", "37d1018bf50002ab",
        "6224939a79f5f593", "b0e4a90bdf82009e", "f3b9dd94c5bb5d7a", "a7ad6b22462fb3f4",
        "fbe50e86bc8f1e75", "903d84c02756ea14", "eef27a8e90ca23f7", "e545be4961ca29a1",
        "db9bc2577fcc2a3f", "9447be2cf5e99a69", "9cd38d96f0b3c14b", "bd6179a71dc96dbb",
        "98eea21af25cd6be", "c7673b2eb0cbf2d0", "883ea3e395675393", "c8ce5ccd8c030ca8",
        "94af49f6c650adb8", "eab8858ade92e1bc", "f315bb5bb835d817", "adcf6b0763612e2f",
        "a5c91da7acaa4dde", "716595876650a2a6", "28ef495c53a387ad", "42c341d8fa92d832",
        "ce7cf2722f512771", "e37859f94623f3a7", "381205bb1ab0e012", "ae97a10fd434e015",
        "b4a31508beff4d31", "81396229f0907902", "4d0cf49ee5d4dcca", "5c73336a76d8bf9a",
        "d0a704536ba93e0e", "925958fcd6420cad", "a915c29bc8067318", "952b79f3bc0aa6d4",
        "f21df2e41d4535f9", "87577519048f53a9", "10a56cf5dfcd9adb", "eb75095ccd986cd0",
        "51a9cb9ecba312e6", "96afadfc2ce666c7", "72fe52975a4364ee", "5a1645b276d592a1",
        "b274cb8ebf87870a", "6f9bb4203de7b381", "eaecb2a30b22a87f", "9924a43cc1315724",
        "bd838d3aafbf8db7", "0b1a2a3265d51aea", "135079a3231ce660", "932b2846e4d70666",
        "e1915f5cb1eca46c", "f325965ca16d629f", "575ff28e60381be5", "724506eb4c328a95",
        ]]

    key = ''.join(chr(i) for i in range(16)).encode('utf-8')
    plaintext = ''.join(chr(i) for i in range(64)).encode('utf-8')
    for i in range(64):
        assert SipHash_2_4(key, plaintext[:i]).hexdigest() == vectors[i], \
            'failed on test no %i' % i

    # Internal doctests
    #
    # To maintain compatibility with both python 2.x and 3.x in tests
    # we need to do a trick. Python 2.x doesn't like b'' notation,
    # Python 3.x doesn't have 2222L long integers notation. To
    # overcome that we'll pipe both results as well as the intended
    # doctest output through an `eval` function before comparison. To
    # do it we need to monkeypatch the OutputChecker:
    import doctest
    EVAL_FLAG = doctest.register_optionflag("EVAL")
    OrigOutputChecker = doctest.OutputChecker

    def relaxed_eval(s):
        if s.strip():
            return eval(s)
        else:
            return None

    class MyOutputChecker:
        def __init__(self):
            self.orig = OrigOutputChecker()

        def check_output(self, want, got, optionflags):
            if optionflags & EVAL_FLAG:
                return relaxed_eval(got) == relaxed_eval(want)
            else:
                return self.orig.check_output(want, got, optionflags)

        def output_difference(self, example, got, optionflags):
            return self.orig.output_difference(example, got, optionflags)

    doctest.OutputChecker = MyOutputChecker
    # Monkey patching done. Go for doctests:

    if doctest.testmod(optionflags=EVAL_FLAG)[0] == 0: print("all tests ok")
