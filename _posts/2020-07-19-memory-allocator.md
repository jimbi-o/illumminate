---
layout: post
title:  "light weight memory allocator for graphics"
date:   2020-07-19 08:00:00 +0900
categories: renderer
---

# motivation

stl containerを軽々しく使いたい。

* スタック(ないしは事前に確保された領域)に積んで領域抜けたら消えるバージョン
  * 関数内での作業領域用
* frame bufferingしてgpu側の処理が終わったら解放されるバージョン
  * constant bufferのコピー先
  * LinearAllocatorで十分
* フレーム開始時に確保されてフレーム終了時に解放されるバージョン
  * 関数間での受け渡しがある場合
  * LinearAllocatorで十分

<https://github.com/foonathan/memory>をお借りする。  
と思ったけどvectorで使うのにドキュメント読み込まないとダメそうだったので中断。  
勉強になるし自前アロケータ作ってみよう。

# implementation

<https://github.com/jimbi-o/integtester/blob/master/src/custom_allocator_test.cpp>

# refs.

* <https://www.modernescpp.com/index.php/memory-management-with-std-allocator>
* <https://github.com/electronicarts/EASTL>
* <https://github.com/google/sanitizers>
* <https://github.com/foonathan/memory>
* <https://en.cppreference.com/w/cpp/named_req/Allocator>
* <https://github.com/microsoft/mimalloc>
