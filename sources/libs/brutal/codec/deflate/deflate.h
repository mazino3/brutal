#pragma once

#include <brutal/io.h>
#include <brutal/io/bit_write.h>

typedef struct _DeflateCompressionContext
{
    int compression_level;
    int min_size_to_compress;
    Alloc *alloc;
    IoResult (*compress_block_impl)(struct _DeflateCompressionContext *c, BitWriter *bit_writer, const uint8_t *in,
                                    size_t in_nbytes, bool last);
} DeflateCompressionContext;

/**
  @brief Allocate our compression context
  @param ctx The context which we want to initialize
  @param compression_level The rate at which we want to compress (0 none, 9 best compression)
  @param alloc The heap object which is used for internal allocations
*/
void deflate_init(DeflateCompressionContext *ctx, int compression_level, Alloc *alloc);
/**
  @brief Free the compression context
*/
void deflate_deinit(DeflateCompressionContext *ctx);
/**
  @brief Compress an entire stream
  @param ctx The compression context used for this operation
  @param in The input data to compress
  @param in_len The size of the input data
  @param out The output buffer where we can store our data
  @param out_len The size of the output buffer
  @return The number of bytes written to the \p out buffer (compressed size)
*/
IoResult deflate_compress_data(DeflateCompressionContext *ctx, const uint8_t *in, size_t in_len, const uint8_t *out, size_t out_len);
/**
  @brief Compress an entire stream
  @param ctx The compression context used for this operation
  @param writer The destination where we write compressed data
  @param reader The source where we read the uncompressed data from
  @return The number of bytes written to the \p writer stream (compressed size)
*/
IoResult deflate_compress_stream(DeflateCompressionContext *ctx, IoWriter *writer, IoReader *reader);
/**
  @brief Decompress an entire stream
  @param writer The destination where we write the uncompressed data
  @param reader The source where we read the compressed data from
  @return The number of bytes written to the \p writer stream (uncompressed size)
*/
IoResult deflate_decompress_stream(IoWriter *writer, IoReader *reader);