#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncbind.hpp>
#include <map>
#include <vector>

extern "C" {
#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"
}

#include "ioapi.h"
#include "narrow.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define CASESENSITIVITY (0)

#define BASENAME TJS_W("zip")

extern void storeFilename(ttstr &name, const char *narrowName, bool utf8);

/**
 * Zip 展開処理クラス
 *
 * mz_zip_reader はエントリ単位での順次読み込みのみ対応しているため、
 * lock/unlock で 1 つの reader を直列化して使う。
 */
class UnzipBase {

public:
	UnzipBase() : refCount(1), zr(NULL), zrstream(NULL), zipfile(), utf8(false), entryOpen(false) {
#ifdef _WIN32
		::InitializeCriticalSection(&cs);
#endif
	}

	void AddRef() {
		refCount++;
	};

	void Release() {
		if (refCount == 1) {
			delete this;
		} else {
			refCount--;
		}
	};

	/**
	 * ZIPファイルを開く
	 * @param filename ファイル名
	 */
	bool init(const ttstr &filename) {
		done();
		zipfile = filename;

		if (!openReader()) return false;

		// UTF8 判定 + ディレクトリエントリ収集
		lock();
		int err = mz_zip_reader_goto_first_entry(zr);
		bool first = true;
		while (err == MZ_OK) {
			mz_zip_file *file_info = NULL;
			if (mz_zip_reader_entry_get_info(zr, &file_info) == MZ_OK && file_info) {
				if (first) {
					utf8 = (file_info->flag & MZ_ZIP_FLAG_UTF8) != 0;
					first = false;
				}
				ttstr name;
				storeFilename(name, file_info->filename ? file_info->filename : "", utf8);
				entryName(name);
			}
			err = mz_zip_reader_goto_next_entry(zr);
		}
		unlock();
		return true;
	}

	/**
	 * 個別の展開用ファイルを開く
	 */
	bool open(const ttstr &srcname, tjs_uint64 *size) {
		if (!zr) return false;
		lock();
		closeEntry();
		NarrowString nname(srcname, utf8);
		if (mz_zip_reader_locate_entry(zr, (const char*)nname, CASESENSITIVITY ? 0 : 1) == MZ_OK) {
			mz_zip_file *file_info = NULL;
			if (size && mz_zip_reader_entry_get_info(zr, &file_info) == MZ_OK && file_info) {
				*size = (tjs_uint64)file_info->uncompressed_size;
			}
			if (mz_zip_reader_entry_open(zr) == MZ_OK) {
				entryOpen = true;
				return true; // unlock は close() 側で行う
			}
		}
		unlock();
		return false;
	}

	/**
	 * 個別の展開用ファイルを最初から開きなおす
	 */
	bool reopenLocked(const ttstr &srcname) {
		// すでに lock 済みでエントリ open 中の状態から呼ばれる
		closeEntry();
		// reader を作り直したほうが安全 (一度 entry を読み始めたあと
		// その entry の先頭まで巻き戻す API がないため)
		if (zr) {
			mz_zip_reader_close(zr);
			mz_zip_reader_delete(&zr);
		}
		if (zrstream) {
			mz_stream_close(zrstream);
			mz_stream_tvp_delete(&zrstream);
		}
		if (!openReader()) return false;
		NarrowString nname(srcname, utf8);
		if (mz_zip_reader_locate_entry(zr, (const char*)nname, CASESENSITIVITY ? 0 : 1) == MZ_OK) {
			if (mz_zip_reader_entry_open(zr) == MZ_OK) {
				entryOpen = true;
				return true;
			}
		}
		return false;
	}

	/**
	 * 個別の展開用ファイルからデータを読み込む
	 */
	tjs_uint read(void *pv, tjs_uint cb) {
		if (!zr || !entryOpen) return 0;
		int32_t r = mz_zip_reader_entry_read(zr, pv, (int32_t)cb);
		return r > 0 ? (tjs_uint)r : 0;
	}

	bool CheckExistentStorage(const ttstr &name) {
		bool ret = false;
		if (zr) {
			lock();
			closeEntry();
			NarrowString nname(name, utf8);
			ret = mz_zip_reader_locate_entry(zr, (const char*)nname, CASESENSITIVITY ? 0 : 1) == MZ_OK;
			unlock();
		}
		return ret;
	}

	void GetListAt(const ttstr &name, iTVPStorageLister *lister) {
		ttstr fname = "/";
		fname += name;
		std::map<ttstr, FileNameList>::const_iterator it = dirEntryTable.find(fname);
		if (it != dirEntryTable.end()) {
			std::vector<ttstr>::const_iterator fit = it->second.begin();
			while (fit != it->second.end()) {
				lister->Add(*fit);
				fit++;
			}
		}
	}

	/**
	 * 個別の展開用ファイルを閉じる
	 */
	void close() {
		if (zr) {
			closeEntry();
			unlock();
		}
	}

	bool isUtf8() const { return utf8; }

protected:

	virtual ~UnzipBase() {
		done();
#ifdef _WIN32
		::DeleteCriticalSection(&cs);
#endif
	}

	bool openReader() {
		iTJSBinaryStream *bs = NULL;
		try {
			bs = TVPCreateStream(zipfile, TJS_BS_READ);
		} catch(...) {
			bs = NULL;
		}
		if (!bs) return false;

		zrstream = mz_stream_tvp_create();
		mz_stream_tvp_attach(zrstream, bs, 1);

		zr = mz_zip_reader_create();
		if (mz_zip_reader_open(zr, zrstream) != MZ_OK) {
			mz_zip_reader_delete(&zr);
			mz_stream_tvp_delete(&zrstream);
			return false;
		}
		return true;
	}

	void done() {
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

	void closeEntry() {
		if (zr && entryOpen) {
			mz_zip_reader_entry_close(zr);
			entryOpen = false;
		}
	}

	void lock() {
#ifdef _WIN32
		::EnterCriticalSection(&cs);
#endif
	}

	void unlock() {
#ifdef _WIN32
		::LeaveCriticalSection(&cs);
#endif
	}

	void entryName(const ttstr &name) {
		ttstr dname = TJS_W("/");
		ttstr fname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = TJS_strrchr(p, '/'))) {
			dname += ttstr(p, q-p+1);
			fname = ttstr(q+1);
		} else {
			fname = name;
		}
		dirEntryTable[dname].push_back(fname);
	}

private:
	int refCount;
	void *zr;
	void *zrstream;
	ttstr zipfile;
	bool utf8;
	bool entryOpen;
#ifdef _WIN32
	CRITICAL_SECTION cs;
#endif

	typedef std::vector<ttstr> FileNameList;
	std::map<ttstr, FileNameList> dirEntryTable;

	// 読み出しのために UnzipStream に開放してもらう
public:
	void unlockAfterRead() { unlock(); }
};

/**
 * ZIP展開ストリームクラス
 */
class UnzipStream : public iTJSBinaryStream {

public:
	UnzipStream(UnzipBase *unzip) : unzip(unzip), filename(), size(0), position(0) {
		unzip->AddRef();
	};

	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) {
		tjs_int64 newpos;
		switch (whence) {
		case TJS_BS_SEEK_CUR: newpos = (tjs_int64)position + offset; break;
		case TJS_BS_SEEK_END: newpos = (tjs_int64)size + offset; break;
		case TJS_BS_SEEK_SET:
		default:              newpos = offset; break;
		}
		if (newpos < 0) newpos = 0;
		if ((tjs_uint64)newpos > size) newpos = (tjs_int64)size;

		// 後方シーク → エントリを開きなおして先頭に戻す
		if ((tjs_uint64)newpos < position) {
			if (!unzip->reopenLocked(filename)) {
				return position;
			}
			position = 0;
		}

		// 前方スキップ
		char dummy[4096];
		while (position < (tjs_uint64)newpos) {
			tjs_uint to_read = sizeof dummy;
			if ((tjs_uint64)newpos - position < to_read) {
				to_read = (tjs_uint)((tjs_uint64)newpos - position);
			}
			tjs_uint r = unzip->read(dummy, to_read);
			if (r == 0) break;
			position += r;
		}
		return position;
	}

	virtual tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) {
		tjs_uint r = unzip->read(buffer, read_size);
		position += r;
		return r;
	}

	virtual tjs_uint TJS_INTF_METHOD Write(const void *buffer, tjs_uint write_size) {
		return 0;
	};

	virtual void TJS_INTF_METHOD SetEndOfStorage() {
	}

	virtual tjs_uint64 TJS_INTF_METHOD GetSize() {
		return size;
	}

	bool init(const ttstr &filename) {
		this->filename = filename;
		return unzip->open(filename, &size);
	}

protected:
	virtual ~UnzipStream() {
		unzip->close();
		unzip->Release();
	}

private:
	UnzipBase *unzip;
	ttstr filename;
	tjs_uint64 size;
	tjs_uint64 position;
};

/**
 * ZIPストレージ
 */
class ZipStorage : public iTVPStorageMedia
{

public:
	ZipStorage() : refCount(1) {
	}

	virtual ~ZipStorage() {
		std::map<ttstr, UnzipBase*>::iterator it = unzipTable.begin();
		while (it != unzipTable.end()) {
			it->second->Release();
			it = unzipTable.erase(it);
		}
	}

public:
	// -----------------------------------
	// iTVPStorageMedia Interfaces
	// -----------------------------------

	virtual void TJS_INTF_METHOD AddRef() {
		refCount++;
	};

	virtual void TJS_INTF_METHOD Release() {
		if (refCount == 1) {
			delete this;
		} else {
			refCount--;
		}
	};

	virtual void TJS_INTF_METHOD GetName(ttstr &name) {
		name = BASENAME;
	}

	virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) {
	}

	virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) {
	}

	virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		return unzip ? unzip->CheckExistentStorage(fname) : false;
	}

	virtual iTJSBinaryStream * TJS_INTF_METHOD Open(const ttstr & name, tjs_uint32 flags) {
		if (flags == TJS_BS_READ) {
			ttstr fname;
			UnzipBase *unzip = getUnzip(name, fname);
			if (unzip) {
				UnzipStream *stream = new UnzipStream(unzip);
				if (stream) {
					if (stream->init(fname)) {
						return stream;
					}
					stream->Destruct();
					stream = 0;
				}
			}
		}
		TVPThrowExceptionMessage(TJS_W("%1:cannot open zipfile"), name);
		return NULL;
	}

	virtual void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister * lister) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		if (unzip) {
			unzip->GetListAt(fname, lister);
		}
	}

	virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) {
		name = "";
	}

public:

	/**
	 * zipファイルをファイルシステムとして mount します
	 * zip://ドメイン名/ファイル名 でアクセス可能になります。読み込み専用になります。
	 * @param name ドメイン名
	 * @param zipfile マウントするZIPファイル名
	 * @return マウントに成功したら true
	 */
	bool mount(const ttstr &name, const ttstr &zipfile) {
		unmount(name);
		UnzipBase *newUnzip = new UnzipBase();
		if (newUnzip) {
			if (newUnzip->init(zipfile)) {
				unzipTable[name] = newUnzip;
				return true;
			} else {
				newUnzip->Release();
			}
		}
		return false;
	}

	/**
	 * zipファイルを unmount します
	 * @param name ドメイン名
	 * @return アンマウントに成功したら true
	 */
	bool unmount(const ttstr &name) {
		std::map<ttstr, UnzipBase*>::iterator it = unzipTable.find(name);
		if (it != unzipTable.end()) {
			it->second->Release();
			unzipTable.erase(it);
			return true;
		}
		return false;
	}

protected:

	UnzipBase *getUnzip(const ttstr &name, ttstr &fname) {
		ttstr dname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = TJS_strchr(p, '/'))) {
			dname = ttstr(p, q-p);
			fname = ttstr(q+1);
		} else {
			TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);
		}
		std::map<ttstr, UnzipBase*>::const_iterator it = unzipTable.find(dname);
		if (it != unzipTable.end()) {
			return it->second;
		}
		return NULL;
	}

private:
	tjs_uint refCount;
	std::map<ttstr, UnzipBase*> unzipTable;
};


/**
 * メソッド追加用
 */
class StoragesZip {

public:

	static void init() {
		if (zip == NULL) {
			zip = new ZipStorage();
			TVPRegisterStorageMedia(zip);
		}
	}

	static void done() {
		if (zip != NULL) {
			TVPUnregisterStorageMedia(zip);
			zip->Release();
			zip = NULL;
		}
	}

	static bool mountZip(const tjs_char *name, const tjs_char *zipfile) {
		if (zip) {
			return zip->mount(ttstr(name), ttstr(zipfile));
		}
		return false;
	}

	static bool unmountZip(const tjs_char *name) {
		if (zip) {
			return zip->unmount(ttstr(name));
		}
		return false;
	}

protected:
	static ZipStorage *zip;
};

ZipStorage *StoragesZip::zip = NULL;

NCB_ATTACH_CLASS(StoragesZip, Storages) {
	NCB_METHOD(mountZip);
	NCB_METHOD(unmountZip);
};

void initZipStorage()
{
	StoragesZip::init();
}

void doneZipStorage()
{
	StoragesZip::done();
}
