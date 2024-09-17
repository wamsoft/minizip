Title: minizip plugin
Author: わたなべごう

●これはなに？

吉里吉里で zip アーカイブを扱うプラグインです。
各種データの出力や管理などに利用できます

This unzip package allow creates .ZIP file, compatible with PKZip 2.04g
WinZip, InfoZip tools and compatible.
Multi volume ZipFile (span) are not supported.
Encryption compatible with pkzip 2.04g only supported
Old compressions used by old PKZip 1.x are not supported

●ライセンス

zlib 付属の contrib/minizip からfork した nmoinvaz/minizip
(https://github.com/nmoinvaz/minizip) をプラグイン化したものです
zlib ライセンスになります。

●ビルド

ビルドにはWindows用のcmakeが必要です。

VisualStudio のコマンドラインから以下で対応

```
# win32指定しないとx64になる場合あり
cmake -A win32 -B build
cmake --build build --config Release

build/Release/minizip.dll

```

●使い方

manual.tjs 参照



