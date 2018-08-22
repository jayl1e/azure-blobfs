#include "Block.h"

using namespace l_blob_adapter;


l_blob_adapter::Block::Block():data(1<<16)
{
}

int l_blob_adapter::Block::gc_notify(pos_t pos)
{
	return 0;
}

void l_blob_adapter::Block::clear()
{
	return;
}

