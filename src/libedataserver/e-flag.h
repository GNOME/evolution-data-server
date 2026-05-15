/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_FLAG_H
#define E_FLAG_H

/* An EFlag is essentially a binary semaphore with a more intuitive interface.
 * Based on Python's threading.Event class ("EEvent" was already taken). */

#include <glib.h>

G_BEGIN_DECLS

/**
 * EFlag:
 * Since: 1.12
 **/
typedef struct _EFlag EFlag;

EFlag *		e_flag_new			(void);
gboolean	e_flag_is_set			(EFlag *flag);
void		e_flag_set			(EFlag *flag);
void		e_flag_clear			(EFlag *flag);
void		e_flag_wait			(EFlag *flag);
gboolean	e_flag_wait_until		(EFlag *flag,
						 gint64 end_time);
void		e_flag_free			(EFlag *flag);

#ifndef EDS_DISABLE_DEPRECATED
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
gboolean	e_flag_timed_wait		(EFlag *flag,
						 GTimeVal *abs_time);
G_GNUC_END_IGNORE_DEPRECATIONS
#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_FLAG_H */
