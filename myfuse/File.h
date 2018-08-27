#pragma once
#include "BasicFile.h"
#include <fuse.h>

namespace l_blob_adapter {

	class Directory;

	class CommonFile :public BasicFile {
		int azs_getattr(struct FUSE_STAT * stbuf);
		Directory* to_dir();
	};


	class Directory :public CommonFile {
		
	};
}

