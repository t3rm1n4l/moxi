/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "vbs_agent.h"
#include <pthread.h>
#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <arpa/inet.h>        /*  inet (3) funtions         */
#include <unistd.h>
#include "cJSON.h"

#define INIT_CMD    "{\"Agent\":\"MOXI\"}"
#define OK_CMD      "{\"Cmd\":\"CONFIG\", \"Status\":\"OK\"}"
#define ALIVE_CMD   "{\"Cmd\":\"ALIVE\"}"

int connect_server(vbs_config_t *config);
int read_socket(int fd, char **buf, int heartbeat);
void vbs_get_config(void *arg);


int connect_server(vbs_config_t *config) {

    // make a connection to the vbs server
    int vbs_fd;
    struct sockaddr_in vbs_addr;
    struct hostent *he = NULL;

    if ((vbs_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {

        moxi_log_write("ERROR: unable to create socket\n");
        return -1;
    }


    memset(&vbs_addr, 0, sizeof(vbs_addr));
    vbs_addr.sin_family = AF_INET;
    vbs_addr.sin_port = htons(config->port);

    if (!(he = gethostbyname(config->hostname))) {
        moxi_log_write("ERROR: unable to resolve address %s\n", config->hostname);
        return -1;
    }

    struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
    vbs_addr.sin_addr = *addr_list[0];

    //now connect to the vbs server

    if (connect(vbs_fd, (struct sockaddr *)&vbs_addr, sizeof(vbs_addr)) < 0) {
        moxi_log_write("ERROR: unable to connect to server  %s\n", config->hostname);
        return -1;
    }

    return vbs_fd;

}


//read data from socket. Called when data available in the socket
//read an entire chunk before returning
int read_socket(int fd, char **buf, int heartbeat) {
    int data_len = 0, rlen = 0, len;
    char *wbuf;

    if (heartbeat) {
        struct timeval tv={heartbeat, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(tv));
    }


    //read four bytes from the socket
    read(fd, &data_len, sizeof(data_len));
    if (data_len == 0) {
        //timeout on the read socket or close
        return data_len;
    }
    data_len = ntohl(data_len);
    wbuf = *buf = malloc(data_len);
    // Read full
    while (data_len != rlen) {
        len = read(fd, (void *)wbuf, data_len - rlen);
        if (len > 0) {
            wbuf += len;
            rlen += len;
        } else if (len == EWOULDBLOCK) { // timed out
            return 0;
        } else if (len != EINTR && len != EAGAIN) {
            break;
        }
    }

    if (len <= 0) {
       // socket is not non blocking. return with an error
        moxi_log_write("ERROR: Unable to read from socket %d (%s)\n", fd, strerror(len));
        free(*buf);
        return -1;
    }

    return data_len;
}

inline static int write_len(char *cmd, int fd) {
    int cmd_len = htonl(strlen(cmd));
    int wlen = write(fd, (char *)&cmd_len, sizeof(int));
    return wlen;
}

void vbs_get_config(void *arg) {

    vbs_config_t *config = (vbs_config_t *)arg;
    assert(config);
    int hb_interval = 0;
    int vbs_fd;
    int retry_connection = 0;

retry:


    vbs_fd = 0;
    if (retry_connection != 0) {
            sleep(5);
    }

    retry_connection++;

    while (vbs_fd <= 0) {

       vbs_fd = connect_server(config);

        if (vbs_fd < 0) {
            moxi_log_write("ERROR: Sleep for 5 seconds  %s\n", config->hostname);
            sleep(5);
        }
    }

    //connected process init command
    char *buf = NULL;
    if (read_socket(vbs_fd, &buf, hb_interval) < 0) {
        //retry connect
        moxi_log_write("ERROR: Unable to read from socket retry connection  %s\n", config->hostname);
        close(vbs_fd);
        goto retry;
    }
    free(buf);

    //send the moxi init command
    write_len(INIT_CMD, vbs_fd);
    if (write(vbs_fd, INIT_CMD, strlen(INIT_CMD)) < 0) {
        moxi_log_write("ERROR: Unable to write to socket retry connection  %s\n", config->hostname);
        close(vbs_fd);
        goto retry;
    }

    // main config loop. Receive config command from moxi
    while (1) {

        int read_len = 0;

        if ((read_len = read_socket(vbs_fd, &buf, hb_interval)) < 0) {
            moxi_log_write("ERROR: Unable to write to socket retry connection  %s\n", config->hostname);
            close(vbs_fd);
            goto retry;
        } else if (read_len == 0) {

            // heartbeat interval
            write_len(ALIVE_CMD, vbs_fd);
            if (write(vbs_fd, ALIVE_CMD, strlen(ALIVE_CMD)) < 0) {
                moxi_log_write("ERROR: Unable to write to socket retry connection  %s\n", config->hostname);
                close(vbs_fd);
                goto retry;
            }
        } else {

            //process moxi config command
            //test the json config here before passing it on the cproxy thread
            cJSON *c = cJSON_Parse(buf);
            if (c != NULL) {
                //json okay. extract heartbeat interval
                cJSON *heartbeat = cJSON_GetObjectItem(c, "HeartBeatTime");
                if (heartbeat != NULL &&
                    heartbeat->type == cJSON_Number &&
                    heartbeat->valueint > 0) {
                    hb_interval = heartbeat->valueint;
                } else {
                    hb_interval = 10; // 10 seconds
                }

                cJSON *data = cJSON_GetObjectItem(c, "Data");
                if (data == NULL) {
                    moxi_log_write("ERROR : Unable to parse JSON config\n");
                } else {
                    if (!config->new_config(config->userdata, data)) {
                        moxi_log_write("ERROR: Failed to update config\n");
                    }
                }

                cJSON_Delete(c);
            }

            free(buf);

            pthread_mutex_lock(&vbsagent_stats.mutex);
            vbsagent_stats.config_received++;
            pthread_mutex_unlock(&vbsagent_stats.mutex);

            write_len(OK_CMD, vbs_fd);
            if (write(vbs_fd, OK_CMD, strlen(OK_CMD)) < 0) {
                moxi_log_write("ERROR: Unable to write to socket retry connection  %s\n", config->hostname);
                close(vbs_fd);
                goto retry;
            }
        }
    }
}

int start_vbs_config(vbs_config_t config) {

    // connection established. launch the protocol thread
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    vbsagent_stats.config_received = 0;

    pthread_create(&thread, &attr, (void *)vbs_get_config, &config);

    return 1;

}

void proxy_stats_dump_vbsagent(ADD_STAT add_stats, conn *c, const char *prefix) {
    pthread_mutex_lock(&vbsagent_stats.mutex);
    APPEND_PREFIX_STAT("config_received", "%d", vbsagent_stats.config_received);
    pthread_mutex_unlock(&vbsagent_stats.mutex);
}
