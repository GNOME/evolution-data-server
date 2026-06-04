/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <glib/gstdio.h>
#include <camel/camel.h>

void camel_test_init (void);
void camel_test_shutdown (void);

const gchar *camel_test_get_dir (void);

/* utility functions */
/* compare strings, ignore whitespace though */
gint string_equal (const gchar *a, const gchar *b);
