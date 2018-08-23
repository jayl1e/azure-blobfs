
#include <iostream>
#include <random>
#include <chrono>
#include <was/auth.h>
#include <was/storage_account.h>
#include "BasicFile.h"
#include "Uploader.h"

using namespace std;
using namespace l_blob_adapter;
using namespace chrono_literals;

int wmain(int argc, wchar_t *argv[]) {
	utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
	storage_connection_string += U(";AccountName=");
	storage_connection_string += U("mystorageaccount27052");
	storage_connection_string += U(";AccountKey=");
	storage_connection_string += U("t14kVApN3SL7CKAz6WWj5ELz2iSM51pDzcr4MBP2Rqa7urEqDF/E7fIuGHkwIQkKCYUDKDoFF/G8deLsFU2zKA==");

	azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

	// Create the blob client.
	azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

	// Retrieve a reference to a previously created container.
	auto azure_blob_container = blob_client.get_container_reference(L"mystoragecontainer");
	

	auto uid = utility::new_uuid();
	auto pf = BasicFile::create(uid, azure_blob_container);
	pf->resize(1<<22);

	auto& cache = BlockCache::instance()->cache;

	auto blob = azure_blob_container.get_block_blob_reference(L"abc");
	vector<uint8_t> rawdata;
	rawdata.resize(1000);
	//blob.upload_block(L"0", concurrency::streams::container_stream< vector<uint8_t> >::open_istream(rawdata), L"");
	auto wr = blob.open_write();
	for(int i=0;i<10000;i++)
		wr.write('c');
	wr.close();

	Uploader::run();
	Uploader::add_to_wait((pos_t)(pf.get()));
	Uploader::stop();
	return 0;
}