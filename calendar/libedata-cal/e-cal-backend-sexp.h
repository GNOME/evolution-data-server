/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * cal-backend-card-sexp.h
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __E_CAL_BACKEND_SEXP_H__
#define __E_CAL_BACKEND_SEXP_H__

#include <glib.h>
#include <glib-object.h>
#include <libecal/e-cal-component.h>
#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_SEXP        (e_cal_backend_sexp_get_type ())
#define E_CAL_BACKEND_SEXP(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_BACKEND_SEXP, ECalBackendSExp))
#define E_CAL_BACKEND_SEXP_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_CAL_BACKEND_TYPE, ECalBackendSExpClass))
#define E_IS_CAL_BACKEND_SEXP(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_BACKEND_SEXP))
#define E_IS_CAL_BACKEND_SEXP_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_BACKEND_SEXP))
#define E_CAL_BACKEND_SEXP_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_BACKEND_SEXP, CALBackendSExpClass))

typedef struct _ECalBackendSExpPrivate ECalBackendSExpPrivate;

struct _ECalBackendSExp {
	GObject parent_object;

	ECalBackendSExpPrivate *priv;
};

struct _ECalBackendSExpClass {
	GObjectClass parent_class;
};

GType                 e_cal_backend_sexp_get_type     (void);
ECalBackendSExp *e_cal_backend_sexp_new          (const char           *text);
const char *e_cal_backend_sexp_text (ECalBackendSExp *sexp);


gboolean              e_cal_backend_sexp_match_object (ECalBackendSExp *sexp,
							    const char           *object,
							    ECalBackend           *backend);
gboolean              e_cal_backend_sexp_match_comp   (ECalBackendSExp *sexp,
							    ECalComponent         *comp,
							    ECalBackend           *backend);

G_END_DECLS

#endif /* __E_CAL_BACKEND_SEXP_H__ */
