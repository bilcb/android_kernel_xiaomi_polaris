/*
 * Functions to manage eBPF programs attached to cgroups
 *
 * Copyright (c) 2016 Daniel Mack
 *
 * This file is subject to the terms and conditions of version 2 of the GNU
 * General Public License.  See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/bpf-cgroup.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <net/sock.h>

DEFINE_STATIC_KEY_FALSE(cgroup_bpf_enabled_key);
EXPORT_SYMBOL(cgroup_bpf_enabled_key);

/**
 * cgroup_bpf_put() - put references of all bpf programs
 * @cgrp: the cgroup to modify
 */
void cgroup_bpf_put(struct cgroup *cgrp)
{
	unsigned int type;

	for (type = 0; type < ARRAY_SIZE(cgrp->bpf.progs); type++) {
		struct list_head *progs = &cgrp->bpf.progs[type];
		struct bpf_prog_list *pl, *tmp;

		list_for_each_entry_safe(pl, tmp, progs, node) {
			list_del(&pl->node);
			bpf_prog_put(pl->prog);
			kfree(pl);
			static_branch_dec(&cgroup_bpf_enabled_key);
		}
		bpf_prog_array_free(cgrp->bpf.effective[type]);
	}
}

/* count number of elements in the list.
 * it's slow but the list cannot be long
 */
static u32 prog_list_length(struct list_head *head)
{
	struct bpf_prog_list *pl;
	u32 cnt = 0;

	list_for_each_entry(pl, head, node) {
		if (!pl->prog)
			continue;
		cnt++;
	}
	return cnt;
}

/* if parent has non-overridable prog attached,
 * disallow attaching new programs to the descendent cgroup.
 * if parent has overridable or multi-prog, allow attaching
 */
static bool hierarchy_allows_attach(struct cgroup *cgrp,
				    enum bpf_attach_type type,
				    u32 new_flags)
{
	struct cgroup *p;

	p = cgroup_parent(cgrp);
	if (!p)
		return true;
	do {
		u32 flags = p->bpf.flags[type];
		u32 cnt;

		if (flags & BPF_F_ALLOW_MULTI)
			return true;
		cnt = prog_list_length(&p->bpf.progs[type]);
		WARN_ON_ONCE(cnt > 1);
		if (cnt == 1)
			return !!(flags & BPF_F_ALLOW_OVERRIDE);
		p = cgroup_parent(p);
	} while (p);
	return true;
}

/* compute a chain of effective programs for a given cgroup:
 * start from the list of programs in this cgroup and add
 * all parent programs.
 * Note that parent's F_ALLOW_OVERRIDE-type program is yielding
 * to programs in this cgroup
 */
static int compute_effective_progs(struct cgroup *cgrp,
				   enum bpf_attach_type type,
				   struct bpf_prog_array __rcu **array)
{
	struct bpf_prog_array *progs;
	struct bpf_prog_list *pl;
	struct cgroup *p = cgrp;
	int cnt = 0;

	/* count number of effective programs by walking parents */
	do {
		if (cnt == 0 || (p->bpf.flags[type] & BPF_F_ALLOW_MULTI))
			cnt += prog_list_length(&p->bpf.progs[type]);
		p = cgroup_parent(p);
	} while (p);

	progs = bpf_prog_array_alloc(cnt, GFP_KERNEL);
	if (!progs)
		return -ENOMEM;

	/* populate the array with effective progs */
	cnt = 0;
	p = cgrp;
	do {
		if (cnt == 0 || (p->bpf.flags[type] & BPF_F_ALLOW_MULTI))
			list_for_each_entry(pl,
					    &p->bpf.progs[type], node) {
				if (!pl->prog)
					continue;
				progs->progs[cnt++] = pl->prog;
			}
		p = cgroup_parent(p);
	} while (p);

	rcu_assign_pointer(*array, progs);
	return 0;
}

static void activate_effective_progs(struct cgroup *cgrp,
				     enum bpf_attach_type type,
				     struct bpf_prog_array __rcu *array)
{
	struct bpf_prog_array __rcu *old_array;

	old_array = xchg(&cgrp->bpf.effective[type], array);
	/* free prog array after grace period, since __cgroup_bpf_run_*()
	 * might be still walking the array
	 */
	bpf_prog_array_free(old_array);
}

/**
 * cgroup_bpf_inherit() - inherit effective programs from parent
 * @cgrp: the cgroup to modify
 */
int cgroup_bpf_inherit(struct cgroup *cgrp)
{
/* has to use marco instead of const int, since compiler thinks
 * that array below is variable length
 */
#define	NR ARRAY_SIZE(cgrp->bpf.effective)
	struct bpf_prog_array __rcu *arrays[NR] = {};
	int i;

	for (i = 0; i < NR; i++)
		INIT_LIST_HEAD(&cgrp->bpf.progs[i]);

	for (i = 0; i < NR; i++)
		if (compute_effective_progs(cgrp, i, &arrays[i]))
			goto cleanup;

	for (i = 0; i < NR; i++)
		activate_effective_progs(cgrp, i, arrays[i]);

	return 0;
cleanup:
	for (i = 0; i < NR; i++)
		bpf_prog_array_free(arrays[i]);
	return -ENOMEM;
}

#define BPF_CGROUP_MAX_PROGS 64

/**
 * __cgroup_bpf_attach() - Attach the program to a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to attach
 * @type: Type of attach operation
 *
 * Must be called with cgroup_mutex held.
 */
int __cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type, u32 flags)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	struct bpf_prog *old_prog = NULL;
	struct cgroup_subsys_state *css;
	struct bpf_prog_list *pl;
	bool pl_was_allocated;
	u32 old_flags;
	int err;

	if ((flags & BPF_F_ALLOW_OVERRIDE) && (flags & BPF_F_ALLOW_MULTI))
		/* invalid combination */
		return -EINVAL;

	if (!hierarchy_allows_attach(cgrp, type, flags))
		return -EPERM;

	if (!list_empty(progs) && cgrp->bpf.flags[type] != flags)
		/* Disallow attaching non-overridable on top
		 * of existing overridable in this cgroup.
		 * Disallow attaching multi-prog if overridable or none
		 */
		return -EPERM;

	if (prog_list_length(progs) >= BPF_CGROUP_MAX_PROGS)
		return -E2BIG;

	if (flags & BPF_F_ALLOW_MULTI) {
		list_for_each_entry(pl, progs, node)
			if (pl->prog == prog)
				/* disallow attaching the same prog twice */
				return -EINVAL;

		pl = kmalloc(sizeof(*pl), GFP_KERNEL);
		if (!pl)
			return -ENOMEM;
		pl_was_allocated = true;
		pl->prog = prog;
		list_add_tail(&pl->node, progs);
	} else {
		if (list_empty(progs)) {
			pl = kmalloc(sizeof(*pl), GFP_KERNEL);
			if (!pl)
				return -ENOMEM;
			pl_was_allocated = true;
			list_add_tail(&pl->node, progs);
		} else {
			pl = list_first_entry(progs, typeof(*pl), node);
			old_prog = pl->prog;
			pl_was_allocated = false;
		}
		pl->prog = prog;
	}

	old_flags = cgrp->bpf.flags[type];
	cgrp->bpf.flags[type] = flags;

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		err = compute_effective_progs(desc, type, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		activate_effective_progs(desc, type, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	static_branch_inc(&cgroup_bpf_enabled_key);
	if (old_prog) {
		bpf_prog_put(old_prog);
		static_branch_dec(&cgroup_bpf_enabled_key);
	}
	return 0;

cleanup:
	/* oom while computing effective. Free all computed effective arrays
	 * since they were not activated
	 */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		bpf_prog_array_free(desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	/* and cleanup the prog list */
	pl->prog = old_prog;
	if (pl_was_allocated) {
		list_del(&pl->node);
		kfree(pl);
	}
	return err;
}

/**
 * __cgroup_bpf_detach() - Detach the program from a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to detach or NULL
 * @type: Type of detach operation
 *
 * Must be called with cgroup_mutex held.
 */
int __cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type, u32 unused_flags)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	u32 flags = cgrp->bpf.flags[type];
	struct bpf_prog *old_prog = NULL;
	struct cgroup_subsys_state *css;
	struct bpf_prog_list *pl;
	int err;

	if (flags & BPF_F_ALLOW_MULTI) {
		if (!prog)
			/* to detach MULTI prog the user has to specify valid FD
			 * of the program to be detached
			 */
			return -EINVAL;
	} else {
		if (list_empty(progs))
			/* report error when trying to detach and nothing is attached */
			return -ENOENT;
	}

	if (flags & BPF_F_ALLOW_MULTI) {
		/* find the prog and detach it */
		list_for_each_entry(pl, progs, node) {
			if (pl->prog != prog)
				continue;
			old_prog = prog;
			/* mark it deleted, so it's ignored while
			 * recomputing effective
			 */
			pl->prog = NULL;
			break;
		}
		if (!old_prog)
			return -ENOENT;
	} else {
		/* to maintain backward compatibility NONE and OVERRIDE cgroups
		 * allow detaching with invalid FD (prog==NULL)
		 */
		pl = list_first_entry(progs, typeof(*pl), node);
		old_prog = pl->prog;
		pl->prog = NULL;
	}

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		err = compute_effective_progs(desc, type, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		activate_effective_progs(desc, type, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	/* now can actually delete it from this cgroup list */
	list_del(&pl->node);
	kfree(pl);
	if (list_empty(progs))
		/* last program was detached, reset flags to zero */
		cgrp->bpf.flags[type] = 0;

	bpf_prog_put(old_prog);
	static_branch_dec(&cgroup_bpf_enabled_key);
	return 0;

cleanup:
	/* oom while computing effective. Free all computed effective arrays
	 * since they were not activated
	 */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		bpf_prog_array_free(desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	/* and restore back old_prog */
	pl->prog = old_prog;
	return err;
}

/**
 * __cgroup_bpf_run_filter() - Run a program for packet filtering
 * @sk: The socket sending or receiving traffic
 * @skb: The skb that is being sent or received
 * @type: The type of program to be exectuted
 *
 * If no socket is passed, or the socket is not of type INET or INET6,
 * this function does nothing and returns 0.
 *
 * The program type passed in via @type must be suitable for network
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter(struct sock *sk,
			    struct sk_buff *skb,
			    enum bpf_attach_type type)
{
	unsigned int offset = skb->data - skb_network_header(skb);
	struct sock *save_sk;
	struct cgroup *cgrp;
	int ret;

	if (!sk || !sk_fullsock(sk))
		return 0;

	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return 0;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	save_sk = skb->sk;
	skb->sk = sk;
	__skb_push(skb, offset);
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], skb,
				 bpf_prog_run_save_cb);
	__skb_pull(skb, offset);
	skb->sk = save_sk;
	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter);

/*
 * eBPF-based device controller for cgroup v2.  Backported from upstream
 * 4.15 (commit ebc614f, "bpf, cgroup: implement eBPF-based device
 * controller for cgroup v2").  4.9 adaptations:
 * - prog type registered via bpf_register_prog_type() list (4.9 has no
 *   bpf_types.h macro table)
 * - is_valid_access() uses the 4.9 verifier signature (no aux info);
 *   narrow (u16/u8) loads of access_type are explicitly allowed so that
 *   masking it works with newer compilers (cf. upstream follow-up fix)
 * - no check_return_code() in the 4.9 verifier; verdict semantics are
 *   enforced at run time (0 -> -EPERM, non-zero -> allow)
 */
int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum bpf_attach_type type)
{
	struct cgroup *cgrp;
	struct bpf_cgroup_dev_ctx ctx = {
		.access_type = (access << 16) | dev_type,
		.major = major,
		.minor = minor,
	};
	int allow = 1;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	/* void * cast: 4.9 bpf_func is typed for sk_buff, same as
	 * trace_call_bpf() passing void * ctx */
	allow = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], (void *)&ctx,
				   BPF_PROG_RUN);
	rcu_read_unlock();

	return !allow;
}
EXPORT_SYMBOL(__cgroup_bpf_check_dev_permission);

BPF_CALL_0(bpf_get_current_task)
{
	return (u64)(long)current;
}

static const struct bpf_func_proto bpf_get_current_task_proto = {
	.func		= bpf_get_current_task,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_get_current_cgroup_id)
{
	struct cgroup *cgrp;
	u64 id = 0;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	if (cgrp && cgrp->kn)
		id = cgrp->kn->ino;
	rcu_read_unlock();

	return id;
}

const struct bpf_func_proto bpf_get_current_cgroup_id_proto = {
	.func		= bpf_get_current_cgroup_id,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

static const struct bpf_func_proto *
cgroup_dev_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_ringbuf_reserve:
		return &bpf_ringbuf_reserve_proto;
	case BPF_FUNC_ringbuf_submit:
		return &bpf_ringbuf_submit_proto;
	case BPF_FUNC_ringbuf_discard:
		return &bpf_ringbuf_discard_proto;
	case BPF_FUNC_ringbuf_query:
		return &bpf_ringbuf_query_proto;
	case BPF_FUNC_ringbuf_output:
		return &bpf_ringbuf_output_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
	default:
		return NULL;
	}
}

static bool cgroup_dev_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       enum bpf_reg_type *reg_type)
{
	if (type == BPF_WRITE)
		return false;

	if (off < 0 || off + size > sizeof(struct bpf_cgroup_dev_ctx))
		return false;
	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;

	switch (off) {
	case offsetof(struct bpf_cgroup_dev_ctx, access_type):
		/* allow narrow loads for masking access_type */
		return size == sizeof(__u32) || size == sizeof(__u16) ||
			size == sizeof(__u8);
	default:
		return size == sizeof(__u32);
	}
}

static const struct bpf_verifier_ops cg_dev_verifier_ops = {
	.get_func_proto		= cgroup_dev_func_proto,
	.is_valid_access	= cgroup_dev_is_valid_access,
};

static struct bpf_prog_type_list cg_dev_type __read_mostly = {
	.ops	= &cg_dev_verifier_ops,
	.type	= BPF_PROG_TYPE_CGROUP_DEVICE,
};

static int __init register_cg_dev_ops(void)
{
	bpf_register_prog_type(&cg_dev_type);
	return 0;
}
late_initcall(register_cg_dev_ops);

/**
 * __cgroup_bpf_query() - query attached programs of a cgroup
 * @cgrp: the cgroup of interest
 * @type: attach type to query
 * @query_flags: must be 0 (reserved)
 * @attach_flags: returns the attach flags of the cgroup
 * @prog_ids: user buffer for program ids (may be NULL if @prog_cnt is 0)
 * @prog_cnt: in: capacity of @prog_ids, out: total number of attached progs
 *
 * Must be called with cgroup_mutex held.  Fills ids up to the input
 * capacity and always reports the total, so userspace can size its
 * buffer with a prog_cnt == 0 probe first.
 */
int __cgroup_bpf_query(struct cgroup *cgrp, enum bpf_attach_type type,
		       u32 query_flags, u32 *attach_flags, u32 __user *prog_ids,
		       u32 *prog_cnt)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	struct bpf_prog_list *pl;
	u32 cnt = 0, total = 0;

	if (query_flags & ~BPF_F_QUERY_EFFECTIVE)
		return -EINVAL;

	/* effective mode: report inherited programs actually in effect;
	 * attach_flags is always 0 in this mode (cf. upstream).
	 * IDs are collected under RCU into a kernel buffer first:
	 * copy_to_user() may fault and must not run under rcu_read_lock().
	 */
	if (query_flags & BPF_F_QUERY_EFFECTIVE) {
		struct bpf_prog_array *array;
		struct bpf_prog **prog;
		u32 *ids, cnt = 0;

		ids = kcalloc(*prog_cnt, sizeof(*ids),
			      GFP_KERNEL | __GFP_NOWARN);
		if (*prog_cnt && !ids)
			return -ENOMEM;

		rcu_read_lock();
		array = rcu_dereference(cgrp->bpf.effective[type]);
		if (array) {
			for (prog = array->progs; *prog; prog++) {
				total++;
				if (cnt >= *prog_cnt)
					continue;
				ids[cnt++] = (u32)(*prog)->aux->id;
			}
		}
		rcu_read_unlock();

		if (cnt && copy_to_user(prog_ids, ids, cnt * sizeof(*ids))) {
			kfree(ids);
			return -EFAULT;
		}
		kfree(ids);

		*attach_flags = 0;
		*prog_cnt = total;
		return 0;
	}

	list_for_each_entry(pl, progs, node) {
		u32 id;

		if (!pl->prog)
			continue;
		total++;
		if (cnt >= *prog_cnt)
			continue;
		id = (u32)pl->prog->aux->id;
		if (copy_to_user(&prog_ids[cnt], &id, sizeof(id)))
			return -EFAULT;
		cnt++;
	}

	*attach_flags = cgrp->bpf.flags[type];
	*prog_cnt = total;
	return 0;
}
