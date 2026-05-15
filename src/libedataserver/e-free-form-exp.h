/*
 * SPDX-FileCopyrightText: (C) 2014 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_FREE_FORM_EXP_H
#define E_FREE_FORM_EXP_H

#include <glib.h>

G_BEGIN_DECLS

typedef gchar * (*EFreeFormExpBuildSexpFunc)	(const gchar *word,
						 const gchar *options,
						 const gchar *hint);

typedef struct _EFreeFormExpSymbol {
	const gchar *names; /* names (alternative separated by a colon (':')); use an empty string for a default sexp builder */
	const gchar *hint; /* passed into build_sexp */
	EFreeFormExpBuildSexpFunc build_sexp;
} EFreeFormExpSymbol;

gchar *		e_free_form_exp_to_sexp		(const gchar *free_form_exp,
						 const EFreeFormExpSymbol *symbols);

G_END_DECLS

#endif /* E_FREE_FORM_EXP_H */
