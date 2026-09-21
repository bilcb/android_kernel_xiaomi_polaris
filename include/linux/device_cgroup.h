#include <linux/fs.h>
#include <linux/bpf-cgroup.h>

#if defined(CONFIG_CGROUP_DEVICE) || defined(CONFIG_CGROUP_BPF)
#ifdef CONFIG_CGROUP_DEVICE
extern int __devcgroup_inode_permission(struct inode *inode, int mask);
extern int __devcgroup_inode_mknod(int mode, dev_t dev);
#endif

/*
 * eBPF device filtering (cgroup v2 device control).  Backported from
 * upstream 4.15: a BPF_PROG_TYPE_CGROUP_DEVICE program attached to the
 * task's default (v2) cgroup runs first and can deny with -EPERM.
 * Works even when the legacy device controller is disabled; the v1
 * whitelist is consulted afterwards when enabled.
 */
static inline int devcgroup_inode_permission(struct inode *inode, int mask)
{
#ifdef CONFIG_CGROUP_DEVICE
	if (likely(!inode->i_rdev))
		return 0;
	if (!S_ISBLK(inode->i_mode) && !S_ISCHR(inode->i_mode))
		return 0;
	return __devcgroup_inode_permission(inode, mask);
#else
	short type, access = 0;

	if (likely(!inode->i_rdev))
		return 0;
	if (!S_ISBLK(inode->i_mode) && !S_ISCHR(inode->i_mode))
		return 0;

	type = S_ISBLK(inode->i_mode) ? BPF_DEVCG_DEV_BLOCK :
					BPF_DEVCG_DEV_CHAR;
	if (mask & MAY_WRITE)
		access |= BPF_DEVCG_ACC_WRITE;
	if (mask & MAY_READ)
		access |= BPF_DEVCG_ACC_READ;

	if (BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(type, imajor(inode),
					      iminor(inode), access))
		return -EPERM;
	return 0;
#endif
}

static inline int devcgroup_inode_mknod(int mode, dev_t dev)
{
#ifdef CONFIG_CGROUP_DEVICE
	return __devcgroup_inode_mknod(mode, dev);
#else
	short type;

	if (!S_ISBLK(mode) && !S_ISCHR(mode))
		return 0;

	type = S_ISBLK(mode) ? BPF_DEVCG_DEV_BLOCK : BPF_DEVCG_DEV_CHAR;

	if (BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(type, MAJOR(dev), MINOR(dev),
					      BPF_DEVCG_ACC_MKNOD))
		return -EPERM;
	return 0;
#endif
}

#else /* neither CONFIG_CGROUP_DEVICE nor CONFIG_CGROUP_BPF */
static inline int devcgroup_inode_permission(struct inode *inode, int mask)
{ return 0; }
static inline int devcgroup_inode_mknod(int mode, dev_t dev)
{ return 0; }
#endif
