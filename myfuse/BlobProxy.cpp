#include "BlobProxy.h"

#include <algorithm>

BlobProxy::BlobProxy(const utility::string_t& name,unique_ptr<storage::cloud_block_blob>&& blob)
{
	this->name = name;
	this->pblob = std::move(blob);
	const auto meta = pblob->metadata();
	this->totalsize = pblob->properties().size();

	auto iter = meta.find(U("l_blocksize"));
	if (iter != meta.end()) {
		long long blksize = std::stoll(iter->second);
		this->blocksize = blksize;
	}
	syslog(LOG_EMERG, "totalsize:%lld, blocksize:%lld", this->totalsize, this->blocksize);
}



size_t BlobProxy::read(char * buf, size_t size, FUSE_OFF_T offset, fuse_file_info * fi)
{
	
	if (offset < 0 ||offset>=this->totalsize || size <= 0)return 0;
	size_t to_read = std::min(size, this->totalsize - offset);
	auto iter=get_c_iter(offset);
	for (int i = 0; i < to_read; ++i,++iter) {
		buf[i] = *iter;
	}
	syslog(LOG_EMERG, "proxy read: offset %lld, size %llu,totallengh %lld, to_read %lld", offset, size, this->totalsize,to_read);
	return to_read;
}


vector<uint8_t>& BlobProxy::get_block(int block_id) {
	auto iter = this->blocks.find(block_id);
	if (iter != blocks.end()) {
		return iter->second;
	}
	else {
		streams::container_buffer<std::vector<uint8_t>> buffer;
		streams::ostream output_stream(buffer);
		
		pblob->download_range_to_stream(output_stream, block_id*this->blocksize, blocksize);
		syslog(LOG_EMERG, "downloading blocks from %lld, size %lld", block_id*this->blocksize, blocksize);

		output_stream.close();
		blocks[block_id]= std::move(buffer.collection());
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

BlobProxy* BlobProxy::open(const utility::string_t& name)
{
	auto pblob = make_unique<storage::cloud_block_blob>(azure_blob_container->get_block_blob_reference(name));
	if (!pblob->exists())return nullptr;
	const auto& meta = pblob->metadata();
	auto iter = meta.find(U("l_managed"));
	if (iter == meta.end())return nullptr;
	syslog(LOG_EMERG, "proxy open success");
	return new BlobProxy(name,std::move(pblob));
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


BlobProxy::RawIterator::RawIterator(BlobProxy& proxy, size_t position):m_base(proxy),m_position(position){
	if (position >= m_base.totalsize) {
		m_pblock = nullptr;
		return;
	}
	m_pblock = &m_base.get_block(m_position / (m_base.blocksize));
	m_next_block_pos = (m_position / (m_base.blocksize) + 1)*(m_base.blocksize);
	m_this_block_pos = m_next_block_pos-m_base.blocksize;
}

bool BlobProxy::RawIterator::operator==(const BlobProxy::RawIterator& other) {
	return &(this->m_base) == &(other.m_base) && this->m_position == other.m_position;
}

BlobProxy::Iterator::char_t& BlobProxy::Iterator::operator*() {
	return m_pblock->at(m_position-m_this_block_pos);
}

BlobProxy::Iterator::char_t BlobProxy::ConstIterator::operator*()
{
	return m_pblock->at(m_position - m_this_block_pos);
}

BlobProxy::RawIterator& BlobProxy::RawIterator::operator++() {
	m_position++;
	if (m_position < m_next_block_pos) {
		return *this;
	}
	if (m_position >= m_base.totalsize) {
		assert(m_position == m_base.totalsize);
		return *this;
	}
	
	m_pblock = &m_base.get_block(m_position / (m_base.blocksize));
	m_this_block_pos = m_next_block_pos;
	m_next_block_pos += m_base.blocksize;
	return *this;
}
