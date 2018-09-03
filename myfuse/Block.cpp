#include "Block.h"
#include <chrono>

using namespace l_blob_adapter;
using namespace std::chrono_literals;


l_blob_adapter::Block::Block(size_t blocksize):data(blocksize), block_index(-1)
{
}

int l_blob_adapter::Block::gc_notify(pos_t pos)
{
	std::unique_lock<std::shared_timed_mutex> lock(basefile->m_mutex, std::try_to_lock);
	if (lock.owns_lock()) {
		if(basefile->blocklist.at(this->block_index)==pos)basefile->blocklist.at(this->block_index) = 0;
		return 0;
	}
	return -1;
}

void l_blob_adapter::Block::clear()
{
	return;
}

BlockCache* BlockCache:: s_instance=nullptr;
mutex BlockCache::instance_mutex;

l_blob_adapter::BlockCache::BlockCache() :BasicCache(default_cache_size){
	this->cache.reserve(default_cache_size+50);
}

pos_t l_blob_adapter::BlockCache::get_free()
{
	if (instance()->is_full()) {
		return instance()->get_free_from_list();
	}
	else {
		unique_ptr<Block> ptr = std::make_unique<Block>(default_block_size);
		return instance()->put_new_item(std::move(ptr));
	}
}

BlockCache * l_blob_adapter::BlockCache::instance()
{
	if(s_instance==nullptr) {
		std::lock_guard<std::mutex> lock(instance_mutex);
		if (s_instance == nullptr) {
			s_instance = new BlockCache();
		}
	}
	return s_instance;
}

