#include "Cache.h"
#include <assert.h>

using namespace l_blob_adapter;

bool l_blob_adapter::CacheItem::compare_and_set(ItemStatus oldval, ItemStatus newval)
{
	std::lock_guard<std::mutex> guard(this->status_mutex);
	if (this->status == oldval) {
		this->status = newval;
		return true;
	}
	else {
		return false;
	}
}

l_blob_adapter::BasicCache::BasicCache(size_t cache_size):max_cache_size(cache_size+2)
{
	cache.resize(2);
	cache[0].next = 1;
	cache[1].prev = 0;
}

l_blob_adapter::BasicCache::~BasicCache()
{

}

CacheItem * l_blob_adapter::BasicCache::get_item(pos_t pos)
{
	CacheItem * ret = nullptr;
	ret = cache.at(pos).item.get();
	assert(ret != nullptr);
	put_item_back(pos);
	return ret;
}

void l_blob_adapter::BasicCache::put_item_front(pos_t pos)
{
	std::lock_guard<std::mutex> guard(link_list_mutex);
	Cell& it = cache.at(pos);
	cache.at(it.prev).next = it.next;
	cache.at(it.next).prev = it.prev;
	it.prev = 0;
	it.next = cache.at(0).next;
	cache.at(cache.at(0).next).prev = pos;
	cache.at(0).next = pos;
	return;
}

void l_blob_adapter::BasicCache::put_item_back(pos_t pos)
{
	std::lock_guard<std::mutex> guard(link_list_mutex);
	// if already in back, just return
	if (cache.at(1).prev == pos)return;

	Cell& it = cache.at(pos);
	cache.at(it.prev).next = it.next;
	cache.at(it.next).prev = it.prev;
	it.prev = cache.at(1).prev;
	it.next = 1;
	cache.at(cache.at(1).prev).next = pos;
	cache.at(1).prev = pos;
	return;
}

pos_t l_blob_adapter::BasicCache::put_new_item(unique_ptr<CacheItem>&& item)
{
	std::lock_guard<std::mutex> guard(link_list_mutex);
	cache.emplace_back(cache.at(1).prev, 1, std::move(item));
	pos_t pos = cache.size() - 1;
	cache.at(cache.at(1).prev).next = pos;
	cache.at(1).prev = pos;
	return pos;
}

pos_t l_blob_adapter::BasicCache::get_free_from_list()
{
	while (true) {

		pos_t pos = cache.at(0).next;
		while (pos != 1)
		{
			CacheItem & item = *(cache.at(pos).item);
			std::lock_guard<std::mutex> lock(item.status_mutex);
			if (item.status == ItemStatus::Expired) {
				item.status = ItemStatus::Ready;
				put_item_back(pos);
				return pos;
			}
			else if (item.status == ItemStatus::Clean) {
				int response = item.gc_notify(pos);
				if (!response) {
					item.clear();
					item.status = ItemStatus::Ready;
					return pos;
				}
				else {
					put_item_back(pos);
				}
			}
			pos = cache.at(pos).next;
		}

	}
	
	return 0;
}
