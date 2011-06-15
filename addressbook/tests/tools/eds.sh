#
# Helper functions to start your own e-d-s instance. This depends
# on you having your own D-Bus session bus started (first).
#
#
# Copyright (C) 2011 Collabora Ltd. <http://www.collabora.co.uk/>
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.

eds_tmpdir=$(mktemp -d)

eds_init_settings () {
    export XDG_DATA_HOME=$eds_tmpdir/.local
    export XDG_CACHE_HOME=$eds_tmpdir/.cache
    export XDG_CONFIG_HOME=$eds_tmpdir/.config
}

# This makes sure that our locally-built service starts,
# in our already-started private D-Bus session,
# so we don't need to use a .service file to make sure that it will be 
# activated instead. 
eds_start() {
   #$(top_builddir)/addressbook/libedata-book/e-addressbook-factory -r
   $cur_dir"/../../libedata-book/e-addressbook-factory" -r&
   eds_pid=$!
}

# This should be called on INT TERM and EXIT
eds_stop () {
    kill $eds_pid
    rm -rf $eds_tmpdir
    rm -rf $eds_tmpdir
}

