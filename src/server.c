/* Wombat personal information server - main file
 *
 * Author: Nat Friedman <nat@ximian.com>
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* define this if you need/want to be able to send USR2 to wombat and
   get a list of the active backends */
/*#define DEBUG_BACKENDS*/

#include <stdlib.h>
#ifdef DEBUG_BACKENDS
#include <sys/signal.h>
#endif

#include <glib.h>
#include <libgnome/gnome-init.h>
#include <bonobo-activation/bonobo-activation.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-generic-factory.h>

#include <libedatabook/e-data-book-factory.h>
#include <libedatabook/e-book-backend-file.h>
#include <libedatabook/e-book-backend-vcf.h>
#ifdef HAVE_LDAP
#include <libedatabook/e-book-backend-ldap.h>
#endif

#include <libedatacal/e-data-cal-factory.h>
#include <libedatacal/e-cal-backend-file-events.h>
#include <libedatacal/e-cal-backend-file-todos.h>
#include <libedatacal/e-cal-backend-http.h>

#include "server-interface-check.h"

#define E_DATA_CAL_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_Wombat_CalendarFactory"
#define E_DATA_BOOK_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_Wombat_ServerFactory"

/* The and addressbook calendar factories */

static EDataCalFactory *e_data_cal_factory;

static EDataBookFactory *e_data_book_factory;

/* Timeout interval in milliseconds for termination */
#define EXIT_TIMEOUT 5000

/* Timeout ID for termination handler */
static guint termination_handler_id;



/* Termination */

/* Termination handler.  Checks if both factories have zero running backends,
 * and if so terminates the program.
 */
static gboolean
termination_handler (gpointer data)
{
	if (e_data_cal_factory_get_n_backends (e_data_cal_factory) == 0 &&
	    e_data_book_factory_get_n_backends (e_data_book_factory) == 0) {
		fprintf (stderr, "termination_handler(): Terminating the Wombat.  Have a nice day.\n");
		bonobo_main_quit ();
	}

	termination_handler_id = 0;
	return FALSE;
}

/* Queues a timeout for handling termination of Wombat */
static void
queue_termination (void)
{
	if (termination_handler_id)
		return;

	termination_handler_id = g_timeout_add (EXIT_TIMEOUT, termination_handler, NULL);
}



static void
last_book_gone_cb (EDataBookFactory *factory, gpointer data)
{
	queue_termination ();
}

static gboolean
setup_pas (void)
{
	e_data_book_factory = e_data_book_factory_new ();

	if (!e_data_book_factory)
		return FALSE;

	e_data_book_factory_register_backend (
		e_data_book_factory, "file", e_book_backend_file_new);

	e_data_book_factory_register_backend (
		e_data_book_factory, "vcf", e_book_backend_vcf_new);

#ifdef HAVE_LDAP
	e_data_book_factory_register_backend (
		e_data_book_factory, "ldap", e_book_backend_ldap_new);
#endif

	g_signal_connect (e_data_book_factory,
			  "last_book_gone",
			  G_CALLBACK (last_book_gone_cb),
			  NULL);

	if (!e_data_book_factory_activate (e_data_book_factory, E_DATA_BOOK_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
		e_data_book_factory = NULL;
		return FALSE;
	}

	return TRUE;
}


/* Personal calendar server */

/* Callback used when the calendar factory has no more running backends */
static void
last_calendar_gone_cb (EDataCalFactory *factory, gpointer data)
{
	queue_termination ();
}

/* Creates the calendar factory object and registers it */
static gboolean
setup_pcs (void)
{
	e_data_cal_factory = e_data_cal_factory_new ();

	if (!e_data_cal_factory) {
		g_message ("setup_pcs(): Could not create the calendar factory");
		return FALSE;
	}

	e_data_cal_factory_register_method (e_data_cal_factory, "file", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_FILE_EVENTS);
	e_data_cal_factory_register_method (e_data_cal_factory, "file", ICAL_VTODO_COMPONENT, E_TYPE_CAL_BACKEND_FILE_TODOS);
	e_data_cal_factory_register_method (e_data_cal_factory, "webcal", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_HTTP);

	if (!e_data_cal_factory_register_storage (e_data_cal_factory, E_DATA_CAL_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
		e_data_cal_factory = NULL;
		return FALSE;
	}

	g_signal_connect (G_OBJECT (e_data_cal_factory),
			  "last_calendar_gone",
			  G_CALLBACK (last_calendar_gone_cb),
			  NULL);

	return TRUE;
}


/* Interface check iface.  */

static gboolean
setup_interface_check (void)
{
	WombatInterfaceCheck *interface_check_iface = wombat_interface_check_new ();
	int result;

	result = bonobo_activation_active_server_register ("OAFIID:GNOME_Evolution_Wombat_InterfaceCheck",
							   BONOBO_OBJREF (interface_check_iface));

	return result == Bonobo_ACTIVATION_REG_SUCCESS;
}



#ifdef DEBUG_BACKENDS
static void
dump_backends (int signal)
{
	e_data_book_factory_dump_active_backends (e_data_book_factory);
	e_data_cal_factory_dump_active_backends (e_data_cal_factory);
}
#endif

int
main (int argc, char **argv)
{
	gboolean did_pas=FALSE, did_pcs=FALSE;

	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	g_message ("Starting wombat");

#ifdef DEBUG_BACKENDS
	signal (SIGUSR2, dump_backends);
#endif

       	gnome_program_init (PACKAGE, VERSION,
			    LIBGNOME_MODULE,
			    argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES, NULL);

	bonobo_init_full (&argc, argv,
			  bonobo_activation_orb_get(),
			  CORBA_OBJECT_NIL,
			  CORBA_OBJECT_NIL);

	if (!( (did_pas = setup_pas ())
	       && (did_pcs = setup_pcs ())
		    )) {

		const gchar *failed = NULL;

		if (!did_pas)
			failed = "PAS";
		else if (!did_pcs)
			failed = "PCS";

		g_message ("main(): could not initialize Wombat service \"%s\"; terminating", failed);

		if (e_data_book_factory) {
			bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
			e_data_book_factory = NULL;
		}

		if (e_data_cal_factory) {
			bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
			e_data_cal_factory = NULL;
		}
		exit (EXIT_FAILURE);
	}

	if (! setup_interface_check ()) {
		g_message ("Cannot register Wombat::InterfaceCheck object");
		exit (EXIT_FAILURE);
	}

	g_print ("Wombat up and running\n");

	bonobo_main ();

	bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
	e_data_cal_factory = NULL;

	bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
	e_data_book_factory = NULL;

	gnome_vfs_shutdown ();

	return 0;
}
