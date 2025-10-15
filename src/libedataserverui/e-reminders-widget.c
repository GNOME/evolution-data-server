/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
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
 */

/**
 * SECTION: e-reminders-widget
 * @short_description: An #ERemindersWidget to work with past reminders
 *
 * The #ERemindersWidget is a widget which does common tasks on past reminders
 * provided by #EReminderWatcher. The owner should connect to the "changed" signal
 * to be notified on any changes, including when the list of past reminders
 * is either expanded or shrunk, which usually causes the dialog with this
 * widget to be shown or hidden.
 *
 * The widget itself is an #EExtensible.
 *
 * The widget does not listen to #EReminderWatcher::triggered signal.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "libedataserver/libedataserver.h"
#include "libecal/libecal.h"

#include "libedataserverui-private.h"

#include "e-buffer-tagger.h"
#include "e-reminders-widget.h"

#define MAX_CUSTOM_SNOOZE_VALUES 7

struct _ERemindersWidgetPrivate {
	EReminderWatcher *watcher;
	GSettings *settings;
	gboolean is_empty;

	GtkPaned *paned;

	GtkTreeView *tree_view;
	GtkTextView *details_text_view;
	GtkWidget *dismiss_button;
	GtkWidget *snooze_button;
	GMenu *snooze_menu;

	GtkWidget *add_snooze_popover;
	GtkWidget *add_snooze_days_spin;
	GtkWidget *add_snooze_hours_spin;
	GtkWidget *add_snooze_minutes_spin;
	GtkWidget *add_snooze_kind_combo;
	GtkWidget *add_snooze_add_button;

	GtkInfoBar *info_bar;

	GCancellable *cancellable;
	guint refresh_idle_id;

	gboolean is_mapped;
	guint overdue_update_id;
	gint64 last_overdue_update; /* in seconds */
	gboolean overdue_update_rounded;

	GHashTable *existing_snooze_times; /* SnoozeTime * ~> SnoozeTime * */
};

enum {
	CHANGED,
	ACTIVATED,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_WATCHER,
	PROP_EMPTY,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (ERemindersWidget, e_reminders_widget, GTK_TYPE_GRID,
			 G_ADD_PRIVATE (ERemindersWidget)
			 G_IMPLEMENT_INTERFACE (E_TYPE_EXTENSIBLE, NULL))

typedef enum _SnoozeKind {
	SNOOZE_KIND_UNKNOWN,
	SNOOZE_KIND_FROM_NOW,
	SNOOZE_KIND_FROM_START,
	SNOOZE_KIND_ADD_CUSTOM_TIME,
	SNOOZE_KIND_CLEAR_CUSTOM_TIMES
} SnoozeKind;

typedef struct _SnoozeTime {
	SnoozeKind kind;
	gint minutes;
} SnoozeTime;

static guint
snooze_time_hash (gconstpointer ptr)
{
	const SnoozeTime *st = ptr;
	guint val = st->kind;

	val |= st->minutes << 4;

	return val;
}

static gboolean
snooze_time_equal (gconstpointer ptr1,
		   gconstpointer ptr2)
{
	const SnoozeTime *st1 = ptr1;
	const SnoozeTime *st2 = ptr2;

	return st1->kind == st2->kind && st1->minutes == st2->minutes;
}

static gint
reminders_widget_cmp_snooze_times_cb (gconstpointer aa,
				      gconstpointer bb)
{
	const SnoozeTime *st1, *st2;

	st1 = *((const SnoozeTime **) aa);
	st2 = *((const SnoozeTime **) bb);

	if (st1->kind == st2->kind)
		return st1->minutes - st2->minutes;

	return st1->kind - st2->kind;
}

static const SnoozeTime predefined_snooze_times[] = {
	{ SNOOZE_KIND_FROM_NOW, 60 },
	{ SNOOZE_KIND_FROM_NOW, 30 },
	{ SNOOZE_KIND_FROM_NOW, 15 },
	{ SNOOZE_KIND_FROM_NOW, 10 },
	{ SNOOZE_KIND_FROM_NOW, 5 },
	{ SNOOZE_KIND_FROM_NOW, 1 },
	{ SNOOZE_KIND_FROM_START, 0 },
	{ SNOOZE_KIND_FROM_START, -1 },
	{ SNOOZE_KIND_FROM_START, -5 },
	{ SNOOZE_KIND_FROM_START, -10 },
	{ SNOOZE_KIND_FROM_START, -15 },
	{ SNOOZE_KIND_FROM_START, -30 },
	{ SNOOZE_KIND_FROM_START, -60 }
};

static gchar *
reminders_widget_label_snooze (SnoozeKind kind,
			       gint minutes,
			       gboolean short_version)
{
	gchar *label = NULL;
	gchar *text;

	if (!minutes) {
		if (short_version)
			/* Translators: short version of "Snooze until start" */
			return g_strdup (C_("snooze-time", "until start"));
		else
			return g_strdup (C_("snooze-time", "Snooze until start"));
	}

	text = e_cal_util_seconds_to_string ((minutes < 0 ? minutes * (-1) : minutes) * 60);

	if (kind == SNOOZE_KIND_FROM_START || minutes < 0) {
		if (minutes < 0) {
			if (short_version) {
				/* Translators: short version of "Snooze until 10 minutes before" ...start */
				label = g_strdup_printf (C_("snooze-time", "until %s before start"), text);
			} else {
				/* Translators: the '%s' is replaced with actual time interval, like "10 minutes", making it "Snooze until 10 minutes before" ...start */
				label = g_strdup_printf (C_("snooze-time", "Snooze until %s before start"), text);
			}
		} else if (short_version) {
			/* Translators: short version of "Snooze until 10 minutes after start" */
			label = g_strdup_printf (C_("snooze-time", "until %s after start"), text);
		} else {
			/* Translators: the '%s' is replaced with actual time interval, like "10 minutes", making it "Snooze until 10 minutes after start" */
			label = g_strdup_printf (C_("snooze-time", "Snooze until %s after start"), text);
		}
	} else if (short_version) {
		/* Translators: short version of "Snooze for 10 minutes" */
		label = g_strdup_printf (C_("snooze-time", "for %s"), text);
	} else {
		/* Translators: the '%s' is replaced with actual time interval, like "10 minutes", making it "Snooze for 10 minutes" */
		label = g_strdup_printf (C_("snooze-time", "Snooze for %s"), text);
	}

	g_free (text);

	return label;
}

static void
reminders_widget_update_snooze_button (ERemindersWidget *reminders,
				       SnoozeKind kind,
				       gint minutes)
{
	GtkWidget *label;
	gchar *text;

#if GTK_CHECK_VERSION(4, 0, 0)
	label = gtk_button_get_child (GTK_BUTTON (reminders->priv->snooze_button));
#else
	label = gtk_bin_get_child (GTK_BIN (reminders->priv->snooze_button));
#endif
	g_return_if_fail (GTK_IS_LABEL (label));

	text = reminders_widget_label_snooze (kind, minutes, FALSE);

	gtk_label_set_label (GTK_LABEL (label), text);
	gtk_widget_set_tooltip_text (reminders->priv->snooze_button, text);
	gtk_actionable_set_action_target (GTK_ACTIONABLE (reminders->priv->snooze_button),
		"(ii)", (gint) (kind), minutes);

	g_free (text);
}

static void
reminders_widget_add_snooze_menu_item (GMenu *section,
				       SnoozeKind kind,
				       gint minutes,
				       const gchar *use_label) /* nullable */
{
	GVariant *target;
	GMenuItem *item;
	gchar *label = NULL;

	if (!use_label)
		label = reminders_widget_label_snooze (kind, minutes, TRUE);
	target = g_variant_new ("(ii)", kind, minutes);
	item = g_menu_item_new (label ? label : use_label, NULL);

	g_menu_item_set_action_and_target_value (item, "reminders.snooze", target);
	g_menu_append_item (section, item);

	g_object_unref (item);
	g_free (label);
}

static void
reminders_widget_fill_snooze_options (ERemindersWidget *reminders,
				      gint preselect_minutes,
				      gboolean relative_to_start)
{
	GVariant *variant;
	GMenu *section = NULL;
	SnoozeKind last_kind = SNOOZE_KIND_UNKNOWN;
	gboolean have_saved_times = FALSE;
	guint ii;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (reminders->priv->snooze_menu != NULL);

	g_menu_remove_all (reminders->priv->snooze_menu);
	g_hash_table_remove_all (reminders->priv->existing_snooze_times);

	/* Custom user values first */
	variant = g_settings_get_value (reminders->priv->settings, "notify-custom-snooze-times");
	if (variant) {
		const gchar **stored;

		stored = g_variant_get_strv (variant, NULL);
		if (stored) {
			GPtrArray *stored_times; /* SnoozeTime * */
			guint jj;

			stored_times = g_ptr_array_new_full (g_strv_length ((gchar **) stored), g_free);

			for (ii = 0; stored[ii]; ii++) {
				const gchar *str = stored[ii];
				SnoozeKind stored_kind = SNOOZE_KIND_UNKNOWN;
				gint stored_minutes = 0;

				if (*str == '*') {
					stored_kind = SNOOZE_KIND_FROM_START;
					stored_minutes = (gint) g_ascii_strtoll (str + 1, NULL, 10);
				} else {
					stored_minutes = (gint) g_ascii_strtoll (str, NULL, 10);
					if (stored_minutes <= 0)
						stored_kind = SNOOZE_KIND_FROM_START;
					else
						stored_kind = SNOOZE_KIND_FROM_NOW;
				}

				for (jj = 0; jj < G_N_ELEMENTS (predefined_snooze_times); jj++) {
					if (stored_kind == predefined_snooze_times[jj].kind && stored_minutes == predefined_snooze_times[jj].minutes)
						break;
				}

				/* no match in the predefined_snooze_times[] had been found */
				if (jj >= G_N_ELEMENTS (predefined_snooze_times)) {
					SnoozeTime *st;

					st = g_new0 (SnoozeTime, 1);
					st->kind = stored_kind;
					st->minutes = stored_minutes;

					g_ptr_array_add (stored_times, st);
				}
			}

			if (stored_times->len) {
				have_saved_times = TRUE;

				section = g_menu_new ();

				g_ptr_array_sort (stored_times, reminders_widget_cmp_snooze_times_cb);

				for (ii = 0; ii < stored_times->len; ii++) {
					SnoozeTime *st = g_ptr_array_index (stored_times, ii);

					reminders_widget_add_snooze_menu_item (section, st->kind, st->minutes, NULL);

					g_hash_table_add (reminders->priv->existing_snooze_times, st);
					stored_times->pdata[ii] = NULL;
				}

				g_menu_append_section (reminders->priv->snooze_menu, NULL, G_MENU_MODEL (section));
				g_clear_object (&section);
			}

			g_ptr_array_unref (stored_times);
			g_free (stored);
		}

		g_variant_unref (variant);
	}

	for (ii = 0; ii < G_N_ELEMENTS (predefined_snooze_times); ii++) {
		if (last_kind != predefined_snooze_times[ii].kind) {
			if (section) {
				g_menu_append_section (reminders->priv->snooze_menu, NULL, G_MENU_MODEL (section));
				g_clear_object (&section);
			}

			section = g_menu_new ();
			last_kind = predefined_snooze_times[ii].kind;
		}

		reminders_widget_add_snooze_menu_item (section, predefined_snooze_times[ii].kind, predefined_snooze_times[ii].minutes, NULL);
	}

	if (section) {
		g_menu_append_section (reminders->priv->snooze_menu, NULL, G_MENU_MODEL (section));
		g_clear_object (&section);
	}

	section = g_menu_new ();

	reminders_widget_add_snooze_menu_item (section, SNOOZE_KIND_ADD_CUSTOM_TIME, 0, _("_Add custom time"));

	if (have_saved_times)
		reminders_widget_add_snooze_menu_item (section, SNOOZE_KIND_CLEAR_CUSTOM_TIMES, 0, _("_Clear custom times"));

	g_menu_append_section (reminders->priv->snooze_menu, NULL, G_MENU_MODEL (section));
	g_clear_object (&section);

	if (preselect_minutes != G_MAXINT) {
		reminders_widget_update_snooze_button (reminders, preselect_minutes < 0 || relative_to_start ?
			SNOOZE_KIND_FROM_START : SNOOZE_KIND_FROM_NOW, preselect_minutes);
	}
}

static void
reminders_widget_custom_snooze_minutes_changed_cb (GSettings *settings,
						   const gchar *key,
						   gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	reminders_widget_fill_snooze_options (reminders, G_MAXINT, FALSE);
}

static void
reminders_get_reminder_markups (ERemindersWidget *reminders,
				const EReminderData *rd,
				gchar **out_overdue_markup,
				gchar **out_description_markup)
{
	g_return_if_fail (rd != NULL);

	if (out_overdue_markup) {
		gint64 diff;
		gboolean in_future;
		gchar *time_str;

		diff = (g_get_real_time () / G_USEC_PER_SEC) - ((gint64) e_cal_component_alarm_instance_get_occur_start (e_reminder_data_get_instance (rd)));
		in_future = diff < 0;
		if (in_future)
			diff = (-1) * diff;

		/* in minutes */
		if (in_future && (diff % 60) > 0)
			diff += 60;

		diff = diff / 60;

		if (!diff) {
			time_str = g_strdup (C_("overdue", "now"));
		} else if (diff < 60) {
			time_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d minute", "%d minutes", diff), (gint) diff);
		} else if (diff < 24 * 60) {
			gint hours = diff / 60;

			time_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d hour", "%d hours", hours), hours);
		} else if (diff < 7 * 24 * 60) {
			gint days = diff / (24 * 60);

			time_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d day", "%d days", days), days);
		} else if (diff < 54 * 7 * 24 * 60) {
			gint weeks = diff / (7 * 24 * 60);

			time_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d week", "%d weeks", weeks), weeks);
		} else {
			gint years = diff / (366 * 24 * 60);

			time_str = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d year", "%d years", years), years);
		}

		if (in_future || !diff) {
			*out_overdue_markup = g_markup_printf_escaped ("<span size=\"x-small\">%s</span>", time_str);
		} else {
			*out_overdue_markup = g_markup_printf_escaped ("<span size=\"x-small\">%s\n%s</span>", time_str, C_("overdue", "overdue"));
		}

		g_free (time_str);
	}

	if (out_description_markup) {
		*out_description_markup = e_reminder_watcher_describe_data (reminders->priv->watcher, rd, E_REMINDER_WATCHER_DESCRIBE_FLAG_MARKUP);
	}
}

static void
reminders_widget_overdue_update (ERemindersWidget *reminders)
{
	GtkListStore *list_store;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean any_changed = FALSE;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	model = gtk_tree_view_get_model (reminders->priv->tree_view);
	if (!model)
		return;

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return;

	list_store = GTK_LIST_STORE (model);

	do {
		EReminderData *rd = NULL;

		gtk_tree_model_get (model, &iter,
			E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, &rd,
			-1);

		if (rd) {
			gchar *overdue_markup = NULL;

			reminders_get_reminder_markups (reminders, rd, &overdue_markup, NULL);
			if (overdue_markup) {
				gchar *current = NULL;

				gtk_tree_model_get (model, &iter,
					E_REMINDERS_WIDGET_COLUMN_OVERDUE, &current,
					-1);

				if (g_strcmp0 (current, overdue_markup) != 0) {
					gtk_list_store_set (list_store, &iter,
						E_REMINDERS_WIDGET_COLUMN_OVERDUE, overdue_markup,
						-1);
					any_changed = TRUE;
				}

				g_free (overdue_markup);
				g_free (current);
			}

			e_reminder_data_free (rd);
		}
	} while (gtk_tree_model_iter_next (model, &iter));

	if (any_changed) {
		GtkTreeViewColumn *column;

		column = gtk_tree_view_get_column (reminders->priv->tree_view, 0);
		if (column)
			gtk_tree_view_column_queue_resize (column);
	}
}

static gboolean
reminders_widget_overdue_update_cb (gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	gint64 now_seconds, last_update;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), FALSE);

	reminders_widget_overdue_update (reminders);

	now_seconds = g_get_real_time () / G_USEC_PER_SEC;
	last_update = reminders->priv->last_overdue_update;
	reminders->priv->last_overdue_update = now_seconds;

	if (!last_update || (
	    (now_seconds - last_update) % 60 > 2 &&
	    (now_seconds - last_update) % 60 < 58)) {
		gint until_minute = 60 - (now_seconds % 60);

		if (until_minute >= 59) {
			reminders->priv->overdue_update_rounded = TRUE;
			until_minute = 60;
		} else {
			reminders->priv->overdue_update_rounded = FALSE;
		}

		reminders->priv->overdue_update_id = g_timeout_add_seconds (until_minute,
			reminders_widget_overdue_update_cb, reminders);

		return FALSE;
	} else if (!reminders->priv->overdue_update_rounded) {
		reminders->priv->overdue_update_rounded = TRUE;
		reminders->priv->overdue_update_id = g_timeout_add_seconds (60,
			reminders_widget_overdue_update_cb, reminders);

		return FALSE;
	}

	return TRUE;
}

static void
reminders_widget_maybe_schedule_overdue_update (ERemindersWidget *reminders)
{
	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (reminders->priv->is_empty || !reminders->priv->is_mapped) {
		if (reminders->priv->overdue_update_id) {
			g_source_remove (reminders->priv->overdue_update_id);
			reminders->priv->overdue_update_id = 0;
		}
	} else if (!reminders->priv->overdue_update_id) {
		gint until_minute = 60 - ((g_get_real_time () / G_USEC_PER_SEC) % 60);

		reminders->priv->last_overdue_update = g_get_real_time () / G_USEC_PER_SEC;

		if (until_minute >= 59) {
			reminders->priv->overdue_update_rounded = TRUE;
			until_minute = 60;
		} else {
			reminders->priv->overdue_update_rounded = FALSE;
		}

		reminders->priv->overdue_update_id = g_timeout_add_seconds (until_minute,
			reminders_widget_overdue_update_cb, reminders);

		reminders_widget_overdue_update (reminders);
	}
}

static void
reminders_widget_map (GtkWidget *widget)
{
	ERemindersWidget *reminders;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (widget));

	/* Chain up to parent's method. */
	GTK_WIDGET_CLASS (e_reminders_widget_parent_class)->map (widget);

	reminders = E_REMINDERS_WIDGET (widget);
	reminders->priv->is_mapped = TRUE;

	reminders_widget_maybe_schedule_overdue_update (reminders);
}


static void
reminders_widget_unmap (GtkWidget *widget)
{
	ERemindersWidget *reminders;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (widget));

	/* Chain up to parent's method. */
	GTK_WIDGET_CLASS (e_reminders_widget_parent_class)->unmap (widget);

	reminders = E_REMINDERS_WIDGET (widget);
	reminders->priv->is_mapped = FALSE;

	reminders_widget_maybe_schedule_overdue_update (reminders);
}

static gint
reminders_sort_by_occur (gconstpointer ptr1,
			 gconstpointer ptr2)
{
	const EReminderData *rd1 = ptr1, *rd2 = ptr2;
	const ECalComponentAlarmInstance *inst1, *inst2;
	gint cmp;

	if (!rd1 || !rd2)
		return rd1 == rd2 ? 0 : rd1 ? 1 : -1;

	inst1 = e_reminder_data_get_instance (rd1);
	inst2 = e_reminder_data_get_instance (rd2);

	if (!inst1 || !inst2)
		return inst1 == inst2 ? 0 : inst1 ? 1 : -1;

	if (e_cal_component_alarm_instance_get_occur_start (inst1) != e_cal_component_alarm_instance_get_occur_start (inst2))
		return e_cal_component_alarm_instance_get_occur_start (inst1) < e_cal_component_alarm_instance_get_occur_start (inst2) ? -1 : 1;

	if (e_cal_component_alarm_instance_get_time (inst1) != e_cal_component_alarm_instance_get_time (inst2))
		return e_cal_component_alarm_instance_get_time (inst1) < e_cal_component_alarm_instance_get_time (inst2) ? -1 : 1;

	cmp = g_strcmp0 (e_reminder_data_get_source_uid (rd1), e_reminder_data_get_source_uid (rd2));
	if (!cmp)
		cmp = g_strcmp0 (e_cal_component_alarm_instance_get_uid (inst1), e_cal_component_alarm_instance_get_uid (inst2));

	return cmp;
}

static gint
reminders_sort_by_occur_reverse (gconstpointer ptr1,
				 gconstpointer ptr2)
{
	return reminders_sort_by_occur (ptr1, ptr2) * (-1);
}

static void
reminders_widget_set_is_empty (ERemindersWidget *reminders,
			       gboolean is_empty)
{
	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (!is_empty == !reminders->priv->is_empty)
		return;

	reminders->priv->is_empty = is_empty;

	g_object_notify_by_pspec (G_OBJECT (reminders), properties[PROP_EMPTY]);

	reminders_widget_maybe_schedule_overdue_update (reminders);
}

static gint
reminders_widget_invert_tree_path_compare (gconstpointer ptr1,
					   gconstpointer ptr2)
{
	return (-1) * gtk_tree_path_compare (ptr1, ptr2);
}

static void
reminders_widget_select_one_of (ERemindersWidget *reminders,
				GList **inout_previous_paths) /* GtkTreePath * */
{
	GList *link;
	guint len;
	gint to_select = -1;
	gint n_rows;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (!inout_previous_paths || !*inout_previous_paths)
		return;

	n_rows = gtk_tree_model_iter_n_children (gtk_tree_view_get_model (reminders->priv->tree_view), NULL);
	if (n_rows <= 0)
		return;

	*inout_previous_paths = g_list_sort (*inout_previous_paths, reminders_widget_invert_tree_path_compare);

	len = g_list_length (*inout_previous_paths);

	for (link = *inout_previous_paths; link && to_select == -1; link = g_list_next (link), len--) {
		GtkTreePath *path = link->data;
		gint *indices, index;

		if (!path || gtk_tree_path_get_depth (path) != 1)
			continue;

		indices = gtk_tree_path_get_indices (path);
		if (!indices)
			continue;

		index = indices[0] - len + 1;

		if (index >= n_rows)
			to_select = n_rows - 1;
		else
			to_select = index;
	}

	if (to_select >= 0 && to_select < n_rows) {
		GtkTreePath *path;

		path = gtk_tree_path_new_from_indices (to_select, -1);
		if (path) {
			gtk_tree_selection_select_path (gtk_tree_view_get_selection (reminders->priv->tree_view), path);
			gtk_tree_path_free (path);
		}
	}
}

static gboolean
reminders_widget_refresh_content_cb (gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	GList *previous_paths;
	GSList *past;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkListStore *list_store;

	if (g_source_is_destroyed (g_main_current_source ()))
		return FALSE;

	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), FALSE);

	reminders->priv->refresh_idle_id = 0;

	model = gtk_tree_view_get_model (reminders->priv->tree_view);
	if (!model)
		return FALSE;

	selection = gtk_tree_view_get_selection (reminders->priv->tree_view);
	previous_paths = gtk_tree_selection_get_selected_rows (selection, NULL);
	list_store = GTK_LIST_STORE (model);

	g_object_ref (model);
	gtk_tree_view_set_model (reminders->priv->tree_view, NULL);

	gtk_list_store_clear (list_store);

	past = e_reminder_watcher_dup_past (reminders->priv->watcher);
	if (past) {
		GSList *link;
		GtkTreeIter iter;

		past = g_slist_sort (past, reminders_sort_by_occur_reverse);
		for (link = past; link; link = g_slist_next (link)) {
			const EReminderData *rd = link->data;
			gchar *overdue = NULL, *description = NULL;

			if (!rd || !e_reminder_data_get_component (rd))
				continue;

			reminders_get_reminder_markups (reminders, rd, &overdue, &description);

			gtk_list_store_append (list_store, &iter);
			gtk_list_store_set (list_store, &iter,
				E_REMINDERS_WIDGET_COLUMN_OVERDUE, overdue,
				E_REMINDERS_WIDGET_COLUMN_DESCRIPTION, description,
				E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, rd,
				-1);

			g_free (description);
			g_free (overdue);
		}
	}

	gtk_tree_view_set_model (reminders->priv->tree_view, model);
	g_object_unref (model);

	reminders_widget_set_is_empty (reminders, !past);

	if (past) {
		GtkTreeViewColumn *column;

		column = gtk_tree_view_get_column (reminders->priv->tree_view, 0);
		if (column)
			gtk_tree_view_column_queue_resize (column);

		reminders_widget_select_one_of (reminders, &previous_paths);
	}

	g_list_free_full (previous_paths, (GDestroyNotify) gtk_tree_path_free);
	g_slist_free_full (past, e_reminder_data_free);

	/* Make sure there's always something selected */
	if (!gtk_tree_selection_count_selected_rows (selection)) {
		GtkTreePath *path;

		path = gtk_tree_path_new_first ();
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
	}

	g_signal_emit (reminders, signals[CHANGED], 0, NULL);

	return FALSE;
}

static void
reminders_widget_schedule_content_refresh (ERemindersWidget *reminders)
{
	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (!reminders->priv->refresh_idle_id) {
		reminders->priv->refresh_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
			reminders_widget_refresh_content_cb, reminders, NULL);
	}
}

static void
reminders_widget_watcher_changed_cb (EReminderWatcher *watcher,
				     gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	reminders_widget_schedule_content_refresh (reminders);
}

static void
reminders_widget_gather_selected_cb (GtkTreeModel *model,
				     GtkTreePath *path,
				     GtkTreeIter *iter,
				     gpointer user_data)
{
	GSList **inout_selected = user_data;
	EReminderData *rd = NULL;

	g_return_if_fail (inout_selected != NULL);

	gtk_tree_model_get (model, iter, E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, &rd, -1);

	if (rd)
		*inout_selected = g_slist_prepend (*inout_selected, rd);
}

static void
reminders_widget_do_dismiss_cb (ERemindersWidget *reminders,
				const EReminderData *rd,
				GString *gathered_errors,
				GCancellable *cancellable,
				gpointer user_data)
{
	GError *local_error = NULL;

	if (g_cancellable_is_cancelled (cancellable))
		return;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (rd != NULL);

	if (!e_reminder_watcher_dismiss_sync (reminders->priv->watcher, rd, cancellable, &local_error) && local_error && gathered_errors &&
	    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		if (gathered_errors->len)
			g_string_append_c (gathered_errors, '\n');
		g_string_append (gathered_errors, local_error->message);
	}

	g_clear_error (&local_error);
}

typedef void (* ForeachSelectedSyncFunc) (ERemindersWidget *reminders,
					  const EReminderData *rd,
					  GString *gathered_errors,
					  GCancellable *cancellable,
					  gpointer user_data);

typedef struct _ForeachSelectedData {
	GSList *selected; /* EReminderData * */
	ForeachSelectedSyncFunc sync_func;
	gpointer user_data;
	GDestroyNotify user_data_destroy;
	gchar *error_prefix;
} ForeachSelectedData;

static void
foreach_selected_data_free (gpointer ptr)
{
	ForeachSelectedData *fsd = ptr;

	if (fsd) {
		g_slist_free_full (fsd->selected, e_reminder_data_free);
		if (fsd->user_data_destroy)
			fsd->user_data_destroy (fsd->user_data);
		g_free (fsd->error_prefix);
		g_slice_free (ForeachSelectedData, fsd);
	}
}

static void
reminders_widget_foreach_selected_thread (GTask *task,
					  gpointer source_object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	ForeachSelectedData *fsd = task_data;
	GString *gathered_errors;
	GSList *link;

	g_return_if_fail (fsd != NULL);
	g_return_if_fail (fsd->selected != NULL);
	g_return_if_fail (fsd->sync_func != NULL);

	if (g_cancellable_is_cancelled (cancellable))
		return;

	gathered_errors = g_string_new ("");

	for (link = fsd->selected; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
		const EReminderData *rd = link->data;

		fsd->sync_func (source_object, rd, gathered_errors, cancellable, fsd->user_data);
	}

	if (gathered_errors->len) {
		if (fsd->error_prefix) {
			g_string_prepend_c (gathered_errors, '\n');
			g_string_prepend (gathered_errors, fsd->error_prefix);
		}

		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", gathered_errors->str);
	} else {
		g_task_return_boolean (task, TRUE);
	}

	g_string_free (gathered_errors, TRUE);
}

static void
reminders_widget_foreach_selected_done_cb (GObject *source_object,
					   GAsyncResult *result,
					   gpointer user_data)
{
	ERemindersWidget *reminders;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (source_object));

	reminders = E_REMINDERS_WIDGET (source_object);
	g_return_if_fail (g_task_is_valid (result, reminders));

	if (!g_task_propagate_boolean (G_TASK (result), &local_error) && local_error) {
		e_reminders_widget_report_error (reminders, NULL, local_error);
	}

	g_clear_error (&local_error);
}

static void
reminders_widget_foreach_selected (ERemindersWidget *reminders,
				   ForeachSelectedSyncFunc sync_func,
				   gpointer user_data,
				   GDestroyNotify user_data_destroy,
				   const gchar *error_prefix)
{
	GtkTreeSelection *selection;
	GSList *selected = NULL; /* EReminderData * */
	GTask *task;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (sync_func != NULL);

	selection = gtk_tree_view_get_selection (reminders->priv->tree_view);
	gtk_tree_selection_selected_foreach (selection, reminders_widget_gather_selected_cb, &selected);

	if (selected) {
		ForeachSelectedData *fsd;

		fsd = g_slice_new0 (ForeachSelectedData);
		fsd->selected = selected; /* Takes ownership */
		fsd->sync_func = sync_func;
		fsd->user_data = user_data;
		fsd->user_data_destroy = user_data_destroy;
		fsd->error_prefix = g_strdup (error_prefix);

		task = g_task_new (reminders, reminders->priv->cancellable, reminders_widget_foreach_selected_done_cb, NULL);
		g_task_set_task_data (task, fsd, foreach_selected_data_free);
		g_task_set_check_cancellable (task, FALSE);
		g_task_run_in_thread (task, reminders_widget_foreach_selected_thread);
		g_object_unref (task);
	}
}

static void
reminders_widget_row_activated_cb (GtkTreeView *tree_view,
				   GtkTreePath *path,
				   GtkTreeViewColumn *column,
				   gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (!path)
		return;

	model = gtk_tree_view_get_model (reminders->priv->tree_view);
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		EReminderData *rd = NULL;

		gtk_tree_model_get (model, &iter,
			E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, &rd,
			-1);

		if (rd) {
			gboolean result = FALSE;

			g_signal_emit (reminders, signals[ACTIVATED], 0, rd, &result);

			if (!result) {
				const gchar *scheme = NULL;
				const gchar *comp_uid = NULL;

				comp_uid = e_cal_component_get_uid (e_reminder_data_get_component (rd));

				switch (e_cal_component_get_vtype (e_reminder_data_get_component (rd))) {
					case E_CAL_COMPONENT_EVENT:
						scheme = "calendar:";
						break;
					case E_CAL_COMPONENT_TODO:
						scheme = "task:";
						break;
					case E_CAL_COMPONENT_JOURNAL:
						scheme = "memo:";
						break;
					default:
						break;
				}

				if (scheme && comp_uid && e_reminder_data_get_source_uid (rd)) {
					GString *uri;
					gchar *tmp;
					GError *error = NULL;

					uri = g_string_sized_new (128);
					g_string_append (uri, scheme);
					g_string_append (uri, "///?");

					tmp = g_uri_escape_string (e_reminder_data_get_source_uid (rd), NULL, TRUE);
					g_string_append (uri, "source-uid=");
					g_string_append (uri, tmp);
					g_free (tmp);

					g_string_append_c (uri, '&');

					tmp = g_uri_escape_string (comp_uid, NULL, TRUE);
					g_string_append (uri, "comp-uid=");
					g_string_append (uri, tmp);
					g_free (tmp);

					if (!g_app_info_launch_default_for_uri (uri->str, NULL, &error) &&
					    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
						gchar *prefix = g_strdup_printf (_("Failed to launch URI “%s”:"), uri->str);
						e_reminders_widget_report_error (reminders, prefix, error);
						g_free (prefix);
					}

					g_string_free (uri, TRUE);
					g_clear_error (&error);
				}
			}

			e_reminder_data_free (rd);
		}
	}
}

static void
reminders_widget_set_text_buffer_markup (GtkTextBuffer *buffer,
					 const gchar *markup)
{
	GtkTextIter start, end;

	g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));
	g_return_if_fail (markup != NULL);

	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	gtk_text_buffer_delete (buffer, &start, &end);

	gtk_text_buffer_get_start_iter (buffer, &start);

	gtk_text_buffer_insert_markup (buffer, &start, markup, -1);
}

static void
reminders_widget_update_content (ERemindersWidget *reminders,
				 GtkTreeSelection *selection,
				 gboolean only_sensitivity)
{
	gchar *markup = NULL;
	gint nselected;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (GTK_IS_TREE_SELECTION (selection));

	nselected = gtk_tree_selection_count_selected_rows (selection);
	gtk_widget_set_sensitive (reminders->priv->snooze_button, nselected > 0);
	gtk_widget_set_sensitive (reminders->priv->dismiss_button, nselected > 0);

	if (nselected == 0) {
		if (!only_sensitivity)
			markup = g_markup_printf_escaped ("<i>%s</i>", _("No reminder is selected."));
	} else if (nselected == 1) {
		GList *rows;
		GtkTreeIter iter;
		GtkTreeModel *model = NULL;

		rows = gtk_tree_selection_get_selected_rows (selection, &model);
		g_return_if_fail (rows != NULL);
		g_return_if_fail (model != NULL);

		if (gtk_tree_model_get_iter (model, &iter, rows->data)) {
			EReminderData *rd = NULL;
			gchar *description = NULL;

			if (only_sensitivity) {
				gtk_tree_model_get (model, &iter,
					E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, &rd,
					-1);
			} else {
				gtk_tree_model_get (model, &iter,
					E_REMINDERS_WIDGET_COLUMN_DESCRIPTION, &description,
					E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA, &rd,
					-1);
			}

			if (rd) {
				if (!only_sensitivity) {
					ECalComponent *comp;

					comp = e_reminder_data_get_component (rd);

					if (comp) {
						ICalComponent *icomp;
						ICalProperty *prop;

						icomp = e_cal_component_get_icalcomponent (comp);
						prop = icomp ? e_cal_util_component_find_property_for_locale (icomp, I_CAL_DESCRIPTION_PROPERTY, NULL) : NULL;

						if (prop) {
							const gchar *icomp_description;

							icomp_description = i_cal_property_get_description (prop);

							if (icomp_description && *icomp_description) {
								gchar *tmp;

								tmp = g_markup_escape_text (icomp_description, -1);

								markup = g_strconcat (description, "\n\n<tt>", tmp, "</tt>", NULL);

								g_free (tmp);
							}

							g_clear_object (&prop);
						}
					}

					if (!markup) {
						markup = description;
						description = NULL;
					}
				}
			}

			e_reminder_data_free (rd);
			g_free (description);
		}

		if (!markup && !only_sensitivity)
			markup = g_markup_printf_escaped ("<i>%s</i>", _("No details are available."));

		g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
	} else if (!only_sensitivity) {
		markup = g_markup_printf_escaped ("<i>%s</i>", _("Multiple reminders are selected."));
	}

	if (!only_sensitivity) {
		reminders_widget_set_text_buffer_markup (gtk_text_view_get_buffer (reminders->priv->details_text_view), markup);

		e_buffer_tagger_update_tags (reminders->priv->details_text_view);
	}

	g_free (markup);
}

static void
reminders_widget_selection_changed_cb (GtkTreeSelection *selection,
				       gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (GTK_IS_TREE_SELECTION (selection));
	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	reminders_widget_update_content (reminders, selection, FALSE);
}

static void
reminders_widget_dismiss_button_clicked_cb (GtkButton *button,
					    gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	g_signal_handlers_block_by_func (reminders->priv->watcher, reminders_widget_watcher_changed_cb, reminders);

	reminders_widget_foreach_selected (reminders, reminders_widget_do_dismiss_cb, NULL, NULL, _("Failed to dismiss reminder:"));

	g_signal_handlers_unblock_by_func (reminders->priv->watcher, reminders_widget_watcher_changed_cb, reminders);

	reminders_widget_watcher_changed_cb (reminders->priv->watcher, reminders);
}

static void
reminders_widget_dismiss_all_done_cb (GObject *source_object,
				      GAsyncResult *result,
				      gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_REMINDER_WATCHER (source_object));

	if (!e_reminder_watcher_dismiss_all_finish (reminders->priv->watcher, result, &local_error) &&
	    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

		e_reminders_widget_report_error (reminders, _("Failed to dismiss all:"), local_error);
	}

	g_clear_error (&local_error);
}

static void
reminders_activate_dismiss_all (GSimpleAction *action,
				GVariant *param,
				gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	e_reminder_watcher_dismiss_all (reminders->priv->watcher, reminders->priv->cancellable,
		reminders_widget_dismiss_all_done_cb, reminders);
}

static void
reminders_widget_add_snooze_add_button_clicked_cb (GtkButton *button,
						   gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	SnoozeTime st = { SNOOZE_KIND_UNKNOWN, 0 };
	const gchar *kind_id;
	gboolean found = FALSE;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	st.minutes =
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_minutes_spin)) +
		(60 * gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_hours_spin))) +
		(24 * 60 * gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_days_spin)));
	g_return_if_fail (st.minutes > 0);

	gtk_widget_hide (reminders->priv->add_snooze_popover);

	kind_id = gtk_combo_box_get_active_id (GTK_COMBO_BOX (reminders->priv->add_snooze_kind_combo));
	if (g_strcmp0 (kind_id, "1") == 0) {
		st.kind = SNOOZE_KIND_FROM_START;
		st.minutes *= -1;
	} else if (g_strcmp0 (kind_id, "2") == 0) {
		st.kind = SNOOZE_KIND_FROM_START;
	} else {
		st.kind = SNOOZE_KIND_FROM_NOW;
	}

	found = g_hash_table_contains (reminders->priv->existing_snooze_times, &st);
	if (!found) {
		guint ii;

		for (ii = 0; !found && ii < G_N_ELEMENTS (predefined_snooze_times); ii++) {
			found = st.kind == predefined_snooze_times[ii].kind && st.minutes == predefined_snooze_times[ii].minutes;
		}
	}

	if (!found) {
		GPtrArray *to_save; /* gchar * */
		gchar **stored_values;
		gchar *new_value;
		guint ii;

		to_save = g_ptr_array_new_with_free_func (g_free);

		stored_values = g_settings_get_strv (reminders->priv->settings, "notify-custom-snooze-times");
		if (stored_values) {
			/* Skip the oldest, when too many stored */
			for (ii = g_strv_length (stored_values) >= MAX_CUSTOM_SNOOZE_VALUES ? 1 : 0; ii < MAX_CUSTOM_SNOOZE_VALUES && stored_values[ii]; ii++) {
				g_ptr_array_add (to_save, stored_values[ii]);
				stored_values[ii] = NULL;
			}

			g_strfreev (stored_values);
		}

		if (st.kind == SNOOZE_KIND_FROM_START && st.minutes > 0)
			new_value = g_strdup_printf ("*%d", st.minutes);
		else
			new_value = g_strdup_printf ("%d", st.minutes);

		/* Add the new at the end of the array */
		g_ptr_array_add (to_save, new_value);

		/* NULL-terminated */
		g_ptr_array_add (to_save, NULL);

		g_settings_set_strv (reminders->priv->settings, "notify-custom-snooze-times", (const gchar * const *) to_save->pdata);

		g_ptr_array_unref (to_save);
	}

	reminders_widget_fill_snooze_options (reminders, st.minutes, st.kind == SNOOZE_KIND_FROM_START);
	gtk_widget_activate (reminders->priv->snooze_button);
}

static void
reminders_widget_add_snooze_update_sensitize_cb (GtkSpinButton *spin,
						 gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	gtk_widget_set_sensitive (reminders->priv->add_snooze_add_button,
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_minutes_spin)) +
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_hours_spin)) +
		gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (reminders->priv->add_snooze_days_spin)) > 0);
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
gtk_paned_pack1 (GtkPaned *paned,
		 GtkWidget *widget,
		 gboolean resize,
		 gboolean shrink)
{
	gtk_paned_set_start_child (paned, widget);
	gtk_paned_set_resize_start_child (paned, resize);
	gtk_paned_set_shrink_start_child (paned, shrink);
}

static void
gtk_paned_pack2 (GtkPaned *paned,
		 GtkWidget *widget,
		 gboolean resize,
		 gboolean shrink)
{
	gtk_paned_set_end_child (paned, widget);
	gtk_paned_set_resize_end_child (paned, resize);
	gtk_paned_set_shrink_end_child (paned, shrink);
}
#endif /* GTK_CHECK_VERSION(4, 0, 0) */

static void
reminders_widget_snooze_add_custom (ERemindersWidget *reminders)
{
	GVariant *target;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (!reminders->priv->add_snooze_popover) {
		GtkWidget *widget;
		GtkBox *vbox, *box;

		reminders->priv->add_snooze_days_spin = gtk_spin_button_new_with_range (0.0, 366.0, 1.0);
		reminders->priv->add_snooze_hours_spin = gtk_spin_button_new_with_range (0.0, 23.0, 1.0);
		reminders->priv->add_snooze_minutes_spin = gtk_spin_button_new_with_range (0.0, 59.0, 1.0);

		g_object_set (G_OBJECT (reminders->priv->add_snooze_days_spin),
			"digits", 0,
			"numeric", TRUE,
			"snap-to-ticks", TRUE,
			NULL);

		g_object_set (G_OBJECT (reminders->priv->add_snooze_hours_spin),
			"digits", 0,
			"numeric", TRUE,
			"snap-to-ticks", TRUE,
			NULL);

		g_object_set (G_OBJECT (reminders->priv->add_snooze_minutes_spin),
			"digits", 0,
			"numeric", TRUE,
			"snap-to-ticks", TRUE,
			NULL);

		vbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 2));

		widget = gtk_label_new (_("Set a custom snooze time for"));
		_libedataserverui_box_pack_start (vbox, widget, FALSE, FALSE, 0);

		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2));
		g_object_set (G_OBJECT (box),
			"halign", GTK_ALIGN_START,
			"hexpand", FALSE,
			"valign", GTK_ALIGN_CENTER,
			"vexpand", FALSE,
			NULL);

		_libedataserverui_box_pack_start (box, reminders->priv->add_snooze_days_spin, FALSE, FALSE, 4);
		/* Translators: this is part of: "Set a custom snooze time for [nnn] days [nnn] hours [nnn] minutes", where the text in "[]" means a separate widget */
		widget = gtk_label_new_with_mnemonic (C_("reminders-snooze", "da_ys"));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), reminders->priv->add_snooze_days_spin);
		_libedataserverui_box_pack_start (box, widget, FALSE, FALSE, 4);

		_libedataserverui_box_pack_start (vbox, GTK_WIDGET (box), FALSE, FALSE, 0);

		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2));
		g_object_set (G_OBJECT (box),
			"halign", GTK_ALIGN_START,
			"hexpand", FALSE,
			"valign", GTK_ALIGN_CENTER,
			"vexpand", FALSE,
			NULL);

		_libedataserverui_box_pack_start (box, reminders->priv->add_snooze_hours_spin, FALSE, FALSE, 4);
		/* Translators: this is part of: "Set a custom snooze time for [nnn] days [nnn] hours [nnn] minutes", where the text in "[]" means a separate widget */
		widget = gtk_label_new_with_mnemonic (C_("reminders-snooze", "_hours"));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), reminders->priv->add_snooze_hours_spin);
		_libedataserverui_box_pack_start (box, widget, FALSE, FALSE, 4);

		_libedataserverui_box_pack_start (vbox, GTK_WIDGET (box), FALSE, FALSE, 0);

		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2));
		g_object_set (G_OBJECT (box),
			"halign", GTK_ALIGN_START,
			"hexpand", FALSE,
			"valign", GTK_ALIGN_CENTER,
			"vexpand", FALSE,
			NULL);

		_libedataserverui_box_pack_start (box, reminders->priv->add_snooze_minutes_spin, FALSE, FALSE, 4);
		/* Translators: this is part of: "Set a custom snooze time for [nnn] days [nnn] hours [nnn] minutes", where the text in "[]" means a separate widget */
		widget = gtk_label_new_with_mnemonic (C_("reminders-snooze", "_minutes"));
		gtk_label_set_mnemonic_widget (GTK_LABEL (widget), reminders->priv->add_snooze_minutes_spin);
		_libedataserverui_box_pack_start (box, widget, FALSE, FALSE, 4);

		_libedataserverui_box_pack_start (vbox, GTK_WIDGET (box), FALSE, FALSE, 0);

		reminders->priv->add_snooze_kind_combo = gtk_combo_box_text_new ();
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (reminders->priv->add_snooze_kind_combo), "0", _("from now"));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (reminders->priv->add_snooze_kind_combo), "1", _("before start"));
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (reminders->priv->add_snooze_kind_combo), "2", _("after start"));
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (reminders->priv->add_snooze_kind_combo), "0");

		_libedataserverui_box_pack_start (vbox, reminders->priv->add_snooze_kind_combo, FALSE, FALSE, 0);

		reminders->priv->add_snooze_add_button = gtk_button_new_with_mnemonic (_("_Add Snooze time"));
		g_object_set (G_OBJECT (reminders->priv->add_snooze_add_button),
			"halign", GTK_ALIGN_CENTER,
			NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
		gtk_box_append (vbox, reminders->priv->add_snooze_add_button);
#else
		_libedataserverui_box_pack_start (vbox, reminders->priv->add_snooze_add_button, FALSE, FALSE, 0);
		gtk_widget_show_all (GTK_WIDGET (vbox));
#endif

#if GTK_CHECK_VERSION(4, 0, 0)
		reminders->priv->add_snooze_popover = gtk_popover_new ();
		g_object_set (G_OBJECT (vbox),
			"margin-start", 6,
			"margin-end", 6,
			"margin-top", 6,
			"margin-bottom", 6,
			NULL);
		gtk_popover_set_child (GTK_POPOVER (reminders->priv->add_snooze_popover), GTK_WIDGET (vbox));
#else
		reminders->priv->add_snooze_popover = gtk_popover_new (GTK_WIDGET (reminders));
		gtk_container_add (GTK_CONTAINER (reminders->priv->add_snooze_popover), GTK_WIDGET (vbox));
		gtk_container_set_border_width (GTK_CONTAINER (reminders->priv->add_snooze_popover), 6);
#endif
		gtk_popover_set_position (GTK_POPOVER (reminders->priv->add_snooze_popover), GTK_POS_BOTTOM);

		g_signal_connect (reminders->priv->add_snooze_add_button, "clicked",
			G_CALLBACK (reminders_widget_add_snooze_add_button_clicked_cb), reminders);

		g_signal_connect (reminders->priv->add_snooze_days_spin, "value-changed",
			G_CALLBACK (reminders_widget_add_snooze_update_sensitize_cb), reminders);

		g_signal_connect (reminders->priv->add_snooze_hours_spin, "value-changed",
			G_CALLBACK (reminders_widget_add_snooze_update_sensitize_cb), reminders);

		g_signal_connect (reminders->priv->add_snooze_minutes_spin, "value-changed",
			G_CALLBACK (reminders_widget_add_snooze_update_sensitize_cb), reminders);

		reminders_widget_add_snooze_update_sensitize_cb (NULL, reminders);
	}

	target = gtk_actionable_get_action_target_value (GTK_ACTIONABLE (reminders->priv->snooze_button));
	if (target) {
		gint ikind = 0, minutes = 0;

		g_variant_get (target, "(ii)", &ikind, &minutes);

		if (minutes < 0) {
			minutes *= -1;
			gtk_combo_box_set_active_id (GTK_COMBO_BOX (reminders->priv->add_snooze_kind_combo), "1");
		} else if (ikind == SNOOZE_KIND_FROM_START) {
			gtk_combo_box_set_active_id (GTK_COMBO_BOX (reminders->priv->add_snooze_kind_combo), "2");
		} else {
			gtk_combo_box_set_active_id (GTK_COMBO_BOX (reminders->priv->add_snooze_kind_combo), "0");
		}

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (reminders->priv->add_snooze_minutes_spin), minutes % 60);

		minutes = minutes / 60;
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (reminders->priv->add_snooze_hours_spin), minutes % 24);

		minutes = minutes / 24;
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (reminders->priv->add_snooze_days_spin), minutes);
	}

	gtk_widget_hide (reminders->priv->add_snooze_popover);
#if GTK_CHECK_VERSION(4, 0, 0)
	if (gtk_widget_get_parent (GTK_WIDGET (reminders->priv->add_snooze_popover)) == NULL)
		gtk_widget_set_parent (GTK_WIDGET (reminders->priv->add_snooze_popover), reminders->priv->snooze_button);
#else
	gtk_popover_set_relative_to (GTK_POPOVER (reminders->priv->add_snooze_popover), reminders->priv->snooze_button);
#endif
	gtk_widget_show (reminders->priv->add_snooze_popover);

	gtk_widget_grab_focus (reminders->priv->add_snooze_days_spin);
}

static void
reminders_activate_snooze (GSimpleAction *action,
			   GVariant *param,
			   gpointer user_data)
{
	ERemindersWidget *reminders = user_data;
	gint kind = SNOOZE_KIND_UNKNOWN, minutes = 0;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (param != NULL);

	g_variant_get (param, "(ii)", &kind, &minutes);

	if (kind == SNOOZE_KIND_FROM_NOW || kind == SNOOZE_KIND_FROM_START) {
		GtkTreeSelection *selection;
		GSList *selected = NULL, *link;
		gint64 until = 0;

		reminders_widget_update_snooze_button (reminders, kind, minutes);

		if (minutes < 0)
			kind = SNOOZE_KIND_FROM_START;

		if (kind == SNOOZE_KIND_FROM_NOW)
			until = (g_get_real_time () / G_USEC_PER_SEC) + (minutes * 60);

		g_settings_set_int (reminders->priv->settings, "notify-last-snooze-minutes", minutes);
		g_settings_set_boolean (reminders->priv->settings, "notify-last-snooze-from-start", kind == SNOOZE_KIND_FROM_START);

		selection = gtk_tree_view_get_selection (reminders->priv->tree_view);
		gtk_tree_selection_selected_foreach (selection, reminders_widget_gather_selected_cb, &selected);

		g_signal_handlers_block_by_func (reminders->priv->watcher, reminders_widget_watcher_changed_cb, reminders);

		for (link = selected; link; link = g_slist_next (link)) {
			const EReminderData *rd = link->data;

			if (kind == SNOOZE_KIND_FROM_START) {
				until = e_cal_component_alarm_instance_get_occur_start (e_reminder_data_get_instance (rd));
				until = until - (minutes * 60);
			}

			e_reminder_watcher_snooze (reminders->priv->watcher, rd, until);
		}

		g_slist_free_full (selected, e_reminder_data_free);

		g_signal_handlers_unblock_by_func (reminders->priv->watcher, reminders_widget_watcher_changed_cb, reminders);

		if (selected)
			reminders_widget_watcher_changed_cb (reminders->priv->watcher, reminders);

	} else if (kind == SNOOZE_KIND_ADD_CUSTOM_TIME) {
		reminders_widget_snooze_add_custom (reminders);
	} else if (kind == SNOOZE_KIND_CLEAR_CUSTOM_TIMES) {
		GVariant *target;

		g_settings_reset (reminders->priv->settings, "notify-custom-snooze-times");

		/* when the current value is not one of those preselcted, then reset it to "for 5 minutes" */
		target = gtk_actionable_get_action_target_value (GTK_ACTIONABLE (reminders->priv->snooze_button));
		if (target) {
			guint ii;

			kind = SNOOZE_KIND_UNKNOWN;
			minutes = 0;

			g_variant_get (target, "(ii)", &kind, &minutes);

			for (ii = 0; ii < G_N_ELEMENTS (predefined_snooze_times); ii++) {
				if (kind == predefined_snooze_times[ii].kind && minutes == predefined_snooze_times[ii].minutes)
					break;
			}

			/* no match in the predefined_snooze_times[] had been found */
			if (ii >= G_N_ELEMENTS (predefined_snooze_times)) {
				g_settings_set_int (reminders->priv->settings, "notify-last-snooze-minutes", 5);
				g_settings_set_boolean (reminders->priv->settings, "notify-last-snooze-from-start", FALSE);
				reminders_widget_update_snooze_button (reminders, SNOOZE_KIND_FROM_NOW, 5);
			}
		}
	}
}

static GtkWidget * /* (transfer-full): the box */
reminders_widget_create_split_button (ERemindersWidget *self,
				      const gchar *label_text,
				      GMenu *menu,
				      GtkWidget **out_button)
{
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *widget;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	widget = gtk_button_new ();

	label = gtk_label_new_with_mnemonic (label_text);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_button_set_child (GTK_BUTTON (widget), label);
#else
	gtk_container_add (GTK_CONTAINER (widget), label);
#endif

	_libedataserverui_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	*out_button = widget;

	widget = gtk_menu_button_new ();
#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_menu_button_set_use_popover (GTK_MENU_BUTTON (widget), FALSE);
#endif
	gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (widget), G_MENU_MODEL (menu));
	_libedataserverui_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	gtk_style_context_add_class (gtk_widget_get_style_context (box), "linked");

	e_binding_bind_property (
		*out_button, "sensitive",
		widget, "sensitive",
		G_BINDING_SYNC_CREATE);

	return box;
}

static void
reminders_widget_set_watcher (ERemindersWidget *reminders,
			      EReminderWatcher *watcher)
{
	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));
	g_return_if_fail (E_IS_REMINDER_WATCHER (watcher));
	g_return_if_fail (reminders->priv->watcher == NULL);

	reminders->priv->watcher = g_object_ref (watcher);
}

static void
reminders_widget_set_property (GObject *object,
			       guint property_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_WATCHER:
			reminders_widget_set_watcher (
				E_REMINDERS_WIDGET (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
reminders_widget_get_property (GObject *object,
			       guint property_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_WATCHER:
			g_value_set_object (
				value, e_reminders_widget_get_watcher (
				E_REMINDERS_WIDGET (object)));
			return;

		case PROP_EMPTY:
			g_value_set_boolean (
				value, e_reminders_widget_is_empty (
				E_REMINDERS_WIDGET (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
reminders_widget_constructed (GObject *object)
{
	const GActionEntry actions[] = {
		{ "dismiss-all", reminders_activate_dismiss_all, NULL, NULL, NULL },
		{ "snooze", reminders_activate_snooze, "(ii)", NULL, NULL }
	};
	ERemindersWidget *reminders = E_REMINDERS_WIDGET (object);
	GSimpleActionGroup *action_group;
	GtkWidget *scrolled_window;
	GtkListStore *list_store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkWidget *widget;
	GtkCssProvider *css_provider;
	GtkFlowBox *flow_box;
	GMenu *menu;
	GError *error = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_reminders_widget_parent_class)->constructed (object);

	reminders->priv->paned = GTK_PANED (gtk_paned_new (GTK_ORIENTATION_VERTICAL));

	gtk_grid_attach (GTK_GRID (reminders), GTK_WIDGET (reminders->priv->paned), 0, 0, 1, 1);

#if GTK_CHECK_VERSION(4, 0, 0)
	scrolled_window = gtk_scrolled_window_new ();
#else
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
#endif
	g_object_set (G_OBJECT (scrolled_window),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"hscrollbar-policy", GTK_POLICY_NEVER,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
#if GTK_CHECK_VERSION(4, 0, 0)
		"has-frame", TRUE,
#else
		"shadow-type", GTK_SHADOW_IN,
#endif
		NULL);

	gtk_paned_pack1 (reminders->priv->paned, scrolled_window, FALSE, FALSE);

	list_store = gtk_list_store_new (E_REMINDERS_WIDGET_N_COLUMNS,
		G_TYPE_STRING, /* E_REMINDERS_WIDGET_COLUMN_OVERDUE */
		G_TYPE_STRING, /* E_REMINDERS_WIDGET_COLUMN_DESCRIPTION */
		E_TYPE_REMINDER_DATA); /* E_REMINDERS_WIDGET_COLUMN_REMINDER_DATA */

	reminders->priv->tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (list_store)));

	g_object_unref (list_store);

	g_object_set (G_OBJECT (reminders->priv->tree_view),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"activate-on-single-click", FALSE,
		"enable-search", FALSE,
		"fixed-height-mode", TRUE,
		"headers-visible", FALSE,
		"hover-selection", FALSE,
		NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), GTK_WIDGET (reminders->priv->tree_view));
#else
	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (reminders->priv->tree_view));
#endif

	e_binding_bind_property (reminders, "empty",
		scrolled_window, "sensitive",
		G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

	gtk_tree_view_set_tooltip_column (reminders->priv->tree_view, E_REMINDERS_WIDGET_COLUMN_DESCRIPTION);

	/* Headers not visible, thus column's caption is not localized */
	gtk_tree_view_insert_column_with_attributes (reminders->priv->tree_view, -1, "Overdue",
		gtk_cell_renderer_text_new (), "markup", E_REMINDERS_WIDGET_COLUMN_OVERDUE, NULL);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (renderer),
		"ellipsize", PANGO_ELLIPSIZE_END,
		NULL);

	gtk_tree_view_insert_column_with_attributes (reminders->priv->tree_view, -1, "Description",
		renderer, "markup", E_REMINDERS_WIDGET_COLUMN_DESCRIPTION, NULL);

	column = gtk_tree_view_get_column (reminders->priv->tree_view, 0);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);

	column = gtk_tree_view_get_column (reminders->priv->tree_view, 1);
	gtk_tree_view_column_set_expand (column, TRUE);

#if GTK_CHECK_VERSION(4, 0, 0)
	scrolled_window = gtk_scrolled_window_new ();
#else
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
#endif
	g_object_set (G_OBJECT (scrolled_window),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"hscrollbar-policy", GTK_POLICY_NEVER,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
#if GTK_CHECK_VERSION(4, 0, 0)
		"has-frame", TRUE,
#else
		"shadow-type", GTK_SHADOW_IN,
#endif
		NULL);

	gtk_paned_pack2 (reminders->priv->paned, scrolled_window, TRUE, FALSE);

	reminders->priv->details_text_view = GTK_TEXT_VIEW (gtk_text_view_new ());

	g_object_set (G_OBJECT (reminders->priv->details_text_view),
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"editable", FALSE,
		"wrap-mode", GTK_WRAP_WORD_CHAR,
		"margin-start", 6,
		"margin-end", 6,
		"margin-top", 6,
		"margin-bottom", 6,
		NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), GTK_WIDGET (reminders->priv->details_text_view));
	gtk_widget_add_css_class (scrolled_window, "view");
#else
	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (reminders->priv->details_text_view));
	gtk_style_context_add_class (gtk_widget_get_style_context (scrolled_window), "view");
#endif

	e_buffer_tagger_connect (reminders->priv->details_text_view);

	flow_box = GTK_FLOW_BOX (gtk_flow_box_new ());
	g_object_set (G_OBJECT (flow_box),
		"homogeneous", FALSE,
		"selection-mode", GTK_SELECTION_NONE,
		"max-children-per-line", 10000, /* to glue the buttons without extra space between them */
		"column-spacing", 4,
		"row-spacing", 4,
		"margin-start", 6,
		"margin-top", 6,
		"margin-end", 6,
		"margin-bottom", 6,
		NULL);

	menu = g_menu_new ();
	g_menu_append (menu, _("Dismiss _All"), "reminders.dismiss-all");

	action_group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (action_group), actions, G_N_ELEMENTS (actions), reminders);

	widget = reminders_widget_create_split_button (reminders, _("_Dismiss"), menu,
		&reminders->priv->dismiss_button);

	g_clear_object (&menu);

	gtk_widget_insert_action_group (widget, "reminders", G_ACTION_GROUP (action_group));

	g_signal_connect (reminders->priv->dismiss_button, "clicked",
		G_CALLBACK (reminders_widget_dismiss_button_clicked_cb), reminders);

	gtk_flow_box_insert (flow_box, widget, -1);

	reminders->priv->snooze_menu = g_menu_new ();

	/* do not localize, the text is replaced with actual value in the reminders_widget_fill_snooze_options() */
	widget = reminders_widget_create_split_button (reminders, "Snooze", reminders->priv->snooze_menu,
		&reminders->priv->snooze_button);

	gtk_widget_insert_action_group (widget, "reminders", G_ACTION_GROUP (action_group));

	reminders_widget_fill_snooze_options (reminders,
		g_settings_get_int (reminders->priv->settings, "notify-last-snooze-minutes"),
		g_settings_get_boolean (reminders->priv->settings, "notify-last-snooze-from-start"));

	gtk_flow_box_insert (flow_box, widget, -1);

	g_clear_object (&action_group);

	gtk_actionable_set_action_name (GTK_ACTIONABLE (reminders->priv->snooze_button), "reminders.snooze");

	gtk_grid_attach (GTK_GRID (reminders), GTK_WIDGET (flow_box), 0, 1, 1, 1);

	css_provider = gtk_css_provider_new ();

	gtk_css_provider_load_from_data (css_provider, "flowboxchild { padding: 0px; }", -1
		#if !GTK_CHECK_VERSION(4, 0, 0)
		, &error
		#endif
		);
	if (!error) {
		GtkFlowBoxChild *child;
		guint ii = 0;

		while (child = gtk_flow_box_get_child_at_index (flow_box, ii), child) {
			gtk_style_context_add_provider (
				gtk_widget_get_style_context (GTK_WIDGET (child)),
					GTK_STYLE_PROVIDER (css_provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
			ii++;
		}
	} else {
		g_warning ("%s: Failed to parse CSS: %s", G_STRFUNC, error ? error->message : "Unknown error");
	}

	g_clear_object (&css_provider);
	g_clear_error (&error);

#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show_all (GTK_WIDGET (reminders));
#endif

	selection = gtk_tree_view_get_selection (reminders->priv->tree_view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect (reminders->priv->tree_view, "row-activated",
		G_CALLBACK (reminders_widget_row_activated_cb), reminders);

	g_signal_connect (selection, "changed",
		G_CALLBACK (reminders_widget_selection_changed_cb), reminders);

	g_signal_connect (reminders->priv->watcher, "changed",
		G_CALLBACK (reminders_widget_watcher_changed_cb), reminders);

	g_signal_connect (reminders->priv->settings, "changed::notify-custom-snooze-times",
		G_CALLBACK (reminders_widget_custom_snooze_minutes_changed_cb), reminders);

	_libedataserverui_load_modules ();

	e_extensible_load_extensions (E_EXTENSIBLE (object));

	reminders_widget_schedule_content_refresh (reminders);
}

static void
reminders_widget_dispose (GObject *object)
{
	ERemindersWidget *reminders = E_REMINDERS_WIDGET (object);

	g_cancellable_cancel (reminders->priv->cancellable);

	if (reminders->priv->refresh_idle_id) {
		g_source_remove (reminders->priv->refresh_idle_id);
		reminders->priv->refresh_idle_id = 0;
	}

	if (reminders->priv->overdue_update_id) {
		g_source_remove (reminders->priv->overdue_update_id);
		reminders->priv->overdue_update_id = 0;
	}

	if (reminders->priv->watcher)
		g_signal_handlers_disconnect_by_data (reminders->priv->watcher, reminders);

	if (reminders->priv->settings)
		g_signal_handlers_disconnect_by_data (reminders->priv->settings, reminders);

#if GTK_CHECK_VERSION(4, 0, 0)
	if (reminders->priv->add_snooze_popover)
		gtk_widget_unparent (reminders->priv->add_snooze_popover);
#endif

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_reminders_widget_parent_class)->dispose (object);
}

static void
reminders_widget_finalize (GObject *object)
{
	ERemindersWidget *reminders = E_REMINDERS_WIDGET (object);

	g_clear_object (&reminders->priv->snooze_menu);
	g_clear_object (&reminders->priv->watcher);
	g_clear_object (&reminders->priv->settings);
	g_clear_object (&reminders->priv->cancellable);
	g_clear_pointer (&reminders->priv->existing_snooze_times, g_hash_table_unref);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_reminders_widget_parent_class)->finalize (object);
}

static void
e_reminders_widget_class_init (ERemindersWidgetClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = reminders_widget_set_property;
	object_class->get_property = reminders_widget_get_property;
	object_class->constructed = reminders_widget_constructed;
	object_class->dispose = reminders_widget_dispose;
	object_class->finalize = reminders_widget_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->map = reminders_widget_map;
	widget_class->unmap = reminders_widget_unmap;

	/**
	 * ERemindersWidget::watcher:
	 *
	 * An #EReminderWatcher used to work with reminders.
	 *
	 * Since: 3.30
	 **/
	properties[PROP_WATCHER] =
		g_param_spec_object (
			"watcher",
			NULL, NULL,
			E_TYPE_REMINDER_WATCHER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * ERemindersWidget::empty:
	 *
	 * Set to %TRUE when there's no past reminder in the widget.
	 *
	 * Since: 3.30
	 **/
	properties[PROP_EMPTY] =
		g_param_spec_boolean (
			"empty",
			NULL, NULL,
			TRUE,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * ERemindersWidget:changed:
	 * @reminders: an #ERemindersWidget
	 *
	 * A signal being called to notify about changes in the past reminders list.
	 *
	 * Since: 3.30
	 **/
	signals[CHANGED] = g_signal_new (
		"changed",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (ERemindersWidgetClass, changed),
		NULL,
		NULL,
		g_cclosure_marshal_generic,
		G_TYPE_NONE, 0,
		G_TYPE_NONE);

	/**
	 * ERemindersWidget:activated:
	 * @reminders: an #ERemindersWidget
	 * @rd: an #EReminderData
	 *
	 * A signal being called when the user activates one of the past reminders in the tree view.
	 * The @rd corresponds to the activated reminder.
	 *
	 * Returns: %TRUE, when the further processing of this signal should be stopped, %FALSE otherwise.
	 *
	 * Since: 3.30
	 **/
	signals[ACTIVATED] = g_signal_new (
		"activated",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (ERemindersWidgetClass, activated),
		g_signal_accumulator_first_wins,
		NULL,
		g_cclosure_marshal_generic,
		G_TYPE_BOOLEAN, 1,
		E_TYPE_REMINDER_DATA);
}

static void
e_reminders_widget_init (ERemindersWidget *reminders)
{
	reminders->priv = e_reminders_widget_get_instance_private (reminders);
	reminders->priv->settings = g_settings_new ("org.gnome.evolution-data-server.calendar");
	reminders->priv->cancellable = g_cancellable_new ();
	reminders->priv->is_empty = TRUE;
	reminders->priv->is_mapped = FALSE;
	reminders->priv->existing_snooze_times = g_hash_table_new_full (snooze_time_hash, snooze_time_equal, g_free, NULL);
}

/**
 * e_reminders_widget_new:
 * @watcher: an #EReminderWatcher
 *
 * Creates a new instance of #ERemindersWidget. It adds its own reference
 * on the @watcher.
 *
 * Returns: (transfer full): a new instance of #ERemindersWidget.
 *
 * Since: 3.30
 **/
ERemindersWidget *
e_reminders_widget_new (EReminderWatcher *watcher)
{
	g_return_val_if_fail (E_IS_REMINDER_WATCHER (watcher), NULL);

	return g_object_new (E_TYPE_REMINDERS_WIDGET,
		"watcher", watcher,
		NULL);
}

/**
 * e_reminders_widget_get_watcher:
 * @reminders: an #ERemindersWidget
 *
 * Returns: (transfer none): an #EReminderWatcher with which the @reminders had
 *    been created. Do on unref it, it's owned by the @reminders.
 *
 * Since: 3.30
 **/
EReminderWatcher *
e_reminders_widget_get_watcher (ERemindersWidget *reminders)
{
	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), NULL);

	return reminders->priv->watcher;
}

/**
 * e_reminders_widget_get_settings:
 * @reminders: an #ERemindersWidget
 *
 * Returns: (transfer none): a #GSettings pointing to org.gnome.evolution-data-server.calendar
 *    used by the @reminders widget.
 *
 * Since: 3.30
 **/
GSettings *
e_reminders_widget_get_settings (ERemindersWidget *reminders)
{
	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), NULL);

	return reminders->priv->settings;
}

/**
 * e_reminders_widget_is_empty:
 * @reminders: an #ERemindersWidget
 *
 * Returns: %TRUE, when there is no past reminder left, %FALSE otherwise.
 *
 * Since: 3.30
 **/
gboolean
e_reminders_widget_is_empty (ERemindersWidget *reminders)
{
	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), FALSE);

	return reminders->priv->is_empty;
}

/**
 * e_reminders_widget_get_tree_view:
 * @reminders: an #ERemindersWidget
 *
 * Returns: (transfer none): a #GtkTreeView with past reminders. It's owned
 *    by the @reminders widget.
 *
 * Since: 3.30
 **/
GtkTreeView *
e_reminders_widget_get_tree_view (ERemindersWidget *reminders)
{
	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), NULL);

	return reminders->priv->tree_view;
}

/**
 * e_reminders_widget_get_paned:
 * @reminders: an #ERemindersWidget
 *
 * Returns: (transfer none): a #GtkPaned used to split list of events and
 *    the description of the reminders. It's owned by the @reminders widget.
 *
 * Since: 3.38
 **/
GtkPaned *
e_reminders_widget_get_paned (ERemindersWidget *reminders)
{
	g_return_val_if_fail (E_IS_REMINDERS_WIDGET (reminders), NULL);

	return reminders->priv->paned;
}

static void
reminders_widget_error_response_cb (GtkInfoBar *info_bar,
				    gint response_id,
				    gpointer user_data)
{
	ERemindersWidget *reminders = user_data;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (reminders->priv->info_bar == info_bar) {
		g_object_unref (reminders->priv->info_bar);
		reminders->priv->info_bar = NULL;
	}
}

/**
 * e_reminders_widget_report_error:
 * @reminders: an #ERemindersWidget
 * @prefix: (nullable): an optional prefix to show before the error message, or %NULL for none
 * @error: (nullable): a #GError to show the message from in the UI, or %NULL for unknown error
 *
 * Shows a warning in the GUI with the @error message, optionally prefixed
 * with @prefix. When @error is %NULL, an "Unknown error" message is shown
 * instead.
 *
 * Since: 3.30
 **/
void
e_reminders_widget_report_error (ERemindersWidget *reminders,
				 const gchar *prefix,
				 const GError *error)
{
	GtkLabel *label;
	const gchar *message;
	gchar *tmp = NULL;

	g_return_if_fail (E_IS_REMINDERS_WIDGET (reminders));

	if (error)
		message = error->message;
	else
		message = _("Unknown error");

	if (prefix && *prefix) {
		if (gtk_widget_get_direction (GTK_WIDGET (reminders)) == GTK_TEXT_DIR_RTL)
			tmp = g_strconcat (message, " ", prefix, NULL);
		else
			tmp = g_strconcat (prefix, " ", message, NULL);

		message = tmp;
	}

	if (reminders->priv->info_bar) {
		g_object_unref (reminders->priv->info_bar);
		reminders->priv->info_bar = NULL;
	}

	reminders->priv->info_bar = GTK_INFO_BAR (gtk_info_bar_new ());
	gtk_info_bar_set_message_type (reminders->priv->info_bar, GTK_MESSAGE_ERROR);
	gtk_info_bar_set_show_close_button (reminders->priv->info_bar, TRUE);

	label = GTK_LABEL (gtk_label_new (message));
	gtk_label_set_width_chars (label, 20);
	gtk_label_set_max_width_chars (label, 120);
	gtk_label_set_selectable (label, TRUE);
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_label_set_wrap (label, TRUE);
	gtk_info_bar_add_child (GTK_INFO_BAR (reminders->priv->info_bar), GTK_WIDGET (label));
#else
	gtk_label_set_line_wrap (label, TRUE);
	gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (reminders->priv->info_bar)), GTK_WIDGET (label));
	gtk_widget_show (GTK_WIDGET (label));
	gtk_widget_show (GTK_WIDGET (reminders->priv->info_bar));
#endif

	g_signal_connect (reminders->priv->info_bar, "response", G_CALLBACK (reminders_widget_error_response_cb), reminders);

	gtk_grid_attach (GTK_GRID (reminders), GTK_WIDGET (reminders->priv->info_bar), 0, 2, 1, 1);

	g_free (tmp);
}
