/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "context.h"

#include "event.h"
#include "upf-sm.h"

#include "pfcp-path.h"
#include "n4-build.h"

static void pfcp_recv_cb(short when, ogs_socket_t fd, void *data)
{
    int rv;

    ssize_t size;
    upf_event_t *e = NULL;
    ogs_pkbuf_t *pkbuf = NULL;
    ogs_sockaddr_t from;
    ogs_pfcp_node_t *pnode = NULL;
    ogs_pfcp_header_t *h = NULL;

    ogs_assert(fd != INVALID_SOCKET);

    pkbuf = ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    ogs_pkbuf_put(pkbuf, OGS_MAX_SDU_LEN);

    size = ogs_recvfrom(fd, pkbuf->data, pkbuf->len, 0, &from);
    if (size <= 0) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                "ogs_recvfrom() failed");
        ogs_pkbuf_free(pkbuf);
        return;
    }

    ogs_pkbuf_trim(pkbuf, size);

    h = (ogs_pfcp_header_t *)pkbuf->data;
    if (h->version > OGS_PFCP_VERSION) {
        ogs_pfcp_header_t rsp;

        ogs_error("Not supported version[%d]", h->version);

        memset(&rsp, 0, sizeof rsp);
        rsp.flags = (OGS_PFCP_VERSION << 5);
        rsp.type = OGS_PFCP_VERSION_NOT_SUPPORTED_RESPONSE_TYPE;
        rsp.length = htobe16(4);
        rsp.sqn_only = h->sqn_only;
        ogs_sendto(fd, &rsp, 8, 0, &from);
        ogs_pkbuf_free(pkbuf);

        return;
    }

    e = upf_event_new(UPF_EVT_N4_MESSAGE);
    pnode = ogs_pfcp_node_find_by_addr(&upf_self()->smf_n4_list, &from);
    if (!pnode) {
        pnode = ogs_pfcp_node_add_by_addr(&upf_self()->smf_n4_list, &from);
        ogs_assert(pnode);
        pnode->sock = data;
    }
    ogs_assert(e);
    e->pnode = pnode;
    e->pkbuf = pkbuf;

    rv = ogs_queue_push(upf_self()->queue, e);
    if (rv != OGS_OK) {
        ogs_error("ogs_queue_push() failed:%d", (int)rv);
        ogs_pkbuf_free(e->pkbuf);
        upf_event_free(e);
    }
}

int upf_pfcp_open(void)
{
    ogs_socknode_t *node = NULL;
    ogs_sock_t *sock = NULL;

    /* PFCP Server */
    ogs_list_for_each(&upf_self()->pfcp_list, node) {
        sock = ogs_pfcp_server(node);
        ogs_assert(sock);
        
        node->poll = ogs_pollset_add(upf_self()->pollset,
                OGS_POLLIN, sock->fd, pfcp_recv_cb, sock);
    }
    ogs_list_for_each(&upf_self()->pfcp_list6, node) {
        sock = ogs_pfcp_server(node);
        ogs_assert(sock);

        node->poll = ogs_pollset_add(upf_self()->pollset,
                OGS_POLLIN, sock->fd, pfcp_recv_cb, sock);
    }

    upf_self()->pfcp_sock = ogs_socknode_sock_first(&upf_self()->pfcp_list);
    if (upf_self()->pfcp_sock)
        upf_self()->pfcp_addr = &upf_self()->pfcp_sock->local_addr;

    upf_self()->pfcp_sock6 = ogs_socknode_sock_first(&upf_self()->pfcp_list6);
    if (upf_self()->pfcp_sock6)
        upf_self()->pfcp_addr6 = &upf_self()->pfcp_sock6->local_addr;

    ogs_assert(upf_self()->pfcp_addr || upf_self()->pfcp_addr6);

    return OGS_OK;
}

void upf_pfcp_close(void)
{
    ogs_socknode_remove_all(&upf_self()->pfcp_list);
    ogs_socknode_remove_all(&upf_self()->pfcp_list6);
}

static void timeout(ogs_pfcp_xact_t *xact, void *data)
{
}

void upf_pfcp_send_association_setup_request(ogs_pfcp_node_t *pnode)
{
    int rv;
    ogs_pkbuf_t *n4buf = NULL;
    ogs_pfcp_header_t h;
    ogs_pfcp_xact_t *xact = NULL;

    ogs_assert(pnode);

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_ASSOCIATION_SETUP_REQUEST_TYPE;
    h.seid = 0;

    n4buf = upf_n4_build_association_setup_request(h.type);
    ogs_expect_or_return(n4buf);

    xact = ogs_pfcp_xact_local_create(pnode, &h, n4buf, timeout, pnode);
    ogs_expect_or_return(xact);

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);
}