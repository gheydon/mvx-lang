/*
 * MVX — a native compiler and runtime for Pick/MultiValue BASIC.
 * Copyright (C) 2026 Gordon Heydon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.  There is NO WARRANTY, to
 * the extent permitted by law; see the LICENSE file for details.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* mvxd wire protocol — shared by the daemon and the lmdbnet driver.
 *
 * Frames are length-prefixed:
 *   request:  u32 payload-length | u8 op | fields
 *   response: u32 payload-length | u8 status | fields
 * Field encodings: spec/item/id are u16 length + bytes; record data is
 * u32 length + bytes.  Integers are little-endian host order (the
 * daemon and its clients are assumed same-architecture for now).
 *
 * Requests carry the file spec each time — the protocol is stateless
 * apart from locks, which are leased to the CONNECTION and released
 * by the daemon when it drops (ARCHITECTURE.md 4.3).
 */
#ifndef MVXD_PROTO_H
#define MVXD_PROTO_H

enum {
    MVXD_OP_OPEN = 1,       /* spec -> ok/notfound */
    MVXD_OP_READ,           /* spec, id -> ok+data / notfound */
    MVXD_OP_WRITE,          /* spec, id, data -> ok */
    MVXD_OP_DEL,            /* spec, id -> ok/notfound */
    MVXD_OP_SELECT,         /* spec -> count, ids (snapshot, then sent) */
    MVXD_OP_CREATE,         /* spec -> ok / exists(no) */
    MVXD_OP_REMOVE,         /* spec -> ok / absent(no) */
    MVXD_OP_NAMES,          /* -> count, names */
    MVXD_OP_LOCK,           /* spec, id -> ok / busy */
    MVXD_OP_UNLOCK,         /* spec, id -> ok */
    MVXD_OP_WRITE_IX,       /* spec, id, data, nops{item,key,add} -> ok */
    MVXD_OP_DEL_IX,         /* spec, id, nops{item,key,add} -> ok/notfound */
    MVXD_OP_IDX_SELECT,     /* spec, item, key -> count, ids / notfound */
    MVXD_OP_IDX_DROP,       /* spec, item -> ok/notfound */
};

enum {
    MVXD_ST_OK = 0,
    MVXD_ST_NO = 1,         /* not found / already exists / absent */
    MVXD_ST_BUSY = 2,       /* lock held by another connection */
    MVXD_ST_ERR = 3,
};

#endif /* MVXD_PROTO_H */
