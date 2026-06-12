/*
 * startup.c - Coaster loader & C-runtime entry glue
 *
 * The shipped binary contains ZERO original game code. At launch we read the
 * player's own COASTER.EXE, undo its LZEXE v0.91 packing *in memory* (the same
 * algorithm as tools/unlzexe.py, ported to C), drop the fully-relocated image
 * into the flat 8086 address space, fix up its relocations, set the CPU into
 * the exact register state DOS would hand the program, and jump to the
 * recompiled Microsoft-C start-up routine.
 *
 * Part of the Coaster Recomp project (sp00nznet/coaster)
 */

#include "recomp/cpu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coaster_recomp.h"   /* COASTER_ENTRY_POINT, COASTER_DGROUP */

/* DOS loads the program image 16 paragraphs (one PSP) above the PSP segment. */
#define COASTER_PSP_SEG    0x0100
#define COASTER_LOAD_SEG   (COASTER_PSP_SEG + 0x10)   /* 0x0110 */

/* ---------- LZEXE v0.91 in-memory decompressor ---------- */

typedef struct {
    const uint8_t *data;
    long pos;
    uint16_t span;
    int count;
} BitStream;

static uint16_t rd16(const uint8_t *p, long off)
{
    return (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
}

static void bits_init(BitStream *b, const uint8_t *data, long pos)
{
    b->data = data;
    b->pos = pos;
    b->span = rd16(data, pos);
    b->pos += 2;
    b->count = 16;
}

static int get_bit(BitStream *b)
{
    int bit = b->span & 1;
    b->span >>= 1;
    if (--b->count == 0) {
        b->span = rd16(b->data, b->pos);
        b->pos += 2;
        b->count = 16;
    }
    return bit;
}

static uint8_t get_byte(BitStream *b)
{
    return b->data[b->pos++];
}

/*
 * Decompress an LZEXE 0.91 image. Returns a malloc'd MZ image in *out_image
 * (caller frees), its length in *out_len, and the rebuilt relocation table
 * (offset/segment pairs, load-relative) in *out_relocs / *out_nrel.
 * Returns 0 on success, non-zero on error.
 */
static int lzexe_unpack(const uint8_t *data, long len,
                        uint8_t **out_image, long *out_len,
                        uint16_t (**out_relocs)[2], int *out_nrel)
{
    uint16_t ih[14], oh[14], inf[8];
    long dec, in_pos, hdr_bytes, hdr_end, total;
    int pad, rel_count = 0, rel_cap = 4096;
    uint16_t (*relocs)[2];
    uint8_t *image;
    long cap, n;
    BitStream bits;

    if (len < 0x20)
        return 1;
    for (int i = 0; i < 14; i++)
        ih[i] = rd16(data, i * 2);
    if (ih[0] != 0x5A4D && ih[0] != 0x4D5A)
        return 2;                                   /* not MZ */
    if (ih[0x0D] != 0 || ih[0x0C] != 0x1C)
        return 3;                                   /* not LZEXE-shaped */
    if (memcmp(data + 0x1C, "LZ91", 4) != 0)
        return 4;                                   /* only v0.91 supported here */

    memcpy(oh, ih, sizeof oh);

    dec = (long)(ih[0x0B] + ih[4]) << 4;            /* decompressor segment */
    for (int i = 0; i < 8; i++)
        inf[i] = rd16(data, dec + i * 2);
    oh[0x0A] = inf[0];   /* IP */
    oh[0x0B] = inf[1];   /* CS */
    oh[0x08] = inf[2];   /* SP */
    oh[0x07] = inf[3];   /* SS */
    oh[0x0C] = 0x1C;

    /* rebuild relocation table (v0.91 RLE form) */
    relocs = malloc(sizeof(*relocs) * rel_cap);
    if (!relocs)
        return 5;
    {
        long p = dec + 0x158;
        uint16_t rel_off = 0, rel_seg = 0, span;
        for (;;) {
            span = data[p++];
            if (span == 0) {
                span = rd16(data, p);
                p += 2;
                if (span == 0) { rel_seg += 0x0FFF; continue; }
                if (span == 1) break;
            }
            rel_off = (uint16_t)(rel_off + span);
            rel_seg = (uint16_t)(rel_seg + ((rel_off & ~0x0F) >> 4));
            rel_off &= 0x0F;
            if (rel_count == rel_cap) {
                rel_cap *= 2;
                relocs = realloc(relocs, sizeof(*relocs) * rel_cap);
                if (!relocs) return 5;
            }
            relocs[rel_count][0] = rel_off;
            relocs[rel_count][1] = rel_seg;
            rel_count++;
        }
    }
    oh[3] = (uint16_t)rel_count;

    hdr_end = 0x1C + (long)rel_count * 4;
    pad = (int)((0x200 - hdr_end) & 0x1FF);
    hdr_bytes = hdr_end + pad;
    oh[4] = (uint16_t)(hdr_bytes >> 4);

    /* decompress the load module */
    in_pos = (long)(ih[0x0B] - inf[4] + ih[4]) << 4;
    bits_init(&bits, data, in_pos);
    cap = 256 * 1024;
    image = malloc(cap);
    if (!image) { free(relocs); return 5; }
    n = 0;
    for (;;) {
        int length;
        uint16_t s;
        long off, base;
        if (n + 16 > cap) {
            cap *= 2;
            image = realloc(image, cap);
            if (!image) { free(relocs); return 5; }
        }
        if (get_bit(&bits)) {
            image[n++] = get_byte(&bits);
            continue;
        }
        if (!get_bit(&bits)) {                       /* short match */
            length = (get_bit(&bits) << 1) | get_bit(&bits);
            length += 2;
            s = (uint16_t)(get_byte(&bits) | 0xFF00);
        } else {                                     /* long match */
            uint8_t lo = get_byte(&bits);
            uint8_t hi = get_byte(&bits);
            s = (uint16_t)(lo | (((hi & ~0x07) << 5) | 0xE000));
            length = (hi & 0x07) + 2;
            if (length == 2) {
                int e = get_byte(&bits);
                if (e == 0) break;
                if (e == 1) continue;
                length = e + 1;
            }
        }
        off = (int16_t)s;                            /* always negative */
        base = n;
        if (n + length > cap) {
            while (n + length > cap) cap *= 2;
            image = realloc(image, cap);
            if (!image) { free(relocs); return 5; }
        }
        for (int i = 0; i < length; i++)
            image[n + i] = image[base + off + i];
        n += length;
    }

    /* assemble the output MZ: header + relocs + pad + image */
    total = hdr_bytes + n;
    oh[1] = (uint16_t)(total & 0x1FF);
    oh[2] = (uint16_t)((total + 0x1FF) >> 9);

    *out_image = malloc(total);
    if (!*out_image) { free(relocs); free(image); return 5; }
    for (int i = 0; i < 14; i++)
        (*out_image)[i * 2] = (uint8_t)(oh[i] & 0xFF),
        (*out_image)[i * 2 + 1] = (uint8_t)(oh[i] >> 8);
    for (int i = 0; i < rel_count; i++) {
        long o = 0x1C + (long)i * 4;
        (*out_image)[o + 0] = (uint8_t)(relocs[i][0] & 0xFF);
        (*out_image)[o + 1] = (uint8_t)(relocs[i][0] >> 8);
        (*out_image)[o + 2] = (uint8_t)(relocs[i][1] & 0xFF);
        (*out_image)[o + 3] = (uint8_t)(relocs[i][1] >> 8);
    }
    memset(*out_image + hdr_end, 0, hdr_bytes - hdr_end);
    memcpy(*out_image + hdr_bytes, image, n);
    free(image);

    *out_len = total;
    *out_relocs = relocs;
    *out_nrel = rel_count;
    return 0;
}

/* ---------- Public loader ---------- */

/*
 * coaster_load - read the player's COASTER.EXE, unpack it, and place the
 * relocated image into flat memory at COASTER_LOAD_SEG. Returns 0 on success.
 */
int coaster_load(CPU *cpu, const char *exe_path)
{
    FILE *f = fopen(exe_path, "rb");
    if (!f) {
        fprintf(stderr, "[LOAD] cannot open %s\n", exe_path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *packed = malloc(len);
    if (!packed || fread(packed, 1, len, f) != (size_t)len) {
        fclose(f);
        free(packed);
        return -1;
    }
    fclose(f);

    uint8_t *image = NULL;
    long img_len = 0;
    uint16_t (*relocs)[2] = NULL;
    int nrel = 0;
    int rc = lzexe_unpack(packed, len, &image, &img_len, &relocs, &nrel);
    free(packed);
    if (rc != 0) {
        fprintf(stderr, "[LOAD] LZEXE unpack failed (code %d). "
                        "Is this the original packed COASTER.EXE?\n", rc);
        free(image);
        free(relocs);
        return -1;
    }

    uint16_t hdr_para = rd16(image, 8);
    long hdr = (long)hdr_para * 16;
    long load_size = img_len - hdr;

    uint32_t dest = seg_off(COASTER_LOAD_SEG, 0);
    if (dest + load_size > MEM_SIZE) {
        fprintf(stderr, "[LOAD] image too large for address space\n");
        free(image);
        free(relocs);
        return -1;
    }
    memcpy(cpu->mem + dest, image + hdr, load_size);

    /* apply relocations: add the load segment to each fixup word */
    for (int i = 0; i < nrel; i++) {
        uint32_t fa = dest + (uint32_t)relocs[i][1] * 16 + relocs[i][0];
        uint16_t v = (uint16_t)cpu->mem[fa] | ((uint16_t)cpu->mem[fa + 1] << 8);
        v = (uint16_t)(v + COASTER_LOAD_SEG);
        cpu->mem[fa] = (uint8_t)(v & 0xFF);
        cpu->mem[fa + 1] = (uint8_t)(v >> 8);
    }

    printf("[LOAD] COASTER.EXE unpacked: %ld bytes, %d relocations, "
           "loaded at %04X:0000\n", load_size, nrel, COASTER_LOAD_SEG);

    free(image);
    free(relocs);
    return 0;
}

/*
 * coaster_start - put the CPU in the register state DOS hands a freshly loaded
 * program, then call the recompiled Microsoft-C start-up routine, which sets up
 * DGROUP/stack, clears BSS, and calls the game's main().
 */
void coaster_start(CPU *cpu)
{
    cpu->cs = COASTER_LOAD_SEG;        /* header CS = 0 */
    cpu->ip = 0x1C71;                  /* header IP (entry) */
    cpu->ss = COASTER_LOAD_SEG + COASTER_DGROUP;
    cpu->sp = 0x0000;                  /* header SP (wraps to top of 64K stack) */
    cpu->ds = COASTER_PSP_SEG;         /* DS = ES = PSP at entry */
    cpu->es = COASTER_PSP_SEG;
    cpu->bp = 0;
    cpu->halted = 0;

    printf("[START] entry %s  CS:IP=%04X:%04X SS:SP=%04X:%04X DS=%04X\n",
           "COASTER_ENTRY_POINT", cpu->cs, cpu->ip, cpu->ss, cpu->sp, cpu->ds);

    COASTER_ENTRY_POINT(cpu);
}
