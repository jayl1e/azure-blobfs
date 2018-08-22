#include "BasicFile.h"
#include <string>
#include <assert.h>
#include "myutils.h"


using namespace l_blob_adapter;

BasicFile::BasicFile()
{
}

l_blob_adapter::BasicFile::~BasicFile()
{
}

unique_ptr<BasicFile> l_blob_adapter::BasicFile::create(guid_t file_identifier)
{
	//todo
	auto ptr= std::make_unique<BasicFile>();
	ptr->m_file_identifier = file_identifier;
	ptr->blocksize = 1 << 20;
	ptr->filesize = 0;
	ptr->m_exist = true;
	return ptr;
}



unique_ptr<Snapshot> l_blob_adapter::BasicFile::create_snap()
{
	std::unique_lock<std::mutex> uplock(this->up_mutex);
	std::unique_lock<std::shared_timed_mutex> lock(this->m_mutex);
	return std::make_unique<Snapshot>(*this,std::move(uplock));
}

size_t l_blob_adapter::BasicFile::resize(size_t size)
{
	std::lock_guard<std::shared_timed_mutex> lock(m_mutex);
	size_t new_size = 0, old_size=0;
	if(size>0)new_size=((size - 1) / this->blocksize +1);
	if(this->filesize>0)old_size = ((this->filesize - 1) / this->blocksize + 1);
	if (new_size > old_size) {
		blocklist.resize(new_size, 0);
		for (size_t i = old_size; i < new_size; ++i) {
			pos_t blockpos = BlockCache::get_new();
			Block * pblock = BlockCache::get(blockpos);
			pblock->basefile = this;
			std::lock_guard blockguard(pblock->status_mutex);
			pblock->status = ItemStatus::Dirty;
			this->blocklist.at(i) = blockpos;
		}
	}
	else if(new_size<old_size){
		for (size_t i = new_size; i < old_size; i++) {
			pos_t pos = blocklist.at(i);
			Block* block = BlockCache::get(pos);
			std::lock_guard<std::mutex> blocklock(block->status_mutex);
			if (block->status == ItemStatus::Uploading)block->status = ItemStatus::Up_Expired;
			else if (block->status != ItemStatus::Expired&&block->status != ItemStatus::Ready) {
				block->clear();
				block->status = ItemStatus::Expired;
				BlockCache::put_front(pos);
			}
			else {
				assert(false);
			}
		}
		blocklist.resize(new_size);
	}
	this->filesize = size;
	return size;
}

const Block & l_blob_adapter::BasicFile::get_read_block(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t &pos = blocklist.at(blockindex);
	if ( pos> 0) {
		return *(BlockCache::get(pos));
	}
	else {
		auto buffer = concurrency::streams::container_buffer< vector<uint8_t> >();
		buffer.set_buffer_size(this->blocksize, std::ios_base::out);
		auto ostream = concurrency::streams::ostream(buffer);
		m_pblob->download_range_to_stream(ostream, blockindex*this->blocksize, this->blocksize);
		// may be GC
		//pos_t blockpos=Blocks::get_free_block();
		pos_t blockpos = BlockCache:: get_new();
		Block * pblock = BlockCache::get(blockpos);
		std::lock_guard blockguard(pblock->status_mutex);
		pblock->data.assign(buffer.collection().begin(), buffer.collection().end());
		pblock->status = ItemStatus::Clean;
		this->blocklist.at(blockindex) = blockpos;
		return *pblock;
	}
}

Block & l_blob_adapter::BasicFile::get_write_block(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t pos;
	pos = blocklist.at(blockindex);
	Block * pblock = nullptr;
	
	if (pos> 0) {
		pblock= (BlockCache::get(pos));
		std::lock_guard blockguard(pblock->status_mutex);
		
		if (pblock->status == ItemStatus::Clean) {
			pblock->status = ItemStatus::Dirty;
		}
		else if (pblock->status == ItemStatus::Uploading) {
			Block *nblock = nullptr;
			pos_t npos = 0;
			pblock->status = ItemStatus::Up_Expired;
			npos = BlockCache::get_new();
			nblock = BlockCache::get(npos);
			std::lock_guard<std::mutex> nlock(nblock->status_mutex);
			*nblock = *pblock;
			nblock->status = ItemStatus::Dirty;
			blocklist.at(blockindex) = npos;
			pblock = nblock;
		}
		return *pblock;
	}
	else {
		pos_t blockpos = BlockCache::get_new();
		Block * pblock = BlockCache::get(blockpos);
		pblock->basefile = this;
		std::lock_guard blockguard(pblock->status_mutex);
		pblock->status = ItemStatus::Dirty;
		this->blocklist.at(blockindex) = blockpos;
		return *pblock;
	}
	
}


l_blob_adapter::Snapshot::Snapshot(BasicFile & file, std::unique_lock<std::mutex> && uplock)
{
	this->uplock = std::move(uplock);
	this->basefile = file.shared_from_this();
	this->metadata = file.metadata;
	this->metadata[_XPLATSTR("l_filesize")] = my_to_string(file.filesize);
	this->metadata[_XPLATSTR("l_blocksize")] = my_to_string(file.blocksize);
	this->properties = file.properties;
	for (pos_t index = 0; index < file.blocklist.size();index++) {
		if (file.blocklist.at(index) > 0) {
			Block& block =*(BlockCache::get(file.blocklist.at(index)));
			if (block.compare_and_set(ItemStatus::Dirty,ItemStatus::Uploading)) {
				this->dirtyblock[index] = file.blocklist.at(index);
				this->blocklist.emplace_back( my_to_string(index) , azure::storage::block_list_item::uncommitted );
			}
			else {
				this->blocklist.emplace_back(my_to_string(index), azure::storage::block_list_item::committed);
			}
		}
		else {
			this->blocklist.emplace_back(my_to_string(index), azure::storage::block_list_item::committed);
		}
	}

}

l_blob_adapter::Snapshot::~Snapshot()
{
	std::unique_lock<std::shared_timed_mutex> lock(basefile->m_mutex);
	if (basefile->status == FileStatus::F_Uploading) {
		for (auto& pair : dirtyblock) {
			//restore blocks
			auto& block = *(BlockCache::get(pair.second));
			block.compare_and_set(ItemStatus::Uploading, ItemStatus::Clean);
			block.compare_and_set(ItemStatus::Up_Expired, ItemStatus::Expired);
		}
		basefile->status = FileStatus::F_Clean;
	}
	else if(basefile->status == FileStatus::F_Dirty){
		for (auto& pair : dirtyblock) {
			//restore blocks and maybe gc
			auto& block = *(BlockCache::get(pair.second));
			block.compare_and_set(ItemStatus::Uploading, ItemStatus::Clean);
			block.compare_and_set(ItemStatus::Up_Expired, ItemStatus::Expired);
		}
	}
}
