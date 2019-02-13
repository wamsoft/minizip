#include <windows.h>
#include <stdio.h>
#include "tp_stub.h"
#include "mz.h"
#include "mz_strm.h"
#include "mz_compat.h"


extern mz_stream_vtbl KrkrMzStreamVtbl;

struct KrkrMzStream
{
  mz_stream stream;
  IStream *file;
  bool initalized;

  KrkrMzStream()
    : file(NULL)
    , initalized(false)
  {
    memset(&stream, 0, sizeof(stream));
    stream.vtbl = &KrkrMzStreamVtbl;
  }
};

int32_t
krkr_mz_stream_open (void *stream, const char *filename, int mode)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  int tjsmode = 0;
  if ((mode & MZ_OPEN_MODE_READWRITE)==MZ_OPEN_MODE_READ)
    tjsmode = TJS_BS_READ;
  else
    if (mode & MZ_OPEN_MODE_EXISTING)
      tjsmode= TJS_BS_APPEND;
    else
      if (mode & MZ_OPEN_MODE_CREATE)
        tjsmode = TJS_BS_WRITE;
  
  if ((filename!=NULL)) {
    kstream->file = TVPCreateIStream(ttstr((const tjs_char*)filename), tjsmode);
    kstream->initalized = true;
  }
  return MZ_OK;
}

int32_t
krkr_mz_stream_is_open(void *stream)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  if (! kstream->initalized)
    return MZ_OPEN_ERROR;
  return MZ_OK;
}

int32_t
krkr_mz_stream_read(void *stream, void* buf, int32_t size)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;
  if (is) {
    ULONG len;
    if (is->Read(buf,size,&len) == S_OK) {
      return len;
    }
  }
  return MZ_DATA_ERROR;
}

int32_t
krkr_mz_stream_write(void *stream, const void* buf, int32_t size)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;
  if (is) {
    DWORD len;
    if (is->Write(buf,size,&len) == S_OK) {
      return len;
    }
  }
  return MZ_DATA_ERROR;
}

int64_t
krkr_mz_stream_tell (void *stream)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;
  if (is) {
    LARGE_INTEGER move = {0};
    ULARGE_INTEGER newposition;
    if (is->Seek(move, STREAM_SEEK_CUR, &newposition) == S_OK) {
      return newposition.QuadPart;
    }
  }
  return MZ_TELL_ERROR;
}

int32_t
krkr_mz_stream_seek(void *stream, int64_t offset, int32_t origin)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;

  DWORD dwOrigin;
  switch(origin) {
  case MZ_SEEK_CUR: dwOrigin = STREAM_SEEK_CUR; break;
  case MZ_SEEK_END: dwOrigin = STREAM_SEEK_END; break;
  case MZ_SEEK_SET: dwOrigin = STREAM_SEEK_SET; break;
  default: return MZ_SEEK_ERROR; //failed
  }
  if (is) {
    LARGE_INTEGER move;
    move.QuadPart = offset;
    ULARGE_INTEGER newposition;
    if (is->Seek(move, origin, &newposition) == S_OK) {
      return MZ_OK;
    }
  }
  return MZ_SEEK_ERROR;
}

int32_t
krkr_mz_stream_close(void *stream)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;
  if (is) {
    is->Release();
    is = NULL;
    kstream->file = NULL;
    kstream->initalized = false;
    return MZ_OK;
  }
  return MZ_CLOSE_ERROR;
}

int32_t
krkr_mz_stream_error(void *stream)
{
  KrkrMzStream *kstream = (KrkrMzStream*)stream;
  IStream *is = kstream->file;
  if (is) {
    return MZ_OK;
  }
  return MZ_STREAM_ERROR;
}

void*
krkr_mz_stream_create(void **stream)
{
  KrkrMzStream *kstream = new KrkrMzStream();
  if (stream != NULL)
    *stream = kstream;
  return kstream;
}

void
krkr_mz_stream_destroy(void **stream)
{
  if (stream == NULL)
    return;
  KrkrMzStream *kstream = (KrkrMzStream*)*stream;
  delete kstream;
  *stream = NULL;
}

mz_stream_vtbl
KrkrMzStreamVtbl =
  {
   krkr_mz_stream_open,
   krkr_mz_stream_is_open,
   krkr_mz_stream_read,
   krkr_mz_stream_write,
   krkr_mz_stream_tell,
   krkr_mz_stream_seek,
   krkr_mz_stream_close,
   krkr_mz_stream_error,
   krkr_mz_stream_create,
   krkr_mz_stream_destroy,
   NULL,
   NULL,
  };

zlib_filefunc_def KrkrFileFuncDef = &KrkrMzStreamVtbl;
