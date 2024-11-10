Title: minizip plugin
Author: わたなべごう

●これはなに？

吉里吉里で zip アーカイブを扱うプラグインです。
各種データの出力や管理などに利用できます

https://github.com/zlib-ng/minizip-ng をプラグイン化したものです

●ライセンス

zlib ライセンス

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
