/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include "camel-mime-filter-preview.h"

struct _CamelMimeFilterPreviewPrivate {
	GString *text;
	gboolean last_was_space;
	guint n_stored_chars;
	guint limit;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelMimeFilterPreview, camel_mime_filter_preview, CAMEL_TYPE_MIME_FILTER)

static void
mime_filter_preview_run (CamelMimeFilter *mime_filter,
			 const gchar *in,
			 gsize inlen,
			 gsize prespace,
			 gchar **out,
			 gsize *outlenptr,
			 gsize *outprespace,
			 gboolean is_last)
{
	CamelMimeFilterPreview *self = CAMEL_MIME_FILTER_PREVIEW (mime_filter);
	const gchar *ptr, *end;

	ptr = in;
	end = in + inlen;

	while (ptr && ptr < end) {
		gunichar chr;

		chr = g_utf8_get_char_validated (ptr, end - ptr);

		if (chr != ((gunichar) -1) && chr != ((gunichar) -2)) {
			if (g_unichar_isspace (chr)) {
				if (!self->priv->last_was_space && self->priv->text->len > 0) {
					g_string_append_c (self->priv->text, ' ');
					self->priv->n_stored_chars++;
				}

				self->priv->last_was_space = TRUE;
			} else {
				self->priv->last_was_space = FALSE;
				g_string_append_unichar (self->priv->text, chr);
				self->priv->n_stored_chars++;
			}

			if (self->priv->limit > 0 &&
			    self->priv->n_stored_chars >= self->priv->limit) {
				camel_mime_filter_set_request_stop (mime_filter, TRUE);
				break;
			}
		}

		ptr = g_utf8_find_next_char (ptr, end);
	}

	*out = (gchar *) in;
	*outlenptr = inlen;
	*outprespace = prespace;
}

static void
mime_filter_preview_filter (CamelMimeFilter *mime_filter,
			    const gchar *in,
			    gsize len,
			    gsize prespace,
			    gchar **out,
			    gsize *outlenptr,
			    gsize *outprespace)
{
	mime_filter_preview_run (
		mime_filter, in, len, prespace,
		out, outlenptr, outprespace, FALSE);
}

static void
mime_filter_preview_complete (CamelMimeFilter *mime_filter,
			      const gchar *in,
			      gsize len,
			      gsize prespace,
			      gchar **out,
			      gsize *outlenptr,
			      gsize *outprespace)
{
	mime_filter_preview_run (
		mime_filter, in, len, prespace,
		out, outlenptr, outprespace, TRUE);
}

static void
mime_filter_preview_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterPreview *self = CAMEL_MIME_FILTER_PREVIEW (mime_filter);

	g_string_truncate (self->priv->text, 0);
	self->priv->last_was_space = FALSE;
	self->priv->n_stored_chars = 0;
}

static void
mime_filter_preview_finalize (GObject *object)
{
	CamelMimeFilterPreview *self = CAMEL_MIME_FILTER_PREVIEW (object);

	g_string_free (self->priv->text, TRUE);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (camel_mime_filter_preview_parent_class)->finalize (object);
}

static void
camel_mime_filter_preview_class_init (CamelMimeFilterPreviewClass *klass)
{
	GObjectClass *object_class;
	CamelMimeFilterClass *mime_filter_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = mime_filter_preview_finalize;

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (klass);
	mime_filter_class->filter = mime_filter_preview_filter;
	mime_filter_class->complete = mime_filter_preview_complete;
	mime_filter_class->reset = mime_filter_preview_reset;
}

static void
camel_mime_filter_preview_init (CamelMimeFilterPreview *self)
{
	self->priv = camel_mime_filter_preview_get_instance_private (self);
	self->priv->text = g_string_new ("");
}

/**
 * camel_mime_filter_preview_new:
 * @limit: a limit for the preview length
 *
 * Creates a new #CamelMimeFilterPreview object. It filters passed-in
 * data into a text suitable for a message content preview.
 *
 * Returns: (transfer full): a new #CamelMimeFilterPreview object
 *
 * Since: 3.52
 **/
CamelMimeFilter *
camel_mime_filter_preview_new (guint limit)
{
	CamelMimeFilterPreview *self;

	self = g_object_new (CAMEL_TYPE_MIME_FILTER_PREVIEW, NULL);

	camel_mime_filter_preview_set_limit (self, limit);

	return CAMEL_MIME_FILTER (self);
}

/**
 * camel_mime_filter_preview_get_limit:
 * @self: a #CamelMimeFilterPreview
 *
 * Returns set limit for the text length, in characters.
 * Zero means unlimited length.
 *
 * Returns: limit for the text length, in characters
 *
 * Since: 3.52
 **/
guint
camel_mime_filter_preview_get_limit (CamelMimeFilterPreview *self)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_PREVIEW (self), 0);

	return self->priv->limit;
}

/**
 * camel_mime_filter_preview_set_limit:
 * @self: a #CamelMimeFilterPreview
 * @limit: a limit to set
 *
 * Sets limit for the text length, in characters. Zero
 * means unlimited length.
 *
 * Since: 3.52
 **/
void
camel_mime_filter_preview_set_limit (CamelMimeFilterPreview *self,
				     guint limit)
{
	g_return_if_fail (CAMEL_IS_MIME_FILTER_PREVIEW (self));

	self->priv->limit = limit;
}

/**
 * camel_mime_filter_preview_get_text:
 * @self: a #CamelMimeFilterPreview
 *
 * Returns read text until now.
 *
 * Returns: (nullable): read text until now or %NULL, when nothing was read
 *
 * Since: 3.52
 **/
const gchar *
camel_mime_filter_preview_get_text (CamelMimeFilterPreview *self)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_PREVIEW (self), NULL);

	if (!self->priv->text->len)
		return NULL;

	return self->priv->text->str;
}
