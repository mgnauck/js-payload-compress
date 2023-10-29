# js-payload-compress

Compress input javascript with [zopfli](https://github.com/google/zopfli) deflate. Write html outfile with small unpack script in onload of svg element that uses [DecompressionStream](https://developer.mozilla.org/en-US/docs/Web/API/DecompressionStream) to uncompress and eval the input javascript. Offers a 'no compression' mode for testing.

clang -lz -lzopfli -std=c17 -Wall -Wextra -pedantic js-payload-compress.c

Based on (thanks!):
- [0b5vr](https://gist.github.com/0b5vr/09ee96ca2efbe5bf9d64dad7220e923b)
- [subzey](https://github.com/subzey/fetchcrunch)

Uses:
- [Google zopfli](https://github.com/google/zopfli)
