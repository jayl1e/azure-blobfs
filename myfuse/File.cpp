#include "File.h"
#include <cwchar>
#include <cwctype>
#include "myutils.h"

using namespace l_blob_adapter;

unique_ptr<CommonFile> l_blob_adapter::CommonFile::get(guid_t guid, const azure::storage::cloud_blob_container& container)
{
	auto basicfile = BasicFile::get(guid, container);
	if (basicfile == nullptr) {
		return nullptr;
	}
	else {
		if (basicfile->type() == FileType::F_Regular) {
			auto ret = std::make_unique<RegularFile>();
			ret->basicfile = std::move(basicfile);
			return ret;
		}
		else if (basicfile->type() == FileType::F_Directory) {
			auto ret = std::make_unique<Directory>();
			ret->basicfile = std::move(basicfile);
			return ret;
		}
		else {
			throw std::logic_error("file type error");
		}
	}
}

int l_blob_adapter::CommonFile::azs_getattr(FUSE_STAT * stbuf, int default_permission)
{
	if (!this->exist()) {
		return ENOENT;
	}
	if (basicfile->type() == FileType::F_Regular) {
		stbuf->st_mode = S_IFREG | default_permission; // Regular file
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
		stbuf->st_mtim = {};
		stbuf->st_nlink = basicfile->get_nlink();
		stbuf->st_blksize = basicfile->get_blocksize();
		stbuf->st_blocks = basicfile->get_blockcnt();
		stbuf->st_size = basicfile->get_filesize();
	}
	else {
		stbuf->st_mode = S_IFDIR | default_permission; // Regular file
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
		stbuf->st_mtim = {};
		stbuf->st_nlink = basicfile->get_nlink();
		stbuf->st_blksize = basicfile->get_blocksize();
		stbuf->st_blocks = basicfile->get_blockcnt();
		stbuf->st_size = basicfile->get_filesize();
	}
	return 0;
}

size_t l_blob_adapter::RegularFile::write(const pos_t offset, const size_t size, const uint8_t * buf)
{
	return basicfile->write_bytes(offset, size, buf);
}

size_t l_blob_adapter::RegularFile::read(const pos_t offset, const size_t size, uint8_t * buf)
{
	return basicfile->read_bytes(offset, size, buf);
}

size_t l_blob_adapter::RegularFile::trancate(pos_t off)
{
	return basicfile->resize(off);
}

unique_ptr<RegularFile> l_blob_adapter::RegularFile::create(guid_t guid, const azure::storage::cloud_blob_container& container)
{
	auto basicfile = BasicFile::create(guid, FileType::F_Regular, container);
	auto ret = std::make_unique<RegularFile>();
	ret->basicfile = std::move(basicfile);
	return ret;
}

int l_blob_adapter::Directory::azs_readdir(void * buf, fuse_fill_dir_t filler)
{
	std::shared_lock lock(this->inner_f_mutex);
	pos_t offset = basicfile->get_filesize();
	DirEntry entry;
	int is_buf_full = 0;
	for (pos_t i = 0; i < offset; i += sizeof(DirEntry)) {
		basicfile->read_bytes(i, sizeof(DirEntry), (uint8_t *)(&entry));
		is_buf_full=filler(buf, utility::conversions::to_utf8string(entry.name).c_str(), nullptr, 0);
		if (is_buf_full)return is_buf_full;
	}
	return 0;
}

guid_t l_blob_adapter::Directory::create_dir(const string_t & name)
{
	std::unique_lock lock(this->inner_f_mutex);
	if (find_nolock(name) != guid_t()) {
		errno = EEXIST;
		return guid_t();
	}
	guid_t uid = utility::new_uuid();
	Directory * pf = FileMap::instance()->create_dir(uid);
	pf->addEntry(_XPLATSTR("."), uid);
	pf->addEntry(_XPLATSTR(".."), this->get_id());
	DirEntry entry;
	wcscpy_s(entry.name, name.c_str());
	entry.identifier = uid;
	if (!addEntry_nolock(entry)) {
		errno = EACCES;
		return guid_t();
	}
	return uid;
}

guid_t l_blob_adapter::Directory::create_reg(const string_t & name)
{
	std::unique_lock lock(this->inner_f_mutex);
	if (find_nolock(name) != guid_t()) {
		errno = EEXIST;
		return guid_t();
	}
	guid_t uid = utility::new_uuid();
	RegularFile * pf = FileMap::instance()->create_reg(uid);
	DirEntry entry;
	wcscpy_s(entry.name, name.c_str());
	entry.identifier = uid;
	if (!addEntry_nolock(entry)) {
		errno = EACCES;
		return guid_t();
	}
	return uid;
}

guid_t l_blob_adapter::Directory::find(const string_t & name)
{
	std::shared_lock lock(this->inner_f_mutex);
	return find_nolock(name);
}

bool l_blob_adapter::Directory::addEntry(const string_t name, guid_t identifier)
{
	DirEntry entry;
	wcscpy_s(entry.name, name.c_str());
	entry.identifier = identifier;
	std::unique_lock lock(this->inner_f_mutex);
	if (find_nolock(name) != guid_t()) {
		return false;
	}
	return addEntry_nolock(entry);
}


bool l_blob_adapter::Directory::rmEntry(const string_t & name)
{
	std::unique_lock lock(this->inner_f_mutex);
	pos_t offset = basicfile->get_filesize();
	DirEntry entry;
	for (pos_t i = 0; i < offset; i += sizeof(DirEntry)) {
		basicfile->read_bytes(i, sizeof(DirEntry), (uint8_t *)(&entry));
		if (wcscmp(name.c_str(), entry.name) == 0) {
			FileMap::instance()->get(entry.identifier)->basicfile->dec_nlink();
			pos_t lpos = basicfile->get_filesize() - sizeof(DirEntry);
			if (i >=lpos) { //last entry
				basicfile->resize(lpos);
			}
			else {
				DirEntry tentry;
				basicfile->read_bytes(lpos, sizeof(DirEntry), (uint8_t*)(&tentry));
				basicfile->write_bytes(i, sizeof(DirEntry), (uint8_t*)(&tentry));
				basicfile->resize(lpos);
			}
			return true;
		}
	}
	return false;
}

unique_ptr<CommonFile> l_blob_adapter::Directory::create(guid_t guid, const azure::storage::cloud_blob_container & container)
{
	auto basicfile = BasicFile::create(guid, FileType::F_Directory, container);
	auto ret = std::make_unique<Directory>();
	ret->basicfile = std::move(basicfile);
	return ret;
}

bool l_blob_adapter::Directory::addEntry_nolock(const DirEntry & entry)
{
	if (entry.name[0] == 0)return false;
	pos_t offset = basicfile->get_filesize();
	FileMap::instance()->get(entry.identifier)->basicfile->inc_nlink();
	basicfile->write_bytes(offset, sizeof(DirEntry), (uint8_t*)(&entry));
	return true;
}

guid_t l_blob_adapter::Directory::find_nolock(const string_t & name)
{
	pos_t offset = basicfile->get_filesize();
	DirEntry tentry;
	for (pos_t i = 0; i < offset; i += sizeof(DirEntry)) {
		basicfile->read_bytes(i, sizeof(DirEntry), (uint8_t *)(&tentry));
		if (wcscmp(name.c_str(), tentry.name) == 0) {
			return tentry.identifier;
		}
	}
	return guid_t();
}



FileMap*  l_blob_adapter::FileMap::m_instance;
std::mutex  l_blob_adapter::FileMap::instance_mutex;

FileMap * l_blob_adapter::FileMap::instance()
{
	if (m_instance == nullptr) {
		std::lock_guard<std::mutex> lock(instance_mutex);
		if (m_instance == nullptr) {
			m_instance = new FileMap();
		}
	}
	return m_instance;
}

CommonFile * l_blob_adapter::FileMap::get(guid_t guid)
{
	std::lock_guard lock(maplock);
	auto iter = this->filemap.find(guid);
	if (iter != this->filemap.end()) {
		if (iter->second->exist())return iter->second.get();
		else return nullptr;
	}
	else {
		auto ptr=CommonFile::get(guid, *azure_blob_container);
		if (ptr == nullptr)return nullptr;
		filemap[guid] = std::move(ptr);
		return filemap[guid].get();
	}
}

RegularFile * l_blob_adapter::FileMap::create_reg(guid_t guid)
{
	std::unique_ptr<CommonFile> ptr = RegularFile::create(guid, *azure_blob_container);
	std::lock_guard lock(maplock);
	this->filemap[guid] = std::move(ptr);
	return (RegularFile*)(this->filemap[guid]).get();
}

Directory * l_blob_adapter::FileMap::create_dir(guid_t guid)
{
	std::unique_ptr<CommonFile> ptr = Directory::create(guid, *azure_blob_container);
	std::lock_guard lock(maplock);
	this->filemap[guid] = std::move(ptr);
	return (Directory *)(this->filemap[guid]).get();
}

guid_t l_blob_adapter::parse_path_relative(const string_t& path, guid_t base)
{
	utf16istringstream iss(path);
	string_t item;
	wchar_t delim = _XPLATSTR('/');
	guid_t ret = base;
	guid_t nul = guid_t();
	while (std::getline(iss, item, delim)) {
		trim(item);
		if (item.empty())continue;
		else {
			Directory* p= FileMap::instance()->get(ret)->to_dir();
			if (!p)return nul;
			ret = p->find(item);
			if (ret == nul) {
				return nul;
			}
		}
	}
	return ret;
}


