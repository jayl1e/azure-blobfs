#pragma once

#include <vector>
#include <was/blob.h>
#include <memory>
#include <string>
#include <shared_mutex>

namespace l_blob_adapter {

	using guid_t = utility::uuid;
	using std::ptrdiff_t;
	using pos_t = std::ptrdiff_t;
	using std::size_t;
	using std::uint8_t;
	using std::unique_ptr;
	using std::shared_ptr;
	using std::wstring;
	using std::shared_mutex;
	using std::shared_timed_mutex;
	using azure::storage::cloud_page_blob;
	using azure::storage::cloud_block_blob;
	using std::unordered_map;
	using std::vector;
	using utility::string_t;

	enum class ItemStatus {
		Clean,
		Ready,
		Dirty,
		Uploading,
		Up_Expired,
		Expired
	};




	class CacheItem {
	public:
		pos_t prev, next;
		ItemStatus status;
		std::mutex status_mutex;
		// mutex for read count and write count
		// read write only take share lock
		// clear and upload take unique lock
		shared_timed_mutex read_mut, write_mut;

		bool compare_and_set(ItemStatus oldval, ItemStatus newval);
		virtual int gc_notify(pos_t pos)=0;
		virtual void clear()=0;
		CacheItem& operator=(const CacheItem& other) {
			std::wcout << "copy assigner called" << std::endl;
			return *this; 
		}
		CacheItem& operator=(CacheItem&& other) { 
			std::wcout << "move assigner called" << std::endl; 
			return *this; 
		}
		CacheItem() = default;
		CacheItem(CacheItem&& other) noexcept{
			std::wcout << "move constructor called" << std::endl;
		}
		CacheItem(const CacheItem& other) {
			std::wcout << "copy constructor called" << std::endl;
		}
		
	};


	template<typename Item>
	class Cache
	{
	public:
		static_assert(std::is_base_of<CacheItem, Item>::value, "Item not derived from CacheItem");

		static Cache* get_instance();
		//position in cache is always larger than zero
		Item* get_item(pos_t pos);
		bool put_to_front(pos_t pos);
		bool put_to_back(pos_t pos);
		pos_t get_new_item();
		
	public:
		static Cache * instance;
		static std::mutex instance_mutex;
		pos_t create_item();
		
		//cache item 0 is front, 1 is back
		vector<Item> cache;
		size_t max_cache_size;

		std::mutex link_list_mutex;
	};

	template<typename Item>
	Cache<Item>*  Cache<Item>::instance=nullptr;

	template<typename Item>
	std::mutex  Cache<Item>::instance_mutex;

	template<typename Item>
	inline Item * Cache<Item>::get_item(pos_t pos)
	{
		Item * ret = nullptr;
		try {
			ret=& cache.at(pos);
		}
		catch (std::exception& e) {
			std::wcerr << e.what() << std::endl;
			return ret;
		}
		put_to_back(pos);
		return ret;
	}

	template<typename Item>
	inline bool Cache<Item>::put_to_back(pos_t pos) {
		std::lock_guard<std::mutex> guard(link_list_mutex);
		// if already in back, just return
		if (cache.at(1).prev == pos)return true;

		Item& it = cache.at(pos);
		cache.at(it.prev).next = it.next;
		cache.at(it.next).prev = it.prev;
		it.prev = cache.at(1).prev;
		it.next = 1;
		cache.at(cache.at(1).prev).next = pos;
		cache.at(1).prev = pos;
		return true;
	}

	template<typename Item>
	inline pos_t Cache<Item>::get_new_item()
	{
		if (cache.size() < max_cache_size) {
			pos_t ret= create_item();
			cache.at(ret).status = ItemStatus::Ready;
			return ret;
		}
		else {
			pos_t pos = cache.at(0).next;
			while (pos!=1)
			{
				if (cache.at(pos).compare_and_set(ItemStatus::Expired, ItemStatus::Ready)) {
					put_to_back(pos);
					return pos;
				}
				pos = cache.at(pos).next;
			}
			return 0;//todo
		}
	}

	template<typename Item>
	inline pos_t Cache<Item>::create_item()
	{
		std::lock_guard<std::mutex> guard(link_list_mutex);
		cache.emplace_back();
		pos_t pos = cache.size() - 1;
		cache.back().prev = cache.at(1).prev;
		cache.back().next = 1;
		cache.at(cache.at(1).prev).next = pos;
		cache.at(1).prev = pos;
		return pos;
	}

	template<typename Item>
	inline bool Cache<Item>::put_to_front(pos_t pos) {
		std::lock_guard<std::mutex> guard(link_list_mutex);
		Item& it = cache.at(pos);
		cache.at(it.prev).next = it.next;
		cache.at(it.next).prev = it.prev;
		it.prev = 0;
		it.next = cache.at(0).next;
		cache.at(cache.at(0).next).prev = pos;
		cache.at(0).next = pos;
		return true;
	}


	template<typename Item>
	inline Cache<Item> * Cache<Item>::get_instance()
	{
		if (instance == nullptr) {
			std::lock_guard<std::mutex> lock(instance_mutex);
			if (instance == nullptr) {
				instance = new Cache<Item>();
				instance->cache.resize(4);
				instance->cache.at(0).next = 1;
				instance->cache.at(1).prev = 0;
				instance->max_cache_size = 10000;
			}
		}
		return instance;
	}



}

