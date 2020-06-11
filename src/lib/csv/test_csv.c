#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "csv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
        struct csv csv;
        csv_create(&csv);
        char *buf = calloc(size, sizeof(char*));
        if (buf == NULL)
                return -1;
        memcpy(buf, data, size);
        buf[size] = '\0';
        char *end = buf + size;
        csv_parse_chunk(&csv, buf, end);
        csv_finish_parsing(&csv);
        int rc = csv_get_error_status(&csv) == CSV_ER_INVALID ? 1 : 0;
        csv_destroy(&csv);
        free(buf);

        return rc;
}
