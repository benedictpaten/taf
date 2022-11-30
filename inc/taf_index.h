#ifndef TAF_INDEX_H_
#define TAF_INDEX_H_

/*
 * Index a TAF file.  Modeled on .fai from samtools faidx
 * Index format is 
 * Sequence Length Offset Mean-Row-Length TAF-header
 * where TAF header is everything we need to get going parsing the TAF file at that exact position
 *
 */

#include "line_iterator.h"

typedef stSortedSet Tai;
typedef struct _TaiIt TaiIt;

/* Return taf_path + .tai 
 */
char *tai_path(const char *taf_path);

/*
 * Parse a region into contig / start / length, where subrange is optional
 * chr1:10-13 -> chr1 / 10 / 3
 * chr1:10 -> chr1 / 10 / 1
 * chr1 -> chr1 / 0 / LONG_MAX
 */
char *tai_parse_region(const char* region, int64_t *start, int64_t *length);

/*
 * Make an index of a TAF in "idx_fh" 
 * The index is made on the "reference" first contig of each block
 * For each such contig, the index will have one line for each index_block_size
 * region of it found in the TAF. 
 */
int tai_index(LI *li, FILE* idx_fh, int64_t index_block_size);

/*
 * Load the index from disk
 */
Tai *tai_load(FILE* idx_fh);

/*
 * Free the index
 */
void tai_destruct(Tai* idx);

/*
 * Query the taf index
 * start is 0-based
 * length can be -1 for everything
 */
TaiIt *tai_iterator(Tai* idx, LI *li, bool run_length_encode_bases, const char *contig, int64_t start, int64_t length);

/*
 * Iterate through a region as obtained via the iterator
 */
Alignment *tai_next(TaiIt *tai_it, LI *li);
                    
/*
 * Free a tai iterator
 */
void tai_iterator_destruct(TaiIt *tai_it);

#endif
