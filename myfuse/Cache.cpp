#include "Cache.h"

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
