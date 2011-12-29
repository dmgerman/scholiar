/*
 * Copyright � 2002, 2003 Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Sun Microsystems, Inc. nor the names of 
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * This software is provided "AS IS," without a warranty of any kind.
 *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES,
 * INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED.
 * SUN AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES OR
 * LIABILITIES SUFFERED BY LICENSEE AS A RESULT OF OR RELATING TO USE,
 * MODIFICATION OR DISTRIBUTION OF THE SOFTWARE OR ITS DERIVATIVES.
 * IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST REVENUE,
 * PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL,
 * INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE
 * THEORY OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE
 * SOFTWARE, EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

/* $Id: ttcr.c,v 1.7 2005/01/04 20:10:46 jody Exp $ */
/* @(#)ttcr.c 1.7 03/01/08 SMI */

/*
 * @file ttcr.c
 * @brief TrueTypeCreator method implementation
 * @author Alexander Gelfenbain <adg@sun.com>
 * @version 1.3
 *
 */

#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#ifdef sun
#include <strings.h> /* bzero() only in strings.h on Solaris */
#else
#include <string.h>
#endif
#include <assert.h>
#include "ttcr.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* These must be #defined so that they can be used in initializers */
#define T_maxp  0x6D617870
#define T_glyf  0x676C7966
#define T_head  0x68656164
#define T_loca  0x6C6F6361
#define T_name  0x6E616D65
#define T_hhea  0x68686561
#define T_hmtx  0x686D7478
#define T_cmap  0x636D6170
#define T_vhea  0x76686561
#define T_vmtx  0x766D7478
#define T_OS2   0x4F532F32
#define T_post  0x706F7374
#define T_kern  0x6B65726E
#define T_cvt   0x63767420

typedef struct {
    guint32 tag;
    guint32 length;
    guint8  *data;
} TableEntry;

/*
 * this is a duplicate code from sft.c but it is left here for performance reasons
 */
#ifdef __GNUC__
#define _inline static __inline__
#else
#define _inline static
#endif

_inline guint32 mkTag(guint8 a, guint8 b, guint8 c, guint8 d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/*- Data access macros for data stored in big-endian or little-endian format */
_inline gint16 GetInt16(const guint8 *ptr, size_t offset, int bigendian)
{
    gint16 t;
    assert(ptr != 0);
    
    if (bigendian) {
        t = (ptr+offset)[0] << 8 | (ptr+offset)[1];
    } else {
        t = (ptr+offset)[1] << 8 | (ptr+offset)[0];
    }
        
    return t;
}

_inline guint16 GetUInt16(const guint8 *ptr, size_t offset, int bigendian)
{
    guint16 t;
    assert(ptr != 0);

    if (bigendian) {
        t = (ptr+offset)[0] << 8 | (ptr+offset)[1];
    } else {
        t = (ptr+offset)[1] << 8 | (ptr+offset)[0];
    }

    return t;
}

_inline gint32  GetInt32(const guint8 *ptr, size_t offset, int bigendian)
{
    gint32 t;
    assert(ptr != 0);
    
    if (bigendian) {
        t = (ptr+offset)[0] << 24 | (ptr+offset)[1] << 16 |
            (ptr+offset)[2] << 8  | (ptr+offset)[3];
    } else {
        t = (ptr+offset)[3] << 24 | (ptr+offset)[2] << 16 |
            (ptr+offset)[1] << 8  | (ptr+offset)[0];
    }
        
    return t;
}

_inline guint32 GetUInt32(const guint8 *ptr, size_t offset, int bigendian)
{
    guint32 t;
    assert(ptr != 0);
    

    if (bigendian) {
        t = (ptr+offset)[0] << 24 | (ptr+offset)[1] << 16 |
            (ptr+offset)[2] << 8  | (ptr+offset)[3];
    } else {
        t = (ptr+offset)[3] << 24 | (ptr+offset)[2] << 16 |
            (ptr+offset)[1] << 8  | (ptr+offset)[0];
    }
        
    return t;
}


_inline void PutInt16(gint16 val, guint8 *ptr, size_t offset, int bigendian)
{
    assert(ptr != 0);

    if (bigendian) {
        ptr[offset] = (val >> 8) & 0xFF;
        ptr[offset+1] = val & 0xFF;
    } else {
        ptr[offset+1] = (val >> 8) & 0xFF;
        ptr[offset] = val & 0xFF;
    }

}

_inline void PutUInt16(guint16 val, guint8 *ptr, size_t offset, int bigendian)
{
    assert(ptr != 0);

    if (bigendian) {
        ptr[offset] = (val >> 8) & 0xFF;
        ptr[offset+1] = val & 0xFF;
    } else {
        ptr[offset+1] = (val >> 8) & 0xFF;
        ptr[offset] = val & 0xFF;
    }

}


_inline void PutUInt32(guint32 val, guint8 *ptr, size_t offset, int bigendian)
{
    assert(ptr != 0);

    if (bigendian) {
        ptr[offset] = (val >> 24) & 0xFF;
        ptr[offset+1] = (val >> 16) & 0xFF;
        ptr[offset+2] = (val >> 8) & 0xFF;
        ptr[offset+3] = val & 0xFF;
    } else {
        ptr[offset+3] = (val >> 24) & 0xFF;
        ptr[offset+2] = (val >> 16) & 0xFF;
        ptr[offset+1] = (val >> 8) & 0xFF;
        ptr[offset] = val & 0xFF;
    }

}


_inline void PutInt32(gint32 val, guint8 *ptr, size_t offset, int bigendian)
{
    assert(ptr != 0);

    if (bigendian) {
        ptr[offset] = (val >> 24) & 0xFF;
        ptr[offset+1] = (val >> 16) & 0xFF;
        ptr[offset+2] = (val >> 8) & 0xFF;
        ptr[offset+3] = val & 0xFF;
    } else {
        ptr[offset+3] = (val >> 24) & 0xFF;
        ptr[offset+2] = (val >> 16) & 0xFF;
        ptr[offset+1] = (val >> 8) & 0xFF;
        ptr[offset] = val & 0xFF;
    }

}

static int TableEntryCompareF(const void *l, const void *r)
{
    return ((const TableEntry *) l)->tag - ((const TableEntry *) r)->tag;
}

static int NameRecordCompareF(const void *l, const void *r)
{
    NameRecord *ll = (NameRecord *) l;
    NameRecord *rr = (NameRecord *) r;

    if (ll->platformID != rr->platformID) {
        return ll->platformID - rr->platformID;
    } else if (ll->encodingID != rr->encodingID) {
        return ll->encodingID - rr->encodingID;
    } else if (ll->languageID != rr->languageID) {
        return ll->languageID - rr->languageID;
    } else if (ll->nameID != rr->nameID) {
        return ll->nameID - rr->nameID;
    }
    return 0;
}
    

static guint32 CheckSum(guint32 *ptr, guint32 length)
{
    guint32 sum = 0;
    guint32 *endptr = ptr + ((length + 3) & (guint32) ~3) / 4;

    while (ptr < endptr) sum += *ptr++;

    return sum;
}

_inline void *smalloc(size_t size)
{
    void *res = malloc(size);
    assert(res != 0);
    return res;
}

_inline void *scalloc(size_t n, size_t size)
{
    void *res = calloc(n, size);
    assert(res != 0);
    return res;
}

/*
 * Public functions
 */

void TrueTypeCreatorNewEmpty(guint32 tag, TrueTypeCreator **_this)
{
    TrueTypeCreator *ptr = smalloc(sizeof(TrueTypeCreator));

    ptr->tables = listNewEmpty();
    listSetElementDtor(ptr->tables, (GDestroyNotify)TrueTypeTableDispose);

    ptr->tag = tag;

    *_this = ptr;
}

void TrueTypeCreatorDispose(TrueTypeCreator *_this)
{
    listDispose(_this->tables);
    free(_this);
}

int AddTable(TrueTypeCreator *_this, TrueTypeTable *table)
{
    if (table != 0) {
        listAppend(_this->tables, table);
    }
    return SF_OK;
}

void RemoveTable(TrueTypeCreator *_this, guint32 tag)
{
    int done = 0;
    
    if (listCount(_this->tables)) {
        listToFirst(_this->tables);
        do {
            if (((TrueTypeTable *) listCurrent(_this->tables))->tag == tag) {
                listRemove(_this->tables);
            } else {
                if (listNext(_this->tables)) {
                    done = 1;
                }
            }
        } while (!done);
    }
}

static void ProcessTables(TrueTypeCreator *);

int StreamToMemory(TrueTypeCreator *_this, guint8 **ptr, guint32 *length)
{
    guint16 numTables, searchRange=1, entrySelector=0, rangeShift;
    guint32 s, offset, checkSumAdjustment = 0;
    guint32 *p;
    guint8 *ttf;
    int i=0, n;
    TableEntry *te;
    guint8 *head = NULL;     /* saved pointer to the head table data for checkSumAdjustment calculation */
    
    if ((n = listCount(_this->tables)) == 0) return SF_TTFORMAT;

    ProcessTables(_this);

    /* ProcessTables() adds 'loca' and 'hmtx' */
    
    n = listCount(_this->tables);
    numTables = (guint16) n;
    

    te = scalloc(n, sizeof(TableEntry));

    listToFirst(_this->tables);
    for (i = 0; i < n; i++) {
        GetRawData((TrueTypeTable *) listCurrent(_this->tables), &te[i].data, &te[i].length, &te[i].tag);
        listNext(_this->tables);
    }

    qsort(te, n, sizeof(TableEntry), TableEntryCompareF);
    
    do {
        searchRange *= 2;
        entrySelector++;
    } while (searchRange <= numTables);

    searchRange *= 8;
    entrySelector--;
    rangeShift = numTables * 16 - searchRange;

    s = offset = 12 + 16 * n;

    for (i = 0; i < n; i++) {
        s += (te[i].length + 3) & (guint32) ~3;
        /* if ((te[i].length & 3) != 0) s += (4 - (te[i].length & 3)) & 3; */
    }

    ttf = smalloc(s);

    /* Offset Table */
    PutUInt32(_this->tag, ttf, 0, 1);
    PutUInt16(numTables, ttf, 4, 1);
    PutUInt16(searchRange, ttf, 6, 1);
    PutUInt16(entrySelector, ttf, 8, 1);
    PutUInt16(rangeShift, ttf, 10, 1);

    /* Table Directory */
    for (i = 0; i < n; i++) {
        PutUInt32(te[i].tag, ttf + 12, 16 * i, 1);
        PutUInt32(CheckSum((guint32 *) te[i].data, te[i].length), ttf + 12, 16 * i + 4, 1);
        PutUInt32(offset, ttf + 12, 16 * i + 8, 1);
        PutUInt32(te[i].length, ttf + 12, 16 * i + 12, 1);

        if (te[i].tag == T_head) {
            head = ttf + offset;
        }

        memcpy(ttf+offset, te[i].data, (te[i].length + 3) & (guint32) ~3 );
        offset += (te[i].length + 3) & (guint32) ~3;
        /* if ((te[i].length & 3) != 0) offset += (4 - (te[i].length & 3)) & 3; */
    }

    free(te);

    p = (guint32 *) ttf;
    for (i = 0; i < s / 4; i++) checkSumAdjustment += p[i];
    PutUInt32(0xB1B0AFBA - checkSumAdjustment, head, 8, 1);

    *ptr = ttf;
    *length = s;
    
    return SF_OK;
}

int StreamToFile(TrueTypeCreator *_this, const char* fname)
{
    guint8 *ptr;
    guint32 length;
    int fd, r;

    if (!fname) return SF_BADFILE;
    if ((fd = open(fname, O_WRONLY | O_BINARY | O_CREAT | O_TRUNC, 0644)) == -1) return SF_BADFILE;

    if ((r = StreamToMemory(_this, &ptr, &length)) != SF_OK) return r;

    if (write(fd, ptr, length) != length) {
        r = SF_FILEIO;
    } else {
        r = SF_OK;
    }

    close(fd);
    free(ptr);
    return r;
}



/*
 * TrueTypeTable private methods
 */

#define TABLESIZE_head 54
#define TABLESIZE_hhea 36
#define TABLESIZE_maxp 32



/*    Table         data points to
 * --------------------------------------------
 *    generic       tdata_generic struct
 *    'head'        TABLESIZE_head bytes of memory
 *    'hhea'        TABLESIZE_hhea bytes of memory
 *    'loca'        tdata_loca struct
 *    'maxp'        TABLESIZE_maxp bytes of memory
 *    'glyf'        list of GlyphData structs (defined in sft.h)
 *    'name'        list of NameRecord structs (defined in sft.h)
 *    'post'        tdata_post struct
 *
 */


#define CMAP_SUBTABLE_INIT 10
#define CMAP_SUBTABLE_INCR 10
#define CMAP_PAIR_INIT 500
#define CMAP_PAIR_INCR 500

typedef struct {
    guint32  id;                         /* subtable ID (platform/encoding ID)    */
    guint32  n;                          /* number of used translation pairs      */
    guint32  m;                          /* number of allocated translation pairs */
    guint32 *xc;                         /* character array                       */
    guint32 *xg;                         /* glyph array                           */
} CmapSubTable;

typedef struct {
    guint32 n;                           /* number of used CMAP sub-tables       */
    guint32 m;                           /* number of allocated CMAP sub-tables  */
    CmapSubTable *s;                    /* sotred array of sub-tables           */
} table_cmap;

typedef struct {
    guint32 tag;
    guint32 nbytes;
    guint8 *ptr;
} tdata_generic;

typedef struct {
    guint32 nbytes;                      /* number of bytes in loca table */
    guint8 *ptr;                          /* pointer to the data */
} tdata_loca;

typedef struct {
    guint32 format;
    guint32 italicAngle;
    gint16  underlinePosition;
    gint16  underlineThickness;
    guint32 isFixedPitch;
    void   *ptr;                        /* format-specific pointer */
} tdata_post;
    

/* allocate memory for a TT table */
static guint8 *ttmalloc(guint32 nbytes)
{
    guint32 n; 
    guint8 *res;

    n = (nbytes + 3) & (guint32) ~3;
    res = malloc(n);
    assert(res != 0);
    bzero(res, n);

    return res;
}
    
static void FreeGlyphData(void *ptr)
{
    GlyphData *p = (GlyphData *) ptr;
    if (p->ptr) free(p->ptr);
    free(p);
}

static void TrueTypeTableDispose_generic(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) {
            tdata_generic *pdata = (tdata_generic *) _this->data;
            if (pdata->nbytes) free(pdata->ptr);
            free(_this->data);
        }
        free(_this);
    }
}

static void TrueTypeTableDispose_head(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) free(_this->data);
        free(_this);
    }
}

static void TrueTypeTableDispose_hhea(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) free(_this->data);
        free(_this);
    }
}

static void TrueTypeTableDispose_loca(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) {
            tdata_loca *p = (tdata_loca *) _this->data;
            if (p->ptr) free(p->ptr);
            free(_this->data);
        }
        free(_this);
    }
}

static void TrueTypeTableDispose_maxp(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) free(_this->data);
        free(_this);
    }
}

static void TrueTypeTableDispose_glyf(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) listDispose((list) _this->data);
        free(_this);
    }
}

static void TrueTypeTableDispose_cmap(TrueTypeTable *_this)
{
    table_cmap *t;
    CmapSubTable *s;
    int i;
    
    if (_this) {
        t = (table_cmap *) _this->data;
        if (t) {
            s = t->s;
            if (s) {
                for (i = 0; i < t->m; i++) {
                    if (s[i].xc) free(s[i].xc);
                    if (s[i].xg) free(s[i].xg);
                }
                free(s);
            }
            free(t);
        }
        free(_this);
    }
}

static void TrueTypeTableDispose_name(TrueTypeTable *_this)
{
    if (_this) {
        if (_this->data) listDispose((list) _this->data);
        free(_this);
    }
}

static void TrueTypeTableDispose_post(TrueTypeTable *_this)
{
    if (_this) {
        tdata_post *p = (tdata_post *) _this->data;
        if (p) {
            if (p->format == 0x00030000) {
                /* do nothing */
            } else {
                fprintf(stderr, "Unsupported format of a 'post' table: %08X.\n", p->format);
            }
            free(p);
        }
        free(_this);
    }
}

/* destructor vtable */

static struct {
    guint32 tag;
    void (*f)(TrueTypeTable *);
} vtable1[] =
{
    {0,      TrueTypeTableDispose_generic},
    {T_head, TrueTypeTableDispose_head},
    {T_hhea, TrueTypeTableDispose_hhea},
    {T_loca, TrueTypeTableDispose_loca},
    {T_maxp, TrueTypeTableDispose_maxp},
    {T_glyf, TrueTypeTableDispose_glyf},
    {T_cmap, TrueTypeTableDispose_cmap},
    {T_name, TrueTypeTableDispose_name},
    {T_post, TrueTypeTableDispose_post}
    
};

static int GetRawData_generic(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    assert(_this != 0);
    assert(_this->data != 0);

    *ptr = ((tdata_generic *) _this->data)->ptr;
    *len = ((tdata_generic *) _this->data)->nbytes;
    *tag = ((tdata_generic *) _this->data)->tag;

    return TTCR_OK;
}


static int GetRawData_head(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    *len = TABLESIZE_head;
    *ptr = (guint8 *) _this->data;
    *tag = T_head;
    
    return TTCR_OK;
}

static int GetRawData_hhea(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    *len = TABLESIZE_hhea;
    *ptr = (guint8 *) _this->data;
    *tag = T_hhea;
    
    return TTCR_OK;
}

static int GetRawData_loca(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    tdata_loca *p;

    assert(_this->data != 0);

    p = (tdata_loca *) _this->data;

    if (p->nbytes == 0) return TTCR_ZEROGLYPHS;

    *ptr = p->ptr;
    *len = p->nbytes;
    *tag = T_loca;

    return TTCR_OK;
}

static int GetRawData_maxp(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    *len = TABLESIZE_maxp;
    *ptr = (guint8 *) _this->data;
    *tag = T_maxp;
    
    return TTCR_OK;
}

static int GetRawData_glyf(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    guint32 n, nbytes = 0;
    list l = (list) _this->data;
    /* guint16 curID = 0;    */               /* to check if glyph IDs are sequential and start from zero */
    guint8 *p;

    *ptr = NULL;
    *len = 0;
    *tag = 0;

    if (listCount(l) == 0) return TTCR_ZEROGLYPHS;

    listToFirst(l);
    do {
        /* if (((GlyphData *) listCurrent(l))->glyphID != curID++) return TTCR_GLYPHSEQ; */
        nbytes += ((GlyphData *) listCurrent(l))->nbytes;
    } while (listNext(l));

    p = _this->rawdata = ttmalloc(nbytes);

    listToFirst(l);
    do {
        n = ((GlyphData *) listCurrent(l))->nbytes;
        if (n != 0) {
            memcpy(p, ((GlyphData *) listCurrent(l))->ptr, n);
            p += n;
        }
    } while (listNext(l));

    *len = nbytes;
    *ptr = _this->rawdata;
    *tag = T_glyf;

    return TTCR_OK;
}

/* cmap packers */
static guint8 *PackCmapType0(CmapSubTable *s, guint32 *length)
{
    guint8 *ptr = smalloc(262);
    guint8 *p = ptr + 6;
    int i, j;
    guint16 g;

    PutUInt16(0, ptr, 0, 1);
    PutUInt16(262, ptr, 2, 1);
    PutUInt16(0, ptr, 4, 1);

    for (i = 0; i < 256; i++) {
        g = 0;
        for (j = 0; j < s->n; j++) {
            if (s->xc[j] == i) {
                g = (guint16) s->xg[j];
            }
        }
        p[i] = (guint8) g;
    }
    *length = 262;
    return ptr;
}
            

/* XXX it only handles Format 0 encoding tables */
static guint8 *PackCmap(CmapSubTable *s, guint32 *length)
{
    return PackCmapType0(s, length);
}

static int GetRawData_cmap(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    table_cmap *t;
    guint8 **subtables;
    guint32 *sizes;            /* of subtables */
    int i;
    guint32 tlen = 0;
    guint32 l;
    guint32 cmapsize;
    guint8 *cmap;
    guint32 coffset;

    assert(_this != 0);
    t = (table_cmap *) _this->data;
    assert(t != 0);
    assert(t->n != 0);

    subtables = scalloc(t->n, sizeof(guint8 *));
    sizes = scalloc(t->n, sizeof(guint32));

    for (i = 0; i < t->n; i++) {
        subtables[i] = PackCmap(t->s+i, &l);
        sizes[i] = l;
        tlen += l;
    }

    cmapsize = tlen + 4 + 8 * t->n;
    _this->rawdata = cmap = ttmalloc(cmapsize);

    PutUInt16(0, cmap, 0, 1);
    PutUInt16(t->n, cmap, 2, 1);
    coffset = 4 + t->n * 8;

    for (i = 0; i < t->n; i++) {
        PutUInt16(t->s[i].id >> 16, cmap + 4, i * 8, 1);
        PutUInt16(t->s[i].id & 0xFF, cmap + 4, 2 + i * 8, 1);
        PutUInt32(coffset, cmap + 4, 4 + i * 8, 1);
        memcpy(cmap + coffset, subtables[i], sizes[i]);
        free(subtables[i]);
        coffset += sizes[i];
    }

    free(subtables);
    free(sizes);

    *ptr = cmap;
    *len = cmapsize;
    *tag = T_cmap;

    return TTCR_OK;
}


static int GetRawData_name(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    list l;
    NameRecord *nr;
    gint16 i=0, n;                          /* number of Name Records */
    guint8 *name;
    guint16 nameLen;
    int stringLen = 0;
    guint8 *p1, *p2;

    *ptr = NULL;
    *len = 0;
    *tag = 0;

    assert(_this != 0);
    l = (list) _this->data;
    assert(l != 0);

    if ((n = listCount(l)) == 0) return TTCR_NONAMES;

    nr = scalloc(n, sizeof(NameRecord));

    listToFirst(l);

    do {
        memcpy(nr+i, listCurrent(l), sizeof(NameRecord));
        stringLen += nr[i].slen;
        i++;
    } while (listNext(l));

    if (stringLen > 65535) {
        free(nr);
        return TTCR_NAMETOOLONG;
    }

    qsort(nr, n, sizeof(NameRecord), NameRecordCompareF);

    nameLen = stringLen + 12 * n + 6;
    name = ttmalloc(nameLen); 

    PutUInt16(0, name, 0, 1);
    PutUInt16(n, name, 2, 1);
    PutUInt16(6 + 12 * n, name, 4, 1);

    p1 = name + 6;
    p2 = p1 + 12 * n;

    for (i = 0; i < n; i++) {
        PutUInt16(nr[i].platformID, p1, 0, 1);
        PutUInt16(nr[i].encodingID, p1, 2, 1);
        PutUInt16(nr[i].languageID, p1, 4, 1);
        PutUInt16(nr[i].nameID, p1, 6, 1);
        PutUInt16(nr[i].slen, p1, 8, 1);
        PutUInt16(p2 - (name + 6 + 12 * n), p1, 10, 1);
        memcpy(p2, nr[i].sptr, nr[i].slen);
        /* {int j; for(j=0; j<nr[i].slen; j++) printf("%c", nr[i].sptr[j]); printf("\n"); }; */
        p2 += nr[i].slen;
        p1 += 12;
    }

    free(nr);
    _this->rawdata = name;

    *ptr = name;
    *len = nameLen;
    *tag = T_name;

     /*{int j; for(j=0; j<nameLen; j++) printf("%c", name[j]); }; */

    return TTCR_OK;
}

static int GetRawData_post(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    tdata_post *p = (tdata_post *) _this->data;
    guint8 *post = NULL;
    guint32 postLen = 0;
    int ret;

    if (_this->rawdata) free(_this->rawdata);

    if (p->format == 0x00030000) {
        postLen = 32;
        post = ttmalloc(postLen);
        PutUInt32(0x00030000, post, 0, 1);
        PutUInt32(p->italicAngle, post, 4, 1);
        PutUInt16(p->underlinePosition, post, 8, 1);
        PutUInt16(p->underlineThickness, post, 10, 1);
        PutUInt16(p->isFixedPitch, post, 12, 1);
        ret = TTCR_OK;
    } else {
        fprintf(stderr, "Unrecognized format of a post table: %08X.\n", p->format);
        ret = TTCR_POSTFORMAT;
    }

    *ptr = _this->rawdata = post;
    *len = postLen;
    *tag = T_post;

    return ret;
}

    

    

static struct {
    guint32 tag;
    int (*f)(TrueTypeTable *, guint8 **, guint32 *, guint32 *);
} vtable2[] =
{
    {0,      GetRawData_generic},
    {T_head, GetRawData_head},
    {T_hhea, GetRawData_hhea},
    {T_loca, GetRawData_loca},
    {T_maxp, GetRawData_maxp},
    {T_glyf, GetRawData_glyf},
    {T_cmap, GetRawData_cmap},
    {T_name, GetRawData_name},
    {T_post, GetRawData_post}
    
    
};
 
/*
 * TrueTypeTable public methods
 */

/* Note: Type42 fonts only need these tables:
 *        head, hhea, loca, maxp, cvt, prep, glyf, hmtx, fpgm
 *
 * Microsoft required tables
 *        cmap, glyf, head, hhea, hmtx, loca, maxp, name, post, OS/2
 *
 * Apple required tables
 *        cmap, glyf, head, hhea, hmtx, loca, maxp, name, post
 *
 */

TrueTypeTable *TrueTypeTableNew(guint32 tag,
                                guint32 nbytes,
                                guint8 *ptr)
{
    TrueTypeTable *table;
    tdata_generic *pdata;

    table = smalloc(sizeof(TrueTypeTable));
    pdata = (tdata_generic *) smalloc(sizeof(tdata_generic)); 
    pdata->nbytes = nbytes;
    pdata->tag = tag;
    if (nbytes) {
        pdata->ptr = ttmalloc(nbytes); 
        memcpy(pdata->ptr, ptr, nbytes);
    } else {
        pdata->ptr = NULL;
    }

    table->tag = 0;
    table->data = pdata;
    table->rawdata = NULL;

    return table;
}
    
TrueTypeTable *TrueTypeTableNew_head(guint32 fontRevision,
                                     guint16 flags,
                                     guint16 unitsPerEm,
                                     guint8  *created,
                                     guint16 macStyle,
                                     guint16 lowestRecPPEM,
                                     gint16  fontDirectionHint)
{
    TrueTypeTable *table;
    guint8 *ptr;

    assert(created != 0);

    table  = smalloc(sizeof(TrueTypeTable));  
    ptr  = ttmalloc(TABLESIZE_head);


    PutUInt32(0x00010000, ptr, 0, 1);             /* version */
    PutUInt32(fontRevision, ptr, 4, 1);
    PutUInt32(0x5F0F3CF5, ptr, 12, 1);            /* magic number */
    PutUInt16(flags, ptr, 16, 1);
    PutUInt16(unitsPerEm, ptr, 18, 1);
    memcpy(ptr+20, created, 8);                   /* Created Long Date */
    bzero(ptr+28, 8);                             /* Modified Long Date */
    PutUInt16(macStyle, ptr, 44, 1);
    PutUInt16(lowestRecPPEM, ptr, 46, 1);
    PutUInt16(fontDirectionHint, ptr, 48, 1);
    PutUInt16(0, ptr, 52, 1);                     /* glyph data format: 0 */

    table->data = (void *) ptr;
    table->tag = T_head;
    table->rawdata = NULL;

    return table;
}

TrueTypeTable *TrueTypeTableNew_hhea(gint16  ascender,
                                     gint16  descender,
                                     gint16  linegap,
                                     gint16  caretSlopeRise,
                                     gint16  caretSlopeRun)
{
    TrueTypeTable *table;
    guint8 *ptr;

    table  = smalloc(sizeof(TrueTypeTable));
    ptr  = ttmalloc(TABLESIZE_hhea);

    PutUInt32(0x00010000, ptr, 0, 1);             /* version */
    PutUInt16(ascender, ptr, 4, 1);
    PutUInt16(descender, ptr, 6, 1);
    PutUInt16(linegap, ptr, 8, 1);
    PutUInt16(caretSlopeRise, ptr, 18, 1);
    PutUInt16(caretSlopeRun, ptr, 20, 1);
    PutUInt16(0, ptr, 22, 1);                     /* reserved 1 */
    PutUInt16(0, ptr, 24, 1);                     /* reserved 2 */
    PutUInt16(0, ptr, 26, 1);                     /* reserved 3 */
    PutUInt16(0, ptr, 28, 1);                     /* reserved 4 */
    PutUInt16(0, ptr, 30, 1);                     /* reserved 5 */
    PutUInt16(0, ptr, 32, 1);                     /* metricDataFormat */
    
    table->data = (void *) ptr;
    table->tag = T_hhea;
    table->rawdata = NULL;

    return table;
}

TrueTypeTable *TrueTypeTableNew_loca(void)
{
    TrueTypeTable *table = smalloc(sizeof(TrueTypeTable));
    table->data = smalloc(sizeof(tdata_loca));

    ((tdata_loca *)table->data)->nbytes = 0;
    ((tdata_loca *)table->data)->ptr = NULL;

    table->tag = T_loca;
    table->rawdata = NULL;

    return table;
}

TrueTypeTable *TrueTypeTableNew_maxp(guint8 *maxp, int size)
{
    TrueTypeTable *table = smalloc(sizeof(TrueTypeTable));
    table->data = ttmalloc(TABLESIZE_maxp);

    if (maxp && size == TABLESIZE_maxp) {
        memcpy(table->data, maxp, TABLESIZE_maxp);
    }
    
    table->tag = T_maxp;
    table->rawdata = NULL;

    return table;
}

TrueTypeTable *TrueTypeTableNew_glyf(void)
{
    TrueTypeTable *table = smalloc(sizeof(TrueTypeTable));
    list l = listNewEmpty();
    
    assert(l != 0);

    listSetElementDtor(l, FreeGlyphData);

    table->data = l;
    table->rawdata = NULL;
    table->tag = T_glyf;

    return table;
}

TrueTypeTable *TrueTypeTableNew_cmap(void)
{
    TrueTypeTable *table = smalloc(sizeof(TrueTypeTable));
    table_cmap *cmap = smalloc(sizeof(table_cmap));
    
    cmap->n = 0;
    cmap->m = CMAP_SUBTABLE_INIT;
    cmap->s = (CmapSubTable *) scalloc(CMAP_SUBTABLE_INIT, sizeof(CmapSubTable));
    memset(cmap->s, 0, sizeof(CmapSubTable) * CMAP_SUBTABLE_INIT);
    
    table->data = (table_cmap *) cmap;

    table->rawdata = NULL;
    table->tag = T_cmap;

    return table;
}

static void DisposeNameRecord(void *ptr)
{
    if (ptr != 0) {
        NameRecord *nr = (NameRecord *) ptr;
        if (nr->sptr) free(nr->sptr);
        free(ptr);
    }
}

static NameRecord* NameRecordNewCopy(NameRecord *nr)
{
    NameRecord *p = smalloc(sizeof(NameRecord));

    memcpy(p, nr, sizeof(NameRecord));

    if (p->slen) {
        p->sptr = smalloc(p->slen);
        memcpy(p->sptr, nr->sptr, p->slen);
    }

    return p;
}

TrueTypeTable *TrueTypeTableNew_name(int n, NameRecord *nr)
{
    TrueTypeTable *table = smalloc(sizeof(TrueTypeTable));
    list l = listNewEmpty();
    
    assert(l != 0);

    listSetElementDtor(l, DisposeNameRecord);

    if (n != 0) {
        int i;
        for (i = 0; i < n; i++) {
            listAppend(l, NameRecordNewCopy(nr+i));
        }
    }

    table->data = l;
    table->rawdata = NULL;
    table->tag = T_name;

    return table;
}

TrueTypeTable *TrueTypeTableNew_post(guint32 format,
                                     guint32 italicAngle,
                                     gint16 underlinePosition,
                                     gint16 underlineThickness,
                                     guint32 isFixedPitch)
{
    TrueTypeTable *table;
    tdata_post *post;

    assert(format == 0x00030000);                 /* Only format 3.0 is supported at this time */
    table = smalloc(sizeof(TrueTypeTable));
    post = smalloc(sizeof(tdata_post));

    post->format = format;
    post->italicAngle = italicAngle;
    post->underlinePosition = underlinePosition;
    post->underlineThickness = underlineThickness;
    post->isFixedPitch = isFixedPitch;
    post->ptr = NULL;

    table->data = post;
    table->rawdata = NULL;
    table->tag = T_post;

    return table;
}



void TrueTypeTableDispose(TrueTypeTable *_this)
{
    /* XXX do a binary search */
    int i;

    assert(_this != 0);

    if (_this->rawdata) free(_this->rawdata);

    for(i=0; i < sizeof(vtable1)/sizeof(*vtable1); i++) {
        if (_this->tag == vtable1[i].tag) {
            vtable1[i].f(_this);
            return;
        }
    }
    assert(!"Unknown TrueType table.\n");
}

int GetRawData(TrueTypeTable *_this, guint8 **ptr, guint32 *len, guint32 *tag)
{
    /* XXX do a binary search */
    int i;

    assert(_this != 0);
    assert(ptr != 0);
    assert(len != 0);
    assert(tag != 0);

    *ptr = NULL; *len = 0; *tag = 0;

    if (_this->rawdata) {
        free(_this->rawdata);
        _this->rawdata = NULL;
    }

    for(i=0; i < sizeof(vtable2)/sizeof(*vtable2); i++) {
        if (_this->tag == vtable2[i].tag) {
            return vtable2[i].f(_this, ptr, len, tag);
        }
    }

    assert(!"Unknwon TrueType table.\n");
    return TTCR_UNKNOWN;
}
    
void cmapAdd(TrueTypeTable *table, guint32 id, guint32 c, guint32 g)
{
    int i, found;
    table_cmap *t;
    CmapSubTable *s;

    assert(table != 0);
    assert(table->tag == T_cmap);
    t = (table_cmap *) table->data; assert(t != 0);
    s = t->s; assert(s != 0);

    found = 0;

    for (i = 0; i < t->n; i++) {
        if (s[i].id == id) {
            found = 1;
            break;
        }
    }

    if (!found) {
        if (t->n == t->m) {
            CmapSubTable *tmp;
            tmp = scalloc(t->m + CMAP_SUBTABLE_INCR, sizeof(CmapSubTable)); 
            memset(tmp, 0, t->m + CMAP_SUBTABLE_INCR * sizeof(CmapSubTable));
            memcpy(tmp, s, sizeof(CmapSubTable) * t->m);
            t->m += CMAP_SUBTABLE_INCR;
            free(s);
            s = tmp;
            t->s = s;
        }

        for (i = 0; i < t->n; i++) {
            if (s[i].id > id) break;
        }

        if (i < t->n) {
            memmove(s+i+1, s+i, t->n-i);
        }

        t->n++;

        s[i].id = id;
        s[i].n = 0;
        s[i].m = CMAP_PAIR_INIT;
        s[i].xc = scalloc(CMAP_PAIR_INIT, sizeof(guint32));
        s[i].xg = scalloc(CMAP_PAIR_INIT, sizeof(guint32));
    }

    if (s[i].n == s[i].m) {
        guint32 *tmp1 = scalloc(s[i].m + CMAP_PAIR_INCR, sizeof(guint32));
        guint32 *tmp2 = scalloc(s[i].m + CMAP_PAIR_INCR, sizeof(guint32));
        assert(tmp1 != 0);
        assert(tmp2 != 0);
        memcpy(tmp1, s[i].xc, sizeof(guint32) * s[i].m);
        memcpy(tmp2, s[i].xg, sizeof(guint32) * s[i].m);
        s[i].m += CMAP_PAIR_INCR;
        free(s[i].xc);
        free(s[i].xg);
        s[i].xc = tmp1;
        s[i].xg = tmp2;
    }

    s[i].xc[s[i].n] = c;
    s[i].xg[s[i].n] = g;
    s[i].n++;
}

guint32 glyfAdd(TrueTypeTable *table, GlyphData *glyphdata, TrueTypeFont *fnt)
{
    list l;
    guint32 currentID;
    int ret, n, ncomponents;
    list glyphlist;
    GlyphData *gd;

    assert(table != 0);
    assert(table->tag == T_glyf);

    if (!glyphdata) return -1;

    glyphlist = listNewEmpty();

    ncomponents = GetTTGlyphComponents(fnt, glyphdata->glyphID, glyphlist);

    l = (list) table->data;
    if (listCount(l) > 0) {
        listToLast(l);
        ret = n = ((GlyphData *) listCurrent(l))->newID + 1;
    } else {
        ret = n = 0;
    }
    glyphdata->newID = n++;
    listAppend(l, glyphdata);

    if (ncomponents > 1) {
        listPositionAt(glyphlist, 1);       /* glyphData->glyphID is always the first glyph on the list */
        do {
            int found = 0;
            currentID = (guint32) listCurrent(glyphlist);
            /* XXX expensive! should be rewritten with sorted arrays! */
            listToFirst(l);
            do {
                if (((GlyphData *) listCurrent(l))->glyphID == currentID) {
                    found = 1;
                    break;
                }
            } while (listNext(l));

            if (!found) {
                gd = GetTTRawGlyphData(fnt, currentID);
                gd->newID = n++;
                listAppend(l, gd);
            }
        } while (listNext(glyphlist));
    }

    listDispose(glyphlist);
    return ret;
}

guint32 glyfCount(const TrueTypeTable *table)
{
    assert(table != 0);
    assert(table->tag == T_glyf);
    return listCount((list) table->data);
}
    

void nameAdd(TrueTypeTable *table, NameRecord *nr)
{
    list l;

    assert(table != 0);
    assert(table->tag == T_name);

    l = (list) table->data;

    listAppend(l, NameRecordNewCopy(nr));
}
               
static TrueTypeTable *FindTable(TrueTypeCreator *tt, guint32 tag)
{
    if (listIsEmpty(tt->tables)) return NULL;

    listToFirst(tt->tables);

    do {
        if (((TrueTypeTable *) listCurrent(tt->tables))->tag == tag) {
            return listCurrent(tt->tables);
        }
    } while (listNext(tt->tables));

    return NULL;
}

/* This function processes all the tables and synchronizes them before creating
 * the output TrueType stream.
 *
 * *** It adds two TrueType tables to the font: 'loca' and 'hmtx' ***
 *
 * It does:
 *
 * - Re-numbers glyph IDs and creates 'glyf', 'loca', and 'hmtx' tables.
 * - Calculates xMin, yMin, xMax, and yMax and stores values in 'head' table.
 * - Stores indexToLocFormat in 'head'
 * - updates 'maxp' table
 * - Calculates advanceWidthMax, minLSB, minRSB, xMaxExtent and numberOfHMetrics
 *   in 'hhea' table
 *
 */
static void ProcessTables(TrueTypeCreator *tt)
{
    TrueTypeTable *glyf, *loca, *head, *maxp, *hhea;
    list glyphlist;
    guint32 nGlyphs, locaLen = 0, glyfLen = 0;
    gint16 xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    int i = 0;
    gint16 indexToLocFormat;
    guint8 *glyfPtr, *locaPtr, *hmtxPtr, *hheaPtr;
    guint32 hmtxSize;
    guint8 *p1, *p2;
    guint16 maxPoints = 0, maxContours = 0, maxCompositePoints = 0, maxCompositeContours = 0;
    TTSimpleGlyphMetrics *met;
    int nlsb = 0;
    guint32 *gid;                        /* array of old glyphIDs */

    glyf = FindTable(tt, T_glyf);
    glyphlist = (list) glyf->data;
    nGlyphs = listCount(glyphlist);
    assert(nGlyphs != 0);
    gid = scalloc(nGlyphs, sizeof(guint32));

    RemoveTable(tt, T_loca);
    RemoveTable(tt, T_hmtx);

    /* XXX Need to make sure that composite glyphs do not break during glyph renumbering */

    listToFirst(glyphlist);
    do {
        GlyphData *gd = (GlyphData *) listCurrent(glyphlist);
        gint16 z;
        glyfLen += gd->nbytes;
        /* XXX if (gd->nbytes & 1) glyfLen++; */


        assert(gd->newID == i);
        gid[i++] = gd->glyphID;
        /* gd->glyphID = i++; */

        /* printf("IDs: %d %d.\n", gd->glyphID, gd->newID); */

        if (gd->nbytes != 0) {
            z = GetInt16(gd->ptr, 2, 1);
            if (z < xMin) xMin = z;

            z = GetInt16(gd->ptr, 4, 1);
            if (z < yMin) yMin = z;
        
            z = GetInt16(gd->ptr, 6, 1);
            if (z > xMax) xMax = z;
        
            z = GetInt16(gd->ptr, 8, 1);
            if (z > yMax) yMax = z;
        }

        if (gd->compflag == 0) {                            /* non-composite glyph */
            if (gd->npoints > maxPoints) maxPoints = gd->npoints;
            if (gd->ncontours > maxContours) maxContours = gd->ncontours;
        } else {                                            /* composite glyph */
            if (gd->npoints > maxCompositePoints) maxCompositePoints = gd->npoints;
            if (gd->ncontours > maxCompositeContours) maxCompositeContours = gd->ncontours;
        }
        
    } while (listNext(glyphlist));

    indexToLocFormat = (glyfLen / 2 > 0xFFFF) ? 1 : 0;
    locaLen = indexToLocFormat ?  (nGlyphs + 1) << 2 : (nGlyphs + 1) << 1;

    glyfPtr = ttmalloc(glyfLen);
    locaPtr = ttmalloc(locaLen); 
    met = scalloc(nGlyphs, sizeof(TTSimpleGlyphMetrics));
    i = 0;

    listToFirst(glyphlist);
    p1 = glyfPtr;
    p2 = locaPtr;
    do {
        GlyphData *gd = (GlyphData *) listCurrent(glyphlist);
        
        if (gd->compflag) {                       /* re-number all components */
            guint16 flags, index;
            guint8 *ptr = gd->ptr + 10;
            do {
                int j;
                flags = GetUInt16(ptr, 0, 1);
                index = GetUInt16(ptr, 2, 1);
                /* XXX use the sorted array of old to new glyphID mapping and do a binary search */
                for (j = 0; j < nGlyphs; j++) {
                    if (gid[j] == index) {
                        break;
                    }
                }
                /* printf("X: %d -> %d.\n", index, j); */

                PutUInt16((guint16) j, ptr, 2, 1);

                ptr += 4;

                if (flags & ARG_1_AND_2_ARE_WORDS) {
                    ptr += 4;
                } else {
                    ptr += 2;
                }

                if (flags & WE_HAVE_A_SCALE) {
                    ptr += 2;
                } else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) {
                    ptr += 4;
                } else if (flags & WE_HAVE_A_TWO_BY_TWO) {
                    ptr += 8;
                }
            } while (flags & MORE_COMPONENTS);
        }

        if (gd->nbytes != 0) {
            memcpy(p1, gd->ptr, gd->nbytes);
        }
        if (indexToLocFormat == 1) {
            PutUInt32(p1 - glyfPtr, p2, 0, 1);
            p2 += 4;
        } else {
            PutUInt16((p1 - glyfPtr) >> 1, p2, 0, 1);
            p2 += 2;
        }
        p1 += gd->nbytes;

        /* fill the array of metrics */
        met[i].adv = gd->aw;
        met[i].sb  = gd->lsb;
        i++;
    } while (listNext(glyphlist));

    free(gid);

    if (indexToLocFormat == 1) {
        PutUInt32(p1 - glyfPtr, p2, 0, 1);
    } else {
        PutUInt16((p1 - glyfPtr) >> 1, p2, 0, 1);
    }

    glyf->rawdata = glyfPtr;

    loca = TrueTypeTableNew_loca(); assert(loca != 0);
    ((tdata_loca *) loca->data)->ptr = locaPtr;
    ((tdata_loca *) loca->data)->nbytes = locaLen;

    AddTable(tt, loca);
    
    head = FindTable(tt, T_head);
    PutInt16(xMin, head->data, 36, 1);
    PutInt16(yMin, head->data, 38, 1);
    PutInt16(xMax, head->data, 40, 1);
    PutInt16(yMax, head->data, 42, 1);
    PutInt16(indexToLocFormat, head->data,  50, 1);

    maxp = FindTable(tt, T_maxp);

    PutUInt16(nGlyphs, maxp->data, 4, 1);
    PutUInt16(maxPoints, maxp->data, 6, 1);
    PutUInt16(maxContours, maxp->data, 8, 1);
    PutUInt16(maxCompositePoints, maxp->data, 10, 1);
    PutUInt16(maxCompositeContours, maxp->data, 12, 1);

#if 0
    /* XXX do not overwrite the existing data. Fix: re-calculate these numbers here */
    PutUInt16(2, maxp->data, 14, 1);                        /* maxZones is always 2       */
    PutUInt16(0, maxp->data, 16, 1);                        /* maxTwilightPoints          */
    PutUInt16(0, maxp->data, 18, 1);                        /* maxStorage                 */
    PutUInt16(0, maxp->data, 20, 1);                        /* maxFunctionDefs            */
    PutUint16(0, maxp->data, 22, 1);                        /* maxInstructionDefs         */
    PutUint16(0, maxp->data, 24, 1);                        /* maxStackElements           */
    PutUint16(0, maxp->data, 26, 1);                        /* maxSizeOfInstructions      */
    PutUint16(0, maxp->data, 28, 1);                        /* maxComponentElements       */
    PutUint16(0, maxp->data, 30, 1);                        /* maxComponentDepth          */
#endif

    /*
     * Generate an htmx table and update hhea table
     */
    hhea = FindTable(tt, T_hhea); assert(hhea != 0);
    hheaPtr = (guint8 *) hhea->data;
    if (nGlyphs > 2) {
        for (i = nGlyphs - 1; i > 0; i--) {
            if (met[i].adv != met[i-1].adv) break;
        }
        nlsb = nGlyphs - 1 - i;
    }
    hmtxSize = (nGlyphs - nlsb) * 4 + nlsb * 2;
    hmtxPtr = ttmalloc(hmtxSize);
    p1 = hmtxPtr;

    for (i = 0; i < nGlyphs; i++) {
        if (i < nGlyphs - nlsb) {
            PutUInt16(met[i].adv, p1, 0, 1);
            PutUInt16(met[i].sb, p1, 2, 1);
            p1 += 4;
        } else {
            PutUInt16(met[i].sb, p1, 0, 1);
            p1 += 2;
        }
    }

    AddTable(tt, TrueTypeTableNew(T_hmtx, hmtxSize, hmtxPtr));
    PutUInt16(nGlyphs - nlsb, hheaPtr, 34, 1);
    free(hmtxPtr);
    free(met);
}

#ifdef TEST_TTCR
int main(void)
{
    TrueTypeCreator *ttcr;
    guint8 *t1, *t2, *t3, *t4, *t5, *t6, *t7;

    TrueTypeCreatorNewEmpty(mkTag('t','r','u','e'), &ttcr);

    t1 = malloc(1000); memset(t1, 'a', 1000);
    t2 = malloc(2000); memset(t2, 'b', 2000);
    t3 = malloc(3000); memset(t3, 'c', 3000);
    t4 = malloc(4000); memset(t4, 'd', 4000);
    t5 = malloc(5000); memset(t5, 'e', 5000);
    t6 = malloc(6000); memset(t6, 'f', 6000);
    t7 = malloc(7000); memset(t7, 'g', 7000);

    AddTable(ttcr, TrueTypeTableNew(0x6D617870, 1000, t1));
    AddTable(ttcr, TrueTypeTableNew(0x4F532F32, 2000, t2));
    AddTable(ttcr, TrueTypeTableNew(0x636D6170, 3000, t3));
    AddTable(ttcr, TrueTypeTableNew(0x6C6F6361, 4000, t4));
    AddTable(ttcr, TrueTypeTableNew(0x68686561, 5000, t5));
    AddTable(ttcr, TrueTypeTableNew(0x676C7966, 6000, t6));
    AddTable(ttcr, TrueTypeTableNew(0x6B65726E, 7000, t7));

    free(t1);
    free(t2);
    free(t3);
    free(t4);
    free(t5);
    free(t6);
    free(t7);


    StreamToFile(ttcr, "ttcrout.ttf");

    TrueTypeCreatorDispose(ttcr);
    return 0;
}
#endif
