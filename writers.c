#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "readers.h"
#include "writers.h"

/* ----------------------------------------------------------------------- */

LIST_HEAD(writers);

void write_register(struct ida_writer *writer)
{
    list_add_tail(&writer->list, &writers);
}
