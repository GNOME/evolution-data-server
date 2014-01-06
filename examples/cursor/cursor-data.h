/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */
#ifndef CURSOR_DATA_H
#define CURSOR_DATA_H

#include <libebook/libebook.h>

EBookClient *cursor_load_data (const gchar        *vcard_path,
			       EBookClientCursor **ret_cursor);

#endif /* CURSOR_DATA_H */
