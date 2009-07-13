/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * cal-backend-card-sexp.h
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __E_CAL_BACKEND_SEXP_H__
#define __E_CAL_BACKEND_SEXP_H__

#include <glib.h>
#include <glib-object.h>
#include <libecal/e-cal-component.h>
#include <libedata-cal/e-cal-backend.h>
#include "libedataserver/e-sexp.h"

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

GType            e_cal_backend_sexp_get_type     (void);

ECalBackendSExp *e_cal_backend_sexp_new          (const gchar      *text);
const gchar      *e_cal_backend_sexp_text         (ECalBackendSExp *sexp);

gboolean         e_cal_backend_sexp_match_object (ECalBackendSExp *sexp,
						  const gchar      *object,
						  ECalBackend     *backend);
gboolean         e_cal_backend_sexp_match_comp   (ECalBackendSExp *sexp,
						  ECalComponent   *comp,
						  ECalBackend     *backend);

/* Default implementations of time functions for use by subclasses */

ESExpResult *e_cal_backend_sexp_func_time_now       (ESExp *esexp, gint argc, ESExpResult **argv, gpointer data);
ESExpResult *e_cal_backend_sexp_func_make_time      (ESExp *esexp, gint argc, ESExpResult **argv, gpointer data);
ESExpResult *e_cal_backend_sexp_func_time_add_day   (ESExp *esexp, gint argc, ESExpResult **argv, gpointer data);
ESExpResult *e_cal_backend_sexp_func_time_day_begin (ESExp *esexp, gint argc, ESExpResult **argv, gpointer data);
ESExpResult *e_cal_backend_sexp_func_time_day_end   (ESExp *esexp, gint argc, ESExpResult **argv, gpointer data);

G_END_DECLS

#endif /* __E_CAL_BACKEND_SEXP_H__ */
