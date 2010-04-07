#include <camel/camel.h>

/* call one, then the other on the same stream content */
void test_stream_seekable_writepart(CamelSeekableStream *s);
void test_stream_seekable_readpart(CamelSeekableStream *s);

/* same, for substreams, multiple ways of writing */
#define SEEKABLE_SUBSTREAM_WAYS (2)

void test_seekable_substream_writepart(CamelStream *s, gint type);
void test_seekable_substream_readpart(CamelStream *s);
