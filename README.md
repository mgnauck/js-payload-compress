# js-payload-compress

Use this to compress your 4k/8k/64k javascript intro. Compresses an input javascript with [zopfli](https://github.com/google/zopfli) deflate. Writes a html output file containing a minimal unpack script in the onload attribute of a svg element that uses a [DecompressionStream](https://developer.mozilla.org/en-US/docs/Web/API/DecompressionStream) to decompress and eval the input javascript.

## Build:
`cc js-payload-compress.c -std=c2x -Wall -Wextra -pedantic -lzopfli -o js-payload-compress`

## Based on (thanks!):
- [0b5vr](https://gist.github.com/0b5vr/09ee96ca2efbe5bf9d64dad7220e923b)
- [subzey](https://github.com/subzey/fetchcrunch)

## Uses:
- [Google zopfli](https://github.com/google/zopfli)

## Usage:
```
js-payload-compress [options] infile.js outfile.html

Options:
--zopfli-iterations=[number]: Number of zopfli iterations. More iterations take more time but can provide slightly better compression. Default is 50.
--decompression-type=[type]: Decompression type as per DecompressionStream API, i.e. gzip or deflate-raw (default).
--no-blocksplitting: Do not use block splitting.
--no-compression: No compression (i.e. with decompression type 'gzip' or for testing).
--no-decompression-script: Use the unpack script w/o decompression (for testing).
--dump-compressed-raw: Dump compressed data to file raw (w/o unpack script). Attaches '.bin' to infile path for raw output.
--write-no-html: Write no html (i.e. raw only).
--no-statistics: Do not show statistics.
```
