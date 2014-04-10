/*
 * Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OFPBUF_H
#define OFPBUF_H 1

#include <stddef.h>
#include <stdint.h>
#include "list.h"
#include "packets.h"
#include "util.h"

#ifdef  __cplusplus
extern "C" {
#endif

enum OVS_PACKED_ENUM ofpbuf_source {
    OFPBUF_MALLOC,              /* Obtained via malloc(). */
    OFPBUF_STACK,               /* Un-movable stack space or static buffer. */
    OFPBUF_STUB,                /* Starts on stack, may expand into heap. */
    OFPBUF_DPDK,                /* buffer data is from DPDK allocated memory.
                                   ref to build_ofpbuf() in netdev-dpdk. */
};

/* Buffer for holding arbitrary data.  An ofpbuf is automatically reallocated
 * as necessary if it grows too large for the available memory. */
struct ofpbuf {
    void *base;                 /* First byte of allocated space. */
    uint32_t allocated;         /* Number of bytes allocated. */
    uint32_t size;              /* Number of bytes in use. */
    void *data;                 /* First byte actually in use. */

    void *l2;                   /* Link-level header. */
    uint16_t l2_5_ofs;          /* MPLS label stack offset from l2, or
                                 * UINT16_MAX */
    uint16_t l3_ofs;            /* Network-level header offset from l2, or
                                 * UINT16_MAX. */
    uint16_t l4_ofs;            /* Transport-level header offset from l2, or
                                   UINT16_MAX. */
    enum ofpbuf_source source;  /* Source of memory allocated as 'base'. */
    struct list list_node;      /* Private list element for use by owner. */
};

void * ofpbuf_resize_l2(struct ofpbuf *, int increment);
void * ofpbuf_resize_l2_5(struct ofpbuf *, int increment);
static inline void * ofpbuf_get_l2_5(const struct ofpbuf *);
static inline void ofpbuf_set_l2_5(struct ofpbuf *, void *);
static inline void * ofpbuf_get_l3(const struct ofpbuf *);
static inline void ofpbuf_set_l3(struct ofpbuf *, void *);
static inline void * ofpbuf_get_l4(const struct ofpbuf *);
static inline void ofpbuf_set_l4(struct ofpbuf *, void *);
static inline size_t ofpbuf_get_l4_size(const struct ofpbuf *);
static inline const void *ofpbuf_get_tcp_payload(const struct ofpbuf *);
static inline const void *ofpbuf_get_udp_payload(const struct ofpbuf *);
static inline const void *ofpbuf_get_sctp_payload(const struct ofpbuf *);
static inline const void *ofpbuf_get_icmp_payload(const struct ofpbuf *);

void ofpbuf_use(struct ofpbuf *, void *, size_t);
void ofpbuf_use_stack(struct ofpbuf *, void *, size_t);
void ofpbuf_use_stub(struct ofpbuf *, void *, size_t);
void ofpbuf_use_const(struct ofpbuf *, const void *, size_t);

void ofpbuf_init(struct ofpbuf *, size_t);
void ofpbuf_uninit(struct ofpbuf *);
static inline void *ofpbuf_get_uninit_pointer(struct ofpbuf *);
void ofpbuf_reinit(struct ofpbuf *, size_t);

struct ofpbuf *ofpbuf_new(size_t);
struct ofpbuf *ofpbuf_new_with_headroom(size_t, size_t headroom);
struct ofpbuf *ofpbuf_clone(const struct ofpbuf *);
struct ofpbuf *ofpbuf_clone_with_headroom(const struct ofpbuf *,
                                          size_t headroom);
struct ofpbuf *ofpbuf_clone_data(const void *, size_t);
struct ofpbuf *ofpbuf_clone_data_with_headroom(const void *, size_t,
                                               size_t headroom);
static inline void ofpbuf_delete(struct ofpbuf *);

static inline void *ofpbuf_at(const struct ofpbuf *, size_t offset,
                              size_t size);
static inline void *ofpbuf_at_assert(const struct ofpbuf *, size_t offset,
                                     size_t size);
static inline void *ofpbuf_tail(const struct ofpbuf *);
static inline void *ofpbuf_end(const struct ofpbuf *);

void *ofpbuf_put_uninit(struct ofpbuf *, size_t);
void *ofpbuf_put_zeros(struct ofpbuf *, size_t);
void *ofpbuf_put(struct ofpbuf *, const void *, size_t);
char *ofpbuf_put_hex(struct ofpbuf *, const char *s, size_t *n);
void ofpbuf_reserve(struct ofpbuf *, size_t);
void ofpbuf_reserve_with_tailroom(struct ofpbuf *b, size_t headroom,
                                  size_t tailroom);
void *ofpbuf_push_uninit(struct ofpbuf *b, size_t);
void *ofpbuf_push_zeros(struct ofpbuf *, size_t);
void *ofpbuf_push(struct ofpbuf *b, const void *, size_t);

static inline size_t ofpbuf_headroom(const struct ofpbuf *);
static inline size_t ofpbuf_tailroom(const struct ofpbuf *);
void ofpbuf_prealloc_headroom(struct ofpbuf *, size_t);
void ofpbuf_prealloc_tailroom(struct ofpbuf *, size_t);
void ofpbuf_trim(struct ofpbuf *);
void ofpbuf_padto(struct ofpbuf *, size_t);
void ofpbuf_shift(struct ofpbuf *, int);

static inline void ofpbuf_clear(struct ofpbuf *);
static inline void *ofpbuf_pull(struct ofpbuf *, size_t);
static inline void *ofpbuf_try_pull(struct ofpbuf *, size_t);

void *ofpbuf_steal_data(struct ofpbuf *);

char *ofpbuf_to_string(const struct ofpbuf *, size_t maxbytes);
static inline struct ofpbuf *ofpbuf_from_list(const struct list *);
void ofpbuf_list_delete(struct list *);
static inline bool ofpbuf_equal(const struct ofpbuf *, const struct ofpbuf *);


/* Returns a pointer that may be passed to free() to accomplish the same thing
 * as ofpbuf_uninit(b).  The return value is a null pointer if ofpbuf_uninit()
 * would not free any memory. */
static inline void *ofpbuf_get_uninit_pointer(struct ofpbuf *b)
{
    /* XXX: If 'source' is OFPBUF_DPDK memory gets leaked! */
    return b && b->source == OFPBUF_MALLOC ? b->base : NULL;
}

/* Frees memory that 'b' points to, as well as 'b' itself. */
static inline void ofpbuf_delete(struct ofpbuf *b)
{
    if (b) {
        ofpbuf_uninit(b);
        free(b);
    }
}

/* If 'b' contains at least 'offset + size' bytes of data, returns a pointer to
 * byte 'offset'.  Otherwise, returns a null pointer. */
static inline void *ofpbuf_at(const struct ofpbuf *b, size_t offset,
                              size_t size)
{
    return offset + size <= b->size ? (char *) b->data + offset : NULL;
}

/* Returns a pointer to byte 'offset' in 'b', which must contain at least
 * 'offset + size' bytes of data. */
static inline void *ofpbuf_at_assert(const struct ofpbuf *b, size_t offset,
                                     size_t size)
{
    ovs_assert(offset + size <= b->size);
    return ((char *) b->data) + offset;
}

/* Returns the byte following the last byte of data in use in 'b'. */
static inline void *ofpbuf_tail(const struct ofpbuf *b)
{
    return (char *) b->data + b->size;
}

/* Returns the byte following the last byte allocated for use (but not
 * necessarily in use) by 'b'. */
static inline void *ofpbuf_end(const struct ofpbuf *b)
{
    return (char *) b->base + b->allocated;
}

/* Returns the number of bytes of headroom in 'b', that is, the number of bytes
 * of unused space in ofpbuf 'b' before the data that is in use.  (Most
 * commonly, the data in a ofpbuf is at its beginning, and thus the ofpbuf's
 * headroom is 0.) */
static inline size_t ofpbuf_headroom(const struct ofpbuf *b)
{
    return (char*)b->data - (char*)b->base;
}

/* Returns the number of bytes that may be appended to the tail end of ofpbuf
 * 'b' before the ofpbuf must be reallocated. */
static inline size_t ofpbuf_tailroom(const struct ofpbuf *b)
{
    return (char*)ofpbuf_end(b) - (char*)ofpbuf_tail(b);
}

/* Clears any data from 'b'. */
static inline void ofpbuf_clear(struct ofpbuf *b)
{
    b->data = b->base;
    b->size = 0;
}

/* Removes 'size' bytes from the head end of 'b', which must contain at least
 * 'size' bytes of data.  Returns the first byte of data removed. */
static inline void *ofpbuf_pull(struct ofpbuf *b, size_t size)
{
    void *data = b->data;
    ovs_assert(b->size >= size);
    b->data = (char*)b->data + size;
    b->size -= size;
    return data;
}

/* If 'b' has at least 'size' bytes of data, removes that many bytes from the
 * head end of 'b' and returns the first byte removed.  Otherwise, returns a
 * null pointer without modifying 'b'. */
static inline void *ofpbuf_try_pull(struct ofpbuf *b, size_t size)
{
    return b->size >= size ? ofpbuf_pull(b, size) : NULL;
}

static inline struct ofpbuf *ofpbuf_from_list(const struct list *list)
{
    return CONTAINER_OF(list, struct ofpbuf, list_node);
}

static inline bool ofpbuf_equal(const struct ofpbuf *a, const struct ofpbuf *b)
{
    return a->size == b->size && memcmp(a->data, b->data, a->size) == 0;
}

static inline void * ofpbuf_get_l2_5(const struct ofpbuf *b)
{
    return b->l2_5_ofs != UINT16_MAX ? (char *)b->l2 + b->l2_5_ofs : NULL;
}

static inline void ofpbuf_set_l2_5(struct ofpbuf *b, void *l2_5)
{
    b->l2_5_ofs = l2_5 ? (char *)l2_5 - (char *)b->l2 : UINT16_MAX;
}

static inline void * ofpbuf_get_l3(const struct ofpbuf *b)
{
    return b->l3_ofs != UINT16_MAX ? (char *)b->l2 + b->l3_ofs : NULL;
}

static inline void ofpbuf_set_l3(struct ofpbuf *b, void *l3)
{
    b->l3_ofs = l3 ? (char *)l3 - (char *)b->l2 : UINT16_MAX;
}

static inline void * ofpbuf_get_l4(const struct ofpbuf *b)
{
    return b->l4_ofs != UINT16_MAX ? (char *)b->l2 + b->l4_ofs : NULL;
}

static inline void ofpbuf_set_l4(struct ofpbuf *b, void *l4)
{
    b->l4_ofs = l4 ? (char *)l4 - (char *)b->l2 : UINT16_MAX;
}

static inline size_t ofpbuf_get_l4_size(const struct ofpbuf *b)
{
    return b->l4_ofs != UINT16_MAX
        ? (const char *)ofpbuf_tail(b) - (const char *)ofpbuf_get_l4(b) : 0;
}

static inline const void *ofpbuf_get_tcp_payload(const struct ofpbuf *b)
{
    size_t l4_size = ofpbuf_get_l4_size(b);

    if (OVS_LIKELY(l4_size >= TCP_HEADER_LEN)) {
        struct tcp_header *tcp = ofpbuf_get_l4(b);
        int tcp_len = TCP_OFFSET(tcp->tcp_ctl) * 4;

        if (OVS_LIKELY(tcp_len >= TCP_HEADER_LEN && tcp_len <= l4_size)) {
            return (const char *)tcp + tcp_len;
        }
    }
    return NULL;
}

static inline const void *ofpbuf_get_udp_payload(const struct ofpbuf *b)
{
    return OVS_LIKELY(ofpbuf_get_l4_size(b) >= UDP_HEADER_LEN)
        ? (const char *)ofpbuf_get_l4(b) + UDP_HEADER_LEN : NULL;
}

static inline const void *ofpbuf_get_sctp_payload(const struct ofpbuf *b)
{
    return OVS_LIKELY(ofpbuf_get_l4_size(b) >= SCTP_HEADER_LEN)
        ? (const char *)ofpbuf_get_l4(b) + SCTP_HEADER_LEN : NULL;
}

static inline const void *ofpbuf_get_icmp_payload(const struct ofpbuf *b)
{
    return OVS_LIKELY(ofpbuf_get_l4_size(b) >= ICMP_HEADER_LEN)
        ? (const char *)ofpbuf_get_l4(b) + ICMP_HEADER_LEN : NULL;
}

#ifdef  __cplusplus
}
#endif

#endif /* ofpbuf.h */
