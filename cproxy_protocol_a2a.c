/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include "memcached.h"
#include "cproxy.h"
#include "work.h"
#include "log.h"
#include <stdlib.h>

// Internal declarations.
//
#define KEY_TOKEN  1
#define MAX_TOKENS 9

extern bool cproxy_forward_or_error(downstream *d);

int a2a_multiget_start(conn *c, char *cmd, int cmd_len);
int a2a_multiget_skey(conn *c, char *skey, int skey_len, int vbucket, int key_index);
int a2a_multiget_end(conn *c);
static void create_options_for_upstream(conn *c, char *options, int *options_len);

void cproxy_init_a2a() {
    // Nothing right now.
}

void cproxy_process_a2a_downstream(conn *c, char *line) {
    assert(c != NULL);
    assert(c->next == NULL);
    assert(c->extra != NULL);
    assert(c->cmd == -1);
    assert(c->item == NULL);
    assert(line != NULL);
    assert(line == c->rcurr);
    assert(IS_ASCII(c->protocol));
    assert(IS_PROXY(c->protocol));

    if (settings.verbose > 1) {
        moxi_log_write("<%d cproxy_process_a2a_downstream %s\n",
                c->sfd, line);
    }

    downstream *d = c->extra;

    assert(d != NULL);
    assert(d->ptd != NULL);
    assert(d->ptd->proxy != NULL);

    zstored_downstream_conns *conns = zstored_get_downstream_conns(c->thread, c->host_ident);

    if (strncmp(line, "VALUE ", 6) == 0) {
        token_t      tokens[MAX_TOKENS];
        size_t       ntokens;
        unsigned int flags;
        int          clen = 0;
        int          vlen;
        uint64_t     cas = CPROXY_NOT_CAS;
        int          offset = 0;

#define FLAGS_INDEX     2
#define VAL_LEN_INDEX   3
#define CHKSUM_INDEX    4
#define CAS_INDEX       (4 + offset)

        if (conns->has_di)
            offset++;
        ntokens = scan_tokens(line, tokens, MAX_TOKENS, &clen);
        if (ntokens >= 5 && // Accounts for extra termimation token.
            ntokens <= 7 &&
            tokens[KEY_TOKEN].length <= KEY_MAX_LENGTH &&
            safe_strtoul(tokens[FLAGS_INDEX].value, (uint32_t *) &flags) &&
            safe_strtoul(tokens[VAL_LEN_INDEX].value, (uint32_t *) &vlen)) {
            char  *key  = tokens[KEY_TOKEN].value;
            size_t nkey = tokens[KEY_TOKEN].length;
            char *chksum = (conns->has_di) ? tokens[CHKSUM_INDEX].value : NULL; 

            item *it = item_alloc(key, nkey, flags, 0, chksum, vlen + 2);
            if (it != NULL) {
                if (ntokens == CAS_INDEX + 1 ||
                    safe_strtoull(tokens[CAS_INDEX].value, &cas)) {
                    ITEM_set_cas(it, cas);

                    c->item = it;
                    c->ritem = ITEM_data(it);
                    c->rlbytes = it->nbytes;
                    c->cmd = -1;

                    conn_set_state(c, conn_nread);

                    return; // Success.
                } else {
                    if (settings.verbose > 1) {
                        moxi_log_write("cproxy could not parse cas\n");
                    }
                }
            } else {
                item tmp_item;
                conn *uc = d->upstream_conn;

                if (uc != NULL && parse_chksum(chksum, &tmp_item) == false) {
                    out_string(uc, "SERVER_ERROR checksum failed, error parsing checksum");
                    if (!update_event(uc, EV_WRITE | EV_PERSIST)) {
                        if (settings.verbose > 1)
                            fprintf(stderr,
                                    "Can't update upstream write event\n");
                        cproxy_close_conn(uc);
                    }
                }
                else {
                    if (settings.verbose > 1)
                    fprintf(stderr, "cproxy could not item_alloc size %u\n",
                            vlen + 2);
                }
            }

            if (it != NULL) {
                item_remove(it);
            }

            it = NULL;

            c->sbytes = vlen + 2; // Number of bytes to swallow.

            conn_set_state(c, conn_swallow);

            // Note, eventually, we'll see an END later.
        } else {
            // We don't know how much to swallow, so close the downstream.
            // The conn_closing should release the downstream,
            // which should write a suffix/error to the upstream.
            //
            conn_set_state(c, conn_closing);
        }
    } else if (strncmp(line, "END", 3) == 0) {
        conn_set_state(c, conn_pause);
    } else if (strncmp(line, "OK", 2) == 0) {
        conn_set_state(c, conn_pause);

        // TODO: Handle flush_all's expiration parameter against
        // the front_cache.
        //
        // TODO: We flush the front_cache too often, inefficiently
        // on every downstream flush_all OK response, rather than
        // on just the last flush_all OK response.
        //
        conn *uc = d->upstream_conn;
        if (uc != NULL &&
            uc->cmd_curr == PROTOCOL_BINARY_CMD_FLUSH) {
            mcache_flush_all(&d->ptd->proxy->front_cache, 0);
        }
    } else if (strncmp(line, "STAT ", 5) == 0 ||
               strncmp(line, "ITEM ", 5) == 0 ||
               strncmp(line, "PREFIX ", 7) == 0) {
        assert(d->merger != NULL);

        conn *uc = d->upstream_conn;
        if (uc != NULL) {
            assert(uc->next == NULL);

            if (protocol_stats_merge_line(d->merger, line) == false) {
                // Forward the line as-is if we couldn't merge it.
                //
                int nline = strlen(line);

                item *it = item_alloc("s", 1, 0, 0, NULL, nline + 2);
                if (it != NULL) {
                    strncpy(ITEM_data(it), line, nline);
                    strncpy(ITEM_data(it) + nline, "\r\n", 2);

                    if (add_conn_item(uc, it)) {
                        add_iov(uc, ITEM_data(it), nline + 2);

                        it = NULL;
                    }

                    if (it != NULL) {
                        item_remove(it);
                    }
                }
            }
        }

        conn_set_state(c, conn_new_cmd);
      } else if (conns->got_options || strncmp(line, "options ", 7) == 0) {
        // If we send an "options" command, we expect either:
        // an "options" response OR
        // an error response (if the membase is old and doesnt understand "options"
        // In both cases, we parse the downstream response and if needed, send a response upstream.
        char options[MAX_OPTIONS_LEN];
        int options_len;
        conn *uc = d->upstream_conn;

        parse_options(NULL, conns, line);

        set_options_in_use(conns, uc);

        if( settings.verbose > 1 )
            moxi_log_write("<%d cproxy_process_a2a_downstream options handling line=%s\n", c->sfd, line);

        conns->got_options = false; // clear downstream state

        //uc->waiting_for_options = false;
        conn_set_state(c, conn_pause);

        //UC holds the upstream command which has been suspended to inject options
        bool upstream_waiting_for_options = uc->waiting_for_options;
        if( upstream_waiting_for_options == false )
        {
            conn_set_state(d->upstream_conn, conn_pause);
            cproxy_forward_or_error(d);
        }

        if (uc != NULL && uc->waiting_for_options == true) {
            uc->waiting_for_options = false;

            create_options_for_upstream(uc, options, &options_len);
            out_string(uc, options);

            if (!update_event(uc, EV_WRITE | EV_PERSIST)) {
                if (settings.verbose > 1)
                    fprintf(stderr,
                            "Can't update upstream write event\n");
                cproxy_close_conn(uc);
            }

            if (settings.verbose > 2)
                fprintf(stderr, "Checksum algorithm sent to upstream [%d] = [%x],\
                        downstream [%d], checksum algo supported by downstream %x\n",
                        uc->sfd, uc->tmp_di_algo, c->sfd, conns->data_integrity_algo);
        }
    } else if (strncmp(line, "LOCK_ERROR", 10) == 0) {
        d->upstream_suffix = "LOCK_ERROR\r\n";
        d->upstream_suffix_len = 0;
        d->upstream_status = PROTOCOL_BINARY_RESPONSE_ETMPFAIL;
        d->upstream_retry = 0;
        d->target_host_ident = NULL;

        conn_set_state(c, conn_pause);
    } else if (strncmp(line, "NOT_FOUND", 9) == 0) {
        d->upstream_suffix = "NOT_FOUND\r\n";
        d->upstream_suffix_len = 0;
        d->upstream_retry = 0;
        d->target_host_ident = NULL;

        conn_set_state(c, conn_pause);
    } else {
        conn_set_state(c, conn_pause);

        // The upstream conn might be NULL when closed already
        // or while handling a noreply.
        //
        conn *uc = d->upstream_conn;
        if (uc != NULL) {
            assert(uc->next == NULL);

            out_string(uc, line);

            if (!update_event(uc, EV_WRITE | EV_PERSIST)) {
                if (settings.verbose > 1) {
                    moxi_log_write("Can't update upstream write event\n");
                }

                d->ptd->stats.stats.err_oom++;
                cproxy_close_conn(uc);
            }

            cproxy_del_front_cache_key_ascii_response(d, line,
                                                      uc->cmd_start);
        }
    }

#undef FLAGS_INDEX
#undef VAL_LEN_INDEX
#undef CAS_INDEX
}

/* We get here after reading the value in a VALUE reply.
 * The item is ready in c->item.
 */
void cproxy_process_a2a_downstream_nread(conn *c) {
    assert(c != NULL);

    if (settings.verbose > 1) {
        moxi_log_write("<%d cproxy_process_a2a_downstream_nread %d %d\n",
                c->sfd, c->ileft, c->isize);
    }

    downstream *d = c->extra;
    assert(d != NULL);

    item *it = c->item;
    assert(it != NULL);

    // Clear c->item because we either move it to the upstream or
    // item_remove() it on error.
    //
    c->item = NULL;

    conn_set_state(c, conn_new_cmd);

    // pthread_mutex_lock(&c->thread->stats.mutex);
    // c->thread->stats.slab_stats[it->slabs_clsid].set_cmds++;
    // pthread_mutex_unlock(&c->thread->stats.mutex);

    multiget_ascii_downstream_response(d, it);

    item_remove(it);
}

/* Do the actual work of forwarding the command from an
 * upstream ascii conn to its assigned ascii downstream.
 */
bool cproxy_forward_a2a_downstream(downstream *d) {
    assert(d != NULL);

    conn *uc = d->upstream_conn;

    assert(uc != NULL);
    assert(uc->state == conn_pause);
    assert(uc->cmd_start != NULL);
    assert(uc->thread != NULL);
    assert(uc->thread->base != NULL);
    assert(IS_ASCII(uc->protocol));
    assert(IS_PROXY(uc->protocol));

    int server_index = -1;

    if (cproxy_is_broadcast_cmd(uc->cmd_curr) == true) {
        cproxy_ascii_broadcast_suffix(d);
    } else {
        char *key = NULL;
        int   key_len = 0;

        if (ascii_scan_key(uc->cmd_start, &key, &key_len) &&
            key != NULL &&
            key_len > 0) {
            server_index = cproxy_server_index(d, key, key_len, NULL);
            if (server_index < 0) {
                return false;
            }
        }
    }

    int nc = cproxy_connect_downstream(d, uc->thread, server_index);
    if (nc == -1) {
        return true;
    }

    if (nc > 0) {
        assert(d->downstream_conns != NULL);

        if (d->usec_start == 0 &&
            d->ptd->behavior_pool.base.time_stats) {
            d->usec_start = usec_now();
        }

        if (uc->cmd == -1) {
            return cproxy_forward_a2a_simple_downstream(d, uc->cmd_start, uc);
        } else {
            return cproxy_forward_a2a_item_downstream(d, uc->cmd, uc->item, uc);
        }
    }

    if (settings.verbose > 2) {
        moxi_log_write("%d: cproxy_forward_a2a_downstream connect failed\n",
                uc->sfd);
    }

    return false;
}

/* Forward a simple one-liner command downstream.
 * For example, get, incr/decr, delete, etc.
 * The response, though, might be a simple line or
 * multiple VALUE+END lines.
 */
bool cproxy_forward_a2a_simple_downstream(downstream *d,
                                          char *command, conn *uc) {
    assert(d != NULL);
    assert(d->ptd != NULL);
    assert(d->ptd->proxy != NULL);
    assert(d->downstream_conns != NULL);
    assert(command != NULL);
    assert(uc != NULL);
    //assert(uc->cmd_curr != (protocol_binary_command) -1);
    assert(d->multiget == NULL);
    assert(d->merger == NULL);

    /* Data Integrity options are received so process downstream as normal.  If not,
       then ths is the first downstream response so it would contain options response.
    */
    if( strstr(command,"options") == NULL )
    {
        // Handles get and gets.
        if (uc->cmd_curr == PROTOCOL_BINARY_CMD_GETK ||
            uc->cmd_curr == PROTOCOL_BINARY_CMD_GETKQ ||
            uc->cmd_curr == PROTOCOL_BINARY_CMD_GETLK) {
            // Only use front_cache for 'get', not for 'gets'.
            //
            mcache *front_cache =
                (command[3] == ' ') ? &d->ptd->proxy->front_cache : NULL;

            return multiget_ascii_downstream(d, uc,
                                             a2a_multiget_start,
                                             a2a_multiget_skey,
                                             a2a_multiget_end,
                                             front_cache);
        }

        assert(uc->next == NULL);

        if (uc->cmd_curr == PROTOCOL_BINARY_CMD_FLUSH) {
            return cproxy_broadcast_a2a_downstream(d, command, uc,
                                                   "OK\r\n");
        }

        if (uc->cmd_curr == PROTOCOL_BINARY_CMD_STAT) {
            if (strncmp(command + 5, " reset", 6) == 0) {
                return cproxy_broadcast_a2a_downstream(d, command, uc,
                                                       "RESET\r\n");
            }

            if (cproxy_broadcast_a2a_downstream(d, command, uc,
                                                "END\r\n")) {
                d->merger = genhash_init(512, skeyhash_ops);
                return true;
            } else {
                return false;
            }
        }
    }

    // TODO: Inefficient repeated scan_tokens.
    //
    int      cmd_len = 0;
    token_t  tokens[MAX_TOKENS];
    size_t   ntokens = scan_tokens(command, tokens, MAX_TOKENS, &cmd_len);
    char    *key     = tokens[KEY_TOKEN].value;
    int      key_len = tokens[KEY_TOKEN].length;

    if (ntokens <= 1) { // This was checked long ago, while parsing
        assert(false);  // the upstream conn.
        return false;
    }

    // Assuming we're already connected to downstream.
    //
    if (!strcmp(command, "version")||strstr(command,"option")) {
        /* fake key for version command handling */
        key = "v";
        key_len = 1;
    }

    conn *c = cproxy_find_downstream_conn(d, key, key_len, NULL);
    if (c != NULL) {

        if (cproxy_prep_conn_for_write(c)) {
            assert(c->state == conn_pause);

            out_string(c, command);

            if (settings.verbose > 1) {
                moxi_log_write("forwarding to %d, noreply %d\n",
                        c->sfd, uc->noreply);
            }

            if (update_event(c, EV_WRITE | EV_PERSIST)) {
                d->downstream_used_start = 1;
                d->downstream_used       = 1;

                if (cproxy_dettach_if_noreply(d, uc) == false) {
                    cproxy_start_downstream_timeout(d, c);
                } else {
                    c->write_and_go = conn_pause;

                    // Do mcache_delete() here only during a noreply,
                    // otherwise for with-reply requests, we could
                    // be in a race with other clients repopulating
                    // the front_cache.  For with-reply requests, we
                    // clear the front_cache when we get a success reply.
                    //
                    cproxy_front_cache_delete(d->ptd, key, key_len);
                }

                return true;
            }

            if (settings.verbose > 1) {
                moxi_log_write("Couldn't update cproxy write event\n");
            }

            d->ptd->stats.stats.err_oom++;
            cproxy_close_conn(c);
        } else {
            d->ptd->stats.stats.err_downstream_write_prep++;
            cproxy_close_conn(c);
        }
    }

    return false;
}

int a2a_multiget_start(conn *c, char *cmd, int cmd_len) {
    return add_iov(c, cmd, cmd_len);
}

/* An skey is a space prefixed key string.
 */
int a2a_multiget_skey(conn *c, char *skey, int skey_length, int vbucket, int key_index) {
    (void)vbucket;
    (void)key_index;
    return add_iov(c, skey, skey_length);
}

int a2a_multiget_end(conn *c) {
    return add_iov(c, "\r\n", 2);
}

/* Used for broadcast commands, like flush_all or stats.
 */
bool cproxy_broadcast_a2a_downstream(downstream *d,
                                     char *command,
                                     conn *uc,
                                     char *suffix) {
    assert(d != NULL);
    assert(d->ptd != NULL);
    assert(d->ptd->proxy != NULL);
    assert(d->downstream_conns != NULL);
    assert(d->downstream_used_start == 0);
    assert(d->downstream_used == 0);
    assert(command != NULL);
    assert(uc != NULL);
    assert(uc->next == NULL);
    assert(uc->item == NULL);

    int nwrite = 0;
    int nconns = mcs_server_count(&d->mst);

    for (int i = 0; i < nconns; i++) {
        conn *c = d->downstream_conns[i];
        if (c != NULL &&
            c != NULL_CONN) {
            if (cproxy_prep_conn_for_write(c)) {
                assert(c->state == conn_pause);

                out_string(c, command);

                if (update_event(c, EV_WRITE | EV_PERSIST)) {
                    nwrite++;

                    if (uc->noreply) {
                        c->write_and_go = conn_pause;
                    }
                } else {
                    if (settings.verbose > 1) {
                        moxi_log_write("Update cproxy write event failed\n");
                    }

                    d->ptd->stats.stats.err_oom++;
                    cproxy_close_conn(c);
                }
            } else {
                d->ptd->stats.stats.err_downstream_write_prep++;
                cproxy_close_conn(c);
            }
        }
    }

    if (settings.verbose > 1) {
        moxi_log_write("%d: a2a broadcast nwrite %d out of %d\n",
                uc->sfd, nwrite, nconns);
    }

    if (nwrite > 0) {
        d->downstream_used_start = nwrite;
        d->downstream_used       = nwrite;

        if (cproxy_dettach_if_noreply(d, uc) == false) {
            d->upstream_suffix = suffix;
            d->upstream_suffix_len = 0;
            d->upstream_status = PROTOCOL_BINARY_RESPONSE_SUCCESS;
            d->upstream_retry = 0;
            d->target_host_ident = NULL;

            cproxy_start_downstream_timeout(d, NULL);
        } else {
            // TODO: Handle flush_all's expiration parameter against
            // the front_cache.
            //
            if (strncmp(command, "flush_all", 9) == 0) {
                mcache_flush_all(&d->ptd->proxy->front_cache, 0);
            }
        }

        return true;
    }

    return false;
}

/* Forward an upstream command that came with item data,
 * like set/add/replace/etc.
 */
bool cproxy_forward_a2a_item_downstream(downstream *d, short cmd,
                                        item *it, conn *uc) {
    assert(d != NULL);
    assert(d->ptd != NULL);
    assert(d->ptd->proxy != NULL);
    assert(d->downstream_conns != NULL);
    assert(it != NULL);
    assert(uc != NULL);
    assert(uc->next == NULL);

    // Assuming we're already connected to downstream.
    //

    conn *c = cproxy_find_downstream_conn(d, ITEM_key(it), it->nkey, NULL);
    if (c != NULL) {

        zstored_downstream_conns *conns = zstored_get_downstream_conns(c->thread, c->host_ident);

        if (cproxy_prep_conn_for_write(c)) {
            assert(c->state == conn_pause);

            char *verb = nread_text(cmd);
            assert(verb != NULL);

            char *str_flags   = ITEM_suffix(it);
            char *str_length  = strchr(str_flags + 1, ' ');
            int   len_flags   = str_length - str_flags;
            int   len_length  = it->nsuffix - len_flags - 2;
            char *str_exptime = add_conn_suffix(c);
            char *str_cas     = (cmd == NREAD_CAS ? add_conn_suffix(c) : NULL);
            char *str_chksum  = NULL;

            if (str_flags != NULL &&
                str_length != NULL &&
                len_flags > 1 &&
                len_length > 1 &&
                str_exptime != NULL &&
                (cmd != NREAD_CAS ||
                 str_cas != NULL)) {
                sprintf(str_exptime, " %u", it->exptime);

                if (str_cas != NULL) {
                    sprintf(str_cas, " %llu",
                            (unsigned long long) ITEM_get_cas(it));
                }


                if (conns->has_di) {
                    // The downstream supports DI, so we have to 
                    // add a checksum to the header. 
                    str_chksum = add_conn_suffix(c);
                    // If the upstream also
                    // supports DI, then we pick up the checksum from the 
                    // item. Otherwise, we send DI_CHKSUM_SUPPORTED_OFF
                    if (uc->has_di) {
                        if (ITEM_chksum2(it) != 0) {
                            sprintf(str_chksum, " %.4x:%.8x:%.8x", it->chksum_metadata,
                                    ITEM_chksum(it), ITEM_chksum2(it));
                        }
                        else {
                            sprintf(str_chksum, " %.4x:%.8x", it->chksum_metadata,
                                    ITEM_chksum(it));
                        }
                    }
                    else {
                        sprintf(str_chksum, " %.4x:", DI_CHKSUM_SUPPORTED_OFF);
                    }
                }

                if (add_iov(c, verb, strlen(verb)) == 0 &&
                    add_iov(c, ITEM_key(it), it->nkey) == 0 &&
                    add_iov(c, str_flags, len_flags) == 0 &&
                    add_iov(c, str_exptime, strlen(str_exptime)) == 0 &&
                    add_iov(c, str_length, len_length) == 0 &&
                    (str_chksum == NULL ||
                     add_iov(c, str_chksum, strlen(str_chksum)) == 0) &&
                    (str_cas == NULL ||
                     add_iov(c, str_cas, strlen(str_cas)) == 0) &&
                    (uc->noreply == false ||
                     add_iov(c, " noreply", 8) == 0) &&
                    (uc->returncas == false ||
                     add_iov(c, " returncas", 10) == 0) &&
                    add_iov(c, ITEM_data(it) - 2, it->nbytes + 2) == 0) {
                    conn_set_state(c, conn_mwrite);
                    c->write_and_go = conn_new_cmd;

                    if (update_event(c, EV_WRITE | EV_PERSIST)) {
                        d->downstream_used_start = 1;
                        d->downstream_used       = 1;

                        if (cproxy_dettach_if_noreply(d, uc) == false) {
                            cproxy_start_downstream_timeout(d, c);

                            // During a synchronous (with-reply) SET,
                            // handle fire-&-forget SET optimization.
                            //
                            if (cmd == NREAD_SET &&
                                cproxy_optimize_set_ascii(d, uc,
                                                          ITEM_key(it),
                                                          it->nkey)) {
                                d->ptd->stats.stats.tot_optimize_sets++;
                            }
                        } else {
                            c->write_and_go = conn_pause;

                            cproxy_front_cache_delete(d->ptd,
                                                      ITEM_key(it), it->nkey);
                        }

                        return true;
                    }
                }

                d->ptd->stats.stats.err_oom++;
                cproxy_close_conn(c);
            } else {
                // TODO: Handle this weird error case.
            }
        } else {
            d->ptd->stats.stats.err_downstream_write_prep++;
            cproxy_close_conn(c);
        }

        if (settings.verbose > 1) {
            moxi_log_write("Proxy item write out of memory");
        }
    }

    return false;
}

void set_options_in_use(zstored_downstream_conns *conns, conn *uc) {
    int downstream_di_algo = DI_CHKSUM_UNSUPPORTED;
    int upstream_di_algo = DI_CHKSUM_UNSUPPORTED;

    if (conns) {
        downstream_di_algo = conns->data_integrity_algo;
    }

    if (uc)
        upstream_di_algo = uc->tmp_di_algo;

    if(downstream_di_algo != upstream_di_algo) {
        // If Membase doesnt understand checksums, there is no point in pecl-memcache generating them
        if (downstream_di_algo == DI_CHKSUM_UNSUPPORTED)
        {
            upstream_di_algo = DI_CHKSUM_UNSUPPORTED;
        }
        else if (downstream_di_algo & DI_CHKSUM_SUPPORTED_OFF)
        {
            upstream_di_algo = DI_CHKSUM_SUPPORTED_OFF;
        }
        // Fixing SEG-9473
        else if ((downstream_di_algo & DI_CHKSUM_CRC) && (upstream_di_algo != DI_CHKSUM_UNSUPPORTED))
        {
            upstream_di_algo = DI_CHKSUM_CRC;
        }
        // This is the case where either the downstream and/or upstream support
        // something other than CRC
        else {
            // If the upstream understands checksums, switch it off,
            // since the downstream wont send what it expects
            if (upstream_di_algo != DI_CHKSUM_UNSUPPORTED) {
                upstream_di_algo = DI_CHKSUM_SUPPORTED_OFF;
            }
        }
    }

    if (uc){
        uc->tmp_di_algo = upstream_di_algo;
    }

}

static void create_options_for_upstream(conn *c, char *options, int *options_len) {
    *options_len = sprintf(options, "options version=%s", VERSION);
    // If we dont understand some option, we obviously cannot make it into a string.
    // So in case of new pecl and new membase that support somethig more than CRC,
    // and old mcmux, we will setill be using CRC

    char *chksum_str = GET_CHKSUM_STR(c->tmp_di_algo);
    if (*chksum_str != '\0')
        *options_len += sprintf(options + *options_len, " DIAlgo=%s",
                GET_CHKSUM_STR(c->tmp_di_algo));
}


void send_options_upstream(conn *uc) {

    char options[MAX_OPTIONS_LEN];
    int options_len;

    create_options_for_upstream(uc, options, &options_len);
    out_string(uc, options);
}

// Will have only one of c (for upstream options) and d (for downstream options) 
// as non NULL
void parse_options(conn *c, zstored_downstream_conns *conns, char *options) {

    int algo = DI_CHKSUM_UNSUPPORTED;
    bool has_di = false;

    if(strncmp(options, "options", sizeof("options")-1) == 0) {
        // Checksums will be unsupported if:
        // - the downstream doesnt understan OPTIONS command OR
        // - the downstream understands OPTIONS command but it doesnt send the DIAlgo option in the reply
        char *di_algo =  strstr(options, "DIAlgo");
        if (di_algo != NULL) {
            has_di = true;
            char *algo_str = strchr(di_algo, '=');
            if (algo_str != NULL && strstr(algo_str, DI_CHKSUM_CRC_STR) != NULL) {
                algo = DI_CHKSUM_CRC;
            }
            else {
                algo = DI_CHKSUM_SUPPORTED_OFF;
            }
        }
    }
    else if (strncmp(options, "ERROR unknown command", sizeof("ERROR unknown command")-1) != 0) {
        if (settings.verbose > 1)
            fprintf(stderr, "Incorrect response from Membase for options command. Response: [%s]\n", options);
    }

    if (c) {
        c->tmp_di_algo = algo;
        c->has_di = has_di;
        if (settings.verbose > 2)
            fprintf(stderr, "Set DI support for upstream [%d] to %d\n", c->sfd, has_di);
    }
    else if (conns) {
        conns->data_integrity_algo = algo;
        conns->has_di = has_di;
        if (settings.verbose > 2)
            fprintf(stderr, "Set DI support for downstream [%s] to %d\n", conns->host_ident,conns->has_di);
    }
}
