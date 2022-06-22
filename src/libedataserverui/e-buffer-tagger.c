/*
 * e-buffer-tagger.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "evolution-data-server-config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n-lib.h>
#include <regex.h>
#include <string.h>
#include <ctype.h>

#include "e-buffer-tagger.h"

static void
e_show_uri (GtkWindow *parent,
	    const gchar *uri)
{
#if !GTK_CHECK_VERSION(4, 0, 0)
	GtkWidget *dialog;
	GdkScreen *screen = NULL;
	GError *error = NULL;
	gboolean success;
#endif
	gchar *scheme, *schemed_uri = NULL;
	guint32 timestamp;

	g_return_if_fail (uri != NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
	timestamp = GDK_CURRENT_TIME;
#else
	timestamp = gtk_get_current_event_time ();

	if (parent != NULL)
		screen = gtk_widget_get_screen (GTK_WIDGET (parent));
#endif

	scheme = g_uri_parse_scheme (uri);

	if (!scheme || !*scheme) {
		schemed_uri = g_strconcat ("http://", uri, NULL);
		uri = schemed_uri;
	}
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_show_uri (parent, uri, timestamp);
#else
	success = gtk_show_uri (screen, uri, timestamp, &error);
#endif

	g_free (schemed_uri);
	g_free (scheme);

#if !GTK_CHECK_VERSION(4, 0, 0)
	if (success)
		return;

	dialog = gtk_message_dialog_new_with_markup (
		parent, GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
		"<big><b>%s</b></big>",
		_("Could not open the link."));

	gtk_message_dialog_format_secondary_text (
		GTK_MESSAGE_DIALOG (dialog), "%s", error->message);

	gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);
	g_error_free (error);
#endif
}

enum EBufferTaggerState {
	E_BUFFER_TAGGER_STATE_NONE = 0,
	E_BUFFER_TAGGER_STATE_INSDEL = 1 << 0, /* set when was called insert or delete of a text */
	E_BUFFER_TAGGER_STATE_CHANGED = 1 << 1, /* remark of the buffer is scheduled */
	E_BUFFER_TAGGER_STATE_IS_HOVERING = 1 << 2, /* mouse is over the link */
	E_BUFFER_TAGGER_STATE_IS_HOVERING_TOOLTIP = 1 << 3, /* mouse is over the link and the tooltip can be shown */
	E_BUFFER_TAGGER_STATE_CTRL_DOWN = 1 << 4  /* Ctrl key is down */
};

#define E_BUFFER_TAGGER_DATA_STATE		"EBufferTagger::state"
#define E_BUFFER_TAGGER_DATA_KEY_CONTROLLER	"EBufferTagger::key-controller"
#define E_BUFFER_TAGGER_DATA_LEGACY_CONTROLLER	"EBufferTagger::legacy-controller"
#define E_BUFFER_TAGGER_DATA_MOTION_CONTROLLER	"EBufferTagger::motion-controller"
#define E_BUFFER_TAGGER_DATA_CURRENT_URI	"EBufferTagger::current-uri"
#define E_BUFFER_TAGGER_LINK_TAG   		"EBufferTagger::link"

struct _MagicInsertMatch {
	const gchar *regex;
	regex_t *preg;
	const gchar *prefix;
};

typedef struct _MagicInsertMatch MagicInsertMatch;

static MagicInsertMatch mim[] = {
	/* prefixed expressions */
	{ "(news|telnet|nntp|file|http|ftp|sftp|https|webcal)://([-a-z0-9]+(:[-a-z0-9]+)?@)?[-a-z0-9.]+[-a-z0-9](:[0-9]*)?(([.])?/[-a-z0-9_$.+!*(),;:@%&=?/~#']*[^]'.}>\\) \n\r\t,?!;:\"]?)?", NULL, NULL },
	{ "(sip|h323|callto|tel):([-_a-z0-9.'\\+]+(:[0-9]{1,5})?(/[-_a-z0-9.']+)?)(@([-_a-z0-9.%=?]+|([0-9]{1,3}.){3}[0-9]{1,3})?)?(:[0-9]{1,5})?", NULL, NULL },
	{ "mailto:[-_a-z0-9.'\\+]+@[-_a-z0-9.%=?]+", NULL, NULL },
	/* not prefixed expression */
	{ "www\\.[-a-z0-9.]+[-a-z0-9](:[0-9]*)?(([.])?/[-A-Za-z0-9_$.+!*(),;:@%&=?/~#]*[^]'.}>\\) \n\r\t,?!;:\"]?)?", NULL, "http://" },
	{ "ftp\\.[-a-z0-9.]+[-a-z0-9](:[0-9]*)?(([.])?/[-A-Za-z0-9_$.+!*(),;:@%&=?/~#]*[^]'.}>\\) \n\r\t,?!;:\"]?)?", NULL, "ftp://" },
	{ "[-_a-z0-9.'\\+]+@[-_a-z0-9.%=?]+", NULL, "mailto:" }
};

static void
init_magic_links (void)
{
	static gboolean done = FALSE;
	gint i;

	if (done)
		return;

	done = TRUE;

	for (i = 0; i < G_N_ELEMENTS (mim); i++) {
		mim[i].preg = g_new0 (regex_t, 1);
		if (regcomp (mim[i].preg, mim[i].regex, REG_EXTENDED | REG_ICASE)) {
			/* error */
			g_free (mim[i].preg);
			mim[i].preg = 0;
		}
	}
}

static void
markup_text (GtkTextBuffer *buffer)
{
	GtkTextIter start, end;
	gchar *text;
	gint i;
	regmatch_t pmatch[2];
	gboolean any;
	const gchar *str;
	gint offset = 0;

	g_return_if_fail (buffer != NULL);

	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	gtk_text_buffer_remove_tag_by_name (buffer, E_BUFFER_TAGGER_LINK_TAG, &start, &end);
	text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	str = text;
	any = TRUE;
	while (any) {
		any = FALSE;
		for (i = 0; i < G_N_ELEMENTS (mim); i++) {
			if (mim[i].preg && !regexec (mim[i].preg, str, 2, pmatch, 0)) {
				gint char_so, char_eo, rm_eo;

				/* Stop on the angle brackets, which cannot be part of the URL (see RFC 3986 Appendix C) */
				for (rm_eo = pmatch[0].rm_eo - 1; rm_eo > pmatch[0].rm_so; rm_eo--) {
					if (str[rm_eo] == '<' || str[rm_eo] == '>') {
						pmatch[0].rm_eo = rm_eo;
						break;
					}
				}

				rm_eo = pmatch[0].rm_eo;

				/* URLs are extremely unlikely to end with any
				 * punctuation, so strip any trailing
				 * punctuation off. Also strip off any closing
				 * double-quotes. */
				while (rm_eo > pmatch[0].rm_so && strchr (",.:;?!-|}])\">", str[rm_eo - 1])) {
					gchar open_bracket = 0, close_bracket = str[rm_eo - 1];

					if (close_bracket == ')')
						open_bracket = '(';
					else if (close_bracket == '}')
						open_bracket = '{';
					else if (close_bracket == ']')
						open_bracket = '[';
					else if (close_bracket == '>')
						open_bracket = '<';

					if (open_bracket != 0) {
						const gchar *ptr, *endptr;
						gint n_opened = 0, n_closed = 0;

						endptr = str + rm_eo;

						for (ptr = str + pmatch[0].rm_so; ptr < endptr; ptr++) {
							if (*ptr == open_bracket)
								n_opened++;
							else if (*ptr == close_bracket)
								n_closed++;
						}

						/* The closing bracket can match one inside the URL,
						   thus keep it there. */
						if (n_opened > 0 && n_opened - n_closed >= 0)
							break;
					}

					rm_eo--;
					pmatch[0].rm_eo--;
				}

				char_so = g_utf8_pointer_to_offset (str, str + pmatch[0].rm_so);
				char_eo = g_utf8_pointer_to_offset (str, str + pmatch[0].rm_eo);

				gtk_text_buffer_get_iter_at_offset (buffer, &start, offset + char_so);
				gtk_text_buffer_get_iter_at_offset (buffer, &end, offset + char_eo);
				gtk_text_buffer_apply_tag_by_name (buffer, E_BUFFER_TAGGER_LINK_TAG, &start, &end);

				any = TRUE;
				str += pmatch[0].rm_eo;
				offset += char_eo;
				break;
			}
		}
	}

	g_free (text);
}

static void
get_pointer_position (GtkTextView *text_view,
		      GdkEvent *event,
                      gint *out_x,
                      gint *out_y)
{
#if GTK_CHECK_VERSION(4, 0, 0)
	gdouble tmp_x = -1.0, tmp_y = -1.0;
	if (!event || !gdk_event_get_position (event, &tmp_x, &tmp_y)) {
		tmp_x = -1;
		tmp_y = -1;
	} else {
		GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (text_view), GTK_TYPE_WINDOW);
		if (window)
			gtk_widget_translate_coordinates (window, GTK_WIDGET (text_view), tmp_x, tmp_y, &tmp_x, &tmp_y);
	}
	if (out_x)
		*out_x = tmp_x;
	if (out_y)
		*out_y = tmp_y;
#else
	GdkWindow *window;
	GdkDisplay *display;
	GdkDeviceManager *device_manager;
	GdkDevice *device;

	window = gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_WIDGET);
	display = gdk_window_get_display (window);
	device_manager = gdk_display_get_device_manager (display);
	device = gdk_device_manager_get_client_pointer (device_manager);

	gdk_window_get_device_position (window, device, out_x, out_y, NULL);
#endif
}

static guint32
get_state (GtkTextBuffer *buffer)
{
	g_return_val_if_fail (buffer != NULL, E_BUFFER_TAGGER_STATE_NONE);
	g_return_val_if_fail (GTK_IS_TEXT_BUFFER (buffer), E_BUFFER_TAGGER_STATE_NONE);

	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (buffer), E_BUFFER_TAGGER_DATA_STATE));
}

static void
set_state (GtkTextBuffer *buffer,
           guint32 state)
{
	g_object_set_data (G_OBJECT (buffer), E_BUFFER_TAGGER_DATA_STATE, GINT_TO_POINTER (state));
}

static void
update_state (GtkTextBuffer *buffer,
              guint32 value,
              gboolean do_set)
{
	guint32 state;

	g_return_if_fail (buffer != NULL);
	g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));

	state = get_state (buffer);

	if (do_set)
		state = state | value;
	else
		state = state & (~value);

	set_state (buffer, state);
}

static gboolean
get_tag_bounds (GtkTextIter *iter,
                GtkTextTag *tag,
                GtkTextIter *start,
                GtkTextIter *end)
{
	gboolean res = FALSE;

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);
	g_return_val_if_fail (start != NULL, FALSE);
	g_return_val_if_fail (end != NULL, FALSE);

	if (gtk_text_iter_has_tag (iter, tag)) {
		*start = *iter;
		*end = *iter;

		if (!gtk_text_iter_starts_tag (start, tag))
			gtk_text_iter_backward_to_tag_toggle (start, tag);

		if (!gtk_text_iter_ends_tag (end, tag))
			gtk_text_iter_forward_to_tag_toggle (end, tag);

		res = TRUE;
	}

	return res;
}

static gchar *
get_url_at_iter (GtkTextBuffer *buffer,
                 GtkTextIter *iter)
{
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
	GtkTextIter start, end;
	gchar *url = NULL;

	g_return_val_if_fail (buffer != NULL, NULL);

	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);
	g_return_val_if_fail (tag != NULL, NULL);

	if (get_tag_bounds (iter, tag, &start, &end))
		url = gtk_text_iter_get_text (&start, &end);

	return url;
}

static gboolean
invoke_link_if_present (GtkTextBuffer *buffer,
                        GtkTextIter *iter)
{
	gboolean res;
	gchar *url;

	g_return_val_if_fail (buffer != NULL, FALSE);

	url = get_url_at_iter (buffer, iter);

	res = url && *url;
	if (res)
		e_show_uri (NULL, url);

	g_free (url);

	return res;
}

static void
remove_tag_if_present (GtkTextBuffer *buffer,
                       GtkTextIter *where)
{
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
	GtkTextIter start, end;

	g_return_if_fail (buffer != NULL);
	g_return_if_fail (where != NULL);

	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);
	g_return_if_fail (tag != NULL);

	if (get_tag_bounds (where, tag, &start, &end))
		gtk_text_buffer_remove_tag (buffer, tag, &start, &end);
}

static void
buffer_insert_text (GtkTextBuffer *buffer,
                    GtkTextIter *location,
                    gchar *text,
                    gint len,
                    gpointer user_data)
{
	update_state (buffer, E_BUFFER_TAGGER_STATE_INSDEL, TRUE);
	remove_tag_if_present (buffer, location);
}

static void
buffer_delete_range (GtkTextBuffer *buffer,
                     GtkTextIter *start,
                     GtkTextIter *end,
                     gpointer user_data)
{
	update_state (buffer, E_BUFFER_TAGGER_STATE_INSDEL, TRUE);
	remove_tag_if_present (buffer, start);
	remove_tag_if_present (buffer, end);
}

static void
buffer_cursor_position (GtkTextBuffer *buffer,
                        gpointer user_data)
{
	guint32 state;

	state = get_state (buffer);
	if (state & E_BUFFER_TAGGER_STATE_INSDEL) {
		state = (state & (~E_BUFFER_TAGGER_STATE_INSDEL)) | E_BUFFER_TAGGER_STATE_CHANGED;
	} else {
		if (state & E_BUFFER_TAGGER_STATE_CHANGED) {
			markup_text (buffer);
		}

		state = state & (~ (E_BUFFER_TAGGER_STATE_CHANGED | E_BUFFER_TAGGER_STATE_INSDEL));
	}

	set_state (buffer, state);
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
textview_open_uri_cb (GSimpleAction *simple,
		      GVariant *parameter,
		      gpointer user_data)
#else
static void
textview_open_uri_cb (GtkWidget *widget,
		      gpointer user_data)
#endif
{
#if GTK_CHECK_VERSION(4, 0, 0)
	const gchar *uri = parameter ? g_variant_get_string (parameter, NULL) : NULL;
#else
	const gchar *uri = user_data;
#endif

	g_return_if_fail (uri != NULL);

	e_show_uri (NULL, uri);
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
textview_copy_uri_cb (GSimpleAction *simple,
		      GVariant *parameter,
		      gpointer user_data)
#else
static void
textview_copy_uri_cb (GtkWidget *widget,
		      gpointer user_data)
#endif
{
#if GTK_CHECK_VERSION(4, 0, 0)
	GdkClipboard *clipboard;
	GtkWidget *widget = user_data;
	const gchar *uri = parameter ? g_variant_get_string (parameter, NULL) : NULL;
#else
	const gchar *uri = user_data;
	GtkClipboard *clipboard;
#endif

	g_return_if_fail (uri != NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
	clipboard = gtk_widget_get_primary_clipboard (widget);
	gdk_clipboard_set_text (clipboard, uri);

	clipboard = gtk_widget_get_clipboard (widget);
	gdk_clipboard_set_text (clipboard, uri);
#else
	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text (clipboard, uri, -1);
	gtk_clipboard_store (clipboard);

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, uri, -1);
	gtk_clipboard_store (clipboard);
#endif
}

static void
update_mouse_cursor (GtkTextView *text_view,
                     gint x,
                     gint y)
{
#if !GTK_CHECK_VERSION(4, 0, 0)
	static GdkCursor *hand_cursor = NULL;
	static GdkCursor *regular_cursor = NULL;
#endif
	gboolean hovering = FALSE, hovering_over_link = FALSE, hovering_real;
	guint32 state;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (text_view);
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
	GtkTextIter iter;

#if !GTK_CHECK_VERSION(4, 0, 0)
	if (!hand_cursor) {
		hand_cursor = gdk_cursor_new (GDK_HAND2);
		regular_cursor = gdk_cursor_new (GDK_XTERM);
	}
#endif

	g_return_if_fail (buffer != NULL);

	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);
	g_return_if_fail (tag != NULL);

	state = get_state (buffer);

	gtk_text_view_get_iter_at_location (text_view, &iter, x, y);
	hovering_real = gtk_text_iter_has_tag (&iter, tag);

#if GTK_CHECK_VERSION(4, 0, 0)
	if (hovering_real) {
		gchar *url;

		url = get_url_at_iter (buffer, &iter);

		if (url && *url && g_strcmp0 (url, g_object_get_data (G_OBJECT (text_view), E_BUFFER_TAGGER_DATA_CURRENT_URI)) != 0) {
			GActionGroup *action_group;
			GMenu *menu, *section;
			GMenuItem *item;
			GVariant *variant;
			const GActionEntry entries[] = {
				{ "copy-uri",	textview_copy_uri_cb, "s" },
				{ "open-uri",	textview_open_uri_cb, "s" }
			};

			action_group = G_ACTION_GROUP (g_simple_action_group_new ());
			g_action_map_add_action_entries (G_ACTION_MAP (action_group), entries, G_N_ELEMENTS (entries), text_view);
			gtk_widget_insert_action_group (GTK_WIDGET (text_view), "e-buffer-tagger", action_group);
			g_object_unref (action_group);

			variant = g_variant_new_string (url);

			section = g_menu_new ();

			item = g_menu_item_new (_("Copy _Link Location"), "e-buffer-tagger.copy-uri");
			g_menu_item_set_attribute_value (item, G_MENU_ATTRIBUTE_TARGET, variant);
			g_menu_append_item (section, item);
			g_object_unref (item);

			item = g_menu_item_new (_("O_pen Link in Browser"), "e-buffer-tagger.open-uri");
			g_menu_item_set_attribute_value (item, G_MENU_ATTRIBUTE_TARGET, variant);
			g_menu_append_item (section, item);
			g_object_unref (item);

			menu = g_menu_new ();
			g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
			g_object_unref (section);

			gtk_text_view_set_extra_menu (text_view, G_MENU_MODEL (menu));

			g_object_unref (menu);

			g_object_set_data_full (G_OBJECT (text_view), E_BUFFER_TAGGER_DATA_CURRENT_URI, url, g_free);
			url = NULL;
		} else if (!url || !*url) {
			gtk_text_view_set_extra_menu (text_view, NULL);
			gtk_widget_insert_action_group (GTK_WIDGET (text_view), "e-buffer-tagger", NULL);
			g_object_set_data (G_OBJECT (text_view), E_BUFFER_TAGGER_DATA_CURRENT_URI, NULL);
		}

		g_free (url);
	} else {
		gtk_text_view_set_extra_menu (text_view, NULL);
		gtk_widget_insert_action_group (GTK_WIDGET (text_view), "e-buffer-tagger", NULL);
		g_object_set_data (G_OBJECT (text_view), E_BUFFER_TAGGER_DATA_CURRENT_URI, NULL);
	}
#endif

	hovering_over_link = (state & E_BUFFER_TAGGER_STATE_IS_HOVERING) != 0;
	if ((state & E_BUFFER_TAGGER_STATE_CTRL_DOWN) == 0) {
		hovering = FALSE;
	} else {
		hovering = hovering_real;
	}

	if (hovering != hovering_over_link) {
		update_state (buffer, E_BUFFER_TAGGER_STATE_IS_HOVERING, hovering);

#if GTK_CHECK_VERSION(4, 0, 0)
		if (hovering && gtk_widget_has_focus (GTK_WIDGET (text_view)))
			gtk_widget_set_cursor_from_name (GTK_WIDGET (text_view), "pointer");
		else
			gtk_widget_set_cursor_from_name (GTK_WIDGET (text_view), NULL);
#else
		if (hovering && gtk_widget_has_focus (GTK_WIDGET (text_view)))
			gdk_window_set_cursor (gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT), hand_cursor);
		else
			gdk_window_set_cursor (gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT), regular_cursor);
#endif
	}

	hovering_over_link = (state & E_BUFFER_TAGGER_STATE_IS_HOVERING_TOOLTIP) != 0;

	if (hovering_real != hovering_over_link) {
		update_state (buffer, E_BUFFER_TAGGER_STATE_IS_HOVERING_TOOLTIP, hovering_real);

		gtk_widget_trigger_tooltip_query (GTK_WIDGET (text_view));
	}
}

#if !GTK_CHECK_VERSION(4, 0, 0)
static void
textview_style_updated_cb (GtkWidget *textview,
			   gpointer user_data)
{
	GtkStyleContext *context;
	GtkStateFlags state;
	GtkTextBuffer *buffer;
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
	GdkRGBA rgba;

	g_return_if_fail (GTK_IS_WIDGET (textview));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);

	if (!tag)
		return;

	context = gtk_widget_get_style_context (textview);

	rgba.red = 0;
	rgba.green = 0;
	rgba.blue = 1;
	rgba.alpha = 1;

	state = gtk_style_context_get_state (context);
	state = state & (~(GTK_STATE_FLAG_VISITED | GTK_STATE_FLAG_LINK));
	state = state | GTK_STATE_FLAG_LINK;

	gtk_style_context_save (context);
	gtk_style_context_set_state (context, state);
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_style_context_get_color (context, &rgba);
#else
	/* Remove the 'view' style, because it can "confuse" some themes */
	gtk_style_context_remove_class (context, GTK_STYLE_CLASS_VIEW);
	gtk_style_context_get_color (context, state, &rgba);
#endif
	gtk_style_context_restore (context);

	g_object_set (G_OBJECT (tag), "foreground-rgba", &rgba, NULL);
}
#endif

static gboolean
textview_query_tooltip (GtkTextView *text_view,
                        gint x,
                        gint y,
                        gboolean keyboard_mode,
                        GtkTooltip *tooltip,
                        gpointer user_data)
{
	GtkTextBuffer *buffer;
	guint32 state;
	gboolean res = FALSE;

	if (keyboard_mode)
		return FALSE;

	buffer = gtk_text_view_get_buffer (text_view);
	g_return_val_if_fail (buffer != NULL, FALSE);

	state = get_state (buffer);

	if ((state & E_BUFFER_TAGGER_STATE_IS_HOVERING_TOOLTIP) != 0) {
		gchar *url;
		GtkTextIter iter;

		gtk_text_view_window_to_buffer_coords (
			text_view,
			GTK_TEXT_WINDOW_WIDGET,
			x, y, &x, &y);
		gtk_text_view_get_iter_at_location (text_view, &iter, x, y);

		url = get_url_at_iter (buffer, &iter);
		res = url && *url;

		if (res) {
			gchar *str;

			/* To Translators: The text is concatenated to a form: "Ctrl-click to open a link http://www.example.com" */
			str = g_strconcat (_("Ctrl-click to open a link"), " ", url, NULL);
			gtk_tooltip_set_text (tooltip, str);
			g_free (str);
		}

		g_free (url);
	}

	return res;
}

/* Links can be activated by pressing Enter. */
#if GTK_CHECK_VERSION(4, 0, 0)
static gboolean
textview_key_press_event (GtkEventControllerKey *controller,
			  guint keyval,
			  guint keycode,
			  GdkModifierType state,
			  GtkWidget *text_view)
#else
static gboolean
textview_key_press_event (GtkWidget *text_view,
                          GdkEventKey *event)
#endif
{
#if !GTK_CHECK_VERSION(4, 0, 0)
	guint keyval = event->keyval;
	GdkModifierType state = event->state;
#endif
	GtkTextIter iter;
	GtkTextBuffer *buffer;

	if ((state & GDK_CONTROL_MASK) == 0)
		return FALSE;

	switch (keyval) {
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
		gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
		if (invoke_link_if_present (buffer, &iter))
			return TRUE;
		break;

	default:
		break;
	}

	return FALSE;
}

static void
update_ctrl_state (GtkTextView *textview,
                   gboolean ctrl_is_down,
		   GdkEvent *event)
{
	GtkTextBuffer *buffer;
	gint x, y;

	buffer = gtk_text_view_get_buffer (textview);
	if (buffer) {
		if (((get_state (buffer) & E_BUFFER_TAGGER_STATE_CTRL_DOWN) != 0) != (ctrl_is_down != FALSE)) {
			update_state (buffer, E_BUFFER_TAGGER_STATE_CTRL_DOWN, ctrl_is_down != FALSE);
		}

		get_pointer_position (textview, event, &x, &y);
		gtk_text_view_window_to_buffer_coords (textview, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
		update_mouse_cursor (textview, x, y);
	}
}

/* Links can also be activated by clicking. */
static gboolean
textview_event_after (GtkTextView *textview,
                      GdkEvent *event)
{
	GtkTextIter start, end, iter;
	GtkTextBuffer *buffer;
	gint x, y;
	GdkModifierType mt = 0;
	GdkEventType event_type;
	guint event_button = 0;
	gdouble event_x_win = 0;
	gdouble event_y_win = 0;

	g_return_val_if_fail (GTK_IS_TEXT_VIEW (textview), FALSE);

#if GTK_CHECK_VERSION(4, 0, 0)
	event_type = gdk_event_get_event_type (event);
#else
	event_type = event->type;
#endif

	if (event_type == GDK_KEY_PRESS || event_type == GDK_KEY_RELEASE) {
		guint event_keyval = 0;

#if GTK_CHECK_VERSION(4, 0, 0)
		event_keyval = gdk_key_event_get_keyval (event);
#else
		gdk_event_get_keyval (event, &event_keyval);
#endif

		switch (event_keyval) {
			case GDK_KEY_Control_L:
			case GDK_KEY_Control_R:
				update_ctrl_state (
					textview,
					event_type == GDK_KEY_PRESS,
					event);
				break;
		}

		return FALSE;
	}

#if GTK_CHECK_VERSION(4, 0, 0)
	mt = gdk_event_get_modifier_state (event);
#else
	if (!gdk_event_get_state (event, &mt)) {
		GdkWindow *window;
		GdkDisplay *display;
		GdkDeviceManager *device_manager;
		GdkDevice *device;

		window = gtk_widget_get_parent_window (GTK_WIDGET (textview));
		display = gdk_window_get_display (window);
		device_manager = gdk_display_get_device_manager (display);
		device = gdk_device_manager_get_client_pointer (device_manager);

		gdk_window_get_device_position (window, device, NULL, NULL, &mt);
	}
#endif

	update_ctrl_state (textview, (mt & GDK_CONTROL_MASK) != 0, event);

	if (event_type != GDK_BUTTON_RELEASE)
		return FALSE;

#if GTK_CHECK_VERSION(4, 0, 0)
	event_button = gdk_button_event_get_button (event);
	if (gdk_event_get_position (event, &event_x_win, &event_y_win)) {
		GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (textview), GTK_TYPE_WINDOW);
		if (window)
			gtk_widget_translate_coordinates (window, GTK_WIDGET (textview), event_y_win, event_y_win, &event_x_win, &event_y_win);
	}
#else
	gdk_event_get_button (event, &event_button);
	gdk_event_get_coords (event, &event_x_win, &event_y_win);
#endif

	if (event_button != 1 || (mt & GDK_CONTROL_MASK) == 0)
		return FALSE;

	buffer = gtk_text_view_get_buffer (textview);

	/* we shouldn't follow a link if the user has selected something */
	gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
	if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end))
		return FALSE;

	gtk_text_view_window_to_buffer_coords (
		textview,
		GTK_TEXT_WINDOW_WIDGET,
		event_x_win, event_y_win, &x, &y);

	gtk_text_view_get_iter_at_location (textview, &iter, x, y);

	invoke_link_if_present (buffer, &iter);
	update_mouse_cursor (textview, x, y);

	return FALSE;
}

#if GTK_CHECK_VERSION(4, 0, 0)
static gboolean
textview_motion_notify_event (GtkTextView *textview,
                              gdouble event_x,
			      gdouble event_y,
			      GtkEventControllerMotion *controller)
#else
static gboolean
textview_motion_notify_event (GtkTextView *textview,
                              GdkEventMotion *event)
#endif
{
#if !GTK_CHECK_VERSION(4, 0, 0)
	gint event_x = event->x, event_y = event->y;
#endif
	gint x, y;

	g_return_val_if_fail (GTK_IS_TEXT_VIEW (textview), FALSE);

	gtk_text_view_window_to_buffer_coords (
		textview,
		GTK_TEXT_WINDOW_WIDGET,
		event_x, event_y, &x, &y);

	update_mouse_cursor (textview, x, y);

	return FALSE;
}

#if !GTK_CHECK_VERSION(4, 0, 0)
static void
textview_populate_popup_cb (GtkTextView *textview,
			    GtkWidget *widget,
			    gpointer user_data)
{
	GtkTextIter iter;
	GtkTextBuffer *buffer;
	GdkDisplay *display;
	gboolean iter_set = FALSE;
	gchar *uri;

	if (!GTK_IS_MENU (widget))
		return;

	buffer = gtk_text_view_get_buffer (textview);

	display = gtk_widget_get_display (GTK_WIDGET (textview));

	if (display && gtk_widget_get_window (GTK_WIDGET (textview))) {
		GdkDeviceManager *device_manager;
		GdkDevice *pointer;
		gint px = 0, py = 0, xx = 0, yy = 0;

		device_manager = gdk_display_get_device_manager (display);
		pointer = gdk_device_manager_get_client_pointer (device_manager);

		gdk_device_get_position (pointer, NULL, &px, &py);
		gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (textview)), &xx, &yy);

		px -= xx;
		py -= yy;

		gtk_text_view_window_to_buffer_coords (
			textview,
			GTK_TEXT_WINDOW_WIDGET,
			px, py, &xx, &yy);

		iter_set = gtk_text_view_get_iter_at_location (textview, &iter, xx, yy);
	}

	if (!iter_set) {
		GtkTextMark *mark;

		mark = gtk_text_buffer_get_selection_bound (buffer);

		if (!mark)
			return;

		gtk_text_buffer_get_iter_at_mark (buffer, &iter, mark);
	}

	uri = get_url_at_iter (buffer, &iter);

	if (uri && *uri) {
		GtkMenuShell *menu = GTK_MENU_SHELL (widget);
		GtkWidget *item;

		item = gtk_separator_menu_item_new ();
		gtk_widget_show (item);
		gtk_menu_shell_prepend (menu, item);

		item = gtk_menu_item_new_with_mnemonic (_("Copy _Link Location"));
		gtk_widget_show (item);
		gtk_menu_shell_prepend (menu, item);

		g_signal_connect_data (item, "activate",
			G_CALLBACK (textview_copy_uri_cb), g_strdup (uri), (GClosureNotify) g_free, 0);

		item = gtk_menu_item_new_with_mnemonic (_("O_pen Link in Browser"));
		gtk_widget_show (item);
		gtk_menu_shell_prepend (menu, item);

		g_signal_connect_data (item, "activate",
			G_CALLBACK (textview_open_uri_cb), uri, (GClosureNotify) g_free, 0);
	} else {
		g_free (uri);
	}
}
#endif

void
e_buffer_tagger_connect (GtkTextView *textview)
{
	GtkTextBuffer *buffer;
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
#if GTK_CHECK_VERSION(4, 0, 0)
	GtkEventController *controller;
	GtkWidget *widget;
#endif

	init_magic_links ();

	g_return_if_fail (textview != NULL);
	g_return_if_fail (GTK_IS_TEXT_VIEW (textview));

	buffer = gtk_text_view_get_buffer (textview);
	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);

	/* if tag is there already, then it is connected, thus claim */
	g_return_if_fail (tag == NULL);

	gtk_text_buffer_create_tag (
		buffer, E_BUFFER_TAGGER_LINK_TAG,
		"foreground", "blue",
		"underline", PANGO_UNDERLINE_SINGLE,
		NULL);

	set_state (buffer, E_BUFFER_TAGGER_STATE_NONE);

	g_signal_connect (
		buffer, "insert-text",
		G_CALLBACK (buffer_insert_text), NULL);
	g_signal_connect (
		buffer, "delete-range",
		G_CALLBACK (buffer_delete_range), NULL);
	g_signal_connect (
		buffer, "notify::cursor-position",
		G_CALLBACK (buffer_cursor_position), NULL);

	gtk_widget_set_has_tooltip (GTK_WIDGET (textview), TRUE);

	g_signal_connect (
		textview, "query-tooltip",
		G_CALLBACK (textview_query_tooltip), NULL);
#if GTK_CHECK_VERSION(4, 0, 0)
	widget = GTK_WIDGET (textview);

	controller = gtk_event_controller_key_new ();
	g_object_set_data_full (G_OBJECT (textview), E_BUFFER_TAGGER_DATA_KEY_CONTROLLER,
		g_object_ref (controller), g_object_unref);
	gtk_widget_add_controller (widget, controller);
	g_signal_connect_object (controller, "key-pressed",
		G_CALLBACK (textview_key_press_event), textview, 0);

	controller = gtk_event_controller_legacy_new ();
	g_object_set_data_full (G_OBJECT (textview), E_BUFFER_TAGGER_DATA_LEGACY_CONTROLLER,
		g_object_ref (controller), g_object_unref);
	gtk_widget_add_controller (widget, controller);
	g_signal_connect_object (controller, "event",
		G_CALLBACK (textview_event_after), textview, G_CONNECT_SWAPPED);

	controller = gtk_event_controller_motion_new ();
	g_object_set_data_full (G_OBJECT (textview), E_BUFFER_TAGGER_DATA_MOTION_CONTROLLER,
		g_object_ref (controller), g_object_unref);
	gtk_widget_add_controller (widget, controller);
	g_signal_connect_object (controller, "motion",
		G_CALLBACK (textview_motion_notify_event), textview, G_CONNECT_SWAPPED);

#else
	g_signal_connect (
		textview, "style-updated",
		G_CALLBACK (textview_style_updated_cb), NULL);
	g_signal_connect (
		textview, "key-press-event",
		G_CALLBACK (textview_key_press_event), NULL);
	g_signal_connect (
		textview, "event-after",
		G_CALLBACK (textview_event_after), NULL);
	g_signal_connect (
		textview, "motion-notify-event",
		G_CALLBACK (textview_motion_notify_event), NULL);
	g_signal_connect (
		textview, "populate-popup",
		G_CALLBACK (textview_populate_popup_cb), NULL);
#endif
}

void
e_buffer_tagger_disconnect (GtkTextView *textview)
{
	GtkTextBuffer *buffer;
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;
#if GTK_CHECK_VERSION(4, 0, 0)
	GtkWidget *widget;
#endif

	g_return_if_fail (textview != NULL);
	g_return_if_fail (GTK_IS_TEXT_VIEW (textview));

	buffer = gtk_text_view_get_buffer (textview);
	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);

	/* if tag is not there, then it is not connected, thus claim */
	g_return_if_fail (tag != NULL);

	gtk_text_tag_table_remove (tag_table, tag);

	set_state (buffer, E_BUFFER_TAGGER_STATE_NONE);

	g_signal_handlers_disconnect_by_func (buffer, G_CALLBACK (buffer_insert_text), NULL);
	g_signal_handlers_disconnect_by_func (buffer, G_CALLBACK (buffer_delete_range), NULL);
	g_signal_handlers_disconnect_by_func (buffer, G_CALLBACK (buffer_cursor_position), NULL);

	gtk_widget_set_has_tooltip (GTK_WIDGET (textview), FALSE);

	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_query_tooltip), NULL);
#if GTK_CHECK_VERSION(4, 0, 0)
	widget = GTK_WIDGET (textview);

#define drop_controller(_name) { \
	gpointer controller = g_object_get_data (G_OBJECT (textview), _name); \
	if (controller) \
		gtk_widget_remove_controller (widget, controller); \
	g_object_set_data (G_OBJECT (textview), _name, NULL); \
	}

	drop_controller (E_BUFFER_TAGGER_DATA_KEY_CONTROLLER);
	drop_controller (E_BUFFER_TAGGER_DATA_LEGACY_CONTROLLER);
	drop_controller (E_BUFFER_TAGGER_DATA_MOTION_CONTROLLER);

#undef drop_controller

	g_object_set_data (G_OBJECT (textview), E_BUFFER_TAGGER_DATA_CURRENT_URI, NULL);

	gtk_text_view_set_extra_menu (textview, NULL);
	gtk_widget_insert_action_group (widget, "e-buffer-tagger", NULL);
#else
	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_style_updated_cb), NULL);
	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_key_press_event), NULL);
	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_event_after), NULL);
	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_motion_notify_event), NULL);
	g_signal_handlers_disconnect_by_func (textview, G_CALLBACK (textview_populate_popup_cb), NULL);
#endif
}

void
e_buffer_tagger_update_tags (GtkTextView *textview)
{
	GtkTextBuffer *buffer;
	GtkTextTagTable *tag_table;
	GtkTextTag *tag;

	g_return_if_fail (textview != NULL);
	g_return_if_fail (GTK_IS_TEXT_VIEW (textview));

	buffer = gtk_text_view_get_buffer (textview);
	tag_table = gtk_text_buffer_get_tag_table (buffer);
	tag = gtk_text_tag_table_lookup (tag_table, E_BUFFER_TAGGER_LINK_TAG);

	/* if tag is not there, then it is not connected, thus claim */
	g_return_if_fail (tag != NULL);

	update_state (buffer, E_BUFFER_TAGGER_STATE_INSDEL | E_BUFFER_TAGGER_STATE_CHANGED, FALSE);

	markup_text (buffer);
}
