/*
 * Copyright (C) 2013 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "cursor-navigator.h"

/* GObjectClass */
static void            cursor_navigator_constructed     (GObject              *object);
static void            cursor_navigator_finalize        (GObject              *object);

/* GtkScaleClass */
static gchar          *cursor_navigator_format_value    (GtkScale             *scale,
							 gdouble               value);

static void            cursor_navigator_changed         (GtkAdjustment        *adj,
							 GParamSpec           *pspec,
							 CursorNavigator      *navigator);

struct _CursorNavigatorPrivate {
	gchar **alphabet;
	gint    letters;
	gint    index;
};

enum {
	INDEX_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

enum {
	PROP_0,
	PROP_INDEX,
};

G_DEFINE_TYPE_WITH_PRIVATE (CursorNavigator, cursor_navigator, GTK_TYPE_SCALE);

/************************************************************************
 *                          GObjectClass                                *
 ************************************************************************/
static void
cursor_navigator_class_init (CursorNavigatorClass *klass)
{
	GObjectClass       *object_class;
	GtkScaleClass      *scale_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = cursor_navigator_constructed;
	object_class->finalize = cursor_navigator_finalize;

	scale_class = GTK_SCALE_CLASS (klass);
	scale_class->format_value = cursor_navigator_format_value;

	signals[INDEX_CHANGED] = g_signal_new (
		"index-changed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0);
}

static void
cursor_navigator_init (CursorNavigator *navigator)
{
	CursorNavigatorPrivate *priv;

	navigator->priv = priv = cursor_navigator_get_instance_private (navigator);

	priv->letters = -1;
}

static void
cursor_navigator_constructed (GObject *object)
{
	CursorNavigator        *navigator = CURSOR_NAVIGATOR (object);
	GtkAdjustment          *adj = NULL;

	G_OBJECT_CLASS (cursor_navigator_parent_class)->constructed (object);

	adj = gtk_adjustment_new (0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F);
	gtk_range_set_adjustment (GTK_RANGE (navigator), adj);

	g_signal_connect (
		adj, "notify::value",
		G_CALLBACK (cursor_navigator_changed), navigator);
}

static void
cursor_navigator_finalize (GObject *object)
{
	CursorNavigator        *navigator = CURSOR_NAVIGATOR (object);
	CursorNavigatorPrivate *priv = navigator->priv;

	g_strfreev (priv->alphabet);

	G_OBJECT_CLASS (cursor_navigator_parent_class)->finalize (object);
}

/************************************************************************
 *                          GtkScaleClass                               *
 ************************************************************************/
static gchar *
cursor_navigator_format_value (GtkScale *scale,
                               gdouble value)
{
	CursorNavigator        *navigator = CURSOR_NAVIGATOR (scale);
	CursorNavigatorPrivate *priv = navigator->priv;
	gint                    index;

	if (priv->letters < 0)
		return NULL;

	index = CLAMP ((gint) value, 0, priv->letters - 1);

	/* Return the letter for the gvoidiven value
	 */
	return g_strdup (priv->alphabet[index]);
}

static void
cursor_navigator_changed (GtkAdjustment *adj,
                          GParamSpec *pspec,
                          CursorNavigator *navigator)
{
	gint index = gtk_adjustment_get_value (adj);

	cursor_navigator_set_index (navigator, index);
}

/************************************************************************
 *                                API                                   *
 ************************************************************************/
CursorNavigator *
cursor_navigator_new (void)
{
	return g_object_new (CURSOR_TYPE_NAVIGATOR, NULL);
}

static void
cursor_navigator_update_parameters (CursorNavigator *navigator)
{
	CursorNavigatorPrivate *priv = navigator->priv;
	GtkScale               *scale = GTK_SCALE (navigator);
	GtkAdjustment          *adj;
	gint                    i;

	gtk_scale_clear_marks (scale);
	for (i = 0; i < priv->letters; i++) {
		gchar *letter;

		letter = g_strdup_printf ("<span size=\"x-small\">%s</span>", priv->alphabet[i]);

		gtk_scale_add_mark (scale, i, GTK_POS_LEFT, letter);
		g_free (letter);
	}

	adj = gtk_range_get_adjustment (GTK_RANGE (navigator));

	gtk_adjustment_set_upper (adj, priv->letters - 1);
}

void
cursor_navigator_set_alphabet (CursorNavigator *navigator,
                               const gchar * const *alphabet)
{
	CursorNavigatorPrivate *priv;

	g_return_if_fail (CURSOR_IS_NAVIGATOR (navigator));
	g_return_if_fail (alphabet == NULL ||
			  g_strv_length ((gchar **) alphabet) > 0);

	priv = navigator->priv;

	g_free (priv->alphabet);
	if (alphabet) {
		priv->alphabet = g_strdupv ((gchar **) alphabet);
		priv->letters = g_strv_length ((gchar **) alphabet);
	} else {
		priv->alphabet = NULL;
		priv->letters = -1;
	}

	cursor_navigator_update_parameters (navigator);
	cursor_navigator_set_index (navigator, 0);
}

const gchar * const *
cursor_navigator_get_alphabet (CursorNavigator *navigator)
{
	CursorNavigatorPrivate *priv;

	g_return_val_if_fail (CURSOR_IS_NAVIGATOR (navigator), NULL);

	priv = navigator->priv;

	return (const gchar * const *) priv->alphabet;
}

void
cursor_navigator_set_index (CursorNavigator *navigator,
                            gint index)
{
	CursorNavigatorPrivate *priv;
	GtkAdjustment          *adj;

	g_return_if_fail (CURSOR_IS_NAVIGATOR (navigator));

	priv = navigator->priv;
	adj = gtk_range_get_adjustment (GTK_RANGE (navigator));

	index = CLAMP (index, 0, priv->letters);

	if (priv->index != index) {

		priv->index = index;

		g_signal_emit (navigator, signals[INDEX_CHANGED], 0);

		g_signal_handlers_block_by_func (adj, cursor_navigator_changed, navigator);
		gtk_adjustment_set_value (adj, priv->index);
		g_signal_handlers_unblock_by_func (adj, cursor_navigator_changed, navigator);
	}
}

gint
cursor_navigator_get_index (CursorNavigator *navigator)
{
	CursorNavigatorPrivate *priv;

	g_return_val_if_fail (CURSOR_IS_NAVIGATOR (navigator), 0);

	priv = navigator->priv;

	return priv->index;
}
