/* Host check that minizip, built for Android with the defines in the main
 * LOCAL_CFLAGS list of mupen64plus-core.mk and the app's minimum API level,
 * writes a zip file and reads it back, as the core does for Project64 save
 * states.
 *
 * minizip's ioapi.h defines __USE_FILE_OFFSET64, so below API 24 bionic
 * declares no ftello or fseeko. Built with IOAPI_NO_64, minizip called them
 * undeclared, and on 32-bit ABIs each seek handed libc its 64-bit offset where
 * libc takes a 32-bit one: on armeabi-v7a seeks went astray, so the zip came
 * out corrupt, and on x86 every seek counted from the start of the file, so a
 * seek to the end landed at the start. Either way unzOpen failed. On 64-bit
 * ABIs, where libc takes a 64-bit offset too, the calls happened to work.
 *
 * test-minizip-android.sh builds this file as a static executable for each
 * ABI the app ships and runs it under qemu's user-mode emulation. */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ioapi.h"
#include "unzip.h"
#include "zip.h"

/* Bionic refuses a static executable whose TLS segment is aligned to less than
 * 32 bytes on armeabi-v7a or 64 on arm64-v8a. The script builds this file with
 * -fno-emulated-tls, so that this variable takes real TLS and raises the
 * segment's alignment. */
__thread char g_tls_align __attribute__((aligned(64), used));

static unsigned char g_data[70000];
static unsigned char g_back[sizeof(g_data)];

static int report(int ok, const char *what)
{
    printf("  %s: %s\n", what, ok ? "ok" : "FAILED");
    fflush(stdout);
    return ok;
}

static int write_zip(const char *path)
{
    zipFile zf = zipOpen(path, APPEND_STATUS_CREATE);
    if (zf == NULL)
        return 0;
    int ok = zipOpenNewFileInZip(zf, "state", NULL, NULL, 0, NULL, 0, NULL,
                                 Z_DEFLATED, Z_DEFAULT_COMPRESSION) == ZIP_OK
          && zipWriteInFileInZip(zf, g_data, sizeof(g_data)) == ZIP_OK
          && zipCloseFileInZip(zf) == ZIP_OK;
    return zipClose(zf, "") == ZIP_OK && ok;
}

static int seek_to_end_lands_there(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;

    zlib_filefunc64_def ff;
    fill_fopen64_filefunc(&ff);
    voidpf stream = ff.zopen64_file(ff.opaque, path,
        ZLIB_FILEFUNC_MODE_READ | ZLIB_FILEFUNC_MODE_EXISTING);
    if (stream == NULL)
        return 0;
    long seek = ff.zseek64_file(ff.opaque, stream, 0, ZLIB_FILEFUNC_SEEK_END);
    ZPOS64_T position = ff.ztell64_file(ff.opaque, stream);
    ff.zclose_file(ff.opaque, stream);
    if (seek != 0 || position != (ZPOS64_T)st.st_size)
        printf("  a seek to the end of the %lld-byte file returned %ld and left it at %lld\n",
               (long long)st.st_size, seek, (long long)position);
    return seek == 0 && position == (ZPOS64_T)st.st_size;
}

static int read_zip(const char *path)
{
    unzFile uf = unzOpen(path);
    if (uf == NULL)
    {
        printf("  unzOpen could not open the zip\n");
        return 0;
    }
    unz_file_info info;
    char name[256];
    int got = -1;
    if (unzGoToFirstFile(uf) == UNZ_OK
     && unzGetCurrentFileInfo(uf, &info, name, sizeof(name) - 1, NULL, 0, NULL, 0) == UNZ_OK
     && unzOpenCurrentFile(uf) == UNZ_OK)
    {
        got = unzReadCurrentFile(uf, g_back, sizeof(g_back));
        unzCloseCurrentFile(uf);
    }
    unzClose(uf);
    return got == (int)sizeof(g_data) && memcmp(g_back, g_data, sizeof(g_data)) == 0;
}

int main(int argc, char **argv)
{
    g_tls_align = 1;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <zip file to write>\n", argv[0]);
        return 2;
    }
    for (size_t i = 0; i < sizeof(g_data); ++i)
        g_data[i] = (unsigned char)(i * 7 + (i >> 9));

    int ok = report(write_zip(argv[1]), "zip written");
    ok &= report(seek_to_end_lands_there(argv[1]), "seek to the end lands there");
    ok &= report(read_zip(argv[1]), "zip read back as written");
    return ok ? 0 : 1;
}
