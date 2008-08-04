/* e-book-backend-webdav.h - Webdav contact backend.
 *
 * Copyright (C) 2008 Matthias Braun <matze@braunis.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Matthias Braun <matze@braunis.de>
 */
#ifndef __E_BOOK_BACKEND_WEBDAV_H__
#define __E_BOOK_BACKEND_WEBDAV_H__

#include <libedata-book/e-book-backend.h>

#define E_TYPE_BOOK_BACKEND_WEBDAV         (e_book_backend_webdav_get_type ())
#define E_BOOK_BACKEND_WEBDAV(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_WEBDAV, EBookBackendWebdav))
#define E_BOOK_BACKEND_WEBDAV_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_WEBDAV, EBookBackendWebdavClass))
#define E_IS_BOOK_BACKEND_WEBDAV(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_WEBDAV))
#define E_IS_BOOK_BACKEND_WEBDAV_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_WEBDAV))
#define E_BOOK_BACKEND_WEBDAV_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_WEBDAV, EBookBackendWebdavClass))

typedef struct _EBookBackendWebdavPrivate EBookBackendWebdavPrivate;

typedef struct {
    EBookBackend               parent_object;
    EBookBackendWebdavPrivate *priv;
} EBookBackendWebdav;

typedef struct {
    EBookBackendClass parent_class;
} EBookBackendWebdavClass;

EBookBackend *e_book_backend_webdav_new      (void);
GType         e_book_backend_webdav_get_type (void);

#endif

