#include "Blocks.h"

using namespace l_blob_adapter;

Blocks::Blocks()
{
}


Blocks::~Blocks()
{
}

vector<Block> Blocks::block_cache;
list<size_t> Blocks::freelist;
size_t Blocks::max_cache;


BlockCache* BlockCache::instance = nullptr;
std::mutex BlockCache::s_mutex;
BlockCache * l_blob_adapter::BlockCache::get_instance()
{
	if (instance == nullptr) {
		std::lock_guard<std::mutex> lock(s_mutex);
		if (instance == nullptr) {
			instance = new BlockCache();
		}
	}
	return instance;
}
