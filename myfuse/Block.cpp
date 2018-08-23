#include "Block.h"

using namespace l_blob_adapter;



l_blob_adapter::Block::Block(size_t blocksize):data(blocksize), block_index(-1)
{
}

int l_blob_adapter::Block::gc_notify(pos_t pos)
{
	return 0;
}

void l_blob_adapter::Block::clear()
{
	return;
}

BlockCache* BlockCache:: s_instance=nullptr;
mutex BlockCache::instance_mutex;

l_blob_adapter::BlockCache::BlockCache() :BasicCache(default_cache_size){

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

