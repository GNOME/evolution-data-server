/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2000-2003 Ximian, Inc.
 * Copyright (C) 2003 Gergõ Érdi
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
 *          Gergõ Érdi <cactus@cactus.rulez.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <bonobo/bonobo-i18n.h>

#include "e-cal-backend-contacts.h"

#include <libedataserver/e-xml-hash-utils.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include <libebook/e-book.h>

#include <libedataserver/e-source-list.h>

static ECalBackendSyncClass *parent_class;

/* Private part of the ECalBackendContacts structure */
struct _ECalBackendContactsPrivate {
        ESourceList  *addressbook_sources;
        
        GHashTable   *addressbooks;       /* UID -> BookRecord */
        gboolean      addressbook_loaded;

        EBookView    *book_view;
        GHashTable   *tracked_contacts;   /* UID -> ContactRecord */

	icaltimezone *default_zone;
};

typedef struct _BookRecord {
        EBook     *book;
        EBookView *book_view;
} BookRecord;

typedef struct _ContactRecord {
        ECalBackendContacts *cbc;
        EContact            *contact;
        ECalComponent       *comp_birthday, *comp_anniversary;
} ContactRecord;

static ECalComponent * create_birthday (ECalBackendContacts *cbc, EContact *contact);
static ECalComponent * create_anniversary (ECalBackendContacts *cbc, EContact *contact);

static void contacts_changed_cb (EBookView *book_view, const GList *contacts, gpointer user_data);
static void contacts_added_cb   (EBookView *book_view, const GList *contacts, gpointer user_data);
static void contacts_removed_cb (EBookView *book_view, const GList *contact_ids, gpointer user_data);

/* BookRecord methods */
static BookRecord *
book_record_new (ECalBackendContacts *cbc, ESource *source)
{
        EBook      *book = e_book_new ();
        GList      *fields = 0;
        EBookQuery *query;
        EBookView  *book_view;
        BookRecord *br;
        
        e_book_load_source (book, source, TRUE, NULL);
        
        /* Create book view */
        fields = g_list_append (fields, (char*)e_contact_field_name (E_CONTACT_FILE_AS));
        fields = g_list_append (fields, (char*)e_contact_field_name (E_CONTACT_BIRTH_DATE));
        fields = g_list_append (fields, (char*)e_contact_field_name (E_CONTACT_ANNIVERSARY));
        query = e_book_query_any_field_contains ("");

        if (!e_book_get_book_view (book, query, fields, -1, &book_view, NULL)) {
		g_list_free (fields);
                e_book_query_unref (query);
                return NULL;
        }
        e_book_query_unref (query);
	g_list_free (fields);

        g_signal_connect (book_view, "contacts_added", G_CALLBACK (contacts_added_cb), cbc);
        g_signal_connect (book_view, "contacts_removed", G_CALLBACK (contacts_removed_cb), cbc);
        g_signal_connect (book_view, "contacts_changed", G_CALLBACK (contacts_changed_cb), cbc);

        e_book_view_start (book_view);

        br = g_new (BookRecord, 1);
        br->book = book;
        br->book_view = book_view;

        return br;
}

static void
book_record_free (BookRecord *br)
{
        if (!br)
                return;
        
        g_object_unref (br->book_view);
        g_object_unref (br->book);

        g_free (br);
}

/* ContactRecord methods */
static ContactRecord *
contact_record_new (ECalBackendContacts *cbc, EContact *contact)
{
        ContactRecord *cr = g_new0 (ContactRecord, 1);
	char *comp_str;
        
        cr->cbc = cbc;
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
        char *comp_str;
        const char *uid;

        g_object_unref (G_OBJECT (cr->contact));

	/* Remove the birthday event */
	if (cr->comp_birthday) {
		comp_str = e_cal_component_get_as_string (cr->comp_birthday);
		e_cal_component_get_uid (cr->comp_birthday, &uid);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cr->cbc), uid, comp_str);
		g_free (comp_str);
		g_object_unref (G_OBJECT (cr->comp_birthday));
	}

	/* Remove the anniversary event */
	if (cr->comp_anniversary) {
		comp_str = e_cal_component_get_as_string (cr->comp_anniversary);
		e_cal_component_get_uid (cr->comp_anniversary, &uid);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cr->cbc), uid, comp_str);
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
        cb_data->result = 0;

        return cb_data;
}

static void
contact_record_cb_free (ContactRecordCB *cb_data)
{
        g_list_foreach (cb_data->result, (GFunc) g_free, 0);
        g_list_free (cb_data->result);
        
        g_free (cb_data);
}

static void
contact_record_cb (gpointer key, gpointer value, gpointer user_data)
{
        ContactRecordCB *cb_data = user_data;
        ContactRecord   *record = value;

        if (record->comp_birthday && e_cal_backend_sexp_match_comp (cb_data->sexp, record->comp_birthday, E_CAL_BACKEND (cb_data->cbc))) {
                char * comp_str = e_cal_component_get_as_string (record->comp_birthday);
                cb_data->result = g_list_append (cb_data->result, comp_str);
        }

        if (record->comp_anniversary && e_cal_backend_sexp_match_comp (cb_data->sexp, record->comp_anniversary, E_CAL_BACKEND (cb_data->cbc))) {
                char * comp_str = e_cal_component_get_as_string (record->comp_anniversary);
                cb_data->result = g_list_append (cb_data->result, comp_str);
        }
}

/* SourceList callbacks */
static void
add_source (ECalBackendContacts *cbc, ESource *source)
{
        BookRecord *br = book_record_new (cbc, source);
        const char *uid = e_source_peek_uid (source);

        g_hash_table_insert (cbc->priv->addressbooks, g_strdup (uid), br);
}

static void
source_added_cb (ESourceGroup *group, ESource *source, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        
        g_return_if_fail (cbc);

        add_source (cbc, source);
}

static void
source_removed_cb (ESourceGroup *group, ESource *source, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        const char          *uid = e_source_peek_uid (source);
        
        g_return_if_fail (cbc);

        g_hash_table_remove (cbc->priv->addressbooks, uid);
}

static void
source_group_added_cb (ESourceList *source_list, ESourceGroup *group, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        GSList *i;
        
        g_return_if_fail (cbc);

        /* Load all address books from this group */
        for (i = e_source_group_peek_sources (group); i; i = i->next)
        {
                ESource *source = E_SOURCE (i->data);
                add_source (cbc, source);
        }

        /* Watch for future changes */
        g_signal_connect (group, "source_added", G_CALLBACK (source_added_cb), cbc);
        g_signal_connect (group, "source_removed", G_CALLBACK (source_removed_cb), cbc);
}

static void
source_group_removed_cb (ESourceList *source_list, ESourceGroup *group, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        GSList *i = 0;
        
        g_return_if_fail (cbc);
        
        /* Unload all address books from this group */
        for (i = e_source_group_peek_sources (group); i; i = i->next)
        {
                ESource *source = E_SOURCE (i->data);
                const char *uid = e_source_peek_uid (source);
                
                g_hash_table_remove (cbc->priv->addressbooks, uid);
        }
}

/************************************************************************************/

static void
contacts_changed_cb (EBookView *book_view, const GList *contacts, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        const GList *i;

        for (i = contacts; i; i = i->next)
        {
                EContact *contact = E_CONTACT (i->data);
                char *uid = e_contact_get_const (contact, E_CONTACT_UID);
                
                /* If no date is set, remove from list of tracked contacts */                 
                if (!e_contact_get (contact, E_CONTACT_BIRTH_DATE) &&
                    !e_contact_get (contact, E_CONTACT_ANNIVERSARY)) {
                        g_hash_table_remove (cbc->priv->tracked_contacts, uid);
                } else {
                        ContactRecord *cr = contact_record_new (cbc, contact);
                        g_hash_table_insert (cbc->priv->tracked_contacts, g_strdup (uid), cr);
                }
        }
}

static void
contacts_added_cb (EBookView *book_view, const GList *contacts, gpointer user_data)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (user_data);
        const GList *i;

        /* See if any new contacts have BIRTHDAY or ANNIVERSARY fields */
        for (i = contacts; i; i = i->next)
        {
                EContact *contact = E_CONTACT (i->data);
                
                if (e_contact_get (contact, E_CONTACT_BIRTH_DATE) ||
                    e_contact_get (contact, E_CONTACT_ANNIVERSARY)) {
                        ContactRecord *cr = contact_record_new (cbc, contact);
                        const char    *uid = e_contact_get_const (contact, E_CONTACT_UID);
                        
                        g_hash_table_insert (cbc->priv->tracked_contacts, g_strdup (uid), cr);
                }
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
	struct icaltimetype ret;

/*FIXME: this is a really _ugly_ (temporary) hack
 *	since several functions are still depending on the epoch,
 *	let entries start (earliest) at 19700101
 */
	ret.year = cdate->year >= 1970 ? cdate->year : 1970;
	ret.month = cdate->month;
	ret.day = cdate->day;
	ret.is_date = TRUE;
	ret.zone = icaltimezone_get_utc_timezone ();
	
	ret.hour = ret.minute = ret.second = 0;

	return ret;
}

/* Contact -> Event creator */
static ECalComponent *
create_component (ECalBackendContacts *cbc, EContactDate *cdate, const char *summary)
{
        ECalComponent             *cal_comp;
	ECalComponentText          comp_summary;
        icalcomponent             *ical_comp;
        struct icaltimetype        itt;
        ECalComponentDateTime      dt;
	struct icalrecurrencetype  r;
        GSList recur_list;

        g_return_val_if_fail (E_IS_CAL_BACKEND_CONTACTS (cbc), 0);

        if (!cdate)
                return NULL;
        
        ical_comp = icalcomponent_new (ICAL_VEVENT_COMPONENT);

        /* Create the event object */
        cal_comp = e_cal_component_new ();
        e_cal_component_gen_uid ();
	e_cal_component_set_icalcomponent (cal_comp, ical_comp);

        /* Set all-day event's date from contact data */
        itt = cdate_to_icaltime (cdate);
        dt.value = &itt;
        dt.tzid = 0;
        e_cal_component_set_dtstart (cal_comp, &dt);
        
	itt = cdate_to_icaltime (cdate);
	icaltime_adjust (&itt, 1, 0, 0, 0);
	dt.value = &itt;
	dt.tzid = 0;
	/* We have to add 1 day to DTEND, as it is not inclusive. */
	e_cal_component_set_dtend (cal_comp, &dt);
 
        /* Create yearly recurrence */
        icalrecurrencetype_clear (&r);
        r.freq = ICAL_YEARLY_RECURRENCE;
	r.interval = 1;
        recur_list.data = &r;
        recur_list.next = 0;        
        e_cal_component_set_rrule_list (cal_comp, &recur_list);

        /* Create summary */
        comp_summary.value = summary;
        comp_summary.altrep = 0;
        e_cal_component_set_summary (cal_comp, &comp_summary);
	
	/* Set category and visibility */
	e_cal_component_set_categories (cal_comp, _("Birthday"));
	e_cal_component_set_classification (cal_comp, E_CAL_COMPONENT_CLASS_PRIVATE);

	/* Birthdays/anniversaries are shown as free time */
	e_cal_component_set_transparency (cal_comp, E_CAL_COMPONENT_TRANSP_TRANSPARENT);
	
        /* Don't forget to call commit()! */
	e_cal_component_commit_sequence (cal_comp);
        
        return cal_comp;
}

static ECalComponent *
create_birthday (ECalBackendContacts *cbc, EContact *contact)
{
        EContactDate  *cdate;
        ECalComponent *cal_comp;
	char          *summary;
        const char    *name;
        
        cdate = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
        name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
        summary = g_strdup_printf (_("Birthday: %s"), name);
        
        cal_comp = create_component (cbc, cdate, summary);

        g_free (summary);
        
        return cal_comp;
}

static ECalComponent *
create_anniversary (ECalBackendContacts *cbc, EContact *contact)
{
        EContactDate  *cdate;
        ECalComponent *cal_comp;
	char          *summary;
        const char    *name;
        
        cdate = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
        name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
        summary = g_strdup_printf (_("Anniversary: %s"), name);
        
        cal_comp = create_component (cbc, cdate, summary);

        g_free (summary);

        return cal_comp;
}

/************************************************************************************/
/* Calendar backend method implementations */

/* First the empty stubs */

static ECalBackendSyncStatus
e_cal_backend_contacts_get_cal_address (ECalBackendSync *backend, EDataCal *cal,
					char **address)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal,
					   char **attribute)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal,
						char **address)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal,
						char **capabilities)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_remove (ECalBackendSync *backend, EDataCal *cal)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_default_object (ECalBackendSync *backend, EDataCal *cal,
					   char **object)
{
	return GNOME_Evolution_Calendar_UnsupportedMethod;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_object (ECalBackendSync *backend, EDataCal *cal,
				   const char *uid, const char *rid,
				   char **object)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_free_busy (ECalBackendSync *backend, EDataCal *cal,
				      GList *users, time_t start, time_t end,
				      GList **freebusy)
{
	/* Birthdays/anniversaries don't count as busy time */

	icalcomponent *vfb = icalcomponent_new_vfreebusy ();
	icaltimezone *utc_zone = icaltimezone_get_utc_timezone ();
	char *calobj;

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

	calobj = icalcomponent_as_ical_string (vfb);
	*freebusy = g_list_append (NULL, g_strdup (calobj));
	icalcomponent_free (vfb);	
	
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_get_changes (ECalBackendSync *backend, EDataCal *cal,
				    const char *change_id,
				    GList **adds, GList **modifies, GList **deletes)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_discard_alarm (ECalBackendSync *backend, EDataCal *cal,
				      const char *uid, const char *auid)
{
	/* WRITE ME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_contacts_receive_objects (ECalBackendSync *backend, EDataCal *cal,
					const char *calobj)
{
	return GNOME_Evolution_Calendar_UnsupportedMethod;	
}

static ECalBackendSyncStatus
e_cal_backend_contacts_send_objects (ECalBackendSync *backend, EDataCal *cal,
				     const char *calobj, GList **users, char **modified_calobj)
{
	*users = NULL;
	*modified_calobj = NULL;
	/* TODO: Investigate this */
	return GNOME_Evolution_Calendar_Success;
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

static ECalBackendSyncStatus
e_cal_backend_contacts_open (ECalBackendSync *backend, EDataCal *cal,
			     gboolean only_if_exists,
			     const char *username, const char *password)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);
        ECalBackendContactsPrivate *priv = cbc->priv;

        GSList *i;

        if (priv->addressbook_loaded)
                return GNOME_Evolution_Calendar_Success;

        /* Create address books for existing sources */
        for (i = e_source_list_peek_groups (priv->addressbook_sources); i; i = i->next)
        {
                ESourceGroup *source_group = E_SOURCE_GROUP (i->data);
                        
                source_group_added_cb (priv->addressbook_sources, source_group, cbc);
        }

        /* Listen for source list changes */
        g_signal_connect (priv->addressbook_sources, "group_added", G_CALLBACK (source_group_added_cb), cbc);
        g_signal_connect (priv->addressbook_sources, "group_removed", G_CALLBACK (source_group_removed_cb), cbc);

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

static ECalBackendSyncStatus
e_cal_backend_contacts_get_object_list (ECalBackendSync *backend, EDataCal *cal,
					const char *sexp_string, GList **objects)
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
e_cal_backend_contacts_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
        ECalBackendContacts *cbc = E_CAL_BACKEND_CONTACTS (backend);

        return cbc->priv->default_zone;
}

/***********************************************************************************
 */

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

        g_hash_table_destroy (priv->tracked_contacts);
        
	g_free (priv);
	cbc->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Object initialization function for the contacts backend */
static void
e_cal_backend_contacts_init (ECalBackendContacts *cbc, ECalBackendContactsClass *class)
{
	ECalBackendContactsPrivate *priv;

	priv = g_new0 (ECalBackendContactsPrivate, 1);

	e_book_get_addressbooks (&priv->addressbook_sources, NULL);

        priv->addressbooks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free, (GDestroyNotify)book_record_free);
        priv->tracked_contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                        g_free, (GDestroyNotify)contact_record_free);
        
	priv->default_zone = icaltimezone_get_utc_timezone ();
        
	cbc->priv = priv;
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
	sync_class->discard_alarm_sync = e_cal_backend_contacts_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_contacts_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_contacts_send_objects;
 	sync_class->get_default_object_sync = e_cal_backend_contacts_get_default_object;
	sync_class->get_object_sync = e_cal_backend_contacts_get_object;
	sync_class->get_object_list_sync = e_cal_backend_contacts_get_object_list;
	sync_class->get_freebusy_sync = e_cal_backend_contacts_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_contacts_get_changes;
	backend_class->is_loaded = e_cal_backend_contacts_is_loaded;
	backend_class->start_query = e_cal_backend_contacts_start_query;
	backend_class->get_mode = e_cal_backend_contacts_get_mode;
	backend_class->set_mode = e_cal_backend_contacts_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_contacts_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_contacts_internal_get_timezone;
}


/**
 * e_cal_backend_contacts_get_type:
 * @void: 
 * 
 * Registers the #ECalBackendContacts class if necessary, and returns
 * the type ID associated to it.
 * 
 * Return value: The type ID of the #ECalBackendContacts class.
 **/
GType
e_cal_backend_contacts_get_type (void)
{
	static GType e_cal_backend_contacts_type = 0;

	if (!e_cal_backend_contacts_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendContactsClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_contacts_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendContacts),
                        0,
                        (GInstanceInitFunc) e_cal_backend_contacts_init
                };
		e_cal_backend_contacts_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
                                                                     "ECalBackendContacts", &info, 0);
	}

	return e_cal_backend_contacts_type;
}
