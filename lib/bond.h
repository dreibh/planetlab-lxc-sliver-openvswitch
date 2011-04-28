/*
 * Copyright (c) 2008, 2009, 2010, 2011 Nicira Networks.
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

#ifndef BOND_H
#define BOND_H 1

#include <stdbool.h>
#include <stdint.h>

#include "packets.h"
#include "tag.h"

struct flow;
struct netdev;
struct ofpbuf;

/* How flows are balanced among bond slaves. */
enum bond_mode {
    BM_TCP, /* Transport Layer Load Balance. */
    BM_SLB, /* Source Load Balance. */
    BM_STABLE, /* Stable. */
    BM_AB   /* Active Backup. */
};

bool bond_mode_from_string(enum bond_mode *, const char *);
const char *bond_mode_to_string(enum bond_mode);

/* How to detect link status. */
enum bond_detect_mode {
    BLSM_CARRIER,               /* Use carrier. */
    BLSM_MIIMON                 /* Poll MII status. */
};

bool bond_detect_mode_from_string(enum bond_detect_mode *, const char *);
const char *bond_detect_mode_to_string(enum bond_detect_mode);

/* Configuration for a bond as a whole. */
struct bond_settings {
    char *name;                 /* Bond's name, for log messages. */
    uint32_t basis;             /* Flow hashing basis. */

    /* Balancing configuration. */
    enum bond_mode balance;
    int rebalance_interval;     /* Milliseconds between rebalances. */

    /* Link status detection. */
    enum bond_detect_mode detect; /* BLSM_CARRIER or BLSM_MIIMON. */
    int miimon_interval;        /* Used only for BLSM_MIIMON. */
    int up_delay;               /* ms before enabling an up slave. */
    int down_delay;             /* ms before disabling a down slave. */

    /* Legacy compatibility. */
    bool fake_iface;            /* Update fake stats for netdev 'name'? */
};

/* Program startup. */
void bond_init(void);

/* Basics. */
struct bond *bond_create(const struct bond_settings *);
void bond_destroy(struct bond *);

bool bond_reconfigure(struct bond *, const struct bond_settings *);
void bond_slave_register(struct bond *, void *slave_,
                         uint16_t stable_id, struct netdev *);
void bond_slave_unregister(struct bond *, const void *slave);

void bond_run(struct bond *, struct tag_set *, bool lacp_negotiated);
void bond_wait(struct bond *);

/* LACP. */
void bond_slave_set_lacp_may_enable(struct bond *, void *slave_,
                                    bool may_enable);

/* Special MAC learning support for SLB bonding. */
bool bond_should_send_learning_packets(struct bond *);
int bond_send_learning_packet(struct bond *,
                              const uint8_t eth_src[ETH_ADDR_LEN],
                              uint16_t vlan);

/* Packet processing. */
enum bond_verdict {
    BV_ACCEPT,                  /* Accept this packet. */
    BV_DROP,                    /* Drop this packet. */
    BV_DROP_IF_MOVED            /* Drop if we've learned a different port. */
};
enum bond_verdict bond_check_admissibility(struct bond *, const void *slave_,
                                           const uint8_t eth_dst[ETH_ADDR_LEN],
                                           tag_type *);
void *bond_choose_output_slave(struct bond *,
                               const struct flow *, uint16_t vlan, tag_type *);

/* Rebalancing. */
void bond_account(struct bond *, const struct flow *, uint16_t vlan,
                  uint64_t n_bytes);
void bond_rebalance(struct bond *, struct tag_set *);

#endif /* bond.h */