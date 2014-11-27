#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include "curl.h"

/* curl globals */
static CURLM *curlm;
static fd_set rd, wr, ex;

/* my globals */
static int url_debug   = 0;
static int url_timeout = 30;

/* my structs */
struct iobuf {
    off_t   start;
    size_t  size;
    char    *data;
};

struct url_state {
    char           *path;
    CURL           *curl;
    char           errmsg[CURL_ERROR_SIZE];
    off_t          curl_pos;
    off_t          buf_pos;
    struct iobuf   buf;
    int            eof;
};

/* ---------------------------------------------------------------------- */
/* curl stuff                                                             */

static void __attribute__ ((constructor)) curl_init(void)
{
    curl_global_init(CURL_GLOBAL_ALL);
    curlm = curl_multi_init();
}

static void __attribute__ ((destructor)) curl_fini(void)
{
    curl_multi_cleanup(curlm);
    curl_global_cleanup();
}

static void curl_free_buffer(struct iobuf *buf)
{
    if (buf->data) {
	free(buf->data);
	memset(buf,0,sizeof(*buf));
    }
}

/* CURLOPT_WRITEFUNCTION */
static int curl_write(void *data,  size_t  size, size_t nmemb, void *handle)
{
    struct url_state *h = handle;

    curl_free_buffer(&h->buf);
    h->buf.start = h->curl_pos;
    h->buf.size  = size * nmemb;
    h->buf.data  = malloc(h->buf.size);
    memcpy(h->buf.data, data, h->buf.size);
    if (url_debug)
	fprintf(stderr,"  put %5d @ %5d\n",
		(int)h->buf.size, (int)h->buf.start);
    
    h->curl_pos += h->buf.size;
    return h->buf.size;
}

/* do transfers */
static int curl_xfer(struct url_state *h)
{
    CURLMcode rc;
    struct timeval tv;
    int count, maxfd;

    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_ZERO(&ex);
    maxfd = -1;
    rc = curl_multi_fdset(curlm, &rd, &wr, &ex, &maxfd);
    if (CURLM_OK != rc) {
	fprintf(stderr,"curl_multi_fdset: %d %s\n",rc,h->errmsg);
	return -1;
    }
    if (-1 == maxfd) {
	/* wait 0.1 sec */
	if (url_debug)
	    fprintf(stderr,"wait 0.01 sec\n");
	tv.tv_sec  = 0;
	tv.tv_usec = 100000;
    } else {
	/* wait for data */
	if (url_debug)
	    fprintf(stderr,"select for data [maxfd=%d]\n",maxfd);
	tv.tv_sec  = url_timeout;
	tv.tv_usec = 0;
    }
    switch (select(maxfd+1, &rd, &wr, &ex, &tv)) {
    case -1:
	/* Huh? */
	perror("select");
	exit(1);
    case 0:
	/* timeout */
	return -1;
    }
    for (;;) {
	rc = curl_multi_perform(curlm,&count);
	if (CURLM_CALL_MULTI_PERFORM == rc)
	    continue;
	if (CURLM_OK != rc) {
	    fprintf(stderr,"curl_multi_perform: %d %s\n",rc,h->errmsg);
	    return -1;
	}
	if (0 == count)
	    h->eof = 1;
	break;
    }
    return 0;
}

/* curl setup */
static int curl_setup(struct url_state *h)
{
    if (h->curl) {
	curl_multi_remove_handle(curlm,h->curl);
	curl_easy_cleanup(h->curl);
    }

    h->curl = curl_easy_init();
    curl_easy_setopt(h->curl, CURLOPT_URL,           h->path);
    curl_easy_setopt(h->curl, CURLOPT_ERRORBUFFER,   h->errmsg);
    curl_easy_setopt(h->curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(h->curl, CURLOPT_WRITEDATA,     h);
    curl_multi_add_handle(curlm, h->curl);

    h->buf_pos  = 0;
    h->curl_pos = 0;
    h->eof      = 0;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* GNU glibc custom stream interface                                      */

static ssize_t url_read(void *handle, char *buf, size_t size)
{
    struct url_state *h = handle;
    size_t bytes, total;
    off_t off;
    int count;
    
    if (url_debug)
	fprintf(stderr,"url_read(size=%d)\n",(int)size);
    for (total = 0; size > 0;) {
	if (h->buf.start                <= h->buf_pos &&
	    h->buf.start + h->buf.size  >  h->buf_pos) {
	    /* can satisfy from current buffer */
	    bytes = h->buf.start + h->buf.size - h->buf_pos;
	    off   = h->buf_pos - h->buf.start;
	    if (bytes > size)
		bytes = size;
	    memcpy(buf+total, h->buf.data + off, bytes);
	    if (url_debug)
		fprintf(stderr,"  get %5d @ %5d [%5d]\n",
			(int)bytes, (int)h->buf_pos, (int)off);
	    size       -= bytes;
	    total      += bytes;
	    h->buf_pos += bytes;
	    continue;
	}
	if (h->buf_pos < h->buf.start) {
	    /* seeking backwards -- restart transfer */
	    if (url_debug)
		fprintf(stderr,"  rewind\n");
	    curl_free_buffer(&h->buf);
	    curl_setup(h);
	    while (CURLM_CALL_MULTI_PERFORM == curl_multi_perform(curlm,&count))
		/* nothing */;
	}
	if (h->eof)
	    /* stop on eof */
	    break;
	/* fetch more data */
	if (-1 == curl_xfer(h)) {
	    if (0 == total)
		return -1;
	    break;
	}
    }
    return total;
}

#if 0
static ssize_t url_write(void *handle, const char *buf, size_t size)
{
    //struct url_state *h = handle;

    if (url_debug)
	fprintf(stderr,"url_write(size=%d)\n",(int)size);
    return -1;
}
#endif

static int url_seek(void *handle, off64_t *pos, int whence)
{
    struct url_state *h = handle;
    int rc = 0;

    if (url_debug)
	fprintf(stderr,"url_seek(pos=%d,whence=%d)\n", (int)(*pos), whence);
    switch (whence) {
    case SEEK_SET:
	h->buf_pos = *pos;
	break;
    case SEEK_CUR:
	h->buf_pos += *pos;
	break;
    case SEEK_END: 
	rc = -1;
    }
    *pos = h->buf_pos;
    return rc;
}

static int url_close(void *handle)
{
    struct url_state *h = handle;

    if (url_debug)
	fprintf(stderr,"url_close()\n");
    curl_multi_remove_handle(curlm,h->curl);
    curl_easy_cleanup(h->curl);
    if (h->buf.data)
	free(h->buf.data);
    free(h->path);
    free(h);
    return 0;
}

static cookie_io_functions_t url_hooks = {
    .read  = url_read,
#if 0
    .write = url_write,
#endif
    .seek  = url_seek,
    .close = url_close,
};

static FILE *url_open(const char *path, const char *mode)
{
    FILE *fp;
    struct url_state *h;
    int count;

    if (url_debug)
	fprintf(stderr,"url_open(%s,%s)\n",path,mode);

    h = malloc(sizeof(*h));
    if (NULL == h)
	goto err;
    memset(h,0,sizeof(*h));

    h->path = strdup(path);
    if (NULL == h->path)
	goto err;
    
    /* setup */
    curl_setup(h);
    fp = fopencookie(h, mode, url_hooks);
    if (NULL == fp)
	goto err;

    /* connect + start fetching */
    while (CURLM_CALL_MULTI_PERFORM == curl_multi_perform(curlm,&count))
	/* nothing */;

    /* check for errors */
    if (0 == count  &&  NULL == h->buf.data) {
	errno = ENOENT;
	goto fetch_err;
    }
	
    /* all done */
    return fp;


 fetch_err:
    curl_multi_remove_handle(curlm,h->curl);
 err:
    if (h->curl)
	curl_easy_cleanup(h->curl);
    if (h->path)
	free(h->path);
    if (h)
	free(h);
    return NULL;
}

/* ---------------------------------------------------------------------- */
/* hook into fopen using GNU ld's --wrap                                  */

int curl_is_url(const char *url)
{
    static char *protocols[] = {
	"ftp://",
	"http://",
	NULL,
    };
    int i;

    for (i = 0; protocols[i] != NULL; i++)
	if (0 == strncasecmp(url, protocols[i], strlen(protocols[i])))
	    return 1;
    return 0;
}

FILE *__wrap_fopen(const char *path, const char *mode);
FILE *__real_fopen(const char *path, const char *mode);

FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (url_debug)
	fprintf(stderr,"fopen(%s,%s)\n",path,mode);

    /* catch URLs */
    if (curl_is_url(path)) {
	if (strchr(mode,'w')) {
	    fprintf(stderr,"write access over ftp/http is not supported, sorry\n");
	    return NULL;
	}
	return url_open(path,mode);
    }
    
    /* files passed to the real fopen */
    return __real_fopen(path,mode);
}
