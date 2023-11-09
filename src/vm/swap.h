#pragma once
#include "page.h"
#include <stdbool.h>
#include <stddef.h>

void swap_init (void);
void swap_in (Page *);
bool swap_out (Page *);
void swap_free (size_t);
