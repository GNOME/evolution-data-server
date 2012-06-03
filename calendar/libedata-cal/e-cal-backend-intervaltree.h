/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Stanislav Slusny <slusnys@gmail.com>
 *
 *  Copyright 2008
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_INTERVALTREE_H
#define E_INTERVALTREE_H

#include <libecal/libecal.h>

#define E_INTERVALTREE_DEBUG 1

#define E_TYPE_INTERVALTREE        (e_intervaltree_get_type ())
#define E_INTERVALTREE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_INTERVALTREE, EIntervalTree))
#define E_INTERVALTREE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_INTERVALTREE, EIntervalTreeClass))
#define E_IS_INTERVALTREE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_INTERVALTREE))
#define E_IS_INTERVALTREE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_INTERVALTREE))
#define E_INTERVALTREE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_INTERVALTREE, EIntervalTreeClass))

G_BEGIN_DECLS

/* #undef E_INTERVALTREE_DEBUG */
/*
 * Implementation of the interval node as described in Introduction to Algorithms
 * book by Cormen et al, chapter 14.3.
 *
 * Basically, the interval tree is the red-black tree, the node key is the start
 * of the interval.
 */

typedef struct _EIntervalTree EIntervalTree;
typedef struct _EIntervalTreeClass EIntervalTreeClass;
typedef struct _EIntervalTreePrivate EIntervalTreePrivate;

/**
 * EIntervalTree:
 *
 * Since: 2.32
 **/
struct _EIntervalTree
{
	GObject parent;
	EIntervalTreePrivate *priv;
};

struct _EIntervalTreeClass
{
	GObjectClass parent;
};

GType		e_intervaltree_get_type	(void);

EIntervalTree * e_intervaltree_new (void);

gboolean e_intervaltree_insert (EIntervalTree *tree, time_t start, time_t end, ECalComponent *comp);

gboolean e_intervaltree_remove (EIntervalTree *tree, const gchar *uid, const gchar *rid);

void e_intervaltree_destroy (EIntervalTree *tree);

GList *
e_intervaltree_search (EIntervalTree *tree, time_t start, time_t end);
#ifdef E_INTERVALTREE_DEBUG
void e_intervaltree_dump (EIntervalTree *tree);
#endif

G_END_DECLS

#endif /* E_INTERVALTREE_H */
