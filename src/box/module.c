/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "module.h"
#include "fiber.h"

#include <tt_pthread.h>


static pthread_mutex_t mtx;
static RLIST_HEAD(msg_queue);
static struct ev_async async;
static ev_loop *tx_loop;

static int
tx_fiber_f(va_list ap)
{
	void (*tx_process_request)(void *) = va_arg(ap, void (*)(void *));
	void *data = va_arg(ap, void *);
	fiber_sleep(0);
	tx_process_request(data);
	return 0;
}

static void
tx_msg_process(MAYBE_UNUSED ev_loop *loop, MAYBE_UNUSED ev_async *ev, MAYBE_UNUSED int revents)
{
	pthread_mutex_lock(&mtx);
	while(!rlist_empty(&msg_queue)) {
		struct rlist *rlist = rlist_first(&msg_queue);
		struct module_request *request = container_of(rlist, struct module_request, entry);
		void (*tx_process_request)(void *) = request->tx_process_request;
		void *data = request->data;
		rlist_del(rlist);
		pthread_mutex_unlock(&mtx);
		struct fiber *tx_fiber = fiber_new("tx_process", tx_fiber_f);
		if (!tx_fiber)
			return;
		fiber_start(tx_fiber, tx_process_request, data);
		pthread_mutex_lock(&mtx);
	}
	pthread_mutex_unlock(&mtx);
}


void
tx_msg_send(struct module_request *request)
{
	pthread_mutex_lock(&mtx);
	rlist_add_tail(&msg_queue, &request->entry);
	pthread_mutex_unlock(&mtx);
	ev_async_send(tx_loop, &async);
}

void
tx_msg_cancel(struct module_request *request)
{
	pthread_mutex_lock(&mtx);
	rlist_del(&request->entry);
	pthread_mutex_unlock(&mtx);
}

void
tx_init_module_api(void)
{
	tx_loop = loop();
	tt_pthread_mutex_init(&mtx, NULL);
	ev_async_init(&async, tx_msg_process);
	ev_async_start(tx_loop, &async);
}

void
tx_destroy_module_api(void)
{
	ev_async_stop(tx_loop, &async);
	tt_pthread_mutex_destroy(&mtx);
}