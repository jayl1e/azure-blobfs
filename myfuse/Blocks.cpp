#include "Blocks.h"

using namespace l_blob_adapter;

Blocks::Blocks()
{
}


Blocks::~Blocks()
{
}

vector<Block> Blocks::block_cache;
list<size_t> Blocks::freelist;
size_t Blocks::max_cache;