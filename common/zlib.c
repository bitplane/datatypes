#include <limits.h>
#include <string.h>

#include <zlib.h>

#ifdef __AROS__
#include <aros/genmodule.h>
#include <defines/z1_LVO.h>
#include <exec/semaphores.h>
#include <proto/exec.h>

/* z1.library has no linklib in the SDK, so make the stubs its linklib would
   hold. They call through Z1Base. The library gives each task its own base,
   so each call opens it in the calling task and holds a lock while Z1Base is
   in use. */
struct Library *Z1Base;
AROS_GM_LIBFUNCSTUB(inflateInit_, Z1Base, LVOinflateInit_)
AROS_GM_LIBFUNCSTUB(inflateInit2_, Z1Base, LVOinflateInit2_)
AROS_GM_LIBFUNCSTUB(inflate, Z1Base, LVOinflate)
AROS_GM_LIBFUNCSTUB(inflateEnd, Z1Base, LVOinflateEnd)
AROS_GM_LIBFUNCSTUB(deflateInit_, Z1Base, LVOdeflateInit_)
AROS_GM_LIBFUNCSTUB(deflate, Z1Base, LVOdeflate)
AROS_GM_LIBFUNCSTUB(deflateEnd, Z1Base, LVOdeflateEnd)

static struct SignalSemaphore z1_lock;
static BOOL z1_lock_ready;

static int z1_open(void)
{
    Forbid();
    if (!z1_lock_ready) {
        InitSemaphore(&z1_lock);
        z1_lock_ready = TRUE;
    }
    Permit();
    ObtainSemaphore(&z1_lock);
    Z1Base = OpenLibrary((CONST_STRPTR)"z1.library", 1);
    if (Z1Base == NULL) {
        ReleaseSemaphore(&z1_lock);
        return 0;
    }
    return 1;
}

static void z1_close(void)
{
    CloseLibrary(Z1Base);
    Z1Base = NULL;
    ReleaseSemaphore(&z1_lock);
}
#else
static int z1_open(void) { return 1; }
static void z1_close(void) {}
#endif

#include "common/zlib.h"

/* zlib counts in uInt, so feed long buffers in pieces. */
static uInt piece(size_t left)
{
    return left > UINT_MAX ? UINT_MAX : (uInt)left;
}

/* window_bits 15 reads a zlib stream, -15 raw deflate. */
static enum codec_result inflate_stream(int window_bits,
                                        const unsigned char *src, size_t src_length,
                                        unsigned char *dst, size_t dst_length,
                                        size_t *written)
{
    z_stream stream;
    size_t in_left = src_length, out_left = dst_length;
    enum codec_result result;
    int status = Z_OK;

    *written = 0;
    if (!z1_open())
        return CODEC_NO_MEMORY;
    memset(&stream, 0, sizeof stream);
    if (inflateInit2(&stream, window_bits) != Z_OK) {
        z1_close();
        return CODEC_NO_MEMORY;
    }
    stream.next_in = (Bytef *)src;
    stream.next_out = dst;
    /* Z_BUF_ERROR means no progress: input or output has run out. */
    do {
        uInt in = piece(in_left), out = piece(out_left);
        stream.avail_in = in;
        stream.avail_out = out;
        status = inflate(&stream, Z_NO_FLUSH);
        in_left -= in - stream.avail_in;
        out_left -= out - stream.avail_out;
    } while (status == Z_OK);
    *written = dst_length - out_left;
    if (status == Z_STREAM_END)
        result = CODEC_OK;
    else if (status == Z_OK || status == Z_BUF_ERROR)
        result = in_left == 0 ? CODEC_TRUNCATED : CODEC_TOO_LARGE;
    else if (status == Z_MEM_ERROR)
        result = CODEC_NO_MEMORY;
    else
        result = CODEC_INVALID;
    inflateEnd(&stream);
    z1_close();
    return result;
}

enum codec_result zlib_inflate(const unsigned char *src, size_t src_length,
                               unsigned char *dst, size_t dst_length,
                               size_t *written)
{
    return inflate_stream(MAX_WBITS, src, src_length, dst, dst_length, written);
}

enum codec_result zlib_inflate_raw(const unsigned char *src, size_t src_length,
                                   unsigned char *dst, size_t dst_length,
                                   size_t *written)
{
    return inflate_stream(-MAX_WBITS, src, src_length, dst, dst_length, written);
}

enum codec_result zlib_deflate(const unsigned char *src, size_t src_length,
                               unsigned char *dst, size_t dst_length,
                               int level, size_t *written)
{
    z_stream stream;
    size_t in_left = src_length, out_left = dst_length;
    enum codec_result result;
    int status = Z_OK;

    *written = 0;
    if (level < -1 || level > 9)
        level = -1;
    if (!z1_open())
        return CODEC_NO_MEMORY;
    memset(&stream, 0, sizeof stream);
    if (deflateInit(&stream, level) != Z_OK) {
        z1_close();
        return CODEC_NO_MEMORY;
    }
    stream.next_in = (Bytef *)src;
    stream.next_out = dst;
    do {
        uInt in = piece(in_left), out = piece(out_left);
        stream.avail_in = in;
        stream.avail_out = out;
        status = deflate(&stream, in_left == in ? Z_FINISH : Z_NO_FLUSH);
        in_left -= in - stream.avail_in;
        out_left -= out - stream.avail_out;
    } while (status == Z_OK);
    *written = dst_length - out_left;
    if (status == Z_STREAM_END)
        result = CODEC_OK;
    else if (status == Z_OK || status == Z_BUF_ERROR)
        result = CODEC_TOO_LARGE;
    else
        result = CODEC_NO_MEMORY;
    deflateEnd(&stream);
    z1_close();
    return result;
}
