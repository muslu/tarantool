#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <small/small.h>
#include <stdarg.h>

#include "system_allocator.h"

enum memtx_allocator_type {
	SMALL_ALLOCATOR_TYPE = 0,
	SYSTEM_ALLOCATOR_TYPE,
};

struct allocator_stats {
	size_t used;
	size_t total;
};

struct SmallAlloc
{
	/* Tuple allocator */
	static struct small_alloc small_alloc;
	/** Slab cache for allocating tuples. */
	static struct slab_cache slab_cache;
	static inline void
	init(struct slab_arena *arena, uint32_t objsize_min, float alloc_factor)
	{
		slab_cache_create(&slab_cache, arena);
		small_alloc_create(&small_alloc, &slab_cache, objsize_min, alloc_factor);
	}
	static inline void
	destroy()
	{
		small_alloc_destroy(&small_alloc);
		slab_cache_destroy(&slab_cache);
	}
	static inline void *
	alloc(size_t size)
	{
		return smalloc(&small_alloc, size);
	}
	static inline void
	free(void *ptr, size_t size)
	{
		return smfree(&small_alloc, ptr, size);
	}
	static inline void
	free_delayed(void *ptr, size_t size)
	{
		return smfree_delayed(&small_alloc, ptr, size);
	}
	static inline void
	enter_delayed_free_mode()
	{
		return small_alloc_setopt(&small_alloc, SMALL_DELAYED_FREE_MODE, true);
	}
	static inline void
	leave_delayed_free_mode()
	{
		return small_alloc_setopt(&small_alloc, SMALL_DELAYED_FREE_MODE, false);
	}
	static inline void
	mem_check()
	{
		return slab_cache_check(&slab_cache);
	}
	static inline void
	stats(struct allocator_stats *stats, va_list argptr)
	{
		mempool_stats_cb stats_cb = va_arg(argptr, mempool_stats_cb);
		void *cb_ctx = va_arg(argptr, void  *);

		struct small_stats data_stats;
		small_stats(&small_alloc, &data_stats, stats_cb, cb_ctx);
		stats->used = data_stats.used;
		stats->total = data_stats.total;
	}
};
extern struct SmallAlloc small_alloc;

struct SystemAlloc
{
	/* Tuple allocator */
	static struct system_alloc system_alloc;
	static inline void
	init(struct quota *quota)
	{
		system_alloc_create(&system_alloc, quota);
	}
	static inline void
	destroy()
	{
		system_alloc_destroy(&system_alloc);
	}
	static inline void *
	alloc(size_t size)
	{
		return sysalloc(&system_alloc, size);
	}
	static inline void
	free(void *ptr, size_t size)
	{
		return sysfree(&system_alloc, ptr, size);
	}
	static inline void
	free_delayed(void *ptr, size_t size)
	{
		return sysfree_delayed(&system_alloc, ptr, size);
	}
	static inline void
	enter_delayed_free_mode()
	{
		return system_alloc_setopt(&system_alloc, SYSTEM_DELAYED_FREE_MODE, true);
	}
	static inline void
	leave_delayed_free_mode()
	{
		return system_alloc_setopt(&system_alloc, SYSTEM_DELAYED_FREE_MODE, false);
	}
	static inline void
	mem_check() 
	{

	}
	static inline void
	stats(struct allocator_stats *stats, MAYBE_UNUSED va_list argptr)
	{
		struct system_stats data_stats;
		system_stats(&system_alloc, &data_stats);
		stats->used = data_stats.used;
		stats->total = data_stats.total;
	}
};
extern struct SystemAlloc system_alloc;