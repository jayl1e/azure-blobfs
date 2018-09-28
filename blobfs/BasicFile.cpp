#include "BasicFile.h"
#include <string>
#include <assert.h>
#include <iomanip>
#include "myutils.h"
#include "Uploader.h"


using namespace l_blob_adapter;

BasicFile::BasicFile()
{
}

l_blob_adapter::BasicFile::~BasicFile()
{
}

unique_ptr<BasicFile> l_blob_adapter::BasicFile::get(guid_t file_identifier, const azure::storage::cloud_blob_container& container)
{
	auto pblob = std::make_unique<cloud_block_blob>(container.get_block_blob_reference(utility::uuid_to_string(file_identifier)));
	if(! pblob->exists())return unique_ptr<BasicFile>();
	auto ptr = std::make_unique<BasicFile>();
	ptr->m_file_identifier = file_identifier;
	pblob->download_attributes();
	ptr->blocksize = std::stoll(pblob->metadata()[_XPLATSTR("l_blocksize")]);
	ptr->filesize = std::stoll(pblob->metadata()[_XPLATSTR("l_filesize")]);
	ptr->nlink = std::stoll(pblob->metadata()[_XPLATSTR("l_nlink")]);
	if (pblob->metadata()[_XPLATSTR("l_type")] == _XPLATSTR("directory")) {
		ptr->m_type = FileType::F_Directory;
	}
	else {
		ptr->m_type = FileType::F_Regular;
	}

	if (ptr->filesize) {
		ptr->blocklist.resize((ptr->filesize - 1) / ptr->blocksize + 1);
	}
	ptr->m_pblob = std::move(pblob);
	return ptr;
}

unique_ptr<BasicFile> l_blob_adapter::BasicFile::create(guid_t file_identifier,FileType type, const azure::storage::cloud_blob_container& container)
{
	//todo
	auto ptr= std::make_unique<BasicFile>();
	ptr->m_file_identifier = file_identifier;
	ptr->blocksize = default_block_size;
	ptr->filesize = 0;
	ptr->nlink = 0;
	ptr->m_exist = true;
	ptr->m_pblob = std::make_unique<cloud_block_blob>(container.get_block_blob_reference(utility::uuid_to_string( file_identifier)));
	ptr->m_type = type;
	ptr->status = ItemStatus::Dirty;
	Uploader::add_to_wait((pos_t)(ptr.get()));
	return ptr;
}

unique_ptr<Snapshot> l_blob_adapter::BasicFile::create_snap()
{
	std::unique_lock<std::mutex> uplock(this->up_mutex);
	return std::make_unique<Snapshot>(*this,std::move(uplock));
}

size_t l_blob_adapter::BasicFile::write_bytes(const pos_t offset, const size_t size, const uint8_t * buf)
{
	if (offset < 0 || size <= 0)return 0;
	if (offset + size > this->filesize) {
		this->resize(offset + size);
	}
	pos_t start_index = ((offset) / this->blocksize);
	pos_t end_index = ((offset + size - 1) / this->blocksize);
	pos_t cblock = offset - start_index * blocksize, cbuf = 0, index = start_index;
	Block* p =nullptr;
	if (start_index == end_index) {
		p = this->get_write_block_copy(start_index);
		// for (cbuf = 0; cbuf < size; ++cbuf) p->data[cblock + cbuf]= buf[cbuf];
		// fast copy
		memcpy_s(p->data.data()+cblock, p->data.size()-cblock, buf, size);
		cbuf = size;
	}
	else {
		p = this->get_write_block_copy(start_index);
		// for (cbuf = 0; cbuf < blocksize - cblock; ++cbuf) p->data[cblock + cbuf] = buf[cbuf];
		// fast copy
		memcpy_s(p->data.data() + cblock, p->data.size() - cblock, buf, blocksize - cblock);
		cbuf = blocksize - cblock;

		for (index = start_index + 1; index < end_index; index++)
		{
			p = get_write_block(index);
			// for (cblock = 0; cblock < blocksize; ++cblock, ++cbuf)p->data[cblock]= buf[cbuf];
			// fast copy
			memcpy_s(p->data.data(), p->data.size(), buf+cbuf, blocksize - cblock);
			cbuf += blocksize;
		}
		p = get_write_block_copy(end_index);
		// for (cblock = 0; cbuf < size; ++cbuf, ++cblock)p->data[cblock]= buf[cbuf];
		// fast copy
		memcpy_s(p->data.data(), p->data.size(), buf + cbuf, size-cbuf);
		cbuf =size;
	}
	std::lock_guard lock(this->m_mutex);
	if (this->status != ItemStatus::Dirty) {
		this->status = ItemStatus::Dirty;
		Uploader::add_to_wait((pos_t)this);
	}
	return size;
}

size_t l_blob_adapter::BasicFile::read_bytes(const pos_t offset, const size_t size, uint8_t * buf)
{
	if (offset < 0 || offset >= this->filesize || size <= 0)return 0;
	size_t to_read = std::min(size, this->filesize - offset);
	pos_t start_index = ((offset) / this->blocksize );
	pos_t end_index = ((offset + to_read - 1) / this->blocksize );
	pos_t cblock=offset-start_index*blocksize, cbuf = 0, index = start_index;
	const Block* p = get_read_block(index);
	if (start_index == end_index) {
		// for (cbuf = 0; cbuf < to_read; ++cbuf)buf[cbuf] = p->data[cblock + cbuf];
		// fast copy
		memcpy_s(buf,size,p->data.data() + cblock, to_read);
		cbuf = to_read;
	}
	else {
		// for(cbuf=0;cbuf<blocksize-cblock;++cbuf)buf[cbuf] = p->data[cblock + cbuf];
		// fast copy
		memcpy_s(buf, size, p->data.data() + cblock, blocksize - cblock);
		cbuf = blocksize - cblock;

		for (index = start_index + 1; index < end_index; index++)
		{
			p = get_read_block(index);
			// for (cblock = 0; cblock < blocksize; ++cblock, ++cbuf)buf[cbuf] = p->data[cblock];
			// fast copy
			memcpy_s(buf+cbuf, size-cbuf, p->data.data(), blocksize);
			cbuf += blocksize;
		}
		p = get_read_block(end_index);
		// for (cblock = 0; cbuf < to_read; ++cbuf, ++cblock)buf[cbuf] = p->data[cblock];
		// fast copy
		memcpy_s(buf + cbuf, size - cbuf, p->data.data(), to_read-cbuf);
		cbuf = to_read;
	}
	return to_read;
}

size_t l_blob_adapter::BasicFile::resize(size_t size)
{
	std::unique_lock<std::shared_timed_mutex> lock(m_mutex);
	size_t new_size = 0, old_size=0;
	if(size>0)new_size=((size - 1) / this->blocksize +1);
	if(this->filesize>0)old_size = ((this->filesize - 1) / this->blocksize + 1);
	if (new_size > old_size) {
		blocklist.resize(new_size, 0);
		for (size_t i = old_size; i < new_size; ++i) {
			pos_t blockpos = BlockCache::get_free(0);

			Block * pblock = BlockCache::get(blockpos);
			pblock->basefile = this;
			pblock->block_index = i;
			std::lock_guard blockguard(pblock->status_mutex);
			pblock->status = ItemStatus::Dirty;
			this->blocklist.at(i) = blockpos;
		}
	}
	else if(new_size<old_size){
		for (size_t i = new_size; i < old_size; i++) {
			pos_t pos = blocklist.at(i);
			if (pos <= 0)continue;
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
	if (this->status != ItemStatus::Dirty) {
		this->status = ItemStatus::Dirty;
		Uploader::add_to_wait((pos_t)this);
	}
	
	return size;
}

void l_blob_adapter::BasicFile::inc_nlink()
{
	std::lock_guard lock(this->m_mutex);
	this->nlink++;
	if (this->status != ItemStatus::Dirty) {
		this->status = ItemStatus::Dirty;
		Uploader::add_to_wait((pos_t)this);
	}
}

void l_blob_adapter::BasicFile::dec_nlink()
{
	std::lock_guard lock(this->m_mutex);
	this->nlink--;
	if (nlink <= 0) {
		this->m_exist = false;
	}
	if (this->status != ItemStatus::Dirty) {
		this->status = ItemStatus::Dirty;
		Uploader::add_to_wait((pos_t)this);
	}
}



const Block * l_blob_adapter::BasicFile::get_read_block(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t pos = blocklist.at(blockindex);
	if ( pos> 0) {
		return (BlockCache::get(pos));
	}
	else {
		auto buffer = concurrency::streams::container_buffer< vector<uint8_t> >();
		buffer.set_buffer_size(this->blocksize, std::ios_base::out);
		auto ostream = concurrency::streams::ostream(buffer);
		m_pblob->download_range_to_stream(ostream, blockindex*this->blocksize, this->blocksize);
		pos_t blockpos = BlockCache:: get_free(0);
		Block * pblock = BlockCache::get(blockpos);
		std::lock_guard blockguard(pblock->status_mutex);
		pblock->basefile = this;
		pblock->block_index = blockindex;
		pblock->data.assign(buffer.collection().begin(), buffer.collection().end());
		pblock->status = ItemStatus::Clean;
		this->blocklist.at(blockindex) = blockpos;
		return pblock;
	}
}

Block * l_blob_adapter::BasicFile::get_write_block(const size_t blockindex)
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
			npos = BlockCache::get_free(pos);
			nblock = BlockCache::get(npos);
			std::lock_guard<std::mutex> nlock(nblock->status_mutex);
			*nblock = *pblock;
			nblock->status = ItemStatus::Dirty;
			blocklist.at(blockindex) = npos;
			pblock = nblock;
		}
		return pblock;
	}
	else {
		pos_t blockpos = BlockCache::get_free(0);
		Block * pblock = BlockCache::get(blockpos);
		pblock->basefile = this;
		pblock->block_index = blockindex;
		std::lock_guard blockguard(pblock->status_mutex);
		pblock->status = ItemStatus::Dirty;
		this->blocklist.at(blockindex) = blockpos;
		return pblock;
	}
}

Block * l_blob_adapter::BasicFile::get_write_block_copy(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t pos;
	pos = blocklist.at(blockindex);
	Block * pblock = nullptr;

	if (pos> 0) {
		pblock = (BlockCache::get(pos));
		std::lock_guard blockguard(pblock->status_mutex);

		if (pblock->status == ItemStatus::Clean) {
			pblock->status = ItemStatus::Dirty;
		}
		else if (pblock->status == ItemStatus::Uploading) {
			Block *nblock = nullptr;
			pos_t npos = 0;
			pblock->status = ItemStatus::Up_Expired;
			npos = BlockCache::get_free(pos);
			nblock = BlockCache::get(npos);
			std::lock_guard<std::mutex> nlock(nblock->status_mutex);
			*nblock = *pblock;
			nblock->status = ItemStatus::Dirty;
			blocklist.at(blockindex) = npos;
			pblock = nblock;
		}
		return pblock;
	}
	else {
		auto buffer = concurrency::streams::container_buffer< vector<uint8_t> >();
		buffer.set_buffer_size(this->blocksize, std::ios_base::out);
		auto ostream = concurrency::streams::ostream(buffer);
		m_pblob->download_range_to_stream(ostream, blockindex*this->blocksize, this->blocksize);
		pos_t blockpos = BlockCache::get_free(0);
		Block * pblock = BlockCache::get(blockpos);
		std::lock_guard blockguard(pblock->status_mutex);
		pblock->basefile = this;
		pblock->block_index = blockindex;
		pblock->data.assign(buffer.collection().begin(), buffer.collection().end());
		pblock->status = ItemStatus::Dirty;
		this->blocklist.at(blockindex) = blockpos;
		return pblock;
	}
}


l_blob_adapter::Snapshot::Snapshot(BasicFile & file, std::unique_lock<std::mutex> && uplock)
{
	std::unique_lock<std::shared_timed_mutex> lock(file.m_mutex);
	file.status = ItemStatus::Uploading;

	this->uplock = std::move(uplock);
	this->basefile = &file;
	this->metadata = file.metadata;
	this->metadata[_XPLATSTR("l_filesize")] = my_to_string(file.filesize);
	this->metadata[_XPLATSTR("l_blocksize")] = my_to_string(file.blocksize);
	this->metadata[_XPLATSTR("l_nlink")] = my_to_string(file.nlink);
	if (file.m_type == FileType::F_Directory) {
		this->metadata[_XPLATSTR("l_type")] = _XPLATSTR("directory");
	}
	else {
		this->metadata[_XPLATSTR("l_type")] = _XPLATSTR("regular");
	}
	this->properties = file.properties;
	for (pos_t index = 0; index < file.blocklist.size();index++) {
		utf8ostringstream ss;
		ss <<"l_"<< std::setw(8) << std::setfill('0') << index;
		utf8string str = ss.str();
		string_t blockid=utility::conversions::to_base64( vector<uint8_t>(str.begin(),str.end()));

		if (file.blocklist.at(index) > 0) {
			Block& block =*(BlockCache::get(file.blocklist.at(index)));
			
			if (block.compare_and_set(ItemStatus::Dirty,ItemStatus::Uploading)) {
				// std::unique_lock lock(block.write_mut);
				this->dirtyblock[index] = file.blocklist.at(index);
				this->blocklist.emplace_back(blockid, azure::storage::block_list_item::uncommitted );
			}
			else {
				this->blocklist.emplace_back(blockid, azure::storage::block_list_item::committed);
			}
		}
		else {
			this->blocklist.emplace_back(blockid, azure::storage::block_list_item::committed);
		}
	}

}

l_blob_adapter::Snapshot::~Snapshot()
{
	for (auto& pair : this->dirtyblock) {
		//restore blocks
		auto& block = *(BlockCache::get(pair.second));
		block.compare_and_set(ItemStatus::Uploading, ItemStatus::Clean);
		block.compare_and_set(ItemStatus::Up_Expired, ItemStatus::Expired);
	}

	std::unique_lock<std::shared_timed_mutex> lock(basefile->m_mutex);
	if (basefile->status == ItemStatus::Uploading) {
		basefile->status = ItemStatus::Clean;
	}
	else if(basefile->status == ItemStatus::Dirty){
		
	}
}
