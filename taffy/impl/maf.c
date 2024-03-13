//
// Created by Benedict Paten on 8/25/22.

#include "taf.h"
#include "sonLib.h"
#include "line_iterator.h"
#include "base64.h"

static void set_maf_qualities(Alignment *alignment, stList* row_qualities, stList* row_quality_rows) {
    // transpose our row qualities into the tags.
    // first, make a tag for each column
    for (int64_t i = 0; i < alignment->column_number; ++i) {
        unsigned char *column_qualities = (unsigned char*)st_calloc(alignment->row_number, sizeof(unsigned char));
        int64_t col_row_idx = 0;
        for (int64_t j = 0; j < alignment->row_number; ++j) {
            unsigned char qual = 255; // todo: is there a better default?
            if (col_row_idx < stList_length(row_quality_rows) &&
                j == (int64_t)stList_get(row_quality_rows, col_row_idx)) {
                char* row_quals = (char*)stList_get(row_qualities, col_row_idx);                
                qual = (unsigned char)row_quals[i];
                // apply conversion from maf qual to raw qual
                // as defined on https://genome.cse.ucsc.edu/FAQ/FAQformat.html#format5
                if (qual == 'F') {
                    qual = 99;
                } else if (qual != '-') {
                    assert(qual >= '0' && qual <= '9');
                    qual = (qual - '0') * 5;
                }
                ++col_row_idx;
            }
            column_qualities[j] = qual;
        }
        char *encoded_qualities = (char*)st_calloc(BASE64_ENCODE_OUT_SIZE(alignment->row_number) + 1, sizeof(char));
        base64_encode(column_qualities, alignment->row_number, encoded_qualities);
        alignment->column_tags[i] = tag_construct(TAF_BASE_QUALITY_TAG_KEY, encoded_qualities, NULL);
        free(column_qualities);
        free(encoded_qualities);
    }
    stList_destruct(row_qualities);
    stList_destruct(row_quality_rows);
}

Alignment *maf_read_block(LI *li) {
    while(1) {
        char *line = LI_get_next_line(li);
        if(line == NULL) {
            return NULL;
        }
        stList *tokens = stString_split(line);
        free(line);
        if(stList_length(tokens) == 0) {
            stList_destruct(tokens);
            continue;
        }
        if(strcmp(stList_get(tokens, 0), "a") == 0) { // If is an "a" line
            stList_destruct(tokens);
            Alignment *alignment = st_calloc(1, sizeof(Alignment));
            Alignment_Row **p_row = &(alignment->row);
            Alignment_Row *last_row = NULL;
            stList *row_qualities = NULL;
            stList *row_quality_rows = NULL;
            while(1) {
                line = LI_get_next_line(li);
                if(line == NULL) {
                    if (row_qualities != NULL) {
                        set_maf_qualities(alignment, row_qualities, row_quality_rows);
                    }
                    return alignment;
                }
                tokens = stString_split(line);
                free(line);
                if(stList_length(tokens) == 0) {
                    stList_destruct(tokens);
                    if (row_qualities != NULL) {
                        set_maf_qualities(alignment, row_qualities, row_quality_rows);
                    }                    
                    return alignment;
                }
                if(strcmp(stList_get(tokens, 0), "s") == 0) {
                    assert(strcmp(stList_get(tokens, 0), "s") == 0); // Must be an "s" line
                    Alignment_Row *row = st_calloc(1, sizeof(Alignment_Row));
                    alignment->row_number++;
                    *p_row = row;
                    p_row = &(row->n_row);
                    last_row = row;
                    row->sequence_name = stString_copy(stList_get(tokens, 1));
                    row->start = atol(stList_get(tokens, 2));
                    row->length = atol(stList_get(tokens, 3));
                    assert(strcmp(stList_get(tokens, 4), "+") == 0 || strcmp(stList_get(tokens, 4), "-") == 0);
                    row->strand = strcmp(stList_get(tokens, 4), "+") == 0;
                    row->sequence_length = atol(stList_get(tokens, 5));
                    row->bases = stString_copy(stList_get(tokens, 6));
                    stList_destruct(tokens);
                    if(alignment->row_number == 1) {
                        alignment->column_number = strlen(row->bases);
                        alignment->column_tags = st_calloc(alignment->column_number, sizeof(Tag *));
                    }
                    else {
                        assert(alignment->column_number == strlen(row->bases));
                    }
                } else if(strcmp(stList_get(tokens, 0), TAF_BASE_QUALITY_TAG_KEY) == 0) {
                    if (row_qualities == NULL) {
                        row_qualities = stList_construct3(0, free);
                        row_quality_rows = stList_construct();
                    }
                    char *qual_seq_name = stList_get(tokens, 1);
                    if (last_row == NULL || strcmp(qual_seq_name, last_row->sequence_name) != 0) {
                        fprintf(stderr, "Error: q line invalid because sequence name does not match previous s line: %s\n", line);
                        exit(1);
                    }
                    assert(stList_length(tokens) == 3);
                    stList_append(row_qualities, stString_copy(stList_get(tokens, 2)));
                    stList_append(row_quality_rows, (void*)(alignment->row_number - 1));
                    stList_destruct(tokens);
                } else {
                    assert(strcmp(stList_get(tokens, 0), "i") == 0 || strcmp(stList_get(tokens, 0), "e") == 0); // Must be an "i" or "e" line, which we ignore
                    stList_destruct(tokens);
                    continue;
                }
            }
            if (row_qualities != NULL) {
                set_maf_qualities(alignment, row_qualities, row_quality_rows);
            }
            return alignment;
        }
        else {
            assert(strcmp(stList_get(tokens, 0), "s") != 0); // Can not be an s line without a prior a line - we will ignore this line
            stList_destruct(tokens);
        }
    }
}

Tag *parse_header(stList *tokens, char *header_prefix, char *delimiter);

Tag *maf_read_header(LI *li) {
    char *line = LI_get_next_line(li);
    stList *tokens = stString_split(line);
    free(line);
    Tag *tag = parse_header(tokens, "##maf", "=");
    stList_destruct(tokens);

    return tag;
}

void maf_write_block2(Alignment *alignment, LW *lw, bool color_bases) {
    LW_write(lw, "a\n");

    // check for base quality ('q' tag). Just looking at position[0] to not be too inefficient
    // so the assumption is either you have a quality for every base in every column, or nothing
    // will fail on an error if this doesn't hold (but we can relax if needed later)
    bool has_qualities = false;
    unsigned char **decoded_col_qualities = NULL;
    char *qual_buffer = NULL;
    if (alignment->column_number > 0) { 
        for (Tag *col_tag = alignment->column_tags[0]; col_tag && !has_qualities; col_tag = col_tag->n_tag) {
            if (strcmp(col_tag->key, TAF_BASE_QUALITY_TAG_KEY) == 0) {
                has_qualities = true;
            }
        }
        if (has_qualities) {
            decoded_col_qualities = (unsigned char**)st_calloc(alignment->column_number, sizeof(unsigned char*));
            qual_buffer = (char*)st_calloc(alignment->column_number, sizeof(char));
            // if we have qualites, decode them into a matrix for the entire block so they can be transposed
            for (int64_t col = 0; col < alignment->column_number; ++col) {
                for (Tag *col_tag = alignment->column_tags[col]; col_tag && !decoded_col_qualities[col]; col_tag = col_tag->n_tag) {
                    if (strcmp(col_tag->key, TAF_BASE_QUALITY_TAG_KEY) == 0) {
                        decoded_col_qualities[col] = (unsigned char*)st_calloc(alignment->row_number + 1, sizeof(unsigned char));
                        base64_decode(col_tag->value, BASE64_ENCODE_OUT_SIZE(alignment->row_number) - 1, decoded_col_qualities[col]);
                    }
                }
                if (decoded_col_qualities[col] == NULL) {
                    // see comment above
                    fprintf(stderr, "Error: missing base quality at column in block with base qualities\n");
                    exit(1);
                }
            }
        }
    }
    
    Alignment_Row *row = alignment->row;
    int64_t row_idx = 0;
    while(row != NULL) {
        char *bases = color_bases ? color_base_string(row->bases, alignment->column_number) : row->bases;
        LW_write(lw, "s\t%s\t%" PRIi64 "\t%" PRIi64 "\t%s\t%" PRIi64 "\t%s\n", row->sequence_name, row->start, row->length,
                 row->strand ? "+" : "-", row->sequence_length, bases);

        if (has_qualities && row->length > 0) {
            int64_t i = 0;
            for (int64_t col = 0; col < alignment->column_number; ++col) {
                if (row->bases[col] != '-') {
                    // this is a raw quality value
                    unsigned char qual = decoded_col_qualities[col][row_idx];
                    // do the transformation shown here
                    // https://genome.ucsc.edu/FAQ/FAQformat.html#format5
                    // MAF quality value = min( floor(actual quality value/5), 9 )
                    qual_buffer[i++] = (char)(qual >= 99 ? 'F' : (qual >= 45 ? '9' : '0' + qual/5));
                } else {
                    qual_buffer[i++] = '-';
                }
            }
            assert(i == alignment->column_number);
            qual_buffer[i] = '\0';
            LW_write(lw, "q\t%s\t\t\t\t\t%s\n", row->sequence_name, qual_buffer);
        }
        
        row = row->n_row;
        ++row_idx;
        if(color_bases) {
            free(bases);
        }
    }
    LW_write(lw, "\n"); // Add a blank line at the end of the block
    if (has_qualities) {
        free(qual_buffer);
        for (int64_t i = 0; i < alignment->column_number; ++i) {
            free(decoded_col_qualities[i]);
        }
        free(decoded_col_qualities);
    }
}

void maf_write_block(Alignment *alignment, LW *lw) {
    maf_write_block2(alignment, lw, 0);
}

void write_header(Tag *tag, LW *lw, char *header_prefix, char *delimiter, char *end);

void maf_write_header(Tag *tag, LW *lw) {
    write_header(tag, lw, "##maf", "=", "\n\n");
}




