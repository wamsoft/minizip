#include <stdio.h>
#include <string.h>
#include <ncbind.hpp>
#include <map>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include "mz_compat.h"

#include "narrow.h"

#define CASESENSITIVITY (0)

#define BASENAME L"zip"

// UTF8なファイル名かどうかのフラグ
#define FLAG_UTF8 (1<<11)
extern void storeFilename(ttstr &name, const char *narrowName, bool utf8);

// ファイルアクセス用
extern zlib_filefunc64_def TVPZlibFileFunc;

/**
 * Zip 展開処理クラス
 */
class UnzipBase {

public:
	UnzipBase() : refCount(1), uf(NULL), utf8(false) {
		::InitializeCriticalSection(&cs);
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
		if ((uf = unzOpen2_64((const void*)filename.c_str(), &TVPZlibFileFunc)) != NULL) {
			lock();
			unzGoToFirstFile(uf);
			unz_file_info file_info;
			// UTF8判定
			if (unzGetCurrentFileInfo(uf, &file_info,NULL,0,NULL,0,NULL,0) == UNZ_OK) {
				utf8 = (file_info.flag & FLAG_UTF8) != 0;
			}
			do {
				char filename_inzip[1024];
				unz_file_info file_info;
				if (unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip),NULL,0,NULL,0) == UNZ_OK) {
					ttstr filename;
					storeFilename(filename, filename_inzip, utf8);
					entryName(filename);
				}
			} while (unzGoToNextFile(uf) == UNZ_OK);
			unlock();
			return true;
		}
		return false;
	}

	/**
	 * 個別の展開用ファイルを開く
	 */
	bool open(const ttstr &srcname, ULONG *size) {
		if (uf) {
			lock();
			if (unzLocateFile(uf, NarrowString(srcname, utf8), CASESENSITIVITY) == UNZ_OK) {
				if (size) {
					unz_file_info file_info;
					if (unzGetCurrentFileInfo(uf, &file_info, NULL,0,NULL,0,NULL,0) == UNZ_OK) {
						*size = file_info.uncompressed_size;
					}
				}
				if (unzOpenCurrentFile(uf) == UNZ_OK) {
					return true;
				}
			}
			unlock();
		}
		return false;
	}

	/**
	 * 個別の展開用ファイルからデータを読み込む
	 */
	HRESULT read(void *pv, ULONG cb, ULONG *pcbRead) {
		if (uf) {
			lock();
			DWORD size = unzReadCurrentFile(uf, pv,cb);
			if (pcbRead) {
				*pcbRead = size;
			}
			unlock();
			return size < cb ? S_FALSE : S_OK;
		}
		return STG_E_ACCESSDENIED;
	}

	HRESULT seek(ZPOS64_T pos) {
		if (uf) {
			HRESULT ret;
			lock();
			ret = unzSetOffset64(uf, pos) == UNZ_OK ? S_OK : S_FALSE;
			unlock();
			return ret;
		}
		return STG_E_ACCESSDENIED;
	}
	
	ZPOS64_T tell() {
		ZPOS64_T ret = 0;
		if (uf) {
			lock();
			ret = unzTell64(uf);
			unlock();
		}
		return ret;
	}

	/**
	 * 個別の展開用ファイルを閉じる
	 */
	void close() {
		if (uf) {
			lock();
			unzCloseCurrentFile(uf);
			unlock();
		}
	}
	
	bool CheckExistentStorage(const ttstr &name) {
		bool ret = true;
		if (uf) {
			lock();
			ret = unzLocateFile(uf, NarrowString(name, utf8), CASESENSITIVITY) == UNZ_OK;
			unlock();
		}
		return ret;
	}
	
	void GetListAt(const ttstr &name, iTVPStorageLister *lister) {
		ttstr fname = "/";
		fname += name;
		std::map<ttstr,FileNameList>::const_iterator it = dirEntryTable.find(fname);
		if (it != dirEntryTable.end()) {
			std::vector<ttstr>::const_iterator fit = it->second.begin();
			while (fit != it->second.end()) {
				lister->Add(*fit);
				fit++;
			}
		}
	}

protected:

	/**
	 * デストラクタ
	 */
	virtual ~UnzipBase() {
		done();
		::DeleteCriticalSection(&cs);
	}

	void done() {
		if (uf) {
			unzClose(uf);
			uf = NULL;
		}
	}

	// ロック
	void lock() {
		::EnterCriticalSection(&cs);
	}

	// ロック解除
	void unlock() {
		::LeaveCriticalSection(&cs);
	}

	void entryName(const ttstr &name) {
		ttstr dname = TJS_W("/");
		ttstr fname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = wcsrchr(p, '/'))) {
			dname += ttstr(p, q-p+1);
			fname = ttstr(q+1);
		} else {
			fname = name;
		}
		dirEntryTable[dname].push_back(fname);
	}
	
private:
	int refCount;
	// zipファイル情報
	unzFile uf;
	bool utf8;
	CRITICAL_SECTION cs;

	// ディレクトリ別ファイル名エントリ情報
	typedef std::vector<ttstr> FileNameList;
	std::map<ttstr,FileNameList> dirEntryTable;
};

/**
 * ZIP展開ストリームクラス
 */
class UnzipStream : public iTJSBinaryStream {

public:
	/**
	 * コンストラクタ
	 */
	UnzipStream(UnzipBase *unzip) : unzip(unzip), size(0) {
		unzip->AddRef();
	};

	/* if error, position is not changed */
	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) {
		// 先頭にだけ戻せる
		ZPOS64_T cur;
		switch (whence) {
		case TJS_BS_SEEK_CUR:
			cur = unzip->tell();
			cur += offset;
			break;
		default:
		case TJS_BS_SEEK_SET:
			cur = offset;
			break;
		case TJS_BS_SEEK_END:
			cur = this->size;
			cur += offset;
			break;
		}
		if (unzip->seek(cur) == S_OK) {
			return cur;
		}
		return EOF;
	}

	virtual tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) {
		ULONG pcbRead;
		if (unzip->read(buffer, read_size, &pcbRead) == S_OK) {
			return pcbRead;
		}
		return 0;
	}

	virtual tjs_uint TJS_INTF_METHOD Write(const void *buffer, tjs_uint write_size) {
		return 0;
	};

	virtual void TJS_INTF_METHOD SetEndOfStorage() {
	}

	virtual tjs_uint64 TJS_INTF_METHOD GetSize() {;
		return size;
	}

	bool init(const ttstr filename) {
		return unzip->open(filename, &size);
	}
	
protected:
	/**
	 * デストラクタ
	 */
	virtual ~UnzipStream() {
		close();
		unzip->Release();
	}

	void close() {
		unzip->close();
	}
	
private:
	UnzipBase *unzip;
	ULONG size;
};

/**
 * ZIPストレージ
 */
class ZipStorage : public iTVPStorageMedia
{

public:
	/**
	 * コンストラクタ
	 */
	ZipStorage() : refCount(1) {
	}

	/**
	 * デストラクタ
	 */
	virtual ~ZipStorage() {
		// 全情報を破棄
		std::map<ttstr, UnzipBase*>::iterator it = unzipTable.begin();
		while (it != unzipTable.end()) {
			it->second->Release();
			it = unzipTable.erase(it);
		}
	}

public:
	// -----------------------------------
	// iTVPStorageMedia Intefaces
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

	// returns media name like "file", "http" etc.
	virtual void TJS_INTF_METHOD GetName(ttstr &name) {
		name = BASENAME;
	}

	//	virtual ttstr TJS_INTF_METHOD IsCaseSensitive() = 0;
	// returns whether this media is case sensitive or not

	// normalize domain name according with the media's rule
	virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) {
		// nothing to do
	}

	// normalize path name according with the media's rule
	virtual void TJS_INTF_METHOD NormalizePathName(ttstr &name) {
		// nothing to do
	}

	// check file existence
	virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		return unzip ? unzip->CheckExistentStorage(fname) : false;
	}

	// open a storage and return a tTJSBinaryStream instance.
	// name does not contain in-archive storage name but
	// is normalized.
	virtual iTJSBinaryStream * TJS_INTF_METHOD Open(const ttstr & name, tjs_uint32 flags) {
		if (flags == TJS_BS_READ) { // 読み込みのみ
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

	// list files at given place
	virtual void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister * lister) {
		ttstr fname;
		UnzipBase *unzip = getUnzip(name, fname);
		if (unzip) {
			unzip->GetListAt(fname, lister);
		}
	}

	// basically the same as above,
	// check wether given name is easily accessible from local OS filesystem.
	// if true, returns local OS native name. otherwise returns an empty string.
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

	/*
	 * ドメインに合致した Unzip 情報を取得
	 * @param name ファイル名
	 * @param fname ファイル名を返す
	 * @return Unzip情報
	 */
	UnzipBase *getUnzip(const ttstr &name, ttstr &fname) {
		ttstr dname;
		const tjs_char *p = name.c_str();
		const tjs_char *q;
		if ((q = wcschr(p, '/'))) {
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
	tjs_uint refCount; //< リファレンスカウント
	std::map<ttstr, UnzipBase*> unzipTable; //< zip情報
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

	/**
	 * zipファイルをファイルシステムとして mount します
	 * zip://ドメイン名/ファイル名 でアクセス可能になります。読み込み専用になります。
	 * @param name ドメイン名
	 * @param zipfile マウントするZIPファイル名
	 * @return マウントに成功したら true
	 */
	static bool mountZip(const tjs_char *name, const tjs_char *zipfile) {
		if (zip) {
			return zip->mount(ttstr(name), ttstr(zipfile));
		}
		return false;
	}

	/**
	 * zipファイルを unmount します
	 * @param name ドメイン名
	 * @return アンマウントに成功したら true
	 */
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
