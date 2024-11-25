#include "tlb.h"
#include "stats.h"
#include "config.hpp"
#include <cmath>
#include "cache_cntlr.h"
#include <iostream>
#include <utility>
#include "memory_manager.h"
#include "core_manager.h"
#include "cache_set.h"
#include "cache_base.h"
#include "utils.h"
#include "log.h"
#include "rng.h"
#include "address_home_lookup.h"
#include "fault_injection.h"
#include "memory_manager.h"

// #define DEBUG_TLB
// #define TLB_STATS

namespace ParametricDramDirectoryMSI
{

	TLB::TLB(String name, String cfgname, core_id_t core_id, ComponentLatency access_latency, UInt32 num_entries, UInt32 associativity, int *page_size_list, int page_sizes, String tlb_type, bool allocate_on_miss, bool prefetch, TLBPrefetcherBase **tpb, int _number_of_prefetchers, int _max_prefetch_count)
		: m_size(num_entries), m_core_id(core_id), m_name(name), m_associativity(associativity), m_cache(name + "_cache",
																										 cfgname,
																										 core_id, num_entries / associativity,
																										 associativity, (1L << 3), "lru",
																										 CacheBase::PR_L1_CACHE, CacheBase::HASH_MASK,
																										 NULL,
																										 NULL, true, page_size_list, page_sizes),
		  m_type(tlb_type), m_page_size_list(NULL),
		  m_page_sizes(page_sizes),
		  m_allocate_miss(allocate_on_miss), m_prefetch(prefetch),
		  prefetchers(tpb),
		  number_of_prefetchers(_number_of_prefetchers),
		  max_prefetch_count(_max_prefetch_count),
		  m_access_latency(access_latency)
	{
		LOG_ASSERT_ERROR((num_entries / associativity) * associativity == num_entries, "Invalid TLB configuration: num_entries(%d) must be a multiple of the associativity(%d)", num_entries, associativity);

		m_page_size_list = (int *)malloc(sizeof(int) * m_page_sizes);

		for (int i = 0; i < m_page_sizes; i++)
		{
			m_page_size_list[i] = page_size_list[i];
		}

		bzero(&tlb_stats, sizeof(tlb_stats));

		registerStatsMetric(name, core_id, "access", &tlb_stats.m_access);
		registerStatsMetric(name, core_id, "eviction", &tlb_stats.m_eviction);
		registerStatsMetric(name, core_id, "miss", &tlb_stats.m_miss);
	}

	CacheBlockInfo *TLB::lookup(IntPtr address, SubsecondTime now, bool model_count, Core::lock_signal_t lock_signal, IntPtr eip, bool modeled, bool count, PageTable *pt)
	{
		//std::cout << "[DEBUG] Entering TLB1::lookup, Address: " << std::hex << address << std::endl;//added by me


		if (getPrefetch())
		{

			if (entry_priority_queue.size() > 0)
			{
				// std::cout << entry_priority_queue.size() << "\n";
				query_entry q = entry_priority_queue.top();
				while (q.timestamp < now && entry_priority_queue.size() > 0)
				{
					entry_priority_queue.pop();
					allocate(q.address, q.timestamp, true, lock_signal, q.page_size, q.ppn, true);
					// std::cout << entry_priority_queue.size() << " " << q.timestamp << " " << now << " " << q.address << " " << q.page_size << std::endl;
					q = entry_priority_queue.top();
				}
			}
			for (int i = 0; i < number_of_prefetchers; i++)
			{
				std::vector<query_entry> new_entries = prefetchers[i]->performPrefetch(address, eip, lock_signal, modeled, count, pt);
				// std::cout << i << " " << new_entries.size() << std::endl;
				for (int j = 0; j < new_entries.size(); j++)
				{
					if (entry_priority_queue.size() >= max_prefetch_count)
					{
						entry_priority_queue.pop();
					}
					entry_priority_queue.push(new_entries[j]);
				}
			}

			// query_entry q;
			// q.address = address;
			// q.ppn = (IntPtr)&q;
			// q.page_size = 12;
			// q.timestamp = now;
		}

		if (model_count)
			tlb_stats.m_access++;

#ifdef DEBUG_TLB
		std::cout << "TLB " << m_name << " Lookup: " << address << std::endl;
#endif

		CacheBlockInfo *hit = m_cache.accessSingleLineTLB(address, Cache::LOAD, NULL, 0, now, true);

#ifdef DEBUG_TLB
		if (hit)
			std::cout << " Hit at level: " << m_name << std::endl;
		if (!hit)
			std::cout << " Miss at level: " << m_name << std::endl;
#endif

		if (hit)
		{
			tlb_stats.m_hit++;
			return hit;
		}

		if (model_count)
			tlb_stats.m_miss++; // We reach this point if L1 TLB Miss

		return NULL;
	}

	std::tuple<bool, IntPtr, int> TLB::allocate(IntPtr address, SubsecondTime now, bool count, Core::lock_signal_t lock_signal, int page_size, IntPtr ppn, bool self_alloc)
	{

		if (getPrefetch() && !self_alloc)
		{
			return std::make_tuple(false, 0, 0);
		}
		IntPtr evict_addr;
		CacheBlockInfo evict_block_info;

		IntPtr tag;
		UInt32 set_index;
		// std::cout << "Miss at level" << level << " UTR heuristic: " << m_utr->getHeur() << std::endl;

		m_cache.splitAddressTLB(address, tag, set_index, page_size);

#ifdef DEBUG_TLB
		std::cout << " Allocate " << address << " at level: " << m_name << " with page_size " << page_size << "and tag " << tag << std::endl;
#endif

		bool eviction = false;
		m_cache.insertSingleLineTLB(address, NULL, &eviction, &evict_addr, &evict_block_info, NULL, now, NULL, CacheBlockInfo::block_type_t::NON_PAGE_TABLE, page_size, ppn);

		if (eviction && count)
			tlb_stats.m_eviction++;

#ifdef DEBUG_TLB
		if (eviction)
			std::cout << " Evicted " << evict_addr << " from level: " << m_name << " with page_size" << page_size << std::endl;
#endif

		return std::make_tuple(eviction, evict_addr, evict_block_info.getPageSize());
	}

}