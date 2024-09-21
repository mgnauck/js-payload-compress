/*
Compression for 4k/8k/64k javascript intros. Compress input payload (e.g. javascript)
with zopfli deflate. Write html outfile with small unpack script in onload
of svg element that uses DecompressionStream to decompress and eval the
input javascript.

Author: Markus Gnauck

Build:
cc js-payload-compress.c -std=c2x -Wall -Wextra -pedantic -lzopfli -o js-payload-compress

Based on work by 0b5vr and subzey:
https://gist.github.com/0b5vr/09ee96ca2efbe5bf9d64dad7220e923b
https://github.com/subzey/fetchcrunch

Uses:
https://github.com/google/zopfli
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zopfli/zopfli.h>

typedef struct user_options {
  char  *payload_path;
  char  *html_path;
  int   zopfli_iters;
  char  *decompress_type;
  bool  no_blocksplit;
  bool  no_compress;
  bool  no_decompress_script;
  bool  dump_raw;
  bool  no_html;
  bool  no_stats;
} user_options;

// Command line option names
const char *OPT_ZOPFLI_ITERS          = "--zopfli-iterations=";
const char *OPT_DECOMPRESS_TYPE       = "--decompression-type=";
const char *OPT_NO_BLOCK_SPLIT        = "--no-blocksplitting";
const char *OPT_NO_COMPRESS           = "--no-compression";
const char *OPT_NO_DECOMPRESS_SCRIPT  = "--no-decompression-script";
const char *OPT_DUMP_RAW              = "--dump-compressed-raw";
const char *OPT_NO_HTML               = "--write-no-html";
const char *OPT_NO_STATS              = "--no-statistics";

// Length of unpack script is currently 156 chars (deflate-raw). This length
// marker is used to separate (slice) the unpack script from the compressed
// data. (Actual length of the script is calculated dynamically while writing
// the html.) Fetch will get full content of html (script + compressed data)
// and then slice, decompress and eval.
// Alternatives for svg with onload are style, body, script, iframe or img
// onerror with empty src. svg element luckily does NOT display the compressed
// data and is smallest.
const char *DECOMPRESSION_SCRIPT =
    "<svg onload=\"fetch`#`.then(r=>r.blob()).then(b=>new "
    "Response(b.slice(%u).stream().pipeThrough(new "
    "DecompressionStream('%s'))).text()).then(eval)\">";

// For testing purposes. This will basically do the same svg onload unpack but
// omit decompressing the embbeded data (javascript). That means the embbeded
// javascript will be contained as raw source for eval to work.
const char *NO_DECOMPRESSION_SCRIPT =
    "<svg onload=\"fetch`#`.then(r=>r.blob()).then(b=>new "
    "Response(b.slice(%u).stream()).text()).then(eval)\">";

size_t read_data_file(const char *infile_path, unsigned char **data)
{
  FILE *file = fopen(infile_path, "rb");
  if(file != NULL) {
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    *data = malloc(size * sizeof(**data));

    if(fread(*data, 1, size, file) != size) {
      printf("Failed to read source file '%s'\n", infile_path);
      free(data);
      data = NULL;
    }

    fclose(file);
    return size;
  } else {
    printf("Failed to open source file '%s'\n", infile_path);
  }

  return 0;
}

void compress(unsigned char *source_data, size_t source_data_size,
              unsigned char **compressed_data, size_t *compressed_data_size,
              user_options *user_options)
{
  ZopfliOptions zopfli_options;
  ZopfliInitOptions(&zopfli_options);
  zopfli_options.numiterations = user_options->zopfli_iters;
  zopfli_options.blocksplitting = !user_options->no_blocksplit;
  ZopfliCompress(&zopfli_options, ZOPFLI_FORMAT_DEFLATE, source_data,
                 source_data_size, compressed_data, compressed_data_size);
}

bool write_html(char *outfile_path, const char *unpack_script, const char *decompress_type,
               unsigned char *compressed_data, size_t compressed_data_size,
               bool no_decompress, size_t *outfile_size)
{
  FILE *outfile = fopen(outfile_path, "wb+");
  if(outfile == NULL) {
    printf("Failed to create destination file '%s'\n", outfile_path);
    return true;
  }

  // Substitute actual script length in unpack script
  size_t max_script_length = strlen(unpack_script) + 2 +
    (no_decompress ? 0 : strlen(decompress_type));

  char final_script[max_script_length];
  snprintf(final_script, max_script_length, unpack_script,
      // Remove %s and %u from the length (-4), add actual length with 3 chars
      // and the number of chars required for the decompression type
      strlen(unpack_script) + 3 - 2 +
        (no_decompress ? 0 : strlen(decompress_type) - 2), decompress_type);

  if(fwrite(final_script, 1, strlen(final_script), outfile) !=
      strlen(final_script)) {
    printf("Failed to write final script to destination file\n");
    fclose(outfile);
    return true;
  }

  if(fwrite(compressed_data, 1, compressed_data_size, outfile) !=
      compressed_data_size) {
    printf("Failed to write compressed data to destination file\n");
    fclose(outfile);
    return true;
  }

  fclose(outfile);

  // Calculate size of output file
  *outfile_size = strlen(final_script) + compressed_data_size;

  return false;
}

bool write_raw(char *outfile_path, unsigned char *compressed_data,
              size_t compressed_data_size)
{
  FILE *outfile = fopen(outfile_path, "wb+");
  if(outfile == NULL) {
    printf("Failed to create destination file '%s'\n", outfile_path);
    return true;
  }

  if(fwrite(compressed_data, 1, compressed_data_size, outfile) !=
      compressed_data_size) {
    printf("Failed to write compressed data to destination file\n");
    fclose(outfile);
    return true;
  }

  fclose(outfile);

  return false;
}

void print_compression_statistics(size_t source_data_size,
                                  size_t compressed_data_size,
                                  user_options *options,
                                  const char *output_type)
{
  printf("Input Javascript size: %lu bytes\n", source_data_size);
  printf("Output %s file size: %li bytes\n", output_type, compressed_data_size);
  printf("Output is %3.2f percent of input\n",
         compressed_data_size / (float)source_data_size * 100.0f);
  if(options->no_compress)
    printf("No compression flag was specified\n");
  if(!options->no_html)
    printf("Decompression type is '%s'\n", options->decompress_type);
}

void print_usage_information()
{
  printf("Usage: js-payload-compress [options] infile.js outfile.html\n");
  printf("\n");
  printf("Options:\n");
  printf("%s[number]: Number of zopfli iterations. More ", OPT_ZOPFLI_ITERS);
  printf("iterations take\n  more time but can provide slightly better ");
  printf("compression. Default is 50.\n");
  printf("%s[type]: Decompression type as per ", OPT_DECOMPRESS_TYPE);
  printf("DecompressionStream API (gzip or deflate-raw).\n");
  printf("%s: Do not use block splitting.\n", OPT_NO_BLOCK_SPLIT);
  printf("%s: No payload compression ", OPT_NO_COMPRESS);
  printf("(i.e. with decompression type 'gzip' or for testing).\n");
  printf("%s: Use the unpack script w/o decompression (for testing).\n",
      OPT_NO_DECOMPRESS_SCRIPT);
  printf("%s: Dump compressed data raw to file (w/o unpack script).\n",
         OPT_DUMP_RAW);
  printf("  Attaches '.raw' to outfile path for raw output.\n");
  printf("%s: Write no html (i.e. raw only).\n", OPT_NO_HTML);
  printf("%s: Do not show statistics.\n", OPT_NO_STATS);
}

void process_command_line(user_options *user_options, int argc, char *argv[])
{
  if(argc < 3) {
    print_usage_information();
    return;
  }

  for (int i = 1; i < argc; i++) {
    if(strncmp(argv[i], OPT_ZOPFLI_ITERS, strlen(OPT_ZOPFLI_ITERS)) == 0) {
      char iterations[strlen(argv[i]) - strlen(OPT_ZOPFLI_ITERS)];
      strncpy(iterations, argv[i] + strlen(OPT_ZOPFLI_ITERS),
              sizeof(iterations));
      user_options->zopfli_iters = atoi(iterations);
      continue;
    }

    if(strncmp(argv[i], OPT_DECOMPRESS_TYPE, strlen(OPT_DECOMPRESS_TYPE)) == 0) {
      // In theory the user provided decompression type string could be too big
      char type[strlen(argv[i]) - strlen(OPT_DECOMPRESS_TYPE)];
      strncpy(type, argv[i] + strlen(OPT_DECOMPRESS_TYPE), sizeof(type));
      user_options->decompress_type = strdup(type);
      continue;
    }

    if(strncmp(argv[i], OPT_NO_BLOCK_SPLIT, strlen(OPT_NO_BLOCK_SPLIT)) == 0) {
      user_options->no_blocksplit = true;
      continue;
    }

    if(strncmp(argv[i], OPT_NO_COMPRESS, strlen(OPT_NO_COMPRESS)) == 0) {
      user_options->no_compress = true;
      continue;
    }

    if(strncmp(argv[i], OPT_NO_DECOMPRESS_SCRIPT, strlen(OPT_NO_DECOMPRESS_SCRIPT)) == 0) {
      user_options->no_decompress_script = true;
      continue;
    }

    if(strncmp(argv[i], OPT_DUMP_RAW, strlen(OPT_DUMP_RAW)) == 0) {
      user_options->dump_raw = true;
      continue;
    }

    if(strncmp(argv[i], OPT_NO_HTML, strlen(OPT_NO_HTML)) == 0) {
      user_options->no_html = true;
      continue;
    }

    if(strncmp(argv[i], OPT_NO_STATS, strlen(OPT_NO_STATS)) == 0) {
      user_options->no_stats = true;
      continue;
    }

    // Defaults
    if(user_options->decompress_type == NULL) {
      user_options->decompress_type = strdup("deflate-raw");
    }

    if(user_options->payload_path == NULL) {
      user_options->payload_path = argv[i];
    } else {
      user_options->html_path = argv[i];
    }
  }
}

int main(int argc, char *argv[])
{
  bool error = false;
  user_options user_options =
    { NULL, NULL, 50, NULL, false, false, false, false, false, false };
  process_command_line(&user_options, argc, argv);
  if(user_options.payload_path == NULL || user_options.html_path == NULL) {
    printf("Failed to interpret commandline (specified in or out file).\n");
    error = true;
    goto err;
  }

  unsigned char *payload = NULL;
  size_t payload_size = read_data_file(user_options.payload_path, &payload);
  if(payload == NULL || payload_size == 0) {
    error = true;
    goto err;
  }

  unsigned char *compressed_payload = NULL;
  size_t compressed_payload_size = 0;
  if(!user_options.no_compress) {
    compress(payload, payload_size,
        &compressed_payload, &compressed_payload_size, &user_options);
    free(payload);
    if(compressed_payload == NULL) {
      error = true;
      goto err;
    }
  } else {
    compressed_payload = payload;
    compressed_payload_size = payload_size;
  }

  if(!user_options.no_html) {
    size_t outfile_size = 0;
    error = write_html(user_options.html_path, user_options.no_decompress_script ?
       NO_DECOMPRESSION_SCRIPT : DECOMPRESSION_SCRIPT,
       user_options.decompress_type, compressed_payload,
        compressed_payload_size, user_options.no_decompress_script, &outfile_size);
    if(!error && !user_options.no_stats) {
      printf("* Html output stats:\n");
      print_compression_statistics(payload_size, outfile_size, &user_options,
          "html");
    }
  }

  if(user_options.dump_raw) {
    char raw_path[strlen(user_options.html_path) + 5];
    snprintf(raw_path, sizeof(raw_path), "%s.raw",
             user_options.html_path);
    error = write_raw(raw_path, compressed_payload, compressed_payload_size);
    if(!error && !user_options.no_stats) {
      printf("* Raw output stats:\n");
      print_compression_statistics(payload_size, compressed_payload_size,
          &user_options, "raw");
    }
  }

  free(compressed_payload);

err:
  free(user_options.decompress_type);

  return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
