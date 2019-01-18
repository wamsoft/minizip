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

●使い方

manual.tjs 参照

●注意点

ビルドにはWindows用のcmakeが必要です。
https://cmake.org/ から最新の安定版をダウンロードし、
パスが通っている箇所にインストールしておいて下さい。

●ライセンス

zlib 付属の contrib/minizip からfork した nmoinvaz/minizip
(https://github.com/nmoinvaz/minizip) をプラグイン化したものです
zlib ライセンスになります。

