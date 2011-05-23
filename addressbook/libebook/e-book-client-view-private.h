/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifndef E_BOOK_CLIENT_VIEW_PRIVATE_H
#define E_BOOK_CLIENT_VIEW_PRIVATE_H

#include "e-book-client.h"
#include "e-book-view.h"

struct _EGdbusBookView;

EBookClientView *_e_book_client_view_new (EBookClient *book_client, struct _EGdbusBookView *gdbus_bookview);

G_END_DECLS

#endif /* E_BOOK_CLIENT_VIEW_PRIVATE_H */
