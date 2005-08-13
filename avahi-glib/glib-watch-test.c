/* $Id$ */

/***
  This file is part of avahi.
 
  avahi is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  avahi is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <avahi-common/watch.h>
#include <avahi-common/timeval.h>
#include "glib-watch.h"

static AvahiPoll *api = NULL;
static GMainLoop *loop = NULL;

static void callback(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata) {

    if (event & AVAHI_WATCH_IN) {
        ssize_t r;
        char c;
        
        if ((r = read(fd, &c, 1)) <= 0) {
            fprintf(stderr, "read() failed: %s\n", r < 0 ? strerror(errno) : "EOF");
            api->watch_free(w);
            return;
        }

        printf("Read: %c\n", c >= 32 && c < 127 ? c : '.');
    }
}

static void wakeup(AvahiPoll *_api, void *userdata) {
    struct timeval tv;
    static int i = 0;

    printf("Wakeup #%i\n", i++);

    if (i > 10)
        g_main_loop_quit(loop);

    avahi_elapse_time(&tv, 1000, 0);
    api->set_wakeup(api, &tv, wakeup, NULL);
}

int main(int argc, char *argv[]) {
    AvahiGLibPoll *s;
    struct timeval tv;
    
    s = avahi_glib_poll_new(NULL);
    assert(s);

    api = avahi_glib_poll_get(s);

    api->watch_new(api, 0, AVAHI_WATCH_IN, callback, NULL);

    avahi_elapse_time(&tv, 1000, 0);
    api->set_wakeup(api, &tv, wakeup, NULL);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    
    return 0;
}
