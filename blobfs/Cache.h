#pragma once

#include "Common.h"

namespace l_blob_adapter {
	enum class ItemStatus {
		Expired,
		Clean,
		Ready,
		Dirty,
		Uploading,
		Up_Expired,
	};
	class CacheItem {
	public:
		ItemStatus status= ItemStatus::Expired;
		std::mutex status_mutex;
		// mutex for read count and write count
		// read write only take share lock
		// clear and upload take unique lock
		shared_timed_mutex access_mut, write_mut;

		bool compare_and_set(ItemStatus oldval, ItemStatus newval);
		virtual int gc_notify(pos_t pos)=0;
		virtual void clear()=0;
		virtual ~CacheItem() = default;
	};

	class BasicCache {
	public:
		struct Cell {
			pos_t prev=0, next=0;
			unique_ptr<CacheItem> item=nullptr;

			Cell() = default;
			Cell(pos_t prv, pos_t nxt, unique_ptr<CacheItem>&& itm) :prev(prv), next(nxt), item(std::move(itm)) {}
		};
		size_t max_cache_size;//cache size including list head and back
		std::vector<Cell> cache;
		std::mutex link_list_mutex;
	public:
		BasicCache(size_t cache_size);
		virtual ~BasicCache();

		// check full to determine whether add new item
		// do not ensure atomic compare and set
		// the size may exceed the max_cache_size
		bool is_full() { return cache.size() >= max_cache_size;}
		pos_t put_new_item(unique_ptr<CacheItem>&& item);// lock
		pos_t get_free_from_list(pos_t ignore);//lock

		CacheItem* get_item(pos_t pos);// lock
		void put_item_front(pos_t pos);// lock
		void put_item_back(pos_t pos);// lock
		
	};

}

