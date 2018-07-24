#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>  
#include <cpprest/containerstream.h>
#include "blobfuse.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <exception>

struct Str_options str_options = {};
int file_cache_timeout_in_seconds = 30;
int default_permission = 0;
struct fuse_operations azs_fuse_operations = {};
using std::wstring;
using std::string;

// FUSE contains a specific type of command-line option parsing; here we are just following the pattern.
struct Options
{
	const char *tmp_path; // Path to the temp / file cache directory
	const char *config_file; // Connection to Azure Storage information (account name, account key, etc)
	const char *use_https; // True if https should be used (defaults to false)
	const char *file_cache_timeout_in_seconds; // Timeout for the file cache (defaults to 120 seconds)
	const char *container_name; //container to mount. Used only if config_file is not provided
	const char *log_level; // Sets the level at which the process should log to syslog.
	const char *version; // print blobfuse version
	const char *help; // print blobfuse usage
};

struct Options options;

#define OPTION(t, p) { t, offsetof(struct Options, p), 1 }
const struct fuse_opt option_spec[] =
{
	OPTION("--tmp-path=%s", tmp_path),
	OPTION("--config-file=%s", config_file),
	OPTION("--use-https=%s", use_https),
	OPTION("--file-cache-timeout-in-seconds=%s", file_cache_timeout_in_seconds),
	OPTION("--container-name=%s", container_name),
	OPTION("--log-level=%s", log_level),
	OPTION("--version", version),
	OPTION("-v", version),
	OPTION("--help", help),
	OPTION("-h", help),
	FUSE_OPT_END
};

std::shared_ptr<azure::storage::cloud_blob_container> azure_blob_container;

const std::string log_ident = "blobfuse";

void print_usage()
{
	fprintf(stdout, "Usage: blobfuse <mount-folder> --tmp-path=</path/to/fusecache> [--config-file=</path/to/config.cfg> | --container-name=<containername>]");
	fprintf(stdout, "    [--use-https=true] [--file-cache-timeout-in-seconds=120] [--log-level=LOG_OFF|LOG_CRIT|LOG_ERR|LOG_WARNING|LOG_INFO|LOG_DEBUG]\n\n");
	fprintf(stdout, "In addition to setting --tmp-path parameter, you must also do one of the following:\n");
	fprintf(stdout, "1. Specify a config file (using --config-file]=) with account name, account key, and container name, OR\n");
	fprintf(stdout, "2. Set the environment variables AZURE_STORAGE_ACCOUNT and AZURE_STORAGE_ACCESS_KEY, and specify the container name with --container-name=\n\n");
	fprintf(stdout, "See https://github.com/Azure/azure-storage-fuse for detailed installation and configuration instructions.\n");
}

static std::string trim(const std::string& str) {
	const size_t start = str.find_first_not_of(' ');
	if (std::string::npos == start) {
		return std::string();
	}
	const size_t end = str.find_last_not_of(' ');
	return str.substr(start, end - start + 1);
}

int read_config_env()
{
	char* env_account = getenv("AZURE_STORAGE_ACCOUNT");
	char* env_account_key = getenv("AZURE_STORAGE_ACCESS_KEY");
	char* env_sas_token = getenv("AZURE_STORAGE_SAS_TOKEN");

	if (env_account)
	{
		str_options.accountName = env_account;
	}
	else
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  No config file was specified and AZURE_STORAGE_ACCESS_KEY environment variable is empty.");
		fprintf(stderr, "No config file was specified and AZURE_STORAGE_ACCOUNT environment variable is empty.\n");
		return -1;
	}

	if (env_account_key)
	{
		str_options.accountKey = env_account_key;
	}

	if (env_sas_token)
	{
		str_options.sasToken = env_sas_token;
	}

	if ((!env_account_key && !env_sas_token) ||
		(env_account_key && env_sas_token))
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  If no config file is specified, exactly one of the environment variables AZURE_STORAGE_ACCESS_KEY or AZURE_STORAGE_SAS_TOKEN must be set.");
		fprintf(stderr, "Unable to start blobfuse.  If no config file is specified, exactly one of the environment variables AZURE_STORAGE_ACCESS_KEY or AZURE_STORAGE_SAS_TOKEN must be set.\n");
	}

	return 0;
}

int read_config(const std::string configFile)
{
	std::ifstream file(configFile);
	if (!file)
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  No config file found at %s.", configFile.c_str());
		fprintf(stderr, "No config file found at %s.\n", configFile.c_str());
		return -1;
	}

	std::string line;
	std::istringstream data;

	while (std::getline(file, line))
	{

		data.str(line.substr(line.find(" ") + 1));
		const std::string value(trim(data.str()));

		if (line.find("accountName") != std::string::npos)
		{
			std::string accountNameStr(value);
			str_options.accountName = accountNameStr;
		}
		else if (line.find("accountKey") != std::string::npos)
		{
			std::string accountKeyStr(value);
			str_options.accountKey = accountKeyStr;
		}
		else if (line.find("sasToken") != std::string::npos)
		{
			std::string sasTokenStr(value);
			str_options.sasToken = sasTokenStr;
		}
		else if (line.find("containerName") != std::string::npos)
		{
			std::string containerNameStr(value);
			str_options.containerName = containerNameStr;
		}
		else if (line.find("blobEndpoint") != std::string::npos)
		{
			std::string blobEndpointStr(value);
			str_options.blobEndpoint = blobEndpointStr;
		}

		data.clear();
	}

	if (str_options.accountName.empty())
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Account name is missing in the config file.");
		fprintf(stderr, "Account name is missing in the config file.\n");
		return -1;
	}
	else if ((str_options.accountKey.empty() && str_options.sasToken.empty()) ||
		(!str_options.accountKey.empty() && !str_options.sasToken.empty()))
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Exactly one of Account Key and SAS token must be specified in the config file, and the other line should be deleted.");
		fprintf(stderr, "Unable to start blobfuse. Exactly one of Account Key and SAS token must be specified in the config file, and the other line should be deleted.\n");
		return -1;
	}
	else if (str_options.containerName.empty())
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Container name is missing in the config file.");
		fprintf(stderr, "Container name is missing in the config file.\n");
		return -1;
	}
	else
	{
		return 0;
	}
}



void print_version()
{
	fprintf(stdout, "blobfuse 1.0.2\n");
}


int set_log_mask(const char * min_log_level_char)
{
	if (!min_log_level_char)
	{
		setlogmask(LOG_UPTO(LOG_ALERT));
		return 0;
	}
	std::string min_log_level(min_log_level_char);
	if (min_log_level.empty())
	{
		setlogmask(LOG_UPTO(LOG_WARNING));
		return 0;
	}
	// Options for logging: LOG_OFF, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG
	if (min_log_level == "LOG_OFF")
	{
		setlogmask(LOG_UPTO(LOG_EMERG)); // We don't use 'LOG_EMERG', so this won't log anything.
		return 0;
	}
	if (min_log_level == "LOG_CRIT")
	{
		setlogmask(LOG_UPTO(LOG_CRIT));
		return 0;
	}
	if (min_log_level == "LOG_ERR")
	{
		setlogmask(LOG_UPTO(LOG_ERR));
		return 0;
	}
	if (min_log_level == "LOG_WARNING")
	{
		setlogmask(LOG_UPTO(LOG_WARNING));
		return 0;
	}
	if (min_log_level == "LOG_INFO")
	{
		setlogmask(LOG_UPTO(LOG_INFO));
		return 0;
	}
	if (min_log_level == "LOG_DEBUG")
	{
		setlogmask(LOG_UPTO(LOG_DEBUG));
		return 0;
	}

	syslog(LOG_CRIT, "Unable to start blobfuse. Error: Invalid log level \"%s\"", min_log_level.c_str());
	fprintf(stdout, "Error: Invalid log level \"%s\".  Permitted values are LOG_OFF, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG.\n", min_log_level.c_str());
	fprintf(stdout, "If not specified, logging will default to LOG_WARNING.\n\n");
	return 1;
}

int read_and_set_arguments(int argc, char *argv[], struct fuse_args *args)
{
	// FUSE has a standard method of argument parsing, here we just follow the pattern.
	*args = FUSE_ARGS_INIT(argc, argv);

	// Check for existence of allow_other flag and change the default permissions based on that
	default_permission = 0777;
	std::vector<std::string> string_args(argv, argv + argc);
	for (size_t i = 1; i < string_args.size(); ++i) {
		if (string_args[i].find("allow_other") != std::string::npos) {
			default_permission = 0777;
		}
	}

	int ret = 0;
	try
	{

		if (fuse_opt_parse(args, &options, option_spec, NULL) == -1)
		{
			return 1;
		}

		if (options.version)
		{
			print_version();
			exit(0);
		}

		if (options.help)
		{
			print_usage();
			exit(0);
		}

		if (!options.config_file)
		{
			if (!options.container_name)
			{
				syslog(LOG_CRIT, "Unable to start blobfuse, no config file provided and --container-name is not set.");
				fprintf(stderr, "Error: No config file provided and --container-name is not set.\n");
				print_usage();
				return 1;
			}

			std::string container(options.container_name);
			str_options.containerName = container;
			ret = read_config_env();
		}
		else
		{
			ret = read_config(options.config_file);
		}

		if (ret != 0)
		{
			return ret;
		}
	}
	catch (std::exception &)
	{
		print_usage();
		return 1;
	}

	int res = set_log_mask(options.log_level);
	if (res != 0)
	{
		print_usage();
		return 1;
	}

	// remove last trailing slash in tmo_path
	if (!options.tmp_path)
	{
		fprintf(stderr, "Error: --tmp-path is not set.\n");
		print_usage();
		return 1;
	}

	std::string tmpPathStr(options.tmp_path);
	if (!tmpPathStr.empty() && tmpPathStr[tmpPathStr.size() - 1] == '/')
	{
		tmpPathStr.erase(tmpPathStr.size() - 1);
	}

	str_options.tmpPath = tmpPathStr;
	str_options.use_https = true;
	if (options.use_https != NULL)
	{
		std::string https(options.use_https);
		if (https == "false")
		{
			str_options.use_https = false;
		}
	}

	if (options.file_cache_timeout_in_seconds != NULL)
	{
		std::string timeout(options.file_cache_timeout_in_seconds);
		file_cache_timeout_in_seconds = stoi(timeout);
	}
	else
	{
		file_cache_timeout_in_seconds = 120;
	}
	return 0;
}


int validate_storage_connection()
{
	// The current implementation of blob_client_wrapper calls curl_global_init() in the constructor, and curl_global_cleanup in the destructor.
	// Unfortunately, curl_global_init() has to be called in the same process as any HTTPS calls that are made, otherwise NSS is not configured properly.
	// When running in daemon mode, the current process forks() and exits, while the child process lives on as a daemon.
	// So, here we create and destroy a temp blob client in order to test the connection info, and we create the real one in azs_init, which is called after the fork().
	{
		const int defaultMaxConcurrency = 20;
		utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
		storage_connection_string += U(";AccountName=");
		storage_connection_string += string2wstring(str_options.accountName);
		storage_connection_string += U(";AccountKey=");
		storage_connection_string += string2wstring(str_options.accountKey);

		azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

		// Create the blob client.
		azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

		// Retrieve a reference to a previously created container.
		auto azure_blob_container = blob_client.get_container_reference(string2wstring(str_options.containerName));
		auto blob=azure_blob_container.get_block_blob_reference(L"root/my/haha.txt");
		std::vector<std::uint8_t> vec;
		auto ins = "hello";
		vec.assign(ins, ins + 5);
		concurrency::streams::container_buffer<std::vector<uint8_t>> buffer(vec);
		concurrency::streams::istream istream(buffer);
		blob.upload_from_stream(istream);
		istream.close();
	}
	return 0;
}

void configure_fuse(struct fuse_args *args)
{
	fuse_opt_add_arg(args, "-omax_read=131072");
	
	fuse_opt_add_arg(args, "-f");

	if (options.file_cache_timeout_in_seconds != NULL)
	{
		std::string timeout(options.file_cache_timeout_in_seconds);
		file_cache_timeout_in_seconds = stoi(timeout);
	}
	else
	{
		file_cache_timeout_in_seconds = 120;
	}

	// FUSE contains a feature where it automatically implements 'soft' delete if one process has a file open when another calls unlink().
	// This feature causes us a bunch of problems, so we use "-ohard_remove" to disable it, and track the needed 'soft delete' functionality on our own.
	fuse_opt_add_arg(args, "-ohard_remove");
	fuse_opt_add_arg(args, "-obig_writes");
	fuse_opt_add_arg(args, "-ofsname=blobfuse");
	fuse_opt_add_arg(args, "-okernel_cache");
}


void *azs_init(struct fuse_conn_info * conn)
{
	// Retrieve storage account from connection string.

	utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
	storage_connection_string += U(";AccountName=");
	storage_connection_string += string2wstring(str_options.accountName);
	storage_connection_string += U(";AccountKey=");
	storage_connection_string += string2wstring(str_options.accountKey);

	azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

	// Create the blob client.
	azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

	// Retrieve a reference to a previously created container.
	azure_blob_container = std::make_shared<azure::storage::cloud_blob_container>(blob_client.get_container_reference(string2wstring(str_options.containerName)));

	/*
	cfg->attr_timeout = 360;
	cfg->kernel_cache = 1;
	cfg->entry_timeout = 120;
	cfg->negative_timeout = 120;
	*/
	conn->max_write = 4194304;
	//conn->max_read = 4194304;
	conn->max_readahead = 4194304;
	// conn->max_background = 128;
	//  conn->want |= FUSE_CAP_WRITEBACK_CACHE | FUSE_CAP_EXPORT_SUPPORT; // TODO: Investigate putting this back in when we downgrade to fuse 2.9

	//gc_cache.run();

	return NULL;
}


int azs_statfs(const char *path, struct statvfs * stbuf)
{
	AZS_DEBUGLOGV("azs_statfs called with path = %s.\n", path);
	std::string pathString(path);

	struct FUSE_STAT statbuf;
	stbuf->f_bsize = 8;
	stbuf->f_blocks = 1000000000000;
	stbuf->f_bfree = 100000000000;
	stbuf->f_bavail = 100000000000;
	//int getattrret = azs_getattr(path, &statbuf);
	//if (getattrret != 0)
	//{
	//	return getattrret;
	//}

	//// return tmp path stats
	//errno = 0;
	////int res = statvfs(str_options.tmpPath.c_str(), stbuf);
	////if (res == -1)
	////	return -errno;

	return 0;
}


bool is_directory_blob(const azure::storage::cloud_blob & blob)
{
	const auto meta = blob.metadata();
	auto iter = meta.find(U("is_dir"));
	if ( iter!= meta.end() && iter->second==U("true")) {
		return true;
	}
	return false;
}


int azs_getattr(const char *path, struct FUSE_STAT * stbuf)
{
	AZS_DEBUGLOGV("azs_getattr called with path = %s\n", path);

	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}

	if (name.empty()) {
		//name is rootdir
		stbuf->st_mode = S_IFDIR | default_permission;
		// If st_nlink = 2, means direcotry is empty.
		// Directory size will affect behaviour for mv, rmdir, cp etc.
		stbuf->st_uid = fuse_get_context()->uid;
		stbuf->st_gid = fuse_get_context()->gid;
		stbuf->st_nlink = 3;
		stbuf->st_size = 0;
		return 0;
	}
	else {
		//name is regular file or directory
		azure::storage::cloud_blob blob = azure_blob_container->get_blob_reference(name);
		if (!blob.is_valid() || !blob.exists()) {
			errno = ENOENT;
			return -1;
		}
		if (!is_directory_blob(blob)) {
			stbuf->st_mode = S_IFREG | default_permission; // Regular file
			stbuf->st_uid = fuse_get_context()->uid;
			stbuf->st_gid = fuse_get_context()->gid;
			stbuf->st_mtim = {};
			stbuf->st_nlink = 1;
			stbuf->st_blksize = 1;
			stbuf->st_blocks = blob.properties().size();
			stbuf->st_size = blob.properties().size();
			return 0;
		}
		else {
			stbuf->st_mode = S_IFDIR | default_permission; // dir
			stbuf->st_uid = fuse_get_context()->uid;
			stbuf->st_gid = fuse_get_context()->gid;
			stbuf->st_nlink = 3;
			stbuf->st_size = 0;
			return 0;
		}
	}
}

wstring map_to_blob_path(const char *path) throw (std::exception){
	if (!path[0]) {
		throw std::exception("bad path");
	}
	if (path[0] == '/') {
		return string2wstring(path + 1);
	}
	else {
		return string2wstring(path);
	}
}

int azs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, FUSE_OFF_T, struct fuse_file_info *)
{
	AZS_DEBUGLOGV("azs_readdir called with path = %s\n", path);
	wstring dirname;
	try {
		dirname = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}
	

	azure::storage::list_blob_item_iterator end_iter;
	int is_buf_full = 0;
	struct FUSE_STAT statbuf = {};
	statbuf.st_mode = S_IFDIR | default_permission;
	statbuf.st_uid = fuse_get_context()->uid;
	statbuf.st_gid = fuse_get_context()->gid;
	statbuf.st_nlink = 3;
	statbuf.st_size = 0;
	filler(buf, ".", &statbuf, 0);
	filler(buf, "..", &statbuf, 0);

	//if it is the root dir
	if (dirname.empty()) {
		for (auto it = azure_blob_container->list_blobs(); it != end_iter; ++it) {
			if (it->is_blob()) {
				auto blobref = it->as_blob();
				is_buf_full = filler(buf, wstring2string(blobref.name()).c_str(), NULL, 0);
			}
			if (is_buf_full)return is_buf_full;
		}
		return 0;
	}

	//if it is common dir
	auto dirref=azure_blob_container->get_directory_reference(dirname);
	auto blobref = azure_blob_container->get_blob_reference(dirname);
	if (!dirref.is_valid()||!blobref.exists()||!is_directory_blob(blobref)) {
		errno = ENOENT;
		return -1;
	}
	wstring prefix = dirref.prefix();
	
	for (auto it = dirref.list_blobs(); it != end_iter; ++it) {
		if(it->is_blob()) {
			auto blobref = it->as_blob();
			filler(buf, wstring2string(blobref.name().substr(prefix.length())).c_str(), NULL, 0);
		}
		if (is_buf_full)return is_buf_full;
	}
	return 0;
}

static int azs_open_stub(const char *path, struct fuse_file_info *fi)
{
	fi->direct_io = 1;
	return 0;
}

static int azs_read(const char * path, char * buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info * fi)
{
	AZS_DEBUGLOGV("azs_read called with path = %s\n", path);
	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}

	auto blob = azure_blob_container->get_blob_reference(name);
	concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;
	concurrency::streams::ostream output_stream(buffer);
	if (!blob.exists()) {
		errno = ENOENT;
		return -1;
	}
		
	if (is_directory_blob(blob)) {
		errno = EISDIR;
		return -1;
	}
	try {
		blob.download_range_to_stream(output_stream, offset, size);
	}
	catch (std::exception &e) {
		//syslog(LOG_EMERG, e.what());
		/*size_t fake_taken = blob.properties().size();
		fake_taken = (fake_taken / 512 + ((fake_taken % 512) ? 1 : 0)) * 512;
		memset(buf, 0, size);
		if (offset + size >= fake_taken) {
			return (fake_taken - offset)>0?fake_taken-offset:0;
		}*/
		if(size)buf[0] = 0;
		return 0;
	}
	const auto& contain = buffer.collection();
	for (int i = 0; i < contain.size(); i++) {
		buf[i] = contain[i];
	}
	return contain.size();
}

static int azs_write(const char *path, const char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi) {
	syslog(LOG_EMERG,"write called with path = %s, size %d, offset %d\n", path,(int)size,(int)offset);
	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}

	auto blob = azure_blob_container->get_block_blob_reference(name);
	concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;
	concurrency::streams::ostream output_stream(buffer);
	if (!blob.exists()) {
		errno = ENOENT;
		return -1;
	}

	if (is_directory_blob(blob)) {
		errno = EISDIR;
		return -1;
	}

	try {
		blob.download_to_stream(output_stream);
	}
	catch (std::exception & e) {
		//syslog(LOG_ALERT, e.what());
		errno = EIO;
		return -1;
	}
	auto& contain = buffer.collection();
	if (offset + size > contain.size()) {
		contain.resize(offset + size);
	}
	
	for (int i = 0; i < size; i++) {
		contain[offset + i] = buf[i];
	}
	concurrency::streams::container_buffer<std::vector<uint8_t>> ibuffer(contain);
	concurrency::streams::istream istream(ibuffer);
	blob.upload_from_stream(istream);
	return size;
}

int azs_truncate(const char * path, FUSE_OFF_T offset) {
	syslog(LOG_EMERG, "truncate called with path = %s, offset %d\n", path, (int)offset);
	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}

	auto blob = azure_blob_container->get_block_blob_reference(name);
	concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;
	concurrency::streams::ostream output_stream(buffer);
	if (!blob.exists()) {
		errno = ENOENT;
		return -1;
	}

	if (is_directory_blob(blob)) {
		errno = EISDIR;
		return -1;
	}
	try {
		blob.download_to_stream(output_stream);
	}
	catch (std::exception & e) {
		//syslog(LOG_ALERT, e.what());
		errno = EIO;
		return -1;
	}
	auto& contain = buffer.collection();
	contain.resize(offset);
	concurrency::streams::container_buffer<std::vector<uint8_t>> ibuffer(contain);
	concurrency::streams::istream istream(ibuffer);
	blob.upload_from_stream(istream);
	return 0;
}


int azs_create_stub(const char *path, mode_t mode, struct fuse_file_info *fi) {
	syslog(LOG_EMERG, "create %s", path);
	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}
	auto blob = azure_blob_container->get_blob_reference(name);
	if (blob.exists()) {
		errno = EEXIST;
		return -1;
	}
	wstring parent = blob.get_parent_reference().prefix();
	parent.erase(parent.length() - 1, 1);
	

	if (parent.length()) {
		auto parent_blob = azure_blob_container->get_blob_reference(parent);
		if (!parent_blob.exists() || !is_directory_blob(parent_blob)) {
			errno = ENOENT;
			return -1;
		}
	}
	auto blockblob = azure_blob_container->get_block_blob_reference(name);
	blockblob.open_write().close();
	return 0;
}

int azs_release_stub(const char *path, struct fuse_file_info * fi) {
	return 0;
}

int azs_fsync_stub(const char * /*path*/, int /*isdatasync*/, struct fuse_file_info * /*fi*/) {
	return 0;
}

int azs_mkdir_stub(const char *path, mode_t) {
	syslog(LOG_EMERG, "mkdir %s", path);
	wstring name;
	try {
		name = map_to_blob_path(path);
	}
	catch (const std::exception& e) {
		syslog(LOG_ALERT, e.what());
		errno = ENOENT;
		return -1;
	}
	auto blob = azure_blob_container->get_blob_reference(name);
	if (blob.exists()) {
		errno = EEXIST;
		return -1;
	}
	wstring parent = blob.get_parent_reference().prefix();
	if(parent.length() && parent.back()==L'/')parent.pop_back();


	if (parent.length()) {
		auto parent_blob = azure_blob_container->get_blob_reference(parent);
		if (!parent_blob.exists() || !is_directory_blob(parent_blob)) {
			errno = ENOENT;
			return -1;
		}
	}
	auto blockblob = azure_blob_container->get_block_blob_reference(name);
	blockblob.upload_text(L"");
	auto& metadata = blockblob.metadata();
	metadata[L"is_dir"] = L"true";
	blockblob.upload_metadata();
	return 0;
}

int azs_unlink_stub(const char *path) {
	return 0;
}

int azs_rmdir_stub(const char *path) {
	return 0;
}

int azs_chown_stub(const char * /*path*/, uid_t /*uid*/, gid_t /*gid*/) {
	return 0;
}

int azs_chmod_stub(const char * /*path*/, mode_t /*mode*/)
{
	//TODO: Implement
	//    return -ENOSYS;
	return 0;
}

int azs_utimens(const char * /*path*/, const struct timespec[2] /*ts[2]*/)
{
	//TODO: Implement
	//    return -ENOSYS;
	return 0;
}

void azs_destroy(void * /*private_data*/)
{
	AZS_DEBUGLOG("azs_destroy called.\n");

}



int azs_rename(const char *src, const char *dst) {
	return 0;
}

int azs_setxattr(const char * /*path*/, const char * /*name*/, const char * /*value*/, size_t /*size*/, int /*flags*/)
{
	return -ENOSYS;
}
int azs_getxattr(const char * /*path*/, const char * /*name*/, char * /*value*/, size_t /*size*/)
{
	return -ENOSYS;
}
int azs_listxattr(const char * /*path*/, char * /*list*/, size_t /*size*/)
{
	return -ENOSYS;
}
int azs_removexattr(const char * /*path*/, const char * /*name*/)
{
	return -ENOSYS;
}

int azs_flush(const char *path, struct fuse_file_info *fi) {
	return 0;
}

void set_up_callbacks() {
	azs_fuse_operations.init = azs_init;
	azs_fuse_operations.statfs = azs_statfs;
	azs_fuse_operations.getattr = azs_getattr;
	azs_fuse_operations.readdir = azs_readdir;
	azs_fuse_operations.read = azs_read;
	azs_fuse_operations.write = azs_write;
	azs_fuse_operations.open = azs_open_stub;
	azs_fuse_operations.create= azs_create_stub;
	azs_fuse_operations.release = azs_release_stub;
	azs_fuse_operations.fsync = azs_fsync_stub;
	azs_fuse_operations.mkdir = azs_mkdir_stub;
	azs_fuse_operations.unlink = azs_unlink_stub;
	azs_fuse_operations.rmdir = azs_rmdir_stub;
	azs_fuse_operations.chown = azs_chown_stub;
	azs_fuse_operations.chmod = azs_chmod_stub;
	azs_fuse_operations.utimens = azs_utimens;
	azs_fuse_operations.destroy = azs_destroy;
	azs_fuse_operations.truncate = azs_truncate;
	azs_fuse_operations.rename = azs_rename;
	azs_fuse_operations.setxattr = azs_setxattr;
	azs_fuse_operations.getxattr = azs_getxattr;
	azs_fuse_operations.listxattr = azs_listxattr;
	azs_fuse_operations.removexattr = azs_removexattr;
	azs_fuse_operations.flush = azs_flush;
}