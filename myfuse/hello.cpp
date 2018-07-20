#include <fuse.h>
#include <cstdio>
#include <string.h>
#include <cassert>

static struct Options {
	const char *filename;
	const char *contents;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct Options, p), 1 }

static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--contents=%s", contents),
	//OPTION("-h", show_help),
	//OPTION("--help", show_help),
	FUSE_OPT_END
};

static void *hello_init(struct fuse_conn_info *conn)
{
	return nullptr;
}

static int hello_getattr(const char *path, struct FUSE_STAT *stbuf)
{
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else if (strcmp(path + 1, options.filename) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(options.contents);
	}
	else
		res = -ENOENT;
	return res;
}

int hello_readdir (const char * path, void * buf, fuse_fill_dir_t filler, FUSE_OFF_T offset,
	struct fuse_file_info * fi)
{
	if (strcmp(path, "/") != 0)
		return -ENOENT;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, options.filename, NULL, 0);
	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path + 1, options.filename) != 0)
		return -ENOENT;
	if (fi->flags)
		return -EACCES;
	return 0;
}

int hello_read (const char * path, char * buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info * fi)
{
	size_t len;
	(void)fi;
	if (strcmp(path + 1, options.filename) != 0)
		return -ENOENT;
	len = strlen(options.contents);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, options.contents + offset, size);
	}
	else
		size = 0;
	return size;
}

static struct fuse_operations hello_operations = {};

void init_static() {
	hello_operations.init = hello_init;
	hello_operations.getattr = hello_getattr;
	hello_operations.readdir = hello_readdir;
	hello_operations.open = hello_open;
	hello_operations.read = hello_read;
}

static void show_help(const char *progname)
{
	std::printf("usage: %s [options] <mountpoint>\n\n", progname);
	std::printf("File-system specific options:\n"
		"    --name=<s>          Name of the \"hello\" file\n"
		"                        (default: \"hello\")\n"
		"    --contents=<s>      Contents \"hello\" file\n"
		"                        (default \"Hello, World!\\n\")\n"
		"\n");
}

int __main(int argc,char *argv[]) {
	init_static();
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	options.filename = strdup("hello");
	options.contents = strdup("Hello World!\n");
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0] = (char*) "";
	}
	return fuse_main(args.argc, args.argv, &hello_operations, NULL);
}


