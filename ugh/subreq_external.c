#include "ugh.h"

static
void ugh_subreq_external_timeout(EV_P_ ev_timer *w, int tev)
{
    ugh_subreq_external_t *r = aux_memberof(ugh_subreq_external_t, wev_timeout, w);
    ugh_subreq_external_del(r);
}


static
void ugh_subreq_wcb_ready(EV_P_ ev_async *w, int tev)
{
    ugh_subreq_external_t *r = aux_memberof(ugh_subreq_external_t, wev_ready, w);

    ugh_subreq_external_del(r);
}

int ugh_subreq_external_assign_body(ugh_subreq_external_t *r, char *data, size_t size)
{
    r->body.data = aux_pool_nalloc(r->c->pool, size);
    r->body.size = size;
    memcpy(r->body.data, data, size);
    return 0;
}


ugh_subreq_external_t *ugh_subreq_external_add(ugh_client_t *c)
{
    ugh_subreq_external_t *r;

    aux_pool_link(c->pool); /* use c->pool for subreq allocations */

    r = (ugh_subreq_external_t *) aux_pool_calloc(c->pool, sizeof(*r));

    if (NULL == r)
    {
        aux_pool_free(c->pool);
        return NULL;
    }

    r->c = c;

    r->timeout = UGH_CONFIG_SUBREQ_TIMEOUT; /* XXX this should be "no timeout" by default */

    log_debug("ugh_external_subreq_add");

    return r;
}

int ugh_subreq_external_set_timeout(ugh_subreq_external_t *r, ev_tstamp timeout, int timeout_type)
{
    r->timeout = timeout;
    return 0;
}

void ugh_subreq_external_signal_ready(ugh_subreq_external_t* ex_subreq) {
    ev_async_send(loop, &ex_subreq->wev_ready);
}

int ugh_subreq_external_run(ugh_subreq_external_t *r)
{
    r->c->wait++;
    r->request_done = ugh_subreq_external_signal_ready;
    ev_timer_init(&r->wev_timeout, &ugh_subreq_external_timeout, 0, r->timeout);
    ev_async_init(&r->wev_ready, &ugh_subreq_wcb_ready);

    ev_timer_again(loop, &r->wev_timeout);
    ev_async_start(loop, &r->wev_ready);
    return 0;
}

int ugh_subreq_external_del(ugh_subreq_external_t *r)
{
    ev_timer_stop(loop, &r->wev_timeout);
    ev_async_stop(loop, &r->wev_ready);

    r->c->wait--;

    /* We check is_main_coro here, because we could possibly call
     * ugh_subreq_del from module coroutine (e.g. when IP-address of
     * subrequest was definitely mallformed) and in this case we don't need
     * to call coro_transfer
     */
    if (0 == r->c->wait && is_main_coro)
    {
        is_main_coro = 0;
        coro_transfer(&ctx_main, &r->c->ctx);
        is_main_coro = 1;
    }
    aux_pool_free(r->c->pool);

    return 0;
}
