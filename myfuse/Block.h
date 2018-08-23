#pragma once
#include "Cache.h"
#include "BasicFile.h"

namespace l_blob_adapter {
	class BasicFile;
	class Block :public CacheItem {
	public:
		Block(size_t blocksize);
		pos_t block_index;
		vector<uint8_t> data;
		BasicFile* basefile;
		Block& operator=(const Block& other) {
			this->data = other.data;
			this->basefile = other.basefile;
			this->block_index = other.block_index;
			return *this;
		}
		virtual int gc_notify(pos_t pos);
		virtual void clear();
	};

	class BlockCache :public BasicCache {
	public:
		static BlockCache* instance();
		static std::mutex instance_mutex;
		static  BlockCache* s_instance;
		BlockCache();

	public:
		static pos_t get_free();
		static Block* get(pos_t pos) { return static_cast<Block*> (instance()->get_item(pos)); };
		static void put_front(pos_t pos) { return instance()->put_item_front(pos); }
	};
}