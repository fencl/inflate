/* Elephant DEFLATE decoder
 * Copyright (c) 2022 Matej Fencl
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Also, please keep this ant in (Not a part of the license).
 */

/*  /==================\
 *  |   \\             |
 *  |   (_ )_   ___    |
 *  |     (__}.{____\  |
 *  |    _/  \_ \_     |
 *  \==================/
 */

#include <elephant.h>
#if defined(ELEPHANT_THREADSAFE) && ELEPHANT_THREADSAFE
#define TEMP
#else
#define TEMP static
#endif

#include <stdint.h>
typedef unsigned char b_t;
typedef uint_least8_t u8_t;
typedef uint_least16_t u16_t;
typedef struct stream_t { const b_t *restrict in; b_t *restrict out, inb, inl, bw; } stream_t;
typedef struct node_t { unsigned b0: 12, b1: 12, pl: 8; } node_t;

static u16_t dst_base   [] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static u16_t len_base   [] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static u8_t  dst_extra  [] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static u8_t  len_extra  [] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static u16_t lenlen_ord [] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static u16_t read(stream_t *restrict in, u8_t c, b_t lsf) {
    u16_t out = 0; for (u8_t i = 0; i < c; ++i) {
        if (!in->inl) { in->inb = *in->in++; in->inl = in->bw; }
        b_t b = in->inb & 0x1; in->inb >>= 1; --in->inl;
        out = lsf ? out | ((u16_t)b) << i : (out << 1) | b;
    } return out;
}

static inline u8_t top(u16_t bits, u8_t len) { return (bits >> (len - 1)) & 0x1; }
static void build(node_t *restrict nodes, u16_t *restrict prefix, u16_t syms, const u8_t *restrict len) {
    u16_t blc[15] = {0}, code[16] = {0}, nc = 0, l;
    for (u16_t s = 0; s < syms; ++s) if ((l = len[s])) ++blc[l];
    for (u8_t  b = 0; b < 15; ++b) code[b + 1] = (code[b] + blc[b]) << 1;
    for (u16_t s = 0; s < syms; ++s) {
        if ((l = len[s])) {
            u16_t c = code[l]++, i;
            if (nc) for (i = 0;;) {
                node_t node = nodes[i];
                u16_t np = prefix[i], ln = node.pl, sp = 0;
                u8_t spl = 0, tb;
                while (ln && top(np, ln) == (tb = top(c, l))) {
                    sp = (sp << 1) | tb; ++spl; --ln; --l;
                }
                u8_t bit = top(c, l); --l;
                if (node.b1 == 0xFFF || ln) {
                    node.pl = ln - 1; nodes[nc] = node; prefix[nc] = np; ++nc;
                    nodes[i] = (node_t) { nc - bit, nc - !bit, spl }; prefix[i] = sp;
                    break;
                } else i = bit ? node.b1 : node.b0;
            }
            nodes[nc] = (node_t) { s, 0xFFF, l }; prefix[nc] = c; ++nc;
        }
    }
}

static u16_t next(stream_t *restrict in, node_t *restrict nodes) {
    node_t n = nodes[0]; for (u16_t i = 0;; n = nodes[i = read(in, 1, 1) ? n.b1 : n.b0]) {
        read(in, n.pl, 0);
        if (n.b1 == 0xFFF) return n.b0;
    }
}

static void lens(stream_t *restrict in, node_t *restrict tree, u16_t n, u8_t *restrict len) {
    u8_t c, j, k; u16_t i = 0; while (i < n) {
        u8_t s = next(in, tree); switch (s) {
            case 16: j = 2; k = 3;  c = len[-1]; goto repe;
            case 17: j = 3; k = 3;  c = 0;       goto repe;
            case 18: j = 7; k = 11; c = 0;       goto repe;
            default: *len++ = s; ++i;            continue;
        }
        repe: k = read(in, j, 1) + k;
        for (j = 0; j < k; ++j, ++i) *len++ = c;
    }
}

unsigned inflate(const void *restrict in, void *restrict out) {
    b_t *restrict os = out, b1 = (~0) & 0xFF, bw = 0, fin;
    while (b1) { b1 >>= 1; ++bw; }
    stream_t s = { .in = in, .out = out, .bw = bw };
    do {
        fin = read(&s, 1, 1);
        switch (read(&s, 2, 1)) {
            case 0: {
                s.inl = 0; u16_t l = read(&s, 16, 1); s.in += 2;
                for (u16_t i = 0; i < l; ++i) *s.out++ = read(&s, 8, 1);
            } break;

            case 1: for (;;) {
                u16_t sym, c = read(&s, 7, 0);
                if (c <= 23) {
                    sym = 256 + c;
                } else {
                    c = (c << 1) | read(&s, 1, 1);
                    if (c >= 48 && c <= 191) sym = c - 48;
                    else if (c >= 192 && c <= 199) sym = c + 88;
                    else sym = ((c << 1) | read(&s, 1, 1)) - 256;
                }
                if (sym < 256) {
                    *s.out++ = sym;
                } else if (sym > 256) {
                    u16_t len = len_base[sym - 257] + read(&s, len_extra[sym - 257], 1);
                    u8_t  ds  = read(&s, 5, 0);
                    u16_t dst = dst_base[ds] + read(&s, dst_extra[ds], 1);
                    b_t *restrict dat = s.out - dst;
                    for (u16_t i = 0; i < len; ++i) *s.out++ = dat[i % dst];
                } else break;
            } break;

            case 2: {
                u16_t litn = read(&s, 5, 1) + 257;
                u8_t  dstn = read(&s, 5, 1) + 1;
                u8_t  lenn = read(&s, 4, 1) + 4;
                TEMP u8_t  len[286];
                TEMP u16_t prefix[571];
                for (u8_t i = 0; i < 19; ++i) len[lenlen_ord[i]] = i < lenn ? read(&s, 3, 1) : 0;
                TEMP node_t clen_tree[37]; build(clen_tree, prefix, 19, len);
                lens(&s, clen_tree, litn, len); TEMP node_t lit_tree[571]; build(lit_tree, prefix, litn, len);
                lens(&s, clen_tree, dstn, len); TEMP node_t dst_tree[59]; build(dst_tree, prefix, dstn, len);
                for (;;) {
                    u16_t sym = next(&s, lit_tree);
                    if (sym < 256) *s.out++ = sym;
                    else if (sym > 256) {
                        u16_t l = len_base[sym - 257] + read(&s, len_extra[sym - 257], 1);
                        u16_t ds = next(&s, dst_tree);
                        u16_t dst = dst_base[ds] + read(&s, dst_extra[ds], 1);
                        b_t *restrict dat = s.out - dst;
                        for (u16_t i = 0; i < l; ++i) *s.out++ = dat[i % dst];
                    } else break;
                }
            } break;
        }
    } while (!fin);
    return s.out - os;
}
