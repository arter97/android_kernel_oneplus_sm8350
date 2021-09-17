// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various time related
 *  system calls: time, stime, gettimeofday, settimeofday, adjtime
 *
 * Modification history:
 *
 * 1993-09-02    Philip Gladstone
 *      Created file with time related functions from sched/core.c and adjtimex()
 * 1993-10-08    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1589)
 * 1999-01-16    Ulrich Windl
 *	Introduced error checking for many cases in adjtimex().
 *	Updated NTP code according to technical memorandum Jan '96
 *	"A Kernel Model for Precision Timekeeping" by Dave Mills
 *	Allow time_constant larger than MAXTC(6) for NTP v4 (MAXTC == 10)
 *	(Even though the technical memorandum forbids it)
 * 2004-07-14	 Christoph Lameter
 *	Added getnstimeofday to allow the posix timer functions to return
 *	with nanosecond accuracy
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/capability.h>
#include <linux/timekeeper_internal.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/math64.h>
#include <linux/ptrace.h>

#include <linux/uaccess.h>
#include <linux/compat.h>
#include <asm/unistd.h>

#include <generated/timeconst.h>
#include "timekeeping.h"

/*
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz;

EXPORT_SYMBOL(sys_tz);

#ifdef __ARCH_WANT_SYS_TIME

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
SYSCALL_DEFINE1(time, time_t __user *, tloc)
{
	time_t i = (time_t)ktime_get_real_seconds();

	if (tloc) {
		if (put_user(i,tloc))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return i;
}

/*
 * sys_stime() can be implemented in user-level using
 * sys_settimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */

SYSCALL_DEFINE1(stime, time_t __user *, tptr)
{
	struct timespec64 tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime64(&tv, NULL);
	if (err)
		return err;

	do_settimeofday64(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME */

#ifdef CONFIG_COMPAT_32BIT_TIME
#ifdef __ARCH_WANT_SYS_TIME32

/* old_time32_t is a 32 bit "long" and needs to get converted. */
SYSCALL_DEFINE1(time32, old_time32_t __user *, tloc)
{
	old_time32_t i;

	i = (old_time32_t)ktime_get_real_seconds();

	if (tloc) {
		if (put_user(i,tloc))
			return -EFAULT;
	}
	force_successful_syscall_return();
	return i;
}

SYSCALL_DEFINE1(stime32, old_time32_t __user *, tptr)
{
	struct timespec64 tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime64(&tv, NULL);
	if (err)
		return err;

	do_settimeofday64(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME32 */
#endif

SYSCALL_DEFINE2(gettimeofday, struct timeval __user *, tv,
		struct timezone __user *, tz)
{
	if (likely(tv != NULL)) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		if (put_user(ts.tv_sec, &tv->tv_sec) ||
		    put_user(ts.tv_nsec / 1000, &tv->tv_usec))
			return -EFAULT;
	}
	if (unlikely(tz != NULL)) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

/*
 * In case for some reason the CMOS clock has not already been running
 * in UTC, but in some local time: The first time we set the timezone,
 * we will warp the clock so that it is ticking UTC time instead of
 * local time. Presumably, if someone is setting the timezone then we
 * are running in an environment where the programs understand about
 * timezones. This should be done at boot time in the /etc/rc script,
 * as soon as possible, so that the clock can be set right. Otherwise,
 * various programs will get confused when the clock gets warped.
 */

int do_sys_settimeofday64(const struct timespec64 *tv, const struct timezone *tz)
{
	static int firsttime = 1;
	int error = 0;

	if (tv && !timespec64_valid_settod(tv))
		return -EINVAL;

	error = security_settime64(tv, tz);
	if (error)
		return error;

	if (tz) {
		/* Verify we're witin the +-15 hrs range */
		if (tz->tz_minuteswest > 15*60 || tz->tz_minuteswest < -15*60)
			return -EINVAL;

		sys_tz = *tz;
		update_vsyscall_tz();
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				timekeeping_warp_clock();
		}
	}
	if (tv)
		return do_settimeofday64(tv);
	return 0;
}

SYSCALL_DEFINE2(settimeofday, struct timeval __user *, tv,
		struct timezone __user *, tz)
{
	struct timespec64 new_ts;
	struct timeval user_tv;
	struct timezone new_tz;

	if (tv) {
		if (copy_from_user(&user_tv, tv, sizeof(*tv)))
			return -EFAULT;

		if (!timeval_valid(&user_tv))
			return -EINVAL;

		new_ts.tv_sec = user_tv.tv_sec;
		new_ts.tv_nsec = user_tv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(gettimeofday, struct old_timeval32 __user *, tv,
		       struct timezone __user *, tz)
{
	if (tv) {
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		if (put_user(ts.tv_sec, &tv->tv_sec) ||
		    put_user(ts.tv_nsec / 1000, &tv->tv_usec))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}

	return 0;
}

COMPAT_SYSCALL_DEFINE2(settimeofday, struct old_timeval32 __user *, tv,
		       struct timezone __user *, tz)
{
	struct timespec64 new_ts;
	struct timeval user_tv;
	struct timezone new_tz;

	if (tv) {
		if (compat_get_timeval(&user_tv, tv))
			return -EFAULT;

		if (!timeval_valid(&user_tv))
			return -EINVAL;

		new_ts.tv_sec = user_tv.tv_sec;
		new_ts.tv_nsec = user_tv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}
#endif

#if !defined(CONFIG_64BIT_TIME) || defined(CONFIG_64BIT)
SYSCALL_DEFINE1(adjtimex, struct __kernel_timex __user *, txc_p)
{
	struct __kernel_timex txc;		/* Local copy of parameter */
	int ret;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	if (copy_from_user(&txc, txc_p, sizeof(struct __kernel_timex)))
		return -EFAULT;
	ret = do_adjtimex(&txc);
	return copy_to_user(txc_p, &txc, sizeof(struct __kernel_timex)) ? -EFAULT : ret;
}
#endif

#ifdef CONFIG_COMPAT_32BIT_TIME
int get_old_timex32(struct __kernel_timex *txc, const struct old_timex32 __user *utp)
{
	struct old_timex32 tx32;

	memset(txc, 0, sizeof(struct __kernel_timex));
	if (copy_from_user(&tx32, utp, sizeof(struct old_timex32)))
		return -EFAULT;

	txc->modes = tx32.modes;
	txc->offset = tx32.offset;
	txc->freq = tx32.freq;
	txc->maxerror = tx32.maxerror;
	txc->esterror = tx32.esterror;
	txc->status = tx32.status;
	txc->constant = tx32.constant;
	txc->precision = tx32.precision;
	txc->tolerance = tx32.tolerance;
	txc->time.tv_sec = tx32.time.tv_sec;
	txc->time.tv_usec = tx32.time.tv_usec;
	txc->tick = tx32.tick;
	txc->ppsfreq = tx32.ppsfreq;
	txc->jitter = tx32.jitter;
	txc->shift = tx32.shift;
	txc->stabil = tx32.stabil;
	txc->jitcnt = tx32.jitcnt;
	txc->calcnt = tx32.calcnt;
	txc->errcnt = tx32.errcnt;
	txc->stbcnt = tx32.stbcnt;

	return 0;
}

int put_old_timex32(struct old_timex32 __user *utp, const struct __kernel_timex *txc)
{
	struct old_timex32 tx32;

	memset(&tx32, 0, sizeof(struct old_timex32));
	tx32.modes = txc->modes;
	tx32.offset = txc->offset;
	tx32.freq = txc->freq;
	tx32.maxerror = txc->maxerror;
	tx32.esterror = txc->esterror;
	tx32.status = txc->status;
	tx32.constant = txc->constant;
	tx32.precision = txc->precision;
	tx32.tolerance = txc->tolerance;
	tx32.time.tv_sec = txc->time.tv_sec;
	tx32.time.tv_usec = txc->time.tv_usec;
	tx32.tick = txc->tick;
	tx32.ppsfreq = txc->ppsfreq;
	tx32.jitter = txc->jitter;
	tx32.shift = txc->shift;
	tx32.stabil = txc->stabil;
	tx32.jitcnt = txc->jitcnt;
	tx32.calcnt = txc->calcnt;
	tx32.errcnt = txc->errcnt;
	tx32.stbcnt = txc->stbcnt;
	tx32.tai = txc->tai;
	if (copy_to_user(utp, &tx32, sizeof(struct old_timex32)))
		return -EFAULT;
	return 0;
}

SYSCALL_DEFINE1(adjtimex_time32, struct old_timex32 __user *, utp)
{
	struct __kernel_timex txc;
	int err, ret;

	err = get_old_timex32(&txc, utp);
	if (err)
		return err;

	ret = do_adjtimex(&txc);

	err = put_old_timex32(utp, &txc);
	if (err)
		return err;

	return ret;
}
#endif

int get_timespec64(struct timespec64 *ts,
		   const struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts;
	int ret;

	ret = copy_from_user(&kts, uts, sizeof(kts));
	if (ret)
		return -EFAULT;

	ts->tv_sec = kts.tv_sec;

	/* Zero out the padding for 32 bit systems or in compat mode */
	if (IS_ENABLED(CONFIG_64BIT_TIME) && (!IS_ENABLED(CONFIG_64BIT) ||
					      in_compat_syscall()))
		kts.tv_nsec &= 0xFFFFFFFFUL;

	ts->tv_nsec = kts.tv_nsec;

	return 0;
}
EXPORT_SYMBOL_GPL(get_timespec64);

int put_timespec64(const struct timespec64 *ts,
		   struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts = {
		.tv_sec = ts->tv_sec,
		.tv_nsec = ts->tv_nsec
	};

	return copy_to_user(uts, &kts, sizeof(kts)) ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(put_timespec64);

static int __get_old_timespec32(struct timespec64 *ts64,
				   const struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts;
	int ret;

	ret = copy_from_user(&ts, cts, sizeof(ts));
	if (ret)
		return -EFAULT;

	ts64->tv_sec = ts.tv_sec;
	ts64->tv_nsec = ts.tv_nsec;

	return 0;
}

static int __put_old_timespec32(const struct timespec64 *ts64,
				   struct old_timespec32 __user *cts)
{
	struct old_timespec32 ts = {
		.tv_sec = ts64->tv_sec,
		.tv_nsec = ts64->tv_nsec
	};
	return copy_to_user(cts, &ts, sizeof(ts)) ? -EFAULT : 0;
}

int get_old_timespec32(struct timespec64 *ts, const void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_from_user(ts, uts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __get_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(get_old_timespec32);

int put_old_timespec32(const struct timespec64 *ts, void __user *uts)
{
	if (COMPAT_USE_64BIT_TIME)
		return copy_to_user(uts, ts, sizeof(*ts)) ? -EFAULT : 0;
	else
		return __put_old_timespec32(ts, uts);
}
EXPORT_SYMBOL_GPL(put_old_timespec32);

int get_itimerspec64(struct itimerspec64 *it,
			const struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = get_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = get_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(get_itimerspec64);

int put_itimerspec64(const struct itimerspec64 *it,
			struct __kernel_itimerspec __user *uit)
{
	int ret;

	ret = put_timespec64(&it->it_interval, &uit->it_interval);
	if (ret)
		return ret;

	ret = put_timespec64(&it->it_value, &uit->it_value);

	return ret;
}
EXPORT_SYMBOL_GPL(put_itimerspec64);

int get_old_itimerspec32(struct itimerspec64 *its,
			const struct old_itimerspec32 __user *uits)
{

	if (__get_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __get_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(get_old_itimerspec32);

int put_old_itimerspec32(const struct itimerspec64 *its,
			struct old_itimerspec32 __user *uits)
{
	if (__put_old_timespec32(&its->it_interval, &uits->it_interval) ||
	    __put_old_timespec32(&its->it_value, &uits->it_value))
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(put_old_itimerspec32);
