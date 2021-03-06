#include "aug_plugin.h"

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <wordexp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <iconv.h>

const char aug_plugin_name[] = "aug-record";

void input_char(uint32_t *, aug_action *, void *);

const struct aug_api *g_api;
struct aug_plugin *g_plugin;
FILE *g_fp;
iconv_t g_cd;
const int g_dir_mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
const int g_file_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

struct aug_plugin_cb g_callbacks;

#define LOG(...) \
	(*g_api->log)(g_plugin, __VA_ARGS__)

void input_char(uint32_t *ch, aug_action *action, void *user) {
	size_t inbytesleft, outbytesleft;
	char buf[16];
	char *in_p, *out_p;
	uint32_t input;
	(void)(action);
	(void)(user);
	
	input = *ch;
	in_p = (char *) &input;
	out_p = buf;
	inbytesleft = sizeof(input);
	outbytesleft = sizeof(buf);
	if(iconv(g_cd, &in_p, &inbytesleft, &out_p, &outbytesleft) == ((size_t) -1)) {
		LOG("warning, failed to convert 0x%08x to utf-8: %s\n", *ch, strerror(errno));
		return;
	}

	if(fwrite(buf, sizeof(char), sizeof(buf) - outbytesleft, g_fp) != 1)
		LOG("warning, failed to write character\n");
}

int output_file(const char *prefix_dir, int *fd) {
	time_t t;
	struct tm *now; 
	char tfmt[2048]; 
	char buf[2048];

	t = time(NULL); 
	if( (now = localtime(&t) ) == NULL) {
		LOG("localtime failed\n"); 
		return -1; 
	} 
	
	if( snprintf(tfmt, sizeof(tfmt), "%s/%s.txt", prefix_dir, "%y-%m-%d-%H-%M-%S") >= (int) sizeof(tfmt) ) {
		LOG("prefix dir name is too long\n");
		return -1;
	}
		
	if(strftime(buf, sizeof(buf), tfmt, now) < 1) {
		LOG("prefix dir name is too long\n"); 
		return -1;
	}  
	
	LOG("open output file: %s\n", buf);
	*fd = open(buf, O_CREAT|O_EXCL|O_WRONLY, g_file_mode);
	
	return 0;
}

int aug_plugin_init(struct aug_plugin *plugin, const struct aug_api *api) {
	const char *prefix;
	wordexp_t exp;
	int status, fd;

	g_plugin = plugin;	
	g_api = api;

	LOG("init\n");

	aug_callbacks_init(&g_callbacks);
	g_callbacks.input_char = input_char;

	(*g_api->callbacks)(g_plugin, &g_callbacks, NULL);

	if( (*g_api->conf_val)(g_plugin, aug_plugin_name, "prefix", &prefix) != 0) {
		prefix = "~/.aug-record";
	}

	if( (status = wordexp(prefix, &exp, WRDE_NOCMD)) != 0 ) {
		switch(status) {
		case WRDE_BADCHAR:
			LOG("bad character in prefix\n");
			break;
		case WRDE_CMDSUB:
			LOG("command substitution in prefix\n");
			break;
		case WRDE_SYNTAX:
			LOG("syntax error in prefix\n");
			break;
		default:
			LOG("unknown error during configuration file path expansion\n");
		}
		return -1;
	}

	if(exp.we_wordc != 1) {
		if(exp.we_wordc == 0)
			LOG("config file path did not expand to any words\n");
		else
			LOG("config file path expanded to multiple words\n");

		wordfree(&exp);
		return -1;
	}

	LOG("prefix dir: %s\n", exp.we_wordv[0]);
	status = mkdir(exp.we_wordv[0], g_dir_mode);
	if(status != 0 && errno != EEXIST) {
		LOG("failed to create or find prefix dir: %s\n", strerror(errno) );
		wordfree(&exp);
		return -1;
	}

	if(output_file(exp.we_wordv[0], &fd) != 0) {
		LOG("failed to open output file\n");
		wordfree(&exp);
		return -1;
	}
	wordfree(&exp);

	if( (g_cd = iconv_open("UTF8", "WCHAR_T")) == ((iconv_t) -1) )
		return -1;

	if( (g_fp = fdopen(fd, "w") ) == NULL) {
		LOG("failed to open file descriptor as FILE pointer: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
		
	return 0;
}

void aug_plugin_free() {
	if(fclose(g_fp) != 0) {
		LOG("warning, failed to close file pointer: %s\n", strerror(errno));
	}

	if(iconv_close(g_cd) != 0)
		LOG("warning, failed to close iconv descriptor\n");
}

