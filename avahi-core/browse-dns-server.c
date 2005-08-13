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

#include <string.h>

#include <avahi-common/domain.h>
#include <avahi-common/malloc.h>

#include "browse.h"
#include "log.h"
#include "rr.h"

typedef struct AvahiDNSServerInfo AvahiDNSServerInfo;

struct AvahiDNSServerInfo {
    AvahiDNSServerBrowser *browser;

    AvahiIfIndex interface;
    AvahiProtocol protocol;
    AvahiRecord *srv_record;
    AvahiHostNameResolver *host_name_resolver;
    AvahiAddress address;
    
    AVAHI_LLIST_FIELDS(AvahiDNSServerInfo, info);
};

struct AvahiDNSServerBrowser {
    AvahiServer *server;
    char *domain_name;
    
    AvahiRecordBrowser *record_browser;
    AvahiDNSServerBrowserCallback callback;
    void* userdata;
    AvahiProtocol aprotocol;

    unsigned n_info;
    
    AVAHI_LLIST_FIELDS(AvahiDNSServerBrowser, browser);
    AVAHI_LLIST_HEAD(AvahiDNSServerInfo, info);
};

static AvahiDNSServerInfo* get_server_info(AvahiDNSServerBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiRecord *r) {
    AvahiDNSServerInfo *i;
    
    assert(b);
    assert(r);

    for (i = b->info; i; i = i->info_next)
        if (i->interface == interface &&
            i->protocol == protocol &&
            avahi_record_equal_no_ttl(r, i->srv_record))
            return i;

    return NULL;
}

static void server_info_free(AvahiDNSServerBrowser *b, AvahiDNSServerInfo *i) {
    assert(b);
    assert(i);

    avahi_record_unref(i->srv_record);
    if (i->host_name_resolver)
        avahi_host_name_resolver_free(i->host_name_resolver);
    
    AVAHI_LLIST_REMOVE(AvahiDNSServerInfo, info, b->info, i);

    assert(b->n_info >= 1);
    b->n_info--;
    
    avahi_free(i);
}

static void host_name_resolver_callback(AvahiHostNameResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *host_name, const AvahiAddress *a, void* userdata) {
    AvahiDNSServerInfo *i = userdata;
    
    assert(r);
    assert(host_name);
    assert(i);

    if (event == AVAHI_RESOLVER_FOUND) {
        i->address = *a;

        i->browser->callback(i->browser, i->interface, i->protocol, AVAHI_BROWSER_NEW, i->srv_record->data.srv.name, &i->address, i->srv_record->data.srv.port, i->browser->userdata);
    }

    avahi_host_name_resolver_free(i->host_name_resolver);
    i->host_name_resolver = NULL;
}

static void record_browser_callback(AvahiRecordBrowser*rr, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, AvahiRecord *record, void* userdata) {
    AvahiDNSServerBrowser *b = userdata;

    assert(rr);
    assert(record);
    assert(b);

    assert(record->key->type == AVAHI_DNS_TYPE_SRV);

    if (event == AVAHI_BROWSER_NEW) {
        AvahiDNSServerInfo *i;

        if (get_server_info(b, interface, protocol, record))
            return;

        if (b->n_info >= 10)
            return;
        
        if (!(i = avahi_new(AvahiDNSServerInfo, 1)))
            return; /* OOM */
        
        i->browser = b;
        i->interface = interface;
        i->protocol = protocol;
        i->srv_record = avahi_record_ref(record);
        i->host_name_resolver = avahi_host_name_resolver_new(b->server, interface, protocol, record->data.srv.name, b->aprotocol, host_name_resolver_callback, i);
        
        AVAHI_LLIST_PREPEND(AvahiDNSServerInfo, info, b->info, i);

        b->n_info++;
    } else if (event == AVAHI_BROWSER_REMOVE) {
        AvahiDNSServerInfo *i;

        if (!(i = get_server_info(b, interface, protocol, record)))
            return;

        if (!i->host_name_resolver)
            b->callback(b, interface, protocol, event, i->srv_record->data.srv.name, &i->address, i->srv_record->data.srv.port, b->userdata);

        server_info_free(b, i);
    }
}

AvahiDNSServerBrowser *avahi_dns_server_browser_new(AvahiServer *server, AvahiIfIndex interface, AvahiProtocol protocol, const char *domain, AvahiDNSServerType type, AvahiProtocol aprotocol, AvahiDNSServerBrowserCallback callback, void* userdata) {
    AvahiDNSServerBrowser *b;
    AvahiKey *k;
    char *n = NULL;
    
    assert(server);
    assert(callback);
    assert(type == AVAHI_DNS_SERVER_RESOLVE || type == AVAHI_DNS_SERVER_UPDATE);

    if (domain && !avahi_is_valid_domain_name(domain)) {
        avahi_server_set_errno(server, AVAHI_ERR_INVALID_DOMAIN_NAME);
        return NULL;
    }
    
    if (!(b = avahi_new(AvahiDNSServerBrowser, 1))) {
        avahi_server_set_errno(server, AVAHI_ERR_NO_MEMORY);
        return NULL;
    }
    
    b->server = server;
    b->domain_name = avahi_normalize_name(domain ? domain : "local");
    b->callback = callback;
    b->userdata = userdata;
    b->aprotocol = aprotocol;
    b->n_info = 0;

    AVAHI_LLIST_HEAD_INIT(AvahiDNSServerInfo, b->info);
    AVAHI_LLIST_PREPEND(AvahiDNSServerBrowser, browser, server->dns_server_browsers, b);
    
    n = avahi_strdup_printf("%s.%s",type == AVAHI_DNS_SERVER_RESOLVE ? "_domain._udp" : "_dns-update._udp", b->domain_name);
    k = avahi_key_new(n, AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_SRV);
    avahi_free(n);
    
    b->record_browser = avahi_record_browser_new(server, interface, protocol, k, record_browser_callback, b);
    avahi_key_unref(k);

    if (!b->record_browser) {
        avahi_dns_server_browser_free(b);
        return NULL;
    }
    
    return b;
}

void avahi_dns_server_browser_free(AvahiDNSServerBrowser *b) {
    assert(b);

    while (b->info)
        server_info_free(b, b->info);
    
    AVAHI_LLIST_REMOVE(AvahiDNSServerBrowser, browser, b->server->dns_server_browsers, b);

    if (b->record_browser)
        avahi_record_browser_free(b->record_browser);
    avahi_free(b->domain_name);
    avahi_free(b);
}

