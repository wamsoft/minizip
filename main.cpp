/*
 * zip/unzip plugin
 * Copyright (C) 2007 Go Watanabe
 *
 * original copyright
 *  minizip.c
 *  miniunz.c
 *  Version 1.01e, February 12th, 2005
 * Copyright (C) 1998-2005 Gilles Vollant
*/

static const char *copyright =
"\n----- MiniZip Copyright START -----\n"
"Condition of use and distribution are the same as zlib:\n"
"https://github.com/zlib-ng/minizip-ng\n"
"----- MiniZip Copyright END -----\n";

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <ncbind.hpp>

extern "C" {
#include "mz.h"
#include "mz_os.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"
}

#include "ioapi.h"
#include "narrow.h"

#define BUFFERSIZE (16384)
#define CASESENSITIVITY (0)

// Date クラスメンバ
static iTJSDispatch2 *dateClass = NULL;    // Date のクラスオブジェクト
static iTJSDispatch2 *dateSetTime = NULL;  // Date.setTime メソッド

// オブジェクトに数値を格納
static void setIntProp(iTJSDispatch2 *obj, const tjs_char *name, tjs_int64 value)
{
	tTJSVariant var = (tTVInteger)value;
	obj->PropSet(TJS_MEMBERENSURE, name,  NULL, &var, obj);
}

// オブジェクトに文字列を格納
static void setStrProp(iTJSDispatch2 *obj, const tjs_char *name, ttstr &value)
{
	tTJSVariant var = value;
	obj->PropSet(TJS_MEMBERENSURE, name,  NULL, &var, obj);
}

// オブジェクトに日時を格納 (UNIX time から Date オブジェクトを生成)
static void setDateProp(iTJSDispatch2 *obj, const tjs_char *name, time_t unixtime)
{
	if (unixtime <= 0) return;
	iTJSDispatch2 *date;
	if (TJS_SUCCEEDED(dateClass->CreateNew(0, NULL, NULL, &date, 0, NULL, obj))) {
		// Date.setTime はミリ秒
		tjs_int64 msec = (tjs_int64)unixtime * 1000;
		tTJSVariant time(msec);
		tTJSVariant *param[] = { &time };
		dateSetTime->FuncCall(0, NULL, NULL, NULL, 1, param, date);

		tTJSVariant var = date;
		date->Release();
		obj->PropSet(TJS_MEMBERENSURE, name, NULL, &var, obj);
	}
}

// ファイル名変換処理 (UTF-8 narrow → ttstr)
void
storeFilename(ttstr &name, const char *narrowName, bool utf8)
{
	if (utf8) {
		int wlen = TVPUtf8ToWideCharString(narrowName, NULL);
		if (wlen > 0) {
			tjs_char *wbuf = new tjs_char[wlen + 1];
			TVPUtf8ToWideCharString(narrowName, wbuf);
			wbuf[wlen] = 0;
			name = wbuf;
			delete[] wbuf;
		} else {
			name = narrowName;
		}
	} else {
		name = narrowName;
	}
}

// ファイルの mtime を time_t で返す。取得できない場合 0
// TVPLastModifiedFileTimeStorage は FILETIME 仕様の tjs_uint64 を返す
// (1601-01-01 UTC 起点 100ns 単位)。0 ならば取得不可。
static time_t
getFileMTime(const ttstr &filename)
{
	tjs_uint64 ft = 0;
	try {
		ft = TVPLastModifiedFileTimeStorage(filename);
	} catch(...) {
		ft = 0;
	}
	if (ft == 0) return 0;
	// FILETIME → unix time
	return (time_t)(ft / 10000000ULL - 11644473600ULL);
}

/**
 * ZIP圧縮処理クラス
 */
class Zip {

protected:
	void *zw;        // mz_zip_writer ハンドル
	void *zwstream;  // 出力先 iTJSBinaryStream をラップした mz_stream

public:

	Zip() : zw(NULL), zwstream(NULL) {
	}

	~Zip(){
		close();
	}

public:

	/**
	 * ZIPファイルを開く
	 * @param filename ファイル名
	 * @param overwrite 上書き指定 0:エラー 1:上書き 2:追加
	 */
	static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
	                                      tjs_int numparams,
	                                      tTJSVariant **param,
	                                      Zip *self) {

		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr filename = *param[0];
		int overwrite = numparams > 1 ? (int)*param[1] : 0;

		bool append = false;
		ttstr existing = TVPGetPlacedPath(filename);
		if (overwrite == 2) {
			// 追記。存在しなければ新規作成
			append = existing.length() != 0;
		} else if (overwrite == 0) {
			if (existing.length()) {
				// 既に存在している
				ttstr msg = filename + " exists.";
				TVPThrowExceptionMessage(msg.c_str());
			}
		}
		// overwrite == 1 は新規作成 (上書き)

		// 出力ストリームを開いて mz_stream にラップ
		iTJSBinaryStream *bs = NULL;
		try {
			bs = TVPCreateStream(filename, append ? TJS_BS_APPEND : TJS_BS_WRITE);
		} catch(...) {
			bs = NULL;
		}
		if (!bs) {
			ttstr msg = filename + " can't open.";
			TVPThrowExceptionMessage(msg.c_str());
		}

		self->zwstream = mz_stream_tvp_create();
		mz_stream_tvp_attach(self->zwstream, bs, 1);

		self->zw = mz_zip_writer_create();
		if (mz_zip_writer_open(self->zw, self->zwstream, append ? 1 : 0) != MZ_OK) {
			mz_zip_writer_delete(&self->zw);
			mz_stream_tvp_delete(&self->zwstream);
			ttstr msg = filename + " can't open.";
			TVPThrowExceptionMessage(msg.c_str());
		}

		return TJS_S_OK;
	}

	/**
	 * ファイルを閉じる
	 */
	void close() {
		if (zw) {
			mz_zip_writer_close(zw);
			mz_zip_writer_delete(&zw);
			zw = NULL;
		}
		if (zwstream) {
			mz_stream_close(zwstream);
			mz_stream_tvp_delete(&zwstream);
			zwstream = NULL;
		}
	}

	/**
	 * ファイルの追加
	 * @param srcname  追加するファイル
	 * @param destname 登録名（パスを含む）
	 * @param compressLevel 圧縮レベル
	 * @param password パスワード指定
	 * @param compressionMethod 圧縮方法
	 * @param ignoreDate 日付情報を保存しない
	 * @return 追加に成功したら true
	 */
	static tjs_error TJS_INTF_METHOD add(tTJSVariant *result,
	                                     tjs_int numparams,
	                                     tTJSVariant **param,
	                                     Zip *self) {

		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		if (!self->zw) {
			TVPThrowExceptionMessage(TJS_W("don't open zipfile"));
		}

		ttstr srcname  = *param[0];
		ttstr destname = *param[1];
		int   compressLevel = MZ_COMPRESS_LEVEL_DEFAULT;
		if (numparams > 2 && param[2]->Type() == tvtInteger) {
			compressLevel = (int)*param[2];
		}
		bool usePassword = false;
		ttstr password;
		if (numparams > 3 && param[3]->Type() == tvtString) {
			usePassword = true;
			password = *param[3];
		}
		int compressionMethod = MZ_COMPRESS_METHOD_DEFLATE;
		if (numparams > 4 && param[4]->Type() == tvtInteger) {
			compressionMethod = (int)*param[4];
		}
		bool ignoreDate = false;
		if (numparams > 5 && param[5]->Type() == tvtInteger) {
			ignoreDate = (int)*param[5] != 0;
		}

		ttstr filename = TVPGetPlacedPath(srcname);
		if (filename.length() == 0) {
			ttstr msg = srcname + " not exists.";
			TVPThrowExceptionMessage(msg.c_str());
		}

		// 入力ストリームを開く
		iTJSBinaryStream *in = NULL;
		try {
			in = TVPCreateStream(filename, TJS_BS_READ);
		} catch(...) {
			in = NULL;
		}
		if (!in) {
			ttstr msg = filename + " can't open.";
			TVPThrowExceptionMessage(msg.c_str());
		}

		// ファイル時刻情報取得
		time_t modified = 0;
		if (!ignoreDate) {
			modified = getFileMTime(filename);
			if (modified == 0) {
				modified = time(NULL);
			}
		}

		// mz_zip_writer の設定
		mz_zip_writer_set_compress_method(self->zw, (uint16_t)((compressLevel != 0) ? compressionMethod : MZ_COMPRESS_METHOD_STORE));
		mz_zip_writer_set_compress_level(self->zw, (int16_t)compressLevel);
		if (usePassword) {
			NarrowString npass(password);
			mz_zip_writer_set_password(self->zw, (const char*)npass);
		} else {
			mz_zip_writer_set_password(self->zw, NULL);
		}

		// エントリ情報
		NarrowString nname(destname, true);
		mz_zip_file file_info;
		memset(&file_info, 0, sizeof file_info);
		file_info.version_madeby = MZ_VERSION_MADEBY;
		file_info.flag = MZ_ZIP_FLAG_UTF8;
		file_info.compression_method = (uint16_t)((compressLevel != 0) ? compressionMethod : MZ_COMPRESS_METHOD_STORE);
		file_info.modified_date = modified;
		file_info.accessed_date = modified;
		file_info.creation_date = modified;
		file_info.filename = (const char*)nname;
		file_info.filename_size = (uint16_t)(nname.data() ? strlen((const char*)nname) : 0);
		file_info.uncompressed_size = (int64_t)in->GetSize();
		file_info.zip64 = MZ_ZIP64_AUTO;

		// 入力ストリームを mz_stream でラップ
		void *src_stream = mz_stream_tvp_create();
		mz_stream_tvp_attach(src_stream, in, 0); // 所有権は外側 (in) に残す

		bool ret = false;
		int32_t err = mz_zip_writer_add_info(self->zw, src_stream, mz_stream_read, &file_info);
		ret = (err == MZ_OK);

		mz_stream_close(src_stream);
		mz_stream_tvp_delete(&src_stream);
		in->Destruct();

		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}
};

#include <vector>

/**
 * Zip 展開クラス
 */
class Unzip {

protected:
	void *zr;       // mz_zip_reader
	void *zrstream; // 入力 mz_stream
	bool utf8;

public:
	Unzip() : zr(NULL), zrstream(NULL), utf8(false) {
	}

	~Unzip() {
		close();
	}

	/**
	 * ZIPファイルを開く
	 * @param filename ファイル名
	 * @param force_utf8 UTF8強制指定 0:自動判定 1:強制
	 */
	static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
	                                      tjs_int numparams,
	                                      tTJSVariant **param,
	                                      Unzip *self) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;

		ttstr filename = *param[0];

		iTJSBinaryStream *bs = NULL;
		try {
			bs = TVPCreateStream(filename, TJS_BS_READ);
		} catch(...) {
			bs = NULL;
		}
		if (!bs) {
			ttstr msg = filename + TJS_W(" can't open.");
			TVPThrowExceptionMessage(msg.c_str());
		}

		self->zrstream = mz_stream_tvp_create();
		mz_stream_tvp_attach(self->zrstream, bs, 1);

		self->zr = mz_zip_reader_create();
		if (mz_zip_reader_open(self->zr, self->zrstream) != MZ_OK) {
			mz_zip_reader_delete(&self->zr);
			mz_stream_tvp_delete(&self->zrstream);
			ttstr msg = filename + TJS_W(" can't open.");
			TVPThrowExceptionMessage(msg.c_str());
		}

		// UTF-8 ファイル名フラグ判定
		if (numparams > 1 && (int)*param[1]) {
			self->utf8 = true;
		} else {
			if (mz_zip_reader_goto_first_entry(self->zr) == MZ_OK) {
				mz_zip_file *file_info = NULL;
				if (mz_zip_reader_entry_get_info(self->zr, &file_info) == MZ_OK && file_info) {
					self->utf8 = (file_info->flag & MZ_ZIP_FLAG_UTF8) != 0;
				}
			}
		}

		return TJS_S_OK;
	}

	/**
	 * ZIP ファイルを閉じる
	 */
	void close() {
		if (zr) {
			mz_zip_reader_close(zr);
			mz_zip_reader_delete(&zr);
			zr = NULL;
		}
		if (zrstream) {
			mz_stream_close(zrstream);
			mz_stream_tvp_delete(&zrstream);
			zrstream = NULL;
		}
	}

	/**
	 * ファイルリスト取得
	 * @return ファイル情報（辞書）の配列
	 */
	static tjs_error TJS_INTF_METHOD list(tTJSVariant *result,
	                                      tjs_int numparams,
	                                      tTJSVariant **param,
	                                      Unzip *self) {

		if (!self->zr) {
			TVPThrowExceptionMessage(TJS_W("don't open zipfile"));
		}

		iTJSDispatch2 *array = TJSCreateArrayObject();

		int err = mz_zip_reader_goto_first_entry(self->zr);
		while (err == MZ_OK) {
			mz_zip_file *file_info = NULL;
			if (mz_zip_reader_entry_get_info(self->zr, &file_info) == MZ_OK && file_info) {
				iTJSDispatch2 *obj = TJSCreateDictionaryObject();
				if (obj) {
					ttstr filename;
					storeFilename(filename, file_info->filename ? file_info->filename : "", self->utf8);

					setStrProp(obj, TJS_W("filename"), filename);
					setIntProp(obj, TJS_W("uncompressed_size"), file_info->uncompressed_size);
					setIntProp(obj, TJS_W("compressed_size"),   file_info->compressed_size);
					setIntProp(obj, TJS_W("crypted"),           (file_info->flag & MZ_ZIP_FLAG_ENCRYPTED) ? 1 : 0);
					setIntProp(obj, TJS_W("deflated"),          (file_info->compression_method == MZ_COMPRESS_METHOD_DEFLATE) ? 1 : 0);
					setIntProp(obj, TJS_W("deflateLevel"),      (file_info->flag & 0x6) / 2);
					setIntProp(obj, TJS_W("crc"),               file_info->crc);
					setDateProp(obj, TJS_W("date"), file_info->modified_date);

					tTJSVariant var(obj), *p = &var;
					array->FuncCall(0, TJS_W("add"), NULL, 0, 1, &p, array);
					obj->Release();
				}
			}
			err = mz_zip_reader_goto_next_entry(self->zr);
		}

		if (result) {
			tTJSVariant ret(array, array);
			*result = ret;
		}
		array->Release();

		return TJS_S_OK;
	}

	/**
	 * ファイルの展開
	 * @param srcname 展開元ファイル
	 * @param destfile 展開先ファイル
	 * @param password パスワード指定
	 */
	static tjs_error TJS_INTF_METHOD extract(tTJSVariant *result,
	                                         tjs_int numparams,
	                                         tTJSVariant **param,
	                                         Unzip *self) {
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;
		if (!self->zr) {
			TVPThrowExceptionMessage(TJS_W("don't open zipfile"));
		}
		ttstr srcname  = *param[0];
		ttstr destname = *param[1];
		bool usePassword = false;
		ttstr password;
		if (numparams > 2 && param[2]->Type() == tvtString) {
			usePassword = true;
			password = *param[2];
		}

		bool ret = false;

		NarrowString nsrc(srcname, self->utf8);
		if (mz_zip_reader_locate_entry(self->zr, (const char*)nsrc, CASESENSITIVITY ? 0 : 1) == MZ_OK) {
			if (usePassword) {
				NarrowString npass(password);
				mz_zip_reader_set_password(self->zr, (const char*)npass);
			} else {
				mz_zip_reader_set_password(self->zr, NULL);
			}

			iTJSBinaryStream *out = NULL;
			try {
				out = TVPCreateStream(destname, TJS_BS_WRITE);
			} catch(...) {
				out = NULL;
			}
			if (out) {
				void *out_stream = mz_stream_tvp_create();
				mz_stream_tvp_attach(out_stream, out, 0);

				int32_t err = mz_zip_reader_entry_save(self->zr, out_stream, mz_stream_write);
				ret = (err == MZ_OK);

				mz_stream_close(out_stream);
				mz_stream_tvp_delete(&out_stream);
				out->Destruct();
			} else {
				ttstr msg = destname + " can't open.";
				TVPThrowExceptionMessage(msg.c_str());
			}
		}

		if (result) {
			*result = ret;
		}

		return TJS_S_OK;
	}

};

NCB_REGISTER_CLASS(Zip) {
	Constructor();
	RawCallback("open", &ClassT::open, 0);
	NCB_METHOD(close);
	RawCallback("add", &ClassT::add, 0);
	Variant("CompressionMethodStore",   MZ_COMPRESS_METHOD_STORE);
	Variant("CompressionMethodDeflate", MZ_COMPRESS_METHOD_DEFLATE);
	Variant("CompressionMethodBzip2",   MZ_COMPRESS_METHOD_BZIP2);
	Variant("CompressionMethodLzma",    MZ_COMPRESS_METHOD_LZMA);
}

NCB_REGISTER_CLASS(Unzip) {
	Constructor();
	RawCallback("open",    &ClassT::open, 0);
	NCB_METHOD(close);
	RawCallback("list",    &ClassT::list, 0);
	RawCallback("extract", &ClassT::extract, 0);
}

extern void initZipStorage();
extern void doneZipStorage();

/**
 * 登録処理前
 */
static void PreRegistCallback()
{
	TVPAddImportantLog(ttstr(copyright));
	initZipStorage();
}

/**
 * 登録処理後
 */
static void PostRegistCallback()
{
	tTJSVariant var;
	TVPExecuteExpression(TJS_W("Date"), &var);
	dateClass = var.AsObject();
	TVPExecuteExpression(TJS_W("Date.setTime"), &var);
	dateSetTime = var.AsObject();
}

#define RELEASE(name) name->Release();name= NULL

/**
 * 開放処理前
 */
static void PreUnregistCallback()
{
	RELEASE(dateClass);
	RELEASE(dateSetTime);
}

/**
 * 開放処理後
 */
static void PostUnregistCallback()
{
	doneZipStorage();
}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
// 同梱コンポーネントのライセンスを本体収集機構へ登録
// (LicensesGen.cpp = licenses/manifest.json から生成)
extern void RegisterMinizipLicenses();
NCB_PRE_REGIST_CALLBACK(RegisterMinizipLicenses);

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_PRE_UNREGIST_CALLBACK(PreUnregistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
