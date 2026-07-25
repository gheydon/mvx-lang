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

/* mvx-lmdbd wire protocol — shared by the daemon and the lmdbnet driver.
 *
 * Frames are length-prefixed:
 *   request:  u32 payload-length | u8 op | fields
 *   response: u32 payload-length | u8 status | fields
 * Field encodings: spec/item/id are u16 length + bytes; record data is
 * u32 length + bytes.  Integers are little-endian host order (the
 * daemon and its clients are assumed same-architecture for now).
 *
 * Every request begins with a u16 NAMESPACE (the target account, an
 * LMDB environment of its own on the daemon: same-named files in
 * different namespaces are isolated), then the fields below.  The
 * protocol is stateless apart from locks, which are leased to the
 * CONNECTION and released by the daemon when it drops (ARCHITECTURE.md
 * 4.3); lock keys are scoped by namespace.
 */
#ifndef MVXD_PROTO_H
#define MVXD_PROTO_H

enum {
    MVXD_OP_OPEN = 1,       /* ns, spec -> ok/notfound */
    MVXD_OP_READ,           /* ns, spec, id -> ok+data / notfound */
    MVXD_OP_WRITE,          /* ns, spec, id, data -> ok */
    MVXD_OP_DEL,            /* ns, spec, id -> ok/notfound */
    MVXD_OP_SELECT,         /* ns, spec -> count, ids (snapshot, then sent) */
    MVXD_OP_CREATE,         /* ns, spec -> ok / exists(no) */
    MVXD_OP_REMOVE,         /* ns, spec -> ok / absent(no) */
    MVXD_OP_NAMES,          /* ns -> count, names */
    MVXD_OP_LOCK,           /* ns, spec, id -> ok / busy */
    MVXD_OP_UNLOCK,         /* ns, spec, id -> ok */
    MVXD_OP_WRITE_IX,       /* ns, spec, id, data, nops{item,key,add} -> ok */
    MVXD_OP_DEL_IX,         /* ns, spec, id, nops{item,key,add} -> ok/notfound */
    MVXD_OP_IDX_SELECT,     /* ns, spec, item, key -> count, ids / notfound */
    MVXD_OP_IDX_DROP,       /* ns, spec, item -> ok/notfound */
    MVXD_OP_AUTH,           /* ns, token -> ok / denied (authorise the conn) */
};

enum {
    MVXD_ST_OK = 0,
    MVXD_ST_NO = 1,         /* not found / already exists / absent */
    MVXD_ST_BUSY = 2,       /* lock held by another connection */
    MVXD_ST_ERR = 3,
    MVXD_ST_DENIED = 4,     /* authentication required / failed */
};

#endif /* MVXD_PROTO_H */
