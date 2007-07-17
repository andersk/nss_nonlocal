/*
 * nonlocal-passwd.c
 * passwd database for nss_nonlocal proxy.
 *
 * Copyright © 2007 Anders Kaseorg <andersk@mit.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#define _GNU_SOURCE
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <nss.h>
#include "nsswitch-internal.h"
#include "nonlocal.h"


static service_user *
nss_passwd_nonlocal_database(void)
{
    static service_user *nip = NULL;
    if (nip == NULL)
	__nss_database_lookup("passwd_nonlocal", NULL, "", &nip);

    return nip;
}


static __thread int local_only = 0;

enum nss_status
local_getpwuid_r(uid_t uid, struct passwd *pwd,
		 char *buffer, size_t buflen, int *errnop)
{
    int old_local_only = local_only;
    int old_errno = errno;
    int ret;
    errno = *errnop;
    local_only = 1;

    ret = getpwuid_r(uid, pwd, buffer, buflen, &pwd);

    local_only = old_local_only;
    *errnop = errno;
    errno = old_errno;

    if (pwd != NULL)
	return NSS_STATUS_SUCCESS;
    else if (ret == 0)
	return NSS_STATUS_NOTFOUND;
    else
	return NSS_STATUS_TRYAGAIN;
}

enum nss_status
check_nonlocal_uid(const char *user, uid_t uid, int *errnop)
{
    struct passwd local_pwd;
    int local_errno = errno;
    enum nss_status local_status, status = NSS_STATUS_SUCCESS;
    int local_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    char *local_buffer = malloc(local_buflen);
    if (local_buffer == NULL) {
	*errnop = ENOMEM;
	errno = local_errno;
	return NSS_STATUS_TRYAGAIN;
    }
    local_errno = 0;
    local_status = local_getpwuid_r(uid, &local_pwd, local_buffer,
				    local_buflen, &local_errno);
    if (local_status == NSS_STATUS_SUCCESS) {
	syslog(LOG_ERR, "nss_nonlocal: possible spoofing attack: non-local user %s has same UID as local user %s!\n", user, local_pwd.pw_name);
	status = NSS_STATUS_NOTFOUND;
    } else if (local_status != NSS_STATUS_NOTFOUND &&
	       local_status != NSS_STATUS_UNAVAIL) {
	*errnop = local_errno;
	status = local_status;
    }
    free(local_buffer);
    return status;
}


static service_user *pwent_nip = NULL;
static void *pwent_fct_start;
static union {
    enum nss_status (*l)(struct passwd *pwd, char *buffer, size_t buflen,
			 int *errnop);
    void *ptr;
} pwent_fct;
static const char *pwent_fct_name = "getpwent_r";

enum nss_status
_nss_nonlocal_setpwent(int stayopen)
{
    static const char *fct_name = "setpwent";
    static void *fct_start = NULL;
    enum nss_status status;
    service_user *nip;
    union {
	enum nss_status (*l)(int stayopen);
	void *ptr;
    } fct;

    nip = nss_passwd_nonlocal_database();
    if (nip == NULL)
	return NSS_STATUS_UNAVAIL;
    if (fct_start == NULL)
	fct_start = __nss_lookup_function(nip, fct_name);
    fct.ptr = fct_start;
    do {
	if (fct.ptr == NULL)
	    status = NSS_STATUS_UNAVAIL;
	else
	    status = DL_CALL_FCT(fct.l, (stayopen));
    } while (__nss_next(&nip, fct_name, &fct.ptr, status, 0) == 0);
    if (status != NSS_STATUS_SUCCESS)
	return status;

    pwent_nip = nip;
    if (pwent_fct_start == NULL)
	pwent_fct_start = __nss_lookup_function(nip, pwent_fct_name);
    pwent_fct.ptr = pwent_fct_start;
    return NSS_STATUS_SUCCESS;
}

enum nss_status
_nss_nonlocal_endpwent(void)
{
    static const char *fct_name = "endpwent";
    static void *fct_start = NULL;
    enum nss_status status;
    service_user *nip;
    union {
	enum nss_status (*l)(void);
	void *ptr;
    } fct;

    pwent_nip = NULL;

    nip = nss_passwd_nonlocal_database();
    if (nip == NULL)
	return NSS_STATUS_UNAVAIL;
    if (fct_start == NULL)
	fct_start = __nss_lookup_function(nip, fct_name);
    fct.ptr = fct_start;
    do {
	if (fct.ptr == NULL)
	    status = NSS_STATUS_UNAVAIL;
	else
	    status = DL_CALL_FCT(fct.l, ());
    } while (__nss_next(&nip, fct_name, &fct.ptr, status, 0) == 0);
    return status;
}

enum nss_status
_nss_nonlocal_getpwent_r(struct passwd *pwd, char *buffer, size_t buflen,
			 int *errnop)
{
    enum nss_status status;
    if (pwent_nip == NULL) {
	status = _nss_nonlocal_setpwent(0);
	if (status != NSS_STATUS_SUCCESS)
	    return status;
    }
    do {
	if (pwent_fct.ptr == NULL)
	    status = NSS_STATUS_UNAVAIL;
	else {
	    int nonlocal_errno;
	    do
		status = DL_CALL_FCT(pwent_fct.l, (pwd, buffer, buflen, errnop));	
	    while (status == NSS_STATUS_SUCCESS &&
		   check_nonlocal_uid(pwd->pw_name, pwd->pw_uid, &nonlocal_errno) != NSS_STATUS_SUCCESS);
	}
	if (status == NSS_STATUS_TRYAGAIN && *errnop == ERANGE)
	    return status;

	if (status == NSS_STATUS_SUCCESS)
	    return NSS_STATUS_SUCCESS;
    } while (__nss_next(&pwent_nip, pwent_fct_name, &pwent_fct.ptr, status, 0) == 0);

    pwent_nip = NULL;
    return NSS_STATUS_NOTFOUND;
}


enum nss_status
_nss_nonlocal_getpwnam_r(const char *name, struct passwd *pwd,
			 char *buffer, size_t buflen, int *errnop)
{
    static const char *fct_name = "getpwnam_r";
    static void *fct_start = NULL;
    enum nss_status status;
    service_user *nip;
    union {
	enum nss_status (*l)(const char *name, struct passwd *pwd,
			     char *buffer, size_t buflen, int *errnop);
	void *ptr;
    } fct;
    int group_errno;

    nip = nss_passwd_nonlocal_database();
    if (nip == NULL)
	return NSS_STATUS_UNAVAIL;
    if (fct_start == NULL)
	fct_start = __nss_lookup_function(nip, fct_name);
    fct.ptr = fct_start;
    do {
	if (fct.ptr == NULL)
	    status = NSS_STATUS_UNAVAIL;
	else
	    status = DL_CALL_FCT(fct.l, (name, pwd, buffer, buflen, errnop));
	if (status == NSS_STATUS_TRYAGAIN && *errnop == ERANGE)
	    break;
    } while (__nss_next(&nip, fct_name, &fct.ptr, status, 0) == 0);
    if (status != NSS_STATUS_SUCCESS)
	return status;

    status = check_nonlocal_uid(name, pwd->pw_uid, errnop);
    if (status != NSS_STATUS_SUCCESS)
	return status;

    if (check_nonlocal_gid(name, pwd->pw_gid, &group_errno) !=
	NSS_STATUS_SUCCESS)
	pwd->pw_gid = 65534 /* nogroup */;
    return NSS_STATUS_SUCCESS;
}

enum nss_status
_nss_nonlocal_getpwuid_r(uid_t uid, struct passwd *pwd,
			 char *buffer, size_t buflen, int *errnop)
{
    static const char *fct_name = "getpwuid_r";
    static void *fct_start = NULL;
    enum nss_status status;
    service_user *nip;
    union {
	enum nss_status (*l)(uid_t uid, struct passwd *pwd,
			     char *buffer, size_t buflen, int *errnop);
	void *ptr;
    } fct;
    int group_errno;

    if (local_only == 1)
	return NSS_STATUS_UNAVAIL;

    nip = nss_passwd_nonlocal_database();
    if (nip == NULL)
	return NSS_STATUS_UNAVAIL;
    if (fct_start == NULL)
	fct_start = __nss_lookup_function(nip, fct_name);
    fct.ptr = fct_start;
    do {
	if (fct.ptr == NULL)
	    status = NSS_STATUS_UNAVAIL;
	else
	    status = DL_CALL_FCT(fct.l, (uid, pwd, buffer, buflen, errnop));
	if (status == NSS_STATUS_TRYAGAIN && *errnop == ERANGE)
	    break;
    } while (__nss_next(&nip, fct_name, &fct.ptr, status, 0) == 0);
    if (status != NSS_STATUS_SUCCESS)
	return status;

    status = check_nonlocal_uid(pwd->pw_name, pwd->pw_uid, errnop);
    if (status != NSS_STATUS_SUCCESS)
	return status;

    if (check_nonlocal_gid(pwd->pw_name, pwd->pw_gid, &group_errno) !=
	NSS_STATUS_SUCCESS)
	pwd->pw_gid = 65534 /* nogroup */;
    return NSS_STATUS_SUCCESS;
}
