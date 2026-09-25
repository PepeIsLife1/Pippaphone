/* Allocazione per Codec2 compilato con __EMBEDDED__ (codebook costanti in flash). */
#include <stdlib.h>

void *codec2_malloc(size_t size) { return malloc(size); }
void *codec2_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void codec2_free(void *ptr) { free(ptr); }
