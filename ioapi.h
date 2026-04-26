#ifndef __MINIZIP_TVP_IOAPI_H__
#define __MINIZIP_TVP_IOAPI_H__

#include "tp_stub.h"

// minizip-ng の mz_stream として iTJSBinaryStream をラップするユーティリティ。
//
// 使い方:
//   void *stream = mz_stream_tvp_create();
//   mz_stream_open(stream, "/utf-8/path", MZ_OPEN_MODE_READ);  // path で開く
//     - もしくは -
//   mz_stream_tvp_attach(stream, bs, /*owned*/1);              // 既存の bs を割り当て
//   ... mz_zip_reader_open(reader, stream) など ...
//   mz_stream_close(stream);
//   mz_stream_tvp_delete(&stream);

// mz_stream を生成する。vtbl だけセットされた状態。
void *mz_stream_tvp_create(void);

// mz_stream を破棄する。owned で attach されたストリームは Destruct される。
void mz_stream_tvp_delete(void **stream);

// 既存の iTJSBinaryStream を mz_stream に割り当てる。
// owned が真の場合、close/delete 時に Destruct される。
void mz_stream_tvp_attach(void *stream, iTJSBinaryStream *bs, int owned);

// attach した iTJSBinaryStream を取り外して返す (所有権ごと外す)。
iTJSBinaryStream *mz_stream_tvp_detach(void *stream);

#endif
