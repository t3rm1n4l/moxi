/*
 *   Copyright 2013 Zynga Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
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
#include "cproxy.h"

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

// Failure message for host
typedef struct m {
    char *host;
    struct m *next;
} vbs_failmsg;

// Failure queue used to transmit fail message by downstream handlers
struct {
    pthread_mutex_t   mutex;
    vbs_failmsg       *head, *tail;
} vbs_failmsg_queue;

// Helper method to place fail message to queue's back
void vbs_add_failentry(vbs_failmsg *msg);

// Peek the at queue's front
vbs_failmsg  *vbs_peek_failentry(void);

// Remove entry from queue's front
void vbs_remove_failentry(void);

// Helper method to parse host_ident for hostname and place on vbs fail queue
void vbs_notify_hostfail(char const *host_ident);

// Global map for keeping track of connect failures to hosts
struct {
    pthread_mutex_t    mutex;
    genhash_t          *map;
} hostfail_counter;

// Init hashtable
void hostfailcounter_init(void);

// Return a pointer to errcount variable if an entry exists or create a counter
// and return the pointer to it
volatile uint32_t *hostfailcounter_addhost(char *h);

// Atomically increment the error counter
uint32_t hostfailcounter_incr(volatile uint32_t *cntr);

// Atomically reset error counter to zero
bool hostfailcounter_reset(volatile uint32_t *cntr, uint32_t val);

#endif
