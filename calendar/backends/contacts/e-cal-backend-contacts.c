/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2003 Gergõ Érdi
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 *          Gergõ Érdi <cactus@cactus.rulez.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-contacts.h"

#include <glib/gi18n-lib.h>
#include <gconf/gconf-client.h>
#include "libedataserver/e-xml-hash-utils.h"
#include "libedataserver/e-flag.h"
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include <libebook/e-book.h>

#include "libedataserver/e-source-list.h"

G_DEFINE_TYPE (ECalBackendContacts, e_cal_backend_contacts, E_TYPE_CAL_BACKEND_SYNC)

static ECalBackendSyncClass *parent_class;

typedef enum
{
	CAL_DAYS,
	CAL_HOURS,
	CAL_MINUTES
} CalUnits;

/* Private part of the ECalBackendContacts structure */
struct _ECalBackendContactsPrivate {
        ESourceList  *addressbook_sources;

        GHashTable   *addressbooks;       /* UID -> BookRecord */
        gboolean      addressbook_loaded;

        EBookView    *book_view;
        GHashTable   *tracked_contacts;   /* UID -> ContactRecord */

	GHashTable *zones;
	icaltimezone *default_zone;

	EFlag   *init_done_flag; /* is set, when the init thread gone */

	/* properties related to track alarm settings for this backend */
	GConfClient *conf_client;
	guint notifyid1;
	guint notifyid2;
	guint notifyid3;
	guint update_alarms_id;
	gboolean alarm_enabled;
	gint alarm_interval;
	CalUnits alarm_units;
};

typedef struct _BookRecord {
	ECalBackendContacts *cbc;
        EBook     *book;
        EBookView *book_view;
} BookRecord;

typedef struct _ContactRecord {
        ECalBackendContacts *cbc;
	EBook               *book; /* where it comes from */
        EContact            *contact;
        ECalComponent       *comp_birthday, *comp_anniversary;
} ContactRecord;

#define d(x)

#define ANNIVERSARY_UID_EXT "-anniversary"
#define BIRTHDAY_UID_EXT   "-birthday"

static ECalComponent * create_birthday (ECalBackendContacts *cbc, EContact *contact);
static ECalComponent * create_anniversary (ECalBackendContacts *cbc, EContact *contact);

static void contacts_changed_cb (EBookView *book_view, const GList *contacts, gpointer user_data);
static void contacts_added_cb   (EBookView *book_view, const GList *contacts, gpointer user_data);
static void contacts_removed_cb (EBookView *book_view, const GList *contact_ids, gpointer user_data);
static ECalBackendSyncStatus e_cal_backend_contacts_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj);
static void setup_alarm (ECalBackendContacts *cbc, ECalComponent *comp);

/* BookRecord methods */
static BookRecord *
book_record_new (ECalBackendContacts *cbc, ESource *source)
{
        EBook      *book;
        GList      *fields = NULL;
        EBookQuery *query;
        EBookView  *book_view;
        BookRecord *br;
	GError     *error = NULL;

	book = e_book_new (source, NULL);
        if (!book || !e_book_open (book, TRUE, &error) || error) {
		g_object_unref (book);
		if (error) {
			g_warning ("%s: Failed to open book, error: %s", G_STRFUNC, error->message);
			g_error_free (error);
		}
		return NULL;
	}

        /* Create book view */
        fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_FILE_AS));
        fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_BIRTH_DATE));
        fields = g_list_append (fields, (gchar *)e_contact_field_name (E_CONTACT_ANNIVERSARY));
        query = e_book_query_any_field_contains ("");

        if (!e_book_get_book_view (book, query, fields, -1, &book_view, NULL)) {
                e_book_query_unref (query);
                g_object_unref (book);
                g_list_free (fields);
                return NULL;
        }
        e_book_query_unref (query);
	g_list_free (fields);

        g_signal_connect (book_view, "contacts_added", G_CALLBACK (contacts_added_cb), cbc);
        g_signal_connect (book_view, "contacts_removed", G_CALLBACK (contacts_removed_cb), cbc);
        g_signal_connect (book_view, "contacts_changed", G_CALLBACK (contacts_changed_cb), cbc);

        e_book_view_start (book_view);

        br = g_new (BookRecord, 1);
	br->cbc = cbc;
        br->book = book;
        br->book_view = book_view;

        return br;
}

static gboolean
remove_by_book (gpointer key, gpointer value, gpointer user_data)
{
	ContactRecord *cr = value;
	EBook *book = user_data;

	return (cr && cr->book == book);
}

static void
book_record_free (BookRecord *br)
{
        if (!br)
                return;

	g_hash_table_foreach_remove (br->cbc->priv->tracked_contacts, remove_by_book, br->book);
        g_object_unref (br->book_view);
        g_object_unref (br->book);

        g_free (br);
}

/* ContactRecord methods */
static ContactRecord *
contact_record_new (ECalBackendContacts *cbc, EBook *book, EContact *contact)
{
        ContactRecord *cr = g_new0 (ContactRecord, 1);
	gchar *comp_str;

        cr->cbc = cbc;
	cr->book = book;
        cr->contact = contact;
        cr->comp_birthday = create_birthday (cbc, contact);
        cr->comp_anniversary = create_anniversary (cbc, contact);

	if (cr->comp_birthday) {
		comp_str = e_cal_component_get_as_string (cr->comp_birthday);
		e_cal_backend_notify_object_created (E_CAL_BACKEND (cbc),
						     comp_str);
		g_free (comp_str);
	}

	if (cr->comp_anniversary) {

		comp_str = e_cal_component_get_as_string (cr->comp_anniversary);
		e_cal_backend_notify_object_created (E_CAL_BACKEND (cbc),
						     comp_str);
		g_free (comp_str);
	}

        g_object_ref (G_OBJECT (contact));

        return cr;
}

static void
contact_record_free (ContactRecord *cr)
{
        gchar *comp_str;
	ECalComponentId *id;

        g_object_unref (G_OBJECT (cr->contact));

	/* Remove the birthday event */
	if (cr->comp_birthday) {
		comp_str = e_cal_component_get_as_string (cr->comp_birthday);
		id = e_cal_component_get_id (cr->comp_birthday);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cr->cbc), id, comp_str, NULL);

		e_cal_component_free_id (id);
		g_free (comp_str);
		g_object_unref (G_OBJECT (cr->comp_birthday));
	}

	/* Remove the anniversary event */
	if (cr->comp_anniversary) {
		comp_str = e_cal_component_get_as_string (cr->comp_anniversary);
		id = e_cal_component_get_id (cr->comp_anniversary);

		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cr->cbc), id, comp_str, NULL);

		e_cal_component_free_id (id);
		g_free (comp_str);
		g_object_unref (G_OBJECT (cr->comp_anniversary));
	}

        g_free (cr);
}

/* ContactRecordCB methods */
typedef struct _ContactRecordCB {
        ECalBackendContacts *cbc;
        ECalBackendSExp     *sexp;
        GList               *result;
} ContactRecordCB;

static ContactRecordCB *
contact_record_cb_new (ECalBackendContacts *cbc, ECalBackendSExp *sexp)
{
        ContactRecordCB *cb_data = g_new (ContactRecordCB, 1);

        cb_data->cbc = cbc;
        cb_data->sexp = sexp;
        cb_data->result = NULL;

        return cb_data;
}

static void
contact_record_cb_free (ContactRecordCB *cb_data)
{
        g_list_foreach (cb_data->result, (GFunc) g_free, NULL);
        g_list_free (cb_data->result);

        g_free (cb_data);
}

static void
contact_record_cb (gpointer key, gpointer value, gpointer user_data)
{
        ContactRecordCB *cb_data = user_data;
        ContactRecord   *record = value;

        if (record->comp_birthday && e_cal_backend_sexp_match_comp (cb_data->sexp, record->comp_birthday, E_CAL_BACKEND (cb_data->cbc))) {
                gchar * comp_str = e_cal_component_get_as_string (record->comp_birthday);
                cb_data->result = g_list_append (cb_data->result, comp_str);
        }

        if (record->comp_anniversary && e_cal_backend_sexp_match_comp (cb_data->sexp, record->comp_anniversary, E_CAL_BACKEND (cb_data->cbc))) {
                gchar * comp_str = e_cal_component_get_as_string (record->comp_anniversary);
                cb_data->result = g_list_append (cb_data->result, comp_str);
        }
}

static gboolean
is_source_usable (ESource *source, ESourceGroup *group)
{
	const gchar *base_uri;
	const gchar *prop;

        base_uri = e_source_group_peek_base_uri (group);
        if (!base_uri)
                return FALSE;

	prop = e_source_get_property (source, "use-in-contacts-calendar");

	/* the later check is for backward compatibility */
	return (prop && g_str_equal (prop, "1")) || (!prop && g_str_has_prefix (base_uri, "file://"));
}

/* SourceList callbacks */
static void
add_source (ECalBackendContacts *cbc, ESource *source)
{
        BookRecord *br = book_record_new (cbc, source);
        const gchar *uid = e_source_peek_uid (source);

	if (!br)
		return;

        g_hash_table_insert (cbc->priv->addressbooks, g_strdup (uid), br);
}

static void
source_added_cb (ESourceGroup *group, ESource *source, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);

        g_return_if_fail (cbc);

	if (is_source_usable (source, group))
		add_source (cbc, source);
}

static void
source_removed_cb (ESourceGroup *group, ESource *source, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        const gchar          *uid = e_source_peek_uid (source);

        g_return_if_fail (cbc);

        g_hash_table_remove (cbc->priv->addressbooks, uid);
}

static void
source_list_changed_cb (ESourceList *source_list, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        GSList *g, *s;

        g_return_if_fail (cbc);

	for (g = e_source_list_peek_groups (source_list); g; g = g->next) {
		ESourceGroup *group = E_SOURCE_GROUP (g->data);

		if (!group)
			continue;

		for (s = e_source_group_peek_sources (group); s; s = s->next) {
			ESource *source = E_SOURCE (s->data);
			const gchar *uid;

			if (!source)
				continue;

			uid = e_source_peek_uid (source);
			if (!uid)
				continue;

			if (is_source_usable (source, group)) {
				if (!g_hash_table_lookup (cbc->priv->addressbooks, uid))
					source_added_cb (group, source, cbc);
			} else if (g_hash_table_lookup (cbc->priv->addressbooks, uid)) {
				source_removed_cb (group, source, cbc);
			}
		}
	}
}

static void
source_group_added_cb (ESourceList *source_list, ESourceGroup *group, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        GSList *i;

        g_return_if_fail (cbc);

	for (i = e_source_group_peek_sources (group); i; i = i->next) {
		ESource *source = E_SOURCE (i->data);
		source_added_cb (group, source, cbc);
	}

	/* Watch for future changes */
	g_signal_connect (group, "source_added", G_CALLBACK (source_added_cb), cbc);
	g_signal_connect (group, "source_removed", G_CALLBACK (source_removed_cb), cbc);
}

static void
source_group_removed_cb (ESourceList *source_list, ESourceGroup *group, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        GSList *i = NULL;

        g_return_if_fail (cbc);

        /* Unload all address books from this group */
        for (i = e_source_group_peek_sources (group); i; i = i->next) {
                ESource *source = E_SOURCE (i->data);
                const gchar *uid = e_source_peek_uid (source);

                g_hash_table_remove (cbc->priv->addressbooks, uid);
        }
}

/************************************************************************************/

static void
contacts_changed_cb (EBookView *book_view, const GList *contacts, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
	EBook *book = e_book_view_get_book (book_view);
        const GList *i;

        for (i = contacts; i; i = i->next) {
                EContact *contact = E_CONTACT (i->data);
                const gchar *uid = e_contact_get_const (contact, E_CONTACT_UID);
                EContactDate *birthday, *anniversary;

                /* Because this is a change of contact, then always remove old tracked data
		   and if possible, add with (possibly) new values.
		*/
		g_hash_table_remove (cbc->priv->tracked_contacts, (gchar *)uid);

                birthday = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
                anniversary = e_contact_get (contact, E_CONTACT_ANNIVERSARY);

                if (birthday || anniversary) {
                        ContactRecord *cr = contact_record_new (cbc, book, contact);
                        g_hash_table_insert (cbc->priv->tracked_contacts, g_strdup (uid), cr);
                }

                e_contact_date_free (birthday);
                e_contact_date_free (anniversary);
        }
}

static void
contacts_added_cb (EBookView *book_view, const GList *contacts, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
	EBook *book = e_book_view_get_book (book_view);
        const GList *i;

        /* See if any new contacts have BIRTHDAY or ANNIVERSARY fields */
        for (i = contacts; i; i = i->next)
        {
                EContact *contact = E_CONTACT (i->data);
                EContactDate *birthday, *anniversary;

                birthday = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
                anniversary = e_contact_get (contact, E_CONTACT_ANNIVERSARY);

                if (birthday || anniversary) {
                        ContactRecord *cr = contact_record_new (cbc, book, contact);
                        const gchar    *uid = e_contact_get_const (contact, E_CONTACT_UID);

                        g_hash_table_insert (cbc->priv->tracked_contacts, g_strdup (uid), cr);
                }

                e_contact_date_free (birthday);
                e_contact_date_free (anniversary);
        }
}

static void
contacts_removed_cb (EBookView *book_view, const GList *contact_ids, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        const GList *i;

        /* Stop tracking these */
        for (i = contact_ids; i; i = i->next)
                g_hash_table_remove (cbc->priv->tracked_contacts, i->data);
}

/************************************************************************************/
static struct icaltimetype
cdate_to_icaltime (EContactDate *cdate)
{
	struct icaltimetype ret = icaltime_null_time();

	ret.year = cdate->year;
	ret.month = cdate->month;
	ret.day = cdate->day;
	ret.is_date = TRUE;
	ret.is_utc = FALSE;
	ret.zone = NULL;
	ret.is_daylight = FALSE;

	ret.hour = ret.minute = ret.second = 0;

	return ret;
}

static void
manage_comp_alarm_update (ECalBackendContacts *cbc, ECalComponent *comp)
{
	gchar *old_comp, *new_comp;

	g_return_if_fail (cbc != NULL);
	g_return_if_fail (comp != NULL);

	old_comp = e_cal_component_get_as_string (comp);
	setup_alarm (cbc, comp);
	new_comp = e_cal_component_get_as_string (comp);

	/* check if component changed and notify if so */
	if (old_comp && new_comp && !g_str_equal (old_comp, new_comp))
		e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbc), old_comp, new_comp);

	g_free (old_comp);
	g_free (new_comp);
}

static void
update_alarm_cb (gpointer key, gpointer value, gpointer user_data)
{
	ECalBackendContacts *cbc = user_data;
        ContactRecord   *record = value;

	g_return_if_fail (cbc != NULL);
	g_return_if_fail (record != NULL);

        if (record->comp_birthday)
		manage_comp_alarm_update (cbc, record->comp_birthday);

        if (record->comp_anniversary)
                manage_comp_alarm_update (cbc, record->comp_anniversary);
}

static gboolean
update_tracked_alarms_cb (gpointer user_data)
{
	ECalBackendContacts *cbc = user_data;

	g_return_val_if_fail (cbc != NULL, FALSE);

	g_hash_table_foreach (cbc->priv->tracked_contacts, update_alarm_cb, cbc);
	cbc->priv->update_alarms_id = 0;

	return FALSE;
}

static void
alarm_config_changed_cb (GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	ECalBackendContacts *cbc = user_data;

	g_return_if_fail (cbc != NULL);

	setup_alarm (cbc, NULL);

	if (!cbc->priv->update_alarms_id)
		cbc->priv->update_alarms_id = g_idle_add (update_tracked_alarms_cb, cbc);
}

/* When called with NULL, then just refresh local static variables on setup change from the user. */
static void
setup_alarm (ECalBackendContacts *cbc, ECalComponent *comp)
{
	ECalComponentAlarm *alarm;
	ECalComponentAlarmTrigger trigger;
	ECalComponentText summary;

	g_return_if_fail (cbc != NULL);

	if (!comp || cbc->priv->alarm_interval == -1) {
		gchar *str;

		#define BA_CONF_DIR		"/apps/evolution/calendar/other"
		#define BA_CONF_ENABLED		BA_CONF_DIR "/use_ba_reminder"
		#define BA_CONF_INTERVAL	BA_CONF_DIR "/ba_reminder_interval"
		#define BA_CONF_UNITS		BA_CONF_DIR "/ba_reminder_units"

		if (cbc->priv->alarm_interval == -1) {
			/* initial setup, hook callback for changes too */
			gconf_client_add_dir (cbc->priv->conf_client, BA_CONF_DIR, GCONF_CLIENT_PRELOAD_NONE, NULL);
			cbc->priv->notifyid1 = gconf_client_notify_add (cbc->priv->conf_client, BA_CONF_ENABLED,  alarm_config_changed_cb, cbc, NULL, NULL);
			cbc->priv->notifyid2 = gconf_client_notify_add (cbc->priv->conf_client, BA_CONF_INTERVAL, alarm_config_changed_cb, cbc, NULL, NULL);
			cbc->priv->notifyid3 = gconf_client_notify_add (cbc->priv->conf_client, BA_CONF_UNITS,    alarm_config_changed_cb, cbc, NULL, NULL);
		}

		cbc->priv->alarm_enabled = gconf_client_get_bool (cbc->priv->conf_client, BA_CONF_ENABLED, NULL);
		cbc->priv->alarm_interval = gconf_client_get_int (cbc->priv->conf_client, BA_CONF_INTERVAL, NULL);

		str = gconf_client_get_string (cbc->priv->conf_client, BA_CONF_UNITS, NULL);
		if (str && !strcmp (str, "days"))
			cbc->priv->alarm_units = CAL_DAYS;
		else if (str && !strcmp (str, "hours"))
			cbc->priv->alarm_units = CAL_HOURS;
		else
			cbc->priv->alarm_units = CAL_MINUTES;

		g_free (str);

		if (cbc->priv->alarm_interval <= 0)
			cbc->priv->alarm_interval = 1;

		if (!comp)
			return;

		#undef BA_CONF_DIR
		#undef BA_CONF_ENABLED
		#undef BA_CONF_INTERVAL
		#undef BA_CONF_UNITS
	}

	/* ensure no alarms left */
	e_cal_component_remove_all_alarms (comp);

	/* do not want alarms, return */
	if (!cbc->priv->alarm_enabled)
		return;

	alarm = e_cal_component_alarm_new ();
	e_cal_component_get_summary (comp, &summary);
	e_cal_component_alarm_set_description (alarm, &summary);
	e_cal_component_alarm_set_action (alarm, E_CAL_COMPONENT_ALARM_DISPLAY);

	trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;

	memset (&trigger.u.rel_duration, 0, sizeof (trigger.u.rel_duration));

	trigger.u.rel_duration.is_neg = TRUE;

	switch (cbc->priv->alarm_units) {
	case CAL_MINUTES:
		trigger.u.rel_duration.minutes = cbc->priv->alarm_interval;
		break;

	case CAL_HOURS:
		trigger.u.rel_duration.hours = cbc->priv->alarm_interval;
		break;

	case CAL_DAYS:
		trigger.u.rel_duration.days = cbc->priv->alarm_interval;
		break;

	default:
		g_warning ("%s: wrong units %d\n", G_STRFUNC, cbc->priv->alarm_units);
		e_cal_component_alarm_free (alarm);
		return;
	}

	e_cal_component_alarm_set_trigger (alarm, trigger);
	e_cal_component_add_alarm (comp, alarm);
	e_cal_component_alarm_free (alarm);
}

/* Contact -> Event creator */
static ECalComponent *
create_component (ECalBackendContacts *cbc, const gchar *uid, EContactDate *cdate, const gchar *summary)
{
        ECalComponent             *cal_comp;
	ECalComponentText          comp_summary;
        icalcomponent             *ical_comp;
        struct icaltimetype        itt;
        ECalComponentDateTime      dt;
	struct icalrecurrencetype  r;
        GSList recur_list;

        g_return_val_if_fail (E_IS_CAL_BACKEND_CONTACTS (cbc), NULL);

        if (!cdate)
                return NULL;

        ical_comp = icalcomponent_new (ICAL_VEVENT_COMPONENT);

        /* Create the event object */
        cal_comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (cal_comp, ical_comp);

	/* Set uid */
	d(g_message ("Creating UID: %s", uid));
	e_cal_component_set_uid (cal_comp, uid);

        /* Set all-day event's date from contact data */
        itt = cdate_to_icaltime (cdate);
        dt.value = &itt;
        dt.tzid = NULL;
        e_cal_component_set_dtstart (cal_comp, &dt);

	itt = cdate_to_icaltime (cdate);
	icaltime_adjust (&itt, 1, 0, 0, 0);
	dt.value = &itt;
	dt.tzid = NULL;
	/* We have to add 1 day to DTEND, as it is not inclusive. */
	e_cal_component_set_dtend (cal_comp, &dt);

        /* Create yearly recurrence */
        icalrecurrencetype_clear (&r);
        r.freq = ICAL_YEARLY_RECURRENCE;
	r.interval = 1;
        recur_list.data = &r;
        recur_list.next = NULL;
        e_cal_component_set_rrule_list (cal_comp, &recur_list);

        /* Create summary */
        comp_summary.value = summary;
        comp_summary.altrep = NULL;
        e_cal_component_set_summary (cal_comp, &comp_summary);

	/* Set category and visibility */
	if (g_str_has_suffix (uid, ANNIVERSARY_UID_EXT))
		e_cal_component_set_categories (cal_comp, _("Anniversary"));
	else if (g_str_has_suffix (uid, BIRTHDAY_UID_EXT))
		e_cal_component_set_categories (cal_comp, _("Birthday"));

	e_cal_component_set_classification (cal_comp, E_CAL_COMPONENT_CLASS_PRIVATE);

	/* Birthdays/anniversaries are shown as free time */
	e_cal_component_set_transparency (cal_comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

	/* setup alarms if required */
	setup_alarm (cbc, cal_comp);

        /* Don't forget to call commit()! */
	e_cal_component_commit_sequence (cal_comp);

        return cal_comp;
}

static ECalComponent *
create_birthday (ECalBackendContacts *cbc, EContact *contact)
{
        EContactDate  *cdate;
        ECalComponent *cal_comp;
	gchar          *summary;
        const gchar    *name;
        gchar *uid;

        cdate = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
        name = e_contact_get_const (contact, E_CONTACT_FILE_AS);

	uid = g_strdup_printf ("%s%s", (gchar *) e_contact_get_const (contact, E_CONTACT_UID), BIRTHDAY_UID_EXT);
        summary = g_strdup_printf (_("Birthday: %s"), name);

        cal_comp = create_component (cbc, uid, cdate, summary);

        e_contact_date_free (cdate);
        g_free (uid);
        g_free (summary);

        return cal_comp;
}

static ECalComponent *
create_anniversary (ECalBackendContacts *cbc, EContact *contact)
{
        EContactDate  *cdate;
        ECalComponent *cal_comp;
	gchar          *summary;
        const gchar    *name;
        gchar *uid;

        cdate = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
        name = e_contact_get_const (contact, E_CONTACT_FILE_AS);

	uid = g_strdup_printf ("%s%s", (gchar *) e_contact_get_const (contact, E_CONTACT_UID), ANNIVERSARY_UID_EXT);
        summary = g_strdup_printf (_("Anniversary: %s"), name);

        cal_comp = create_component (cbc, uid, cdate, summary);

        e_contact_date_free (cdate);
        g_free (uid);
        g_free (summary);

        return cal_comp;
}

/************************************************************************************/
/* Calendar backend method implementations */

/* First the empty stubs */

static ECalBackendSyncStatus
e_cal_backend_contacts_get_cal_address (ECalBackendSync *backend, EDataCal *cal,
					gchar **address)
{
	/* A contact backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal,
					   gchar **attribute)
{
	*attribute = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal,
						gchar **address)
{
	/* A contact backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal,
						gchar **capabilities)
{
	*capabilities = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_remove (ECalBackendSync *backend, EDataCal *cal)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_default_object (ECalBackendSync *backend, EDataCal *cal,
					   gchar **object)
{
	return GNOME_Evolution_Calendar_UnsupportedMethod;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_object (ECalBackendSync *backend, EDataCal *cal,
				   const gchar *uid, const gchar *rid,
				   gchar **object)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;
	ContactRecord *record;
	gchar *real_uid;

	if (!uid)
		return GNOME_Evolution_Calendar_ObjectNotFound;
	else if (g_str_has_suffix (uid, ANNIVERSARY_UID_EXT))
		real_uid = g_strndup (uid, strlen (uid) - strlen (ANNIVERSARY_UID_EXT));
	else if (g_str_has_suffix (uid, BIRTHDAY_UID_EXT))
		real_uid = g_strndup (uid, strlen (uid) - strlen (BIRTHDAY_UID_EXT));
	else
		return GNOME_Evolution_Calendar_ObjectNotFound;

	record = g_hash_table_lookup (priv->tracked_contacts, real_uid);
	g_free (real_uid);

	if (!record)
		return GNOME_Evolution_Calendar_ObjectNotFound;

        if (record->comp_birthday && g_str_has_suffix (uid, BIRTHDAY_UID_EXT)) {
                *object = e_cal_component_get_as_string (record->comp_birthday);

		d(g_message ("Return birthday: %s", *object));
		return GNOME_Evolution_Calendar_Success;
	}

        if (record->comp_anniversary && g_str_has_suffix (uid, ANNIVERSARY_UID_EXT)) {
                *object = e_cal_component_get_as_string (record->comp_anniversary);

		d(g_message ("Return anniversary: %s", *object));
		return GNOME_Evolution_Calendar_Success;
        }

	d(g_message ("Returning nothing for uid: %s", uid));

	return GNOME_Evolution_Calendar_ObjectNotFound;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_free_busy (ECalBackendSync *backend, EDataCal *cal,
				      GList *users, time_t start, time_t end,
				      GList **freebusy)
{
	/* Birthdays/anniversaries don't count as busy time */

	icalcomponent *vfb = icalcomponent_new_vfreebusy ();
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	gchar *calobj;

#if 0
	icalproperty *prop;
	icalparameter *param;

	prop = icalproperty_new_organizer (address);
	if (prop != NULL && cn != NULL) {
		param = icalparameter_new_cn (cn);
		icalproperty_add_parameter (prop, param);
	}
	if (prop != NULL)
		icalcomponent_add_property (vfb, prop);
#endif

	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	calobj = icalcomponent_as_ical_string_r (vfb);
	*freebusy = g_list_append (NULL, calobj);
	icalcomponent_free (vfb);

	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_changes (ECalBackendSync *backend, EDataCal *cal,
				    const gchar *change_id,
				    GList **adds, GList **modifies, GList **deletes)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_discard_alarm (ECalBackendSync *backend, EDataCal *cal,
				      const gchar *uid, const gchar *auid)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_receive_objects (ECalBackendSync *backend, EDataCal *cal,
					const gchar *calobj)
{
	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_send_objects (ECalBackendSync *backend, EDataCal *cal,
				     const gchar *calobj, GList **users, gchar **modified_calobj)
{
	*users = NULL;
	*modified_calobj = NULL;
	/* TODO: Investigate this */
	return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Then the real implementations */

static CalMode
e_cal_backend_contacts_get_mode (ECalBackend *backend)
{
	return CAL_MODE_LOCAL;
}

static void
e_cal_backend_contacts_set_mode (ECalBackend *backend, CalMode mode)
{
	e_cal_backend_notify_mode (backend,
				   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
				   GNOME_Evolution_Calendar_MODE_LOCAL);

}

static ECalBackendSyncStatus
e_cal_backend_contacts_is_read_only (ECalBackendSync *backend, EDataCal *cal,
				     gboolean *read_only)
{
	*read_only = TRUE;

	return GNOME_Evolution_Calendar_Success;
}

static gpointer
init_sources_cb (ECalBackendContacts *cbc)
{
        ECalBackendContactsPrivate *priv;
        GSList *i;

	g_return_val_if_fail (cbc != NULL, NULL);

	priv = cbc->priv;

	/* Create address books for existing sources */
        for (i = e_source_list_peek_groups (priv->addressbook_sources); i; i = i->next) {
                ESourceGroup *source_group = E_SOURCE_GROUP (i->data);

                source_group_added_cb (priv->addressbook_sources, source_group, cbc);
        }

        /* Listen for source list changes */
        g_signal_connect (priv->addressbook_sources, "changed", G_CALLBACK (source_list_changed_cb), cbc);
        g_signal_connect (priv->addressbook_sources, "group_added", G_CALLBACK (source_group_added_cb), cbc);
        g_signal_connect (priv->addressbook_sources, "group_removed", G_CALLBACK (source_group_removed_cb), cbc);

	e_flag_set (priv->init_done_flag);

	return NULL;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_open (ECalBackendSync *backend, EDataCal *cal,
			     gboolean only_if_exists,
			     const gchar *username, const gchar *password)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;
	GError *error = NULL;

        if (priv->addressbook_loaded)
                return GNOME_Evolution_Calendar_Success;

        if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ()) {
		icalcomponent *icalcomp = icaltimezone_get_component (priv->default_zone);
		icaltimezone *zone = icaltimezone_new ();

		icaltimezone_set_component (zone, icalcomponent_new_clone (icalcomp));

		g_hash_table_insert (priv->zones, g_strdup (icaltimezone_get_tzid (zone)), zone);
	}

	/* initialize addressbook sources in new thread to make this function quick as much as possible */
	if (!g_thread_create ((GThreadFunc)init_sources_cb, cbc, FALSE, &error)) {
		e_flag_set (priv->init_done_flag);
		g_warning ("%s: Cannot create thread to initialize sources! (%s)", G_STRFUNC, error ? error->message : "Unknown error");
		if (error)
			g_error_free (error);

		return GNOME_Evolution_Calendar_OtherError;
	}

        priv->addressbook_loaded = TRUE;

        return GNOME_Evolution_Calendar_Success;
}

static gboolean
e_cal_backend_contacts_is_loaded (ECalBackend *backend)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;

        return priv->addressbook_loaded;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_contacts_add_timezone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	ECalBackendContacts *cbcontacts;
	ECalBackendContactsPrivate *priv;
	icalcomponent *tz_comp;
	icaltimezone *zone;
	const gchar *tzid;

	cbcontacts = (ECalBackendContacts *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CONTACTS (cbcontacts), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbcontacts->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	tzid = icaltimezone_get_tzid (zone);

	if (g_hash_table_lookup (priv->zones, tzid)) {
		icaltimezone_free (zone, TRUE);

		return GNOME_Evolution_Calendar_Success;
	}

	g_hash_table_insert (priv->zones, g_strdup (tzid), zone);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const gchar *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendContacts *cbcontacts;
	ECalBackendContactsPrivate *priv;
	icaltimezone *zone;

	cbcontacts = (ECalBackendContacts *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CONTACTS (cbcontacts), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbcontacts->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ())
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_object_list (ECalBackendSync *backend, EDataCal *cal,
					const gchar *sexp_string, GList **objects)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;
        ECalBackendSExp *sexp = e_cal_backend_sexp_new (sexp_string);
        ContactRecordCB *cb_data;

	if (!sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	cb_data = contact_record_cb_new (cbc, sexp);
        g_hash_table_foreach (priv->tracked_contacts, contact_record_cb, cb_data);
	*objects = cb_data->result;

	/* Don't call cb_data_free as that would destroy the results
	 * in *objects */
	g_free (cb_data);

	return GNOME_Evolution_Calendar_Success;
}

static void
e_cal_backend_contacts_start_query (ECalBackend *backend, EDataCalView *query)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;
        ECalBackendSExp *sexp;
        ContactRecordCB *cb_data;

        sexp = e_data_cal_view_get_object_sexp (query);
	if (!sexp) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}

        cb_data = contact_record_cb_new (cbc, sexp);

        g_hash_table_foreach (priv->tracked_contacts, contact_record_cb, cb_data);
        e_data_cal_view_notify_objects_added (query, cb_data->result);

        contact_record_cb_free (cb_data);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static icaltimezone *
e_cal_backend_contacts_internal_get_default_timezone (ECalBackend *backend)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);

	return cbc->priv->default_zone;
}

static icaltimezone *
e_cal_backend_contacts_internal_get_timezone (ECalBackend *backend, const gchar *tzid)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);

        return cbc->priv->default_zone;
}

/***********************************************************************************
 */

static void
free_zone (gpointer data)
{
	icaltimezone_free (data, TRUE);
}

/* Finalize handler for the contacts backend */
static void
e_cal_backend_contacts_finalize (GObject *object)
{
	ECalBackendContacts *cbc;
	ECalBackendContactsPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_CONTACTS (object));

	cbc = E_CAL_BACKEND_CONTACTS (object);
	priv = cbc->priv;

	if (priv->init_done_flag) {
		e_flag_wait (priv->init_done_flag);
		e_flag_free (priv->init_done_flag);
		priv->init_done_flag = NULL;
	}

	if (priv->update_alarms_id) {
		g_source_remove (priv->update_alarms_id);
		priv->update_alarms_id = 0;
	}

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ()) {
		icaltimezone_free (priv->default_zone, 1);
	}

	priv->default_zone = NULL;
	g_object_unref (priv->addressbook_sources);
	g_hash_table_destroy (priv->addressbooks);
        g_hash_table_destroy (priv->tracked_contacts);
        g_hash_table_destroy (priv->zones);
	if (priv->notifyid1)
		gconf_client_notify_remove (priv->conf_client, priv->notifyid1);
	if (priv->notifyid2)
		gconf_client_notify_remove (priv->conf_client, priv->notifyid2);
	if (priv->notifyid3)
		gconf_client_notify_remove (priv->conf_client, priv->notifyid3);

	g_object_unref (priv->conf_client);

	g_free (priv);
	cbc->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Object initialization function for the contacts backend */
static void
e_cal_backend_contacts_init (ECalBackendContacts *cbc)
{
	ECalBackendContactsPrivate *priv;

	priv = g_new0 (ECalBackendContactsPrivate, 1);

	e_book_get_addressbooks (&priv->addressbook_sources, NULL);

        priv->addressbooks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, (GDestroyNotify) book_record_free);
        priv->tracked_contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                        g_free, (GDestroyNotify)contact_record_free);

	priv->zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_zone);
	priv->default_zone = icaltimezone_get_utc_timezone ();
	priv->init_done_flag = e_flag_new ();
	priv->conf_client = gconf_client_get_default ();
	priv->notifyid1 = 0;
	priv->notifyid2 = 0;
	priv->notifyid3 = 0;
	priv->update_alarms_id = 0;
	priv->alarm_enabled = FALSE;
	priv->alarm_interval = -1;
	priv->alarm_units = CAL_MINUTES;

	cbc->priv = priv;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbc), TRUE);
}

static ECalBackendSyncStatus
e_cal_backend_contacts_create_object (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid)
{
        ECalBackendContacts *cbcontacts;
        ECalBackendContactsPrivate *priv;

        cbcontacts = E_CAL_BACKEND_CONTACTS (backend);
        priv = cbcontacts->priv;

        return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Class initialization function for the contacts backend */
static void
e_cal_backend_contacts_class_init (ECalBackendContactsClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->finalize = e_cal_backend_contacts_finalize;

	sync_class->is_read_only_sync = e_cal_backend_contacts_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_contacts_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_contacts_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_contacts_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_contacts_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_contacts_open;
	sync_class->remove_sync = e_cal_backend_contacts_remove;
	sync_class->create_object_sync = e_cal_backend_contacts_create_object;
	sync_class->discard_alarm_sync = e_cal_backend_contacts_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_contacts_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_contacts_send_objects;
	sync_class->get_default_object_sync = e_cal_backend_contacts_get_default_object;
	sync_class->get_object_sync = e_cal_backend_contacts_get_object;
	sync_class->get_object_list_sync = e_cal_backend_contacts_get_object_list;
	sync_class->add_timezone_sync = e_cal_backend_contacts_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_contacts_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_contacts_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_contacts_get_changes;
	backend_class->is_loaded = e_cal_backend_contacts_is_loaded;
	backend_class->start_query = e_cal_backend_contacts_start_query;
	backend_class->get_mode = e_cal_backend_contacts_get_mode;
	backend_class->set_mode = e_cal_backend_contacts_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_contacts_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_contacts_internal_get_timezone;
}
