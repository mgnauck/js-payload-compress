/*
javascript-payload-compression: Compress input javascript with zopfli deflate.
Write html outfile with small unpack script in onload of svg element that uses
DecompressionStream to uncompress and eval the input javascript.

clang -lz -lzopfli -std=c17 -Wall -Wextra -pedantic js-payload-compress.c

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

#include "zopfli.h"

typedef struct user_options {
  char *javascript_path;
  char *html_path;
  int zopfli_iterations;
  bool no_blocksplitting;
  bool no_compression;
  bool dump_compressed_raw;
  bool no_statistics;
} user_options;

// Command line option names
const char *ZOPFLI_ITERATIONS = "--zopfli-iterations=";
const char *NO_BLOCK_SPLITTING = "--no-blocksplitting";
const char *NO_COMPRESSION = "--no-compression";
const char *DUMP_COMPRESSED_RAW = "--dump-compressed-raw";
const char *NO_STATISTICS = "--no-statistics";

// Length of unpack script is currently 156 chars. This marker is used to
// separate (slice) the unpack from the compressed data. (Actual length of the
// script is calculated dynamically while writing the html.)
// Fetch will get full content of html (script + compressed data) and then
// slice, decompress and eval.
// Alternatives for svg with onload are style, body, script, iframe or img
// onerror with empty src. svg element luckily does NOT display the compressed
// data and is smallest.
char *DECOMPRESSION_SCRIPT =
    "<svg onload=\"fetch`#`.then(r=>r.blob()).then(b=>new "
    "Response(b.slice(%u).stream().pipeThrough(new "
    "DecompressionStream('deflate-raw'))).text()).then(eval)\">";

// For testing purposes. This will basically do the same svg onload unpack but
// omit decompressing the embbeded data (javascript). That means the embbeded
// javascript will be contained as raw source for eval to work.
char *NO_COMPRESSION_SCRIPT =
    "<svg onload=\"fetch`#`.then(r=>r.blob()).then(b=>new "
    "Response(b.slice(%u).stream()).text()).then(eval)\">";

char *read_text_file(const char *infile_path) {
  char *text = NULL;
  FILE *file = fopen(infile_path, "rt");

  if (file != NULL) {
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    // Add an additional byte for null terminating the string
    text = (char *)calloc(size + 1, 1);

    if (fread(text, 1, size, file) != size) {
      printf("Failed to read source file '%s'\n", infile_path);
      free(text);
      text = NULL;
    }

    fclose(file);
  } else {
    printf("Failed to open source file '%s'\n", infile_path);
  }

  return text;
}

void compress(unsigned char *source_data, size_t source_data_size,
              unsigned char **compressed_data, size_t *compressed_data_size,
              user_options *user_options) {
  ZopfliOptions zopfli_options;
  ZopfliInitOptions(&zopfli_options);
  zopfli_options.numiterations = user_options->zopfli_iterations;
  zopfli_options.blocksplitting = !user_options->no_blocksplitting;
  ZopfliCompress(&zopfli_options, ZOPFLI_FORMAT_DEFLATE, source_data,
                 source_data_size, compressed_data, compressed_data_size);
}

bool write_html(char *outfile_path, char *unpack_script,
               unsigned char *compressed_data, size_t compressed_data_size,
               size_t *outfile_size) {
  FILE *outfile = fopen(outfile_path, "wb+");
  if (outfile == NULL) {
    printf("Failed to create destination file '%s'\n", outfile_path);
    return false;
  }

  // Substitute actual script length in unpack script
  size_t max_script_length = strlen(unpack_script) + 2;
  char final_script[max_script_length];
  snprintf(final_script, max_script_length, unpack_script,
           // %u formatter is already included with 2 chars (but will be
           // substituted with actual length) and we need one more char since
           // the actual script length will require 3 chars
           strlen(unpack_script) + 1);
  if (fwrite(final_script, 1, strlen(final_script), outfile) !=
      strlen(final_script)) {
    fclose(outfile);
    return false;
  }

  if (fwrite(compressed_data, 1, compressed_data_size, outfile) !=
      compressed_data_size) {
    fclose(outfile);
    return false;
  }

  fclose(outfile);

  // Calculate size of output file
  *outfile_size = strlen(final_script) + compressed_data_size;

  return true;
}

bool write_raw(char *outfile_path, unsigned char *compressed_data,
              size_t compressed_data_size) {
  FILE *outfile = fopen(outfile_path, "wb+");
  if (outfile == NULL) {
    printf("Failed to create destination file '%s'\n", outfile_path);
    return false;
  }

  if (fwrite(compressed_data, 1, compressed_data_size, outfile) !=
      compressed_data_size) {
    fclose(outfile);
    return false;
  }

  fclose(outfile);

  return true;
}

void print_compression_statistics(size_t source_data_size,
                                  size_t compressed_data_size,
                                  bool no_compression) {
  printf("Input Javascript size: %lu bytes\n", source_data_size);
  printf("Output HTML file size: %li bytes\n", compressed_data_size);
  printf("HTML is %3.2f percent of javascript\n",
         compressed_data_size / (float)source_data_size * 100.0f);
  if (no_compression) {
    printf("No compression flag was specified\n");
  }
}

void print_usage_information() {
  printf("Usage: js-payload-compress [options] infile.js outfile.html\n");
  printf("\n");
  printf("Options:\n");
  printf("%s[number]: Number of zopfli iterations. More ", ZOPFLI_ITERATIONS);
  printf("iterations take\n  more time but can provide slightly better ");
  printf("compression. Default is 50.\n");
  printf("%s: Do not use block splitting.\n", NO_BLOCK_SPLITTING);
  printf("%s: No compression (for testing).\n", NO_COMPRESSION);
  printf("%s: Dump compressed data to file raw (w/o unpack script).\n",
         DUMP_COMPRESSED_RAW);
  printf("  Attaches '.bin' to infile path for raw output.\n");
  printf("%s: Do not show statistics.\n", NO_STATISTICS);
}

void process_command_line(user_options *user_options, int argc, char *argv[]) {
  if (argc < 3) {
    print_usage_information();
    return;
  }

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], ZOPFLI_ITERATIONS, strlen(ZOPFLI_ITERATIONS)) == 0) {
      char iterations[strlen(argv[i]) - strlen(ZOPFLI_ITERATIONS)];
      strncpy(iterations, argv[i] + strlen(ZOPFLI_ITERATIONS),
              sizeof(iterations));
      user_options->zopfli_iterations = atoi(iterations);
      continue;
    }

    if (strncmp(argv[i], NO_BLOCK_SPLITTING, strlen(NO_BLOCK_SPLITTING)) == 0) {
      user_options->no_blocksplitting = true;
      continue;
    }

    if (strncmp(argv[i], NO_COMPRESSION, strlen(NO_COMPRESSION)) == 0) {
      user_options->no_compression = true;
      continue;
    }

    if (strncmp(argv[i], DUMP_COMPRESSED_RAW, strlen(DUMP_COMPRESSED_RAW)) ==
        0) {
      user_options->dump_compressed_raw = true;
      continue;
    }
    if (strncmp(argv[i], NO_STATISTICS, strlen(NO_STATISTICS)) == 0) {
      user_options->no_statistics = true;
      continue;
    }

    if (user_options->javascript_path == NULL) {
      user_options->javascript_path = argv[i];
    } else {
      user_options->html_path = argv[i];
    }
  }
}

int main(int argc, char *argv[]) {
  printf("js-payload-compress\n");

  user_options user_options = {NULL, NULL, 50, false, false, false, false};
  process_command_line(&user_options, argc, argv);
  if (user_options.javascript_path == NULL || user_options.html_path == NULL) {
    printf("Failed to interpret commandline (specified in or out file).\n");
    exit(EXIT_FAILURE);
  }

  char *javascript = read_text_file(user_options.javascript_path);
  if (javascript == NULL) {
    exit(EXIT_FAILURE);
  }

  size_t javascript_size = strlen(javascript);

  unsigned char *compressed_javascript = NULL;
  size_t compressed_javascript_size = 0;
  if (!user_options.no_compression) {
    compress((unsigned char *)javascript, javascript_size,
             &compressed_javascript, &compressed_javascript_size,
             &user_options);
    free(javascript);
    if (compressed_javascript == NULL) {
      exit(EXIT_FAILURE);
    }
  } else {
    compressed_javascript = (unsigned char *)javascript;
    compressed_javascript_size = javascript_size;
  }

  size_t outfile_size = 0;
  bool error = write_html(user_options.html_path,
                          user_options.no_compression ? NO_COMPRESSION_SCRIPT
                                                      : DECOMPRESSION_SCRIPT,
                          compressed_javascript, compressed_javascript_size,
                          &outfile_size);
  if (!error && !user_options.no_statistics) {
    print_compression_statistics(javascript_size, outfile_size,
                                 user_options.no_compression);
  }

  if (!user_options.no_compression && user_options.dump_compressed_raw) {
    char raw_path[strlen(user_options.javascript_path) + 5];
    snprintf(raw_path, sizeof(raw_path), "%s.bin",
             user_options.javascript_path);
    error =
        write_raw(raw_path, compressed_javascript, compressed_javascript_size);
  }

  free(compressed_javascript);

  return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
