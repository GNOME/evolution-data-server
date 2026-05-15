/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CURSOR_DATA_H
#define CURSOR_DATA_H

#include <libebook/libebook.h>

EBookClient *cursor_load_data (const gchar        *vcard_path,
			       EBookClientCursor **ret_cursor);

#endif /* CURSOR_DATA_H */
