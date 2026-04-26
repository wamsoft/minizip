// iTJSBinaryStream を minizip-ng の mz_stream としてラップする実装

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tp_stub.h"

extern "C" {
#include "mz.h"
#include "mz_strm.h"
}

#include "ioapi.h"

typedef struct mz_stream_tvp_s {
	mz_stream stream;
	iTJSBinaryStream *bs;
	int owned;
	int32_t error;
} mz_stream_tvp;

static int32_t mz_stream_tvp_open(void *stream, const char *path, int32_t mode)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (t->bs && t->owned) {
		t->bs->Destruct();
	}
	t->bs = NULL;
	t->error = MZ_OK;
	if (!path) return MZ_PARAM_ERROR;

	int flags;
	if (mode & MZ_OPEN_MODE_CREATE) {
		flags = TJS_BS_WRITE;
	} else if (mode & MZ_OPEN_MODE_APPEND) {
		flags = TJS_BS_APPEND;
	} else if (mode & MZ_OPEN_MODE_WRITE) {
		flags = TJS_BS_WRITE;
	} else {
		flags = TJS_BS_READ;
	}

	// path は UTF-8 narrow なので wide に変換してから ttstr を作る
	ttstr filename;
	int wlen = TVPUtf8ToWideCharString(path, NULL);
	if (wlen <= 0) {
		filename = path; // フォールバック (ANSI 解釈)
	} else {
		tjs_char *wbuf = new tjs_char[wlen + 1];
		TVPUtf8ToWideCharString(path, wbuf);
		wbuf[wlen] = 0;
		filename = wbuf;
		delete[] wbuf;
	}

	try {
		t->bs = TVPCreateStream(filename, flags);
	} catch(...) {
		t->bs = NULL;
	}
	if (!t->bs) {
		t->error = MZ_OPEN_ERROR;
		return MZ_OPEN_ERROR;
	}
	t->owned = 1;
	return MZ_OK;
}

static int32_t mz_stream_tvp_is_open(void *stream)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	return t->bs ? MZ_OK : MZ_OPEN_ERROR;
}

static int32_t mz_stream_tvp_read(void *stream, void *buf, int32_t size)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (!t->bs) return MZ_OPEN_ERROR;
	try {
		return (int32_t)t->bs->Read(buf, (tjs_uint)size);
	} catch(...) {
		t->error = MZ_READ_ERROR;
		return MZ_READ_ERROR;
	}
}

static int32_t mz_stream_tvp_write(void *stream, const void *buf, int32_t size)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (!t->bs) return MZ_OPEN_ERROR;
	try {
		return (int32_t)t->bs->Write(buf, (tjs_uint)size);
	} catch(...) {
		t->error = MZ_WRITE_ERROR;
		return MZ_WRITE_ERROR;
	}
}

static int64_t mz_stream_tvp_tell(void *stream)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (!t->bs) return MZ_OPEN_ERROR;
	try {
		return (int64_t)t->bs->GetPosition();
	} catch(...) {
		t->error = MZ_TELL_ERROR;
		return MZ_TELL_ERROR;
	}
}

static int32_t mz_stream_tvp_seek(void *stream, int64_t offset, int32_t origin)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (!t->bs) return MZ_OPEN_ERROR;
	tjs_int whence;
	switch (origin) {
	case MZ_SEEK_CUR: whence = TJS_BS_SEEK_CUR; break;
	case MZ_SEEK_END: whence = TJS_BS_SEEK_END; break;
	case MZ_SEEK_SET:
	default:          whence = TJS_BS_SEEK_SET; break;
	}
	try {
		t->bs->Seek((tjs_int64)offset, whence);
		return MZ_OK;
	} catch(...) {
		t->error = MZ_SEEK_ERROR;
		return MZ_SEEK_ERROR;
	}
}

static int32_t mz_stream_tvp_close(void *stream)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (t->bs && t->owned) {
		t->bs->Destruct();
	}
	t->bs = NULL;
	return MZ_OK;
}

static int32_t mz_stream_tvp_error(void *stream)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	return t->error;
}

static mz_stream_vtbl mz_stream_tvp_vtbl = {
	mz_stream_tvp_open,
	mz_stream_tvp_is_open,
	mz_stream_tvp_read,
	mz_stream_tvp_write,
	mz_stream_tvp_tell,
	mz_stream_tvp_seek,
	mz_stream_tvp_close,
	mz_stream_tvp_error,
	NULL, // create
	NULL, // destroy
	NULL, // get_prop_int64
	NULL  // set_prop_int64
};

void *mz_stream_tvp_create(void)
{
	mz_stream_tvp *t = (mz_stream_tvp*)calloc(1, sizeof(*t));
	if (t) {
		t->stream.vtbl = &mz_stream_tvp_vtbl;
	}
	return t;
}

void mz_stream_tvp_delete(void **stream)
{
	if (!stream || !*stream) return;
	mz_stream_tvp *t = (mz_stream_tvp*)*stream;
	if (t->bs && t->owned) {
		t->bs->Destruct();
	}
	free(t);
	*stream = NULL;
}

void mz_stream_tvp_attach(void *stream, iTJSBinaryStream *bs, int owned)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	if (t->bs && t->owned) {
		t->bs->Destruct();
	}
	t->bs = bs;
	t->owned = owned;
	t->error = MZ_OK;
}

iTJSBinaryStream *mz_stream_tvp_detach(void *stream)
{
	mz_stream_tvp *t = (mz_stream_tvp*)stream;
	iTJSBinaryStream *bs = t->bs;
	t->bs = NULL;
	t->owned = 0;
	return bs;
}
