/*
 * nonlocal.h
 * common definitions for nss_nonlocal proxy
 *
 * Copyright © 2007–2010 Anders Kaseorg <andersk@mit.edu> and Tim
 * Abbott <tabbott@mit.edu>
 *
 * This file is part of nss_nonlocal.
 *
 * nss_nonlocal is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * nss_nonlocal is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with nss_nonlocal; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef NONLOCAL_H
#define NONLOCAL_H

#include "config.h"

enum nss_status check_nonlocal_uid(const char *user, uid_t uid, int *errnop);
enum nss_status check_nonlocal_gid(const char *user, gid_t gid, int *errnop);
enum nss_status check_nonlocal_user(const char *user, int *errnop);

#define NONLOCAL_IGNORE_ENV "NSS_NONLOCAL_IGNORE"

#endif /* NON_LOCAL_H */
