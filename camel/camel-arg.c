/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 *
 * Author:
 *  Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include "camel-arg.h"

gint camel_argv_build(CamelArgV *tv)
{
	register guint32 tag;
	register gint i;
	register CamelArg *a;
	gint more = TRUE;

	for (i=0;i<CAMEL_ARGV_MAX;i++) {
		a = &tv->argv[i];

		if ( (tag = va_arg(tv->ap, guint32)) == 0) {
			more = FALSE;
			break;
		}

		a->tag = tag;

		switch ((tag & CAMEL_ARG_TYPE)) {
		case CAMEL_ARG_OBJ:
			a->ca_object = va_arg(tv->ap, gpointer );
			break;
		case CAMEL_ARG_INT:
			a->ca_int = va_arg(tv->ap, gint);
			break;
		case CAMEL_ARG_DBL:
			a->ca_double = va_arg(tv->ap, double);
			break;
		case CAMEL_ARG_STR:
			a->ca_str = va_arg(tv->ap, gchar *);
			break;
		case CAMEL_ARG_PTR:
			a->ca_ptr = va_arg(tv->ap, gpointer );
			break;
		case CAMEL_ARG_BOO:
			a->ca_int = va_arg(tv->ap, gint) != 0;
			break;
		default:
			printf("Error, unknown type, truncating result\n");
			more = FALSE;
			goto fail;
		}

	}
fail:
	tv->argc = i;

	return more;
}

gint camel_arggetv_build(CamelArgGetV *tv)
{
	register guint32 tag;
	register gint i;
	register CamelArgGet *a;
	gint more = TRUE;

	for (i=0;i<CAMEL_ARGV_MAX;i++) {
		a = &tv->argv[i];

		if ( (tag = va_arg(tv->ap, guint32)) == 0) {
			more = FALSE;
			break;
		}

		a->tag = tag;

		switch ((tag & CAMEL_ARG_TYPE)) {
		case CAMEL_ARG_OBJ:
			a->ca_object = va_arg(tv->ap, gpointer *);
			*a->ca_object = NULL;
			break;
		case CAMEL_ARG_INT:
		case CAMEL_ARG_BOO:
			a->ca_int = va_arg(tv->ap, gint *);
			*a->ca_int = 0;
			break;
		case CAMEL_ARG_DBL:
			a->ca_double = va_arg(tv->ap, gdouble *);
			*a->ca_double = 0.0;
			break;
		case CAMEL_ARG_STR:
			a->ca_str = va_arg(tv->ap, gchar **);
			*a->ca_str = NULL;
			break;
		case CAMEL_ARG_PTR:
			a->ca_ptr = va_arg(tv->ap, gpointer *);
			*a->ca_ptr = NULL;
			break;
		default:
			printf("Error, unknown type, truncating result\n");
			more = FALSE;
			goto fail;
		}

	}
fail:
	tv->argc = i;

	return more;
}

