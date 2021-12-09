// SPDX-License-Identifier: LicenseRef-Elastic-License-2.0

/*
 * Copyright 2021 Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License 2.0;
 * you may not use this file except in compliance with the Elastic License 2.0.
 */

#include <stdio.h>
#include <signal.h>

#include <libebpf.h>
#include "FileEvents.skel.h"
#include "FileEvents.h"

static volatile sig_atomic_t exiting = 0;

static void sig_int(int signo)
{
    exiting = 1;
    fprintf(stdout, "Received SIGINT, Exiting...\n");
}

static void ebpf_file_event_path__tostring(struct ebpf_event_file_path path, char *pathbuf)
{
    strcpy(pathbuf, "/");
    for (int i = 0; i < path.patharray_len; i++)
    {
        strcat(pathbuf, path.path_array[i]);

        if (i != path.patharray_len - 1)
        {
            strcat(pathbuf, "/");
        }
    }
}

static int buf_process_sample(void *ctx, void *data, size_t len)
{
    struct ebpf_event *evt = (struct ebpf_event *)data;

    if (evt->data == NULL)
    {
        printf("[SKIP] Event with no data\n");
        return 0;
    }
    switch(evt->type)
    {
        case EBPF_EVENT_FILE_DELETE:
            struct ebpf_event_file_delete_data *evt_data = (struct ebpf_event_file_delete_data *)evt->data;
            char pathbuf[MAX_FILEPATH_LENGTH];
            ebpf_file_event_path__tostring(evt_data->path, pathbuf);
            printf("[EBPF_EVENT_FILE_DELETE]: (%d) %s\n", evt_data->pid, pathbuf);
            break;
    }

    return 0;
}

// TODO: change this to use the libebpf.h header, once we have an implementation for it
int main(int argc, char const *argv[])
{
    int err = 0;
    struct FileEvents_bpf *probe = NULL;

    if (signal(SIGINT, sig_int) == SIG_ERR)
    {
        fprintf(stderr, "Failed to register SIGINT handler\n");
        goto cleanup;
    }

    probe = FileEvents_bpf__open_and_load();
    err = FileEvents_bpf__attach(probe);
    if (err < 0)
    {
        fprintf(stderr, "Failed to attach probe\n");
        goto cleanup;
    }

    // todo(fntlnz): this is temporary, until I use the libebpf code here
    struct ring_buffer *ring_buffer;
    ring_buffer = ring_buffer__new(bpf_map__fd(probe->maps.elastic_ebpf_events_buffer), buf_process_sample, NULL, NULL);

    while(!exiting)
    {
        err = ring_buffer__poll(ring_buffer, 100);
        if (err == -EINTR)
        {
            err = 0;
            break;
        }
        if (err < 0)
        {
            fprintf(stderr, "Failed to poll ring buffer\n");
            break;
        }
    }

cleanup:
    if (probe)
    {
        FileEvents_bpf__destroy(probe);
    }
    return err != 0;
}
