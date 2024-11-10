#include <stdio.h>
#include <windows.h>
#include "tp_stub.h"
#include "mz_compat.h"

static void* ZCALLBACK fopen64_file_func (void* opaque, const void* filename, int mode)
{
	iTJSBinaryStream* file = NULL;
	int tjsmode = 0;
	if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER)==ZLIB_FILEFUNC_MODE_READ)
		tjsmode = TJS_BS_READ;
    else
    if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
		tjsmode= TJS_BS_APPEND;
	else
    if (mode & ZLIB_FILEFUNC_MODE_CREATE)
		tjsmode = TJS_BS_WRITE;
	
	if ((filename!=NULL)) {
		file = TVPCreateStream(ttstr((const tjs_char*)filename), tjsmode);
	}
  return file;
}


static unsigned long ZCALLBACK fread_file_func (void* opaque, void* stream, void* buf, unsigned long size)
{
	iTJSBinaryStream *is = (iTJSBinaryStream*)stream;
	if (is) {
		return is->Read(buf,size);
	}
	return 0;
}

static unsigned long ZCALLBACK fwrite_file_func (void* opaque, void* stream, const void* buf, unsigned long size)
{
	iTJSBinaryStream *is = (iTJSBinaryStream*)stream;
	if (is) {
		return is->Write(buf,size);
	}
	return 0;
}

static ZPOS64_T ZCALLBACK ftell64_file_func (void* opaque, void* stream)
{
	iTJSBinaryStream *is = (iTJSBinaryStream *)stream;
	if (is) {
    return is->Seek(0, TJS_BS_SEEK_CUR);
	}
	return -1;
}

static long ZCALLBACK fseek64_file_func (void*  opaque, void* stream, ZPOS64_T offset, int origin)
{
	tjs_int dwOrigin;
	switch(origin) {
	case ZLIB_FILEFUNC_SEEK_CUR: dwOrigin = TJS_BS_SEEK_CUR; break;
	case ZLIB_FILEFUNC_SEEK_END: dwOrigin = TJS_BS_SEEK_END; break;
	case ZLIB_FILEFUNC_SEEK_SET: dwOrigin = TJS_BS_SEEK_SET; break;
	default: return -1; //failed
	}
	iTJSBinaryStream *is = (iTJSBinaryStream *)stream;
	if (is) {
    return is->Seek(offset, dwOrigin);
	}
	return -1;
}


static int ZCALLBACK fclose_file_func (void* opaque, void* stream)
{
	iTJSBinaryStream *is = (iTJSBinaryStream *)stream;
	if (is) {
		is->Destruct();
		return 0;
	}
	return EOF;
}

static int ZCALLBACK ferror_file_func (void* opaque, void* stream)
{
	iTJSBinaryStream *is = (iTJSBinaryStream *)stream;
	if (is) {
		return 0;
	}
	return EOF;
}

zlib_filefunc64_def TVPZlibFileFunc = {
	fopen64_file_func,
	fread_file_func,
	fwrite_file_func,
	ftell64_file_func,
	fseek64_file_func,
	fclose_file_func,
	ferror_file_func,
	NULL
};