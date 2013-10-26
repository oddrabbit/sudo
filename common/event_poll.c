/*
 * Copyright (c) 2013 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# include "compat/stdbool.h"
#endif /* HAVE_STDBOOL_H */
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <errno.h>
#include <poll.h>

#include "missing.h"
#include "alloc.h"
#include "fatal.h"
#include "sudo_debug.h"
#include "sudo_event.h"

/* XXX - use non-exiting allocators? */

int
sudo_ev_base_alloc_impl(struct sudo_event_base *base)
{
    int i;
    debug_decl(sudo_ev_base_alloc_impl, SUDO_DEBUG_EVENT)

    base->pfd_high = -1;
    base->pfd_max = 32;
    base->pfds = erealloc3(NULL, base->pfd_max, sizeof(struct pollfd));
    for (i = 0; i < base->pfd_max; i++) {
	base->pfds[i].fd = -1;
    }

    debug_return_int(0);
}

void
sudo_ev_base_free_impl(struct sudo_event_base *base)
{
    debug_decl(sudo_ev_base_free_impl, SUDO_DEBUG_EVENT)
    efree(base->pfds);
    debug_return;
}

int
sudo_ev_add_impl(struct sudo_event_base *base, struct sudo_event *ev)
{
    debug_decl(sudo_ev_add_impl, SUDO_DEBUG_EVENT)
    struct pollfd *pfd;

    /* If out of space in pfds array, realloc. */
    if (base->pfd_free == base->pfd_max) {
	int i;
	base->pfd_max <<= 1;
	base->pfds =
	    erealloc3(base->pfds, base->pfd_max, sizeof(struct pollfd));
	for (i = base->pfd_free; i < base->pfd_max; i++) {
	    base->pfds[i].fd = -1;
	}
    }

    /* Fill in pfd entry. */
    ev->pfd_idx = base->pfd_free;
    pfd = &base->pfds[ev->pfd_idx];
    pfd->fd = ev->fd;
    pfd->events = 0;
    if (ISSET(ev->events, SUDO_EV_READ))
	pfd->events |= POLLIN;
    if (ISSET(ev->events, SUDO_EV_WRITE))
	pfd->events |= POLLOUT;

    /* Update pfd_high and pfd_free. */
    if (ev->pfd_idx > base->pfd_high)
	base->pfd_high = ev->pfd_idx;
    for (;;) {
	if (++base->pfd_free == base->pfd_max)
	    break;
	if (base->pfds[base->pfd_free].fd == -1)
	    break;
    }

    debug_return_int(0);
}

int
sudo_ev_del_impl(struct sudo_event_base *base, struct sudo_event *ev)
{
    debug_decl(sudo_ev_del_impl, SUDO_DEBUG_EVENT)

    /* Mark pfd entry unused, add to free list and adjust high slot. */
    base->pfds[ev->pfd_idx].fd = -1;
    if (ev->pfd_idx < base->pfd_free)
	base->pfd_free = ev->pfd_idx;
    while (base->pfd_high >= 0 && base->pfds[base->pfd_high].fd == -1)
	base->pfd_high--;

    debug_return_int(0);
}

int
sudo_ev_loop_impl(struct sudo_event_base *base, int flags)
{
    const int timeout = (flags & SUDO_EVLOOP_NONBLOCK) ? 0 : -1;
    struct sudo_event *ev;
    int nready;
    debug_decl(sudo_ev_loop_impl, SUDO_DEBUG_EVENT)

    nready = poll(base->pfds, base->pfd_high + 1, timeout);
    sudo_debug_printf(SUDO_DEBUG_INFO, "%s: %d fds ready", __func__, nready);
    switch (nready) {
    case -1:
	/* error or interrupted by signal */
	debug_return_int(-1);
    case 0:
	/* timeout or no events */
	break;
    default:
	/* Activate each event that fired. */
	TAILQ_FOREACH(ev, &base->events, entries) {
	    if (ev->pfd_idx != -1 && base->pfds[ev->pfd_idx].revents) {
		int what = 0;
		if (base->pfds[ev->pfd_idx].revents & (POLLIN|POLLHUP|POLLNVAL|POLLERR))
		    what |= (ev->events & SUDO_EV_READ);
		if (base->pfds[ev->pfd_idx].revents & (POLLOUT|POLLHUP|POLLNVAL|POLLERR))
		    what |= (ev->events & SUDO_EV_WRITE);
		/* Make event active. */
		ev->revents = what;
		SET(ev->flags, SUDO_EV_ACTIVE);
		TAILQ_INSERT_TAIL(&base->active, ev, active_entries);
	    }
	}
	break;
    }
    debug_return_int(0);
}