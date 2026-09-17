/* Fixture-only declaration: cart_rom.c's unused file helper declarations need
 * gzFile, but this focused PI fixture neither opens nor links zlib streams. */
#ifndef DD_PI_FIXTURE_ZLIB_H
#define DD_PI_FIXTURE_ZLIB_H
typedef void *gzFile;
#endif