/*
 * drivers/video/tegra/nvmap/nvmap.h
 *
 * GPU memory management driver for Tegra
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *'
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __VIDEO_TEGRA_NVMAP_NVMAP_H
#define __VIDEO_TEGRA_NVMAP_NVMAP_H

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/nvmap.h>
#include "nvmap_heap.h"

struct nvmap_device;
struct page;
struct tegra_iovmm_area;

void _nvmap_handle_free(struct nvmap_handle *h);

#if defined(CONFIG_TEGRA_NVMAP)
#define nvmap_err(_client, _fmt, ...)				\
	dev_err(nvmap_client_to_device(_client),		\
		"%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_warn(_client, _fmt, ...)				\
	dev_warn(nvmap_client_to_device(_client),		\
		 "%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_debug(_client, _fmt, ...)				\
	dev_dbg(nvmap_client_to_device(_client),		\
		"%s: "_fmt, __func__, ##__VA_ARGS__)

#define nvmap_ref_to_id(_ref)		((unsigned long)(_ref)->handle)

/* handles allocated using shared system memory (either IOVMM- or high-order
 * page allocations */
struct nvmap_pgalloc {
	struct page **pages;
	struct tegra_iovmm_area *area;
	struct list_head mru_list;	/* MRU entry for IOVMM reclamation */
	bool contig;			/* contiguous system memory */
	bool dirty;			/* area is invalid and needs mapping */
	u32 iovm_addr;	/* is non-zero, if client need specific iova mapping */
};

struct nvmap_handle {
	struct rb_node node;	/* entry on global handle tree */
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	atomic_t pin;		/* pin count */
	unsigned int usecount;	/* how often is used */
	unsigned long flags;
	size_t size;		/* padded (as-allocated) size */
	size_t orig_size;	/* original (as-requested) size */
	size_t align;
	struct nvmap_client *owner;
	struct nvmap_device *dev;
	union {
		struct nvmap_pgalloc pgalloc;
		struct nvmap_heap_block *carveout;
	};
	bool global;		/* handle may be duplicated by other clients */
	bool secure;		/* zap IOVMM area on unpin */
	bool heap_pgalloc;	/* handle is page allocated (sysmem / iovmm) */
	bool alloc;		/* handle has memory allocated */
	unsigned int userflags;	/* flags passed from userspace */
	struct mutex lock;
};

#ifdef CONFIG_NVMAP_PAGE_POOLS
#define NVMAP_UC_POOL NVMAP_HANDLE_UNCACHEABLE
#define NVMAP_WC_POOL NVMAP_HANDLE_WRITE_COMBINE
#define NVMAP_IWB_POOL NVMAP_HANDLE_INNER_CACHEABLE
#define NVMAP_WB_POOL NVMAP_HANDLE_CACHEABLE
#define NVMAP_NUM_POOLS (NVMAP_HANDLE_CACHEABLE + 1)

struct nvmap_page_pool {
	struct spinlock lock;
	unsigned long irq_flags;
	int npages;
	struct page **page_array;
	struct page **shrink_array;
	int max_pages;
	int flags;
};

int nvmap_page_pool_init(struct nvmap_page_pool *pool, int flags);
#endif

struct nvmap_share {
	struct tegra_iovmm_client *iovmm;
	wait_queue_head_t pin_wait;
	struct mutex pin_lock;
#ifdef CONFIG_NVMAP_PAGE_POOLS
	union {
		struct nvmap_page_pool pools[NVMAP_NUM_POOLS];
		struct {
			struct nvmap_page_pool uc_pool;
			struct nvmap_page_pool wc_pool;
			struct nvmap_page_pool iwb_pool;
			struct nvmap_page_pool wb_pool;
		};
	};
#endif
#ifdef CONFIG_NVMAP_RECLAIM_UNPINNED_VM
	struct mutex mru_lock;
	struct list_head *mru_lists;
	int nr_mru;
#endif
};

struct nvmap_carveout_commit {
	size_t commit;
	struct list_head list;
};

struct nvmap_client {
	const char			*name;
	struct nvmap_device		*dev;
	struct nvmap_share		*share;
	struct rb_root			handle_refs;
	atomic_t			iovm_commit;
	size_t				iovm_limit;
	struct mutex			ref_lock;
	bool				super;
	atomic_t			count;
	struct task_struct		*task;
	struct list_head		list;
	struct nvmap_carveout_commit	carveout_commit[0];
};

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
};

static inline void nvmap_ref_lock(struct nvmap_client *priv)
{
	mutex_lock_nested(&priv->ref_lock, SINGLE_DEPTH_NESTING);
}

static inline void nvmap_ref_unlock(struct nvmap_client *priv)
{
	mutex_unlock(&priv->ref_lock);
}

static inline struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h)
{
	if (unlikely(atomic_inc_return(&h->ref) <= 1)) {
		pr_err("%s: %s getting a freed handle\n",
			__func__, current->group_leader->comm);
		if (atomic_read(&h->ref) <= 0)
			return NULL;
	}
	return h;
}

static inline void nvmap_handle_put(struct nvmap_handle *h)
{
	int cnt = atomic_dec_return(&h->ref);

	if (WARN_ON(cnt < 0)) {
		pr_err("%s: %s put to negative references\n",
			__func__, current->comm);
	} else if (cnt == 0)
		_nvmap_handle_free(h);
}

#define L_PTE_MT_INNER_WB	(_AT(pteval_t, 0x05) << 2)	/* 0101 (armv6, armv7) */
static inline pgprot_t nvmap_pgprot(struct nvmap_handle *h, pgprot_t prot)
{
	if (h->flags == NVMAP_HANDLE_UNCACHEABLE)
		return pgprot_noncached(prot);
	else if (h->flags == NVMAP_HANDLE_WRITE_COMBINE)
		return pgprot_writecombine(prot);
	else if (h->flags == NVMAP_HANDLE_INNER_CACHEABLE)
		return __pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_INNER_WB);
	return prot;
}

#else /* CONFIG_TEGRA_NVMAP */
struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h);
void nvmap_handle_put(struct nvmap_handle *h);
pgprot_t nvmap_pgprot(struct nvmap_handle *h, pgprot_t prot);

#endif /* !CONFIG_TEGRA_NVMAP */

struct device *nvmap_client_to_device(struct nvmap_client *client);

pte_t **nvmap_alloc_pte(struct nvmap_device *dev, void **vaddr);

pte_t **nvmap_alloc_pte_irq(struct nvmap_device *dev, void **vaddr);

void nvmap_free_pte(struct nvmap_device *dev, pte_t **pte);

void nvmap_usecount_inc(struct nvmap_handle *h);
void nvmap_usecount_dec(struct nvmap_handle *h);

struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *dev,
					      struct nvmap_handle *handle,
					      unsigned long type);

unsigned long nvmap_carveout_usage(struct nvmap_client *c,
				   struct nvmap_heap_block *b);

struct nvmap_carveout_node;
void nvmap_carveout_commit_add(struct nvmap_client *client,
			       struct nvmap_carveout_node *node, size_t len);

void nvmap_carveout_commit_subtract(struct nvmap_client *client,
				    struct nvmap_carveout_node *node,
				    size_t len);

struct nvmap_share *nvmap_get_share_from_dev(struct nvmap_device *dev);

struct nvmap_handle *nvmap_validate_get(struct nvmap_client *client,
					unsigned long handle);

struct nvmap_handle_ref *_nvmap_validate_id_locked(struct nvmap_client *priv,
						   unsigned long id);

struct nvmap_handle *nvmap_get_handle_id(struct nvmap_client *client,
					 unsigned long id);

struct nvmap_handle_ref *nvmap_create_handle(struct nvmap_client *client,
					     size_t size);

int nvmap_alloc_handle_id(struct nvmap_client *client,
			  unsigned long id, unsigned int heap_mask,
			  size_t align, unsigned int flags);

void nvmap_free_handle_id(struct nvmap_client *c, unsigned long id);

int nvmap_pin_ids(struct nvmap_client *client,
		  unsigned int nr, const unsigned long *ids);

void nvmap_unpin_ids(struct nvmap_client *priv,
		     unsigned int nr, const unsigned long *ids);

int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h);

void nvmap_handle_add(struct nvmap_device *dev, struct nvmap_handle *h);

int is_nvmap_vma(struct vm_area_struct *vma);

struct nvmap_handle_ref *nvmap_alloc_iovm(struct nvmap_client *client,
	size_t size, size_t align, unsigned int flags, unsigned int iova_start);

void nvmap_free_iovm(struct nvmap_client *client, struct nvmap_handle_ref *r);

/*
 * The set_memory_* API can be used to change various attributes of a virtual
 * address range. The attributes include:
 * Cachability   : UnCached, WriteCombining, WriteBack
 * Executability : eXeutable, NoteXecutable
 * Read/Write    : ReadOnly, ReadWrite
 * Presence      : NotPresent
 *
 * Within a catagory, the attributes are mutually exclusive.
 *
 * The implementation of this API will take care of various aspects that
 * are associated with changing such attributes, such as:
 * - Flushing TLBs
 * - Flushing CPU caches
 * - Making sure aliases of the memory behind the mapping don't violate
 *   coherency rules as defined by the CPU in the system.
 *
 * What this API does not do:
 * - Provide exclusion between various callers - including callers that
 *   operation on other mappings of the same physical page
 * - Restore default attributes when a page is freed
 * - Guarantee that mappings other than the requested one are
 *   in any state, other than that these do not violate rules for
 *   the CPU you have. Do not depend on any effects on other mappings,
 *   CPUs other than the one you have may have more relaxed rules.
 * The caller is required to take care of these.
 */

int set_memory_uc(unsigned long addr, int numpages);
int set_memory_wc(unsigned long addr, int numpages);
int set_memory_wb(unsigned long addr, int numpages);
int set_memory_iwb(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_np(unsigned long addr, int numpages);
int set_memory_4k(unsigned long addr, int numpages);

int set_memory_array_uc(unsigned long *addr, int addrinarray);
int set_memory_array_wc(unsigned long *addr, int addrinarray);
int set_memory_array_wb(unsigned long *addr, int addrinarray);
int set_memory_array_iwb(unsigned long *addr, int addrinarray);

int set_pages_array_uc(struct page **pages, int addrinarray);
int set_pages_array_wc(struct page **pages, int addrinarray);
int set_pages_array_wb(struct page **pages, int addrinarray);
int set_pages_array_iwb(struct page **pages, int addrinarray);

#endif
