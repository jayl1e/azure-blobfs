#include "BlobProxy.h"

#include <algorithm>

BlobProxy::BlobProxy(const utility::string_t& _name,bool _managed, unique_ptr<storage::cloud_block_blob>&& _pblob):name(_name),managed(_managed),pblob(std::move(_pblob))
{
	const auto meta = pblob->metadata();
	this->totalsize = pblob->properties().size();

	auto iter = meta.find(U("l_blocksize"));
	if (iter != meta.end()) {
		long long blksize = std::stoll(iter->second);
		this->blocksize = blksize;
	}
	else if(! this->managed){
		this->blocksize = this->totalsize;
	}
	else {
		assert(false);
	}
	syslog(LOG_NOTICE, "totalsize:%lld, blocksize:%lld", this->totalsize, this->blocksize);
}



size_t BlobProxy::read(char * buf, size_t size, FUSE_OFF_T offset, fuse_file_info * fi)
{
	
	if (offset < 0 ||offset>=this->totalsize || size <= 0)return 0;
	size_t to_read = std::min(size, this->totalsize - offset);
	auto iter=get_c_iter(offset);
	for (int i = 0; i < to_read; ++i,++iter) {
		buf[i] = *iter;
	}
	syslog(LOG_NOTICE, "proxy read: offset %lld, size %llu,totallengh %lld, to_read %lld", offset, size, this->totalsize,to_read);
	return to_read;
}


vector<uint8_t>& BlobProxy::get_block(int block_id, bool writable) {
	auto iter = this->blocks.find(block_id);
	if (iter != blocks.end()) {
		return iter->second;
	}
	else {
		streams::container_buffer<std::vector<uint8_t>> buffer;
		streams::ostream output_stream(buffer);
		
		pblob->download_range_to_stream(output_stream, block_id*this->blocksize, blocksize);
		syslog(LOG_NOTICE, "downloading blocks from %lld, size %lld", block_id*this->blocksize, blocksize);

		output_stream.close();
		blocks[block_id] = std::move(buffer.collection());
		blocks[block_id].resize(this->blocksize);
		return blocks[block_id];
	}
	
}

BlobProxy::ConstIterator BlobProxy::get_c_iter(size_t pos)
{
	return ConstIterator(*this,pos);
}

BlobProxy::ConstIterator BlobProxy::c_end() {
	return ConstIterator(*this, this->totalsize);
}

BlobProxy::~BlobProxy()
{
}

BlobProxy* BlobProxy::access(const utility::string_t& name)
{
	auto pblob = make_unique<storage::cloud_block_blob>(azure_blob_container->get_block_blob_reference(name));
	if (!pblob->exists())return nullptr;
	const auto& meta = pblob->metadata();
	auto iter = meta.find(U("l_managed"));
	syslog(LOG_NOTICE, "file proxy construct success");
	if (iter != meta.end() && iter->second==L"true") {
		return new BlobProxy(name, true, std::move(pblob));
	}
	else {
		return new BlobProxy(name, false, std::move(pblob));
	}
}

int BlobProxy::getattr(FUSE_STAT * stbuf)
{
	stbuf->st_mode = S_IFREG | default_permission; // Regular file
	stbuf->st_uid = fuse_get_context()->uid;
	stbuf->st_gid = fuse_get_context()->gid;
	stbuf->st_mtim = {};
	stbuf->st_nlink = 1;
	stbuf->st_blksize = 1;
	stbuf->st_blocks = this->blocksize;
	stbuf->st_size = this->totalsize;
	return 0;
}


BlobProxy::RawIterator::RawIterator(BlobProxy& proxy, size_t position, bool writable):m_base(proxy),m_position(position),is_writable(writable){
	if (position >= m_base.totalsize) {
		m_pblock = nullptr;
		return;
	}
	m_pblock = &m_base.get_block(m_position / (m_base.blocksize),writable);
	m_next_block_pos = (m_position / (m_base.blocksize) + 1)*(m_base.blocksize);
	m_this_block_pos = m_next_block_pos-m_base.blocksize;
}

inline bool BlobProxy::RawIterator::operator==(const BlobProxy::RawIterator& other) {
	return &(this->m_base) == &(other.m_base) && this->m_position == other.m_position;
}

inline BlobProxy::Iterator::char_t& BlobProxy::Iterator::operator*() {
	return m_pblock->at(m_position-m_this_block_pos);
}

inline BlobProxy::Iterator::char_t BlobProxy::ConstIterator::operator*()
{
	return m_pblock->at(m_position - m_this_block_pos);
}

inline BlobProxy::RawIterator& BlobProxy::RawIterator::operator++() {
	m_position++;
	if (m_position < m_next_block_pos) {
		return *this;
	}
	if (m_position >= m_base.totalsize) {
		assert(m_position == m_base.totalsize);
		return *this;
	}
	
	m_pblock = &m_base.get_block(m_position / (m_base.blocksize),this->is_writable);
	m_this_block_pos = m_next_block_pos;
	m_next_block_pos += m_base.blocksize;
	return *this;
}
