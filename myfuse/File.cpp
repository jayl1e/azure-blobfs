#include "File.h"

int l_blob_adapter::CommonFile::azs_getattr(FUSE_STAT * stbuf)
{
	if (this->type() == FileType::F_Regular) {
		stbuf->st_mode = S_IFREG; // Regular file
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
		stbuf->st_mtim = {};
		stbuf->st_nlink = this->nlink;
		stbuf->st_blksize = this->blocksize;
		stbuf->st_blocks = get_blockcnt();
		stbuf->st_size = this->filesize;
	}
	else {
		stbuf->st_mode = S_IFDIR; // Regular file
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
		stbuf->st_mtim = {};
		stbuf->st_nlink = this->nlink;
		stbuf->st_blksize = this->blocksize;
		stbuf->st_blocks = get_blockcnt();
		stbuf->st_size = this->filesize;
	}
	
}
