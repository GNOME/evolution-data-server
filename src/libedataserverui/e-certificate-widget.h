/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CERTIFICATE_WIDGET_H
#define E_CERTIFICATE_WIDGET_H

#include <gtk/gtk.h>

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#if GTK_CHECK_VERSION(4, 0, 0)
#error "Only <libedataserverui4/libedataserverui4.h> should be included directly."
#else
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif
#endif

/* Standard GObject macros */
#define E_TYPE_CERTIFICATE_WIDGET \
	(e_certificate_widget_get_type ())
#define E_CERTIFICATE_WIDGET(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CERTIFICATE_WIDGET, ECertificateWidget))
#define E_CERTIFICATE_WIDGET_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CERTIFICATE_WIDGET, ECertificateWidgetClass))
#define E_IS_CERTIFICATE_WIDGET(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CERTIFICATE_WIDGET))
#define E_IS_CERTIFICATE_WIDGET_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE ((cls), E_TYPE_CERTIFICATE_WIDGET))
#define E_CERTIFICATE_WIDGET_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CERTIFICATE_WIDGET, ECertificateWidgetClass))

G_BEGIN_DECLS

typedef struct _ECertificateWidget ECertificateWidget;
typedef struct _ECertificateWidgetClass ECertificateWidgetClass;
typedef struct _ECertificateWidgetPrivate ECertificateWidgetPrivate;

/**
 * ECertificateWidget:
 *
 * Since: 3.46
 **/
struct _ECertificateWidget {
#if GTK_CHECK_VERSION(4, 0, 0)
	GtkBox parent;
#else
	GtkScrolledWindow parent;
#endif
	ECertificateWidgetPrivate *priv;
};

struct _ECertificateWidgetClass {
#if GTK_CHECK_VERSION(4, 0, 0)
	GtkBoxClass parent_class;
#else
	GtkScrolledWindowClass parent_class;
#endif
};

GType		e_certificate_widget_get_type	(void) G_GNUC_CONST;
GtkWidget *	e_certificate_widget_new	(void);
void		e_certificate_widget_set_der	(ECertificateWidget *self,
						 gconstpointer der_data,
						 guint der_data_len);
void		e_certificate_widget_set_pem	(ECertificateWidget *self,
						 const gchar *pem_data);

G_END_DECLS

#endif /* E_CERTIFICATE_WIDGET_H */
