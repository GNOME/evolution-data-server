The Evolution Data Server package provides a unified backend for programs that work with
contacts, tasks, calendar information, and notes. It was originally developed for
[Evolution](https://gitlab.gnome.org/GNOME/evolution) (hence the name),
but is now used by other packages as well. 

The Evolution Data Server provides a single database for common, desktop-wide information,
such as a user's address book or calendar events.

By using the evolution-data-server, other GNOME applications are integrating with Evolution:
* [GNOME shell](https://gitlab.gnome.org/GNOME/gnome-shell/)'s calendar that opens when you click the date and time in the top bar.
* [GNOME calendar](https://gitlab.gnome.org/GNOME/gnome-calendar) keeps in sync with Evolution
* [GNOME contacts](https://gitlab.gnome.org/GNOME/gnome-contacts) keeps in sync with Evolution
* [GNOME calls](https://gitlab.gnome.org/GNOME/calls)
* [GNOME chatty](https://gitlab.gnome.org/World/Chatty)

Other shells and desktop environments also use Evolution Data Server, for example:
* Elementary with [Elementary mail](https://github.com/elementary/mail) and [Elementary calendar](https://github.com/elementary/calendar)
* [Cinnamon through cinnamon-calendar-server](https://github.com/linuxmint/cinnamon) 
* [Phosh](https://gitlab.gnome.org/World/Phosh/phosh)
