#ifndef VBS_AGENT_H
#define VBS_AGENT_H

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include "log.h"
#include "memcached.h"
//#include "cproxy.h"

#ifdef HAVE_CONFLATE_H
#include <libconflate/conflate.h>
#endif
#ifdef REDIRECTS_FOR_MOCKS
#include "redirects.h"
#endif


typedef struct {
    char *hostname;       // hostname of vbs server
    int port;             // port of vbs server
    void *userdata;       // proxy configuration to return in callbacl
    int (*new_config)(void *, void *); // callback function when new config is received

} vbs_config_t;

// VBS agent stats container
struct {
    pthread_mutex_t   mutex;
    uint64_t          config_received;
} vbsagent_stats;

int start_vbs_config(vbs_config_t config);

void proxy_stats_dump_vbsagent(ADD_STAT add_stats, conn *c, const char *prefix);

#endif

