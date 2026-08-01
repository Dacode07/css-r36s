#ifndef __ZLIB_MIN_H__
#define __ZLIB_MIN_H__

typedef struct z_stream_min {
  const unsigned char *next_in;
  unsigned int avail_in;
  unsigned long total_in;
  unsigned char *next_out;
  unsigned int avail_out;
  unsigned long total_out;
  const char *msg;
  void *state;
  void *(*zalloc)(void *, unsigned int, unsigned int);
  void (*zfree)(void *, void *);
  void *opaque;
  int data_type;
  unsigned long adler;
  unsigned long reserved;
} z_stream_min;

#define ZM_OK 0
#define ZM_STREAM_END 1
#define ZM_NO_FLUSH 0
#define ZM_RAW_WINDOW (-15)

extern int inflateInit2_(z_stream_min *strm, int windowBits, const char *version, int stream_size);
extern int inflate(z_stream_min *strm, int flush);
extern int inflateEnd(z_stream_min *strm);

#define zm_inflate_init(s) inflateInit2_((s), ZM_RAW_WINDOW, "1.2.11", (int)sizeof(z_stream_min))

#endif
