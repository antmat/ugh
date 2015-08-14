#include <ugh/ugh.h>

typedef struct
{
    ugh_template_t unused_configurable_parameter;
} ugh_module_ext_example_conf_t;

extern ugh_module_t ugh_module_ext_example;

void run_ex_request(void* data, ugh_subreq_external_t* ex_subreq) {
    ex_subreq->body.size = 9;
    ex_subreq->body.data = "It works";
    ex_subreq->on_request_done(ex_subreq);
}

int ugh_module_ext_example_handle(ugh_client_t *c, void *data, strp body)
{
//    ugh_module_ext_example_conf_t *conf = data; /* get module conf */

    ugh_subreq_external_t *e_subreq = ugh_subreq_external_add(c);
    e_subreq->run_external = run_ex_request;
    ugh_subreq_external_run(e_subreq);

    /* wait for it (do smth in different coroutine while it is being downloaded) */

    ugh_subreq_wait(c);

    body->data = e_subreq->body.data;
    body->size = e_subreq->body.size;

    return UGH_HTTP_OK;
}

int ugh_module_ext_example_init(ugh_config_t *cfg, void *data)
{
    ugh_module_ext_example_conf_t *conf = data;

    log_info("ugh_module_ext_example_init (called for each added handle, each time server is starting), conf=%p", &conf);

    return 0;
}

int ugh_module_ext_example_free(ugh_config_t *cfg, void *data)
{
    log_info("ugh_module_ext_example_free (called for each added handle, each time server is stopped)");

    return 0;
}

int ugh_command_ext_example(ugh_config_t *cfg, int argc, char **argv, ugh_command_t *cmd)
{
    ugh_module_ext_example_conf_t *conf;

    conf = aux_pool_malloc(cfg->pool, sizeof(*conf));
    if (NULL == conf) return -1;

    ugh_module_handle_add(ugh_module_ext_example, conf, ugh_module_ext_example_handle);

    return 0;
}

static ugh_command_t ugh_module_ext_example_cmds [] =
{
    ugh_make_command(ext_example),
    ugh_make_command_template(unused_configurable_parameter, ugh_module_ext_example_conf_t, unused_configurable_parameter),
    ugh_null_command
};

ugh_module_t ugh_module_ext_example =
{
    ugh_module_ext_example_cmds,
    ugh_module_ext_example_init,
    ugh_module_ext_example_free
};

