// SPDX-License-Identifier: LicenseRef-Elastic-License-2.0

/*
 * Copyright 2021 Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under
 * one or more contributor license agreements. Licensed under the Elastic
 * License 2.0; you may not use this file except in compliance with the Elastic
 * License 2.0.
 */

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#include "LibEbpfEvents.h"

#include <bpf/libbpf.h>
#include <errno.h>
#include <stdio.h>

#include "EventProbe.skel.h"

struct ebpf_event_ctx {
    struct ring_buffer *ringbuf;
    struct EventProbe_bpf *probe;
};

struct ring_buf_cb_ctx {
    ebpf_event_handler_fn cb;
    uint64_t events;
};

/* This is just a thin wrapper that calls the event context's saved callback */
static int ring_buf_cb(void *ctx, void *data, size_t size)
{
    struct ring_buf_cb_ctx *cb_ctx = ctx;
    ebpf_event_handler_fn cb       = cb_ctx->cb;
    struct ebpf_event_header *evt  = data;
    if (evt->type & cb_ctx->events) {
        return cb(evt);
    }
    return 0;
}

int ebpf_event_ctx__new(struct ebpf_event_ctx **ctx,
                        ebpf_event_handler_fn cb,
                        uint64_t features,
                        uint64_t events)
{
    struct ring_buf_cb_ctx *cb_ctx = NULL;

    int err;
    *ctx = calloc(1, sizeof(struct ebpf_event_ctx));
    if (*ctx == NULL)
        return -ENOMEM;

    (*ctx)->probe = EventProbe_bpf__open();
    if ((*ctx)->probe == NULL) {
        free(*ctx);

        /* EventProbe_bpf__open doesn't report errors, hard to find something
         * that fits perfect here
         */
        return -ENOENT;
    }

    err = EventProbe_bpf__load((*ctx)->probe);
    if (err != 0)
        goto out_destroy_probe;

    err = EventProbe_bpf__attach((*ctx)->probe);
    if (err != 0)
        goto out_destroy_probe;

    struct ring_buffer_opts opts;
    opts.sz = sizeof(opts);

    cb_ctx = calloc(1, sizeof(struct ring_buf_cb_ctx));
    if (cb_ctx == NULL) {
        err = -ENOMEM;
        goto out_destroy_probe;
    }

    cb_ctx->cb     = cb;
    cb_ctx->events = events;

    (*ctx)->ringbuf =
        ring_buffer__new(bpf_map__fd((*ctx)->probe->maps.ringbuf), ring_buf_cb, cb_ctx, &opts);

    if ((*ctx)->ringbuf == NULL) {
        /* ring_buffer__new doesn't report errors, hard to find something that
         * fits perfect here
         */
        err = -ENOENT;
        goto out_destroy_probe;
    }

    return ring_buffer__epoll_fd((*ctx)->ringbuf);

out_destroy_probe:
    EventProbe_bpf__destroy((*ctx)->probe);
    free(*ctx);
    *ctx = NULL;
    if (cb_ctx) {

        free(cb_ctx);
        cb_ctx = NULL;
    }
    return err;
}

int ebpf_event_ctx__next(struct ebpf_event_ctx *ctx, int timeout)
{
    int consumed = ring_buffer__poll(ctx->ringbuf, timeout);
    return consumed > 0 ? 0 : consumed;
}

void ebpf_event_ctx__destroy(struct ebpf_event_ctx *ctx)
{
    ring_buffer__free(ctx->ringbuf);
    EventProbe_bpf__destroy(ctx->probe);
}
