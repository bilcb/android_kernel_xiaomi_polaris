/*
 * cpuacct declarations now live in <linux/cgroup.h> (cgroup_account_cputime
 * wrappers, backported from upstream 4.15).  Kept as a compat shim for
 * sched/ users.
 */
#include <linux/cgroup.h>
