/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999-2003 Ximian, Inc. (www.ximian.com)
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
#include <pthread.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/e-memory.h>

#include "camel-debug.h"
#include "camel-exception.h"

/* dont turn this off */
#define w(x) x


/**
 * camel_exception_new: allocate a new exception object. 
 * 
 * Create and returns a new exception object.
 * 
 * Returns the newly allocated exception object
 **/
CamelException *
camel_exception_new (void)
{
	CamelException *ex;

	ex = g_slice_new (CamelException);
	ex->desc = NULL;

	/* set the Exception Id to NULL */
	ex->id = CAMEL_EXCEPTION_NONE;

	return ex;
}

/**
 * camel_exception_init:
 * @ex: a #CamelException
 * 
 * Init an exception. This routine is mainly useful when using a
 * statically allocated exception.
 **/
void
camel_exception_init (CamelException *ex)
{
	ex->desc = NULL;

	/* set the Exception Id to NULL */
	ex->id = CAMEL_EXCEPTION_NONE;
}


/**
 * camel_exception_clear:
 * @ex: a #CamelException
 * 
 * Clear an exception, that is, set the exception ID to
 * #CAMEL_EXCEPTION_NONE and free the description text.  If the
 * exception is %NULL, this funtion just returns.
 **/
void 
camel_exception_clear (CamelException *exception)
{
	if (!exception)
		return;

	if (exception->desc)
		g_free (exception->desc);
	exception->desc = NULL;
	exception->id = CAMEL_EXCEPTION_NONE;
}

/**
 * camel_exception_free:
 * @ex: a #CamelException
 * 
 * Free an exception object. If the exception is %NULL, nothing is
 * done, the routine simply returns.
 **/
void 
camel_exception_free (CamelException *exception)
{
	if (!exception)
		return;
	
	if (exception->desc)
		g_free (exception->desc);

	g_slice_free (CamelException, exception);
}

/**
 * camel_exception_set: set an exception 
 * @ex: a #CamelException
 * @id: exception id 
 * @desc: textual description of the exception
 * 
 * Set the value of an exception. The exception id is 
 * a unique number representing the exception. The 
 * textual description is a small text explaining 
 * what happened and provoked the exception.
 *
 * When @ex is %NULL, nothing is done, this routine
 * simply returns.
 **/
void
camel_exception_set (CamelException *ex, ExceptionId id, const char *desc)
{
	if (camel_debug("exception"))
		printf("CamelException.set(%p, %u, '%s')\n", (void *) ex, id, desc);
	if (!ex)
		return;
	ex->id = id;
	if (desc != ex->desc) {
		g_free (ex->desc);
		ex->desc = g_strdup (desc);
	}
}

/**
 * camel_exception_setv: set an exception 
 * @ex: a #CamelException
 * @id: exception id 
 * @format: format of the description string. The format string is
 * used as in printf().
 * 
 * Set the value of an exception. The exception id is 
 * a unique number representing the exception. The 
 * textual description is a small text explaining 
 * what happened and provoked the exception. 
 * In this version, the string is created from the format 
 * string and the variable argument list.
 *
 * It is safe to say:
 *   camel_exception_setv (ex, ..., camel_exception_get_description (ex), ...);
 *
 * When @ex is %NULL, nothing is done, this routine
 * simply returns.
 **/
void
camel_exception_setv (CamelException *ex, ExceptionId id, const char *format, ...)
{
	va_list args;
	char *desc;

	va_start(args, format);
	desc = g_strdup_vprintf (format, args);
	va_end (args);

	if (camel_debug("exception"))
		printf("CamelException.setv(%p, %u, '%s')\n", (void *) ex, id, desc);

	if (!ex) {
		g_free(desc);
		return;
	}

	g_free(ex->desc);
	ex->desc = desc;
	ex->id = id;
}

/**
 * camel_exception_xfer:
 * @ex_dst: Destination exception object 
 * @ex_src: Source exception object
 * 
 * Transfer the content of an exception from an exception object to
 * another.  The destination exception receives the id and the
 * description text of the source exception.
 **/
void 
camel_exception_xfer (CamelException *ex_dst,
		      CamelException *ex_src)
{
	if (ex_src == NULL) {
		w(g_warning ("camel_exception_xfer: trying to transfer NULL exception to %p\n", ex_dst));
		return;
	}

	if (ex_dst == NULL) {
		/* must have same side-effects */
		camel_exception_clear (ex_src);
		return;
	}

	if (ex_dst->desc)
		g_free (ex_dst->desc);

	ex_dst->id = ex_src->id;
	ex_dst->desc = ex_src->desc;

	ex_src->desc = NULL;
	ex_src->id = CAMEL_EXCEPTION_NONE;
}

/**
 * camel_exception_get_id:
 * @ex: a #CamelException
 * 
 * Get the id of an exception.
 * 
 * Returns the exception id (#CAMEL_EXCEPTION_NONE will be returned if
 * @ex is %NULL or unset)
 **/
ExceptionId
camel_exception_get_id (CamelException *ex)
{
	if (ex)
		return ex->id;
	
	w(g_warning ("camel_exception_get_id called with NULL parameter."));
	
	return CAMEL_EXCEPTION_NONE;
}

/**
 * camel_exception_get_description:
 * @ex: a #CamelException
 * 
 * Get the exception description text.
 * 
 * Returns the exception description text (%NULL will be returned if
 * @ex is %NULL or unset)
 **/
const gchar *
camel_exception_get_description (CamelException *ex)
{
	char *ret = NULL;

	if (ex)
		ret = ex->desc;
	else
		w(g_warning ("camel_exception_get_description called with NULL parameter."));

	return ret ? ret : (_("No description available"));
}
