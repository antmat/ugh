#include "ugh.h"

static
int ugh_channel_del_memory(ugh_channel_t *ch)
{
	ev_async_stop(loop, &ch->wev_message);

	JudyLFreeArray(&ch->messages_hash, PJE0);
	JudyLFreeArray(&ch->clients_hash, PJE0);

	aux_pool_free(ch->pool);

	return 0;
}

static
void ugh_channel_wcb_delete(EV_P_ ev_async *w, int tev)
{
	ugh_channel_t *ch = aux_memberof(ugh_channel_t, wev_message, w);

	log_info("received message for channel=%p (status=%d)", ch, ch->status);

	/* invoke all waiting clients */

	Word_t idx = 0;
	int rc;

	for (rc = Judy1First(ch->clients_hash, &idx, PJE0); 0 != rc;
		 rc = Judy1Next (ch->clients_hash, &idx, PJE0))
	{
		ugh_client_t *c = (ugh_client_t *) idx;

		is_main_coro = 0;
		coro_transfer(&ctx_main, &c->ctx);
		is_main_coro = 1;
	}

	if (ch->status == UGH_CHANNEL_DELETED)
	{
		ugh_channel_del_memory(ch);
	}
}

ugh_channel_t *ugh_channel_add(ugh_server_t *s, strp channel_id)
{
	/* check if channel already exist and return if it does */

	void **dest = JudyLIns(&s->channels_hash, aux_hash_key(channel_id->data, channel_id->size), PJE0);
	if (PJERR == dest) return NULL;

	if (NULL != *dest)
	{
		log_warn("trying to add channel on existing channel_id=%.*s", (int) channel_id->size, channel_id->data);
		return *dest;
	}

	/* create new channel */

	aux_pool_t *pool;
	ugh_channel_t *ch;

	pool = aux_pool_init(0);
	if (NULL == pool) return NULL;

	ch = (ugh_channel_t *) aux_pool_calloc(pool, sizeof(*ch));

	if (NULL == ch)
	{
		aux_pool_free(pool);
		return NULL;
	}

	ch->s = s;
	ch->pool = pool;

	ev_async_init(&ch->wev_message, ugh_channel_wcb_delete);
	ev_async_start(loop, &ch->wev_message);

	*dest = ch;

	return ch;
}

ugh_channel_t *ugh_channel_get(ugh_server_t *s, strp channel_id)
{
	void **dest;

	dest = JudyLGet(s->channels_hash, aux_hash_key(channel_id->data, channel_id->size), PJE0);
	if (PJERR == dest || NULL == dest) return NULL;

	return *dest;
}

int ugh_channel_del(ugh_server_t *s, strp channel_id)
{
	Word_t idx = aux_hash_key(channel_id->data, channel_id->size);

	void **dest;

	dest = JudyLGet(s->channels_hash, idx, PJE0);
	if (PJERR == dest || NULL == dest) return -1;

	ugh_channel_t *ch = *dest;

	ch->status = UGH_CHANNEL_DELETED;

	ev_async_send(loop, &ch->wev_message);

	/* 
	 * we remove channel from server list here, but free channel memory only
	 * after all clients will be served with 410 Gone respone
	 */

	JudyLDel(&s->channels_hash, idx, PJE0);

	return 0;
}

int ugh_channel_add_message(ugh_channel_t *ch, strp body, strp content_type)
{
	Word_t etag = -1;
	void **dest;

	if (NULL != JudyLLast(ch->messages_hash, &etag, PJE0))
	{
		++etag;
	}
	else
	{
		etag = 1; /* first possible etag */
	}

	dest = JudyLIns(&ch->messages_hash, etag, PJE0);
	if (PJERR == dest) return -1;

	/* creating message */

	ugh_channel_message_t *m = aux_pool_malloc(ch->pool, sizeof(*m));
	if (NULL == m) return -1;

	m->body.data = aux_pool_strdup(ch->pool, body);
	m->body.size = body->size;

	m->content_type.data = aux_pool_strdup(ch->pool, content_type);
	m->content_type.size = content_type->size;

	*dest = m;

	ev_async_send(loop, &ch->wev_message);

	return 0;
}

static
int ugh_channel_gen_message(ugh_channel_t *ch, ugh_client_t *c, strp body, Word_t etag)
{
	void **dest = JudyLNext(ch->messages_hash, &etag, PJE0);
	if (NULL == dest) return -1; /* no new messages for this client */

	ugh_channel_message_t *m = *dest;

	body->data = aux_pool_strdup(c->pool, &m->body);
	body->size = m->body.size;

	char *content_type_data = aux_pool_strdup(c->pool, &m->content_type);
	ugh_client_header_out_set(c, "Content-Type", sizeof("Content-Type") - 1, content_type_data, m->content_type.size);

	char *etag_data = aux_pool_malloc(c->pool, 11); /* XXX 11 is maximum length of unsigned 32bit value plus 1 byte */
	int etag_size = snprintf(etag_data, 11, "%lu", etag);

	ugh_client_header_out_set(c, "Etag", sizeof("Etag") - 1, etag_data, etag_size);

	return 0;
}

int ugh_channel_get_message(ugh_channel_t *ch, ugh_client_t *c, strp body, unsigned type)
{
	if (ch->status == UGH_CHANNEL_DELETED) /* XXX do we need this check here? */
	{
		return -1;
	}

	/* check if the next entry exist */

	ugh_header_t *h_if_none_match = ugh_client_header_get_nt(c, "If-None-Match");

	Word_t etag = strtoul(h_if_none_match->value.data, NULL, 10);

	if (0 == ugh_channel_gen_message(ch, c, body, etag))
	{
		return 0;
	}

	/* next entry was not found, wait for new entries or return immediately */

	if (type == UGH_CHANNEL_INTERVAL_POLL)
	{
		return 0;
	}

	Judy1Set(&ch->clients_hash, (uintptr_t) c, PJE0);
	ch->clients_size++;

	is_main_coro = 1;
	coro_transfer(&c->ctx, &ctx_main);
	is_main_coro = 0;

	if (ch->status == UGH_CHANNEL_DELETED)
	{
		return -1;
	}

	return ugh_channel_gen_message(ch, c, body, etag);
}
