#pragma once
#include "Cache.h"
#include "BasicFile.h"

namespace l_blob_adapter {
	class BasicFile;
	class Block :public CacheItem {
	public:
		Block();
		vector<uint8_t> data;
		BasicFile* basefile;
		virtual int gc_notify(pos_t pos);
		virtual void clear();
	};

	class BlockCache :public Cache<Block> {
	public:
		static Block* get(pos_t pos) { return get_instance()->get_item(pos); };
		static pos_t get_new() { return get_instance()->get_new_item(); };
		static bool put_front(pos_t pos) { return get_instance()->put_to_front(pos); }
	};
}