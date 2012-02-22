#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <libexif/exif-data.h>

/* -------------------------------------------------------------------------- */

static const char *shellhelp = 
    "\n"
    "This is a cgi script.  It doesn't do any useful (beside printing this text)\n"
    "when started from the shell prompt, it is supposed to be started by your\n"
    "web server\n";

static const char *description = 
    "\n"
    "The script deliveres the EXIF thumbnail of JPEG images to the web browser.\n"
    "It will lookup the path passed via path info below your document root, i.e.\n"
    "a request like this ...\n"
    "\n"
    "        http://your.server/cgi-bin/thumbnail.cgi/path/file.jpg\n"
    "\n"
    "... will make the script send the thumbnail of the file ...\n"
    "\n"
    "        %s/path/file.jpg\n"
    "\n"
    "to the client\n"
    "\n"
    "Security note:  The script refuses paths containing \"..\" to avoid breaking\n"
    "out of the document root.  There are no other checks through, so it will\n"
    "deliver thumbnails for any JPEG image below below your document root which\n"
    "it is allowed to open by unix file permissions.\n"
    "\n"
    "(c) 2004 Gerd Hoffmann <gerd@kraxel.org> [SUSE Labs]\n"
    "\n";

/* -------------------------------------------------------------------------- */

static void panic(int code, char *message)
{
    printf("Status: %d %s\n"
	   "Content-Type: text/plain\n"
	   "\n"
	   "ERROR: %s\n",
	   code, message, message);
    fflush(stdout);
    exit(1);
}

static void dump_thumbnail(char *filename)
{
    char *cached;
    char mtime[64];
    struct stat st;
    struct tm *tm;
    ExifData *ed = NULL;

    if (-1 == stat(filename,&st))
	panic(404,"can't stat file");
    tm = gmtime(&st.st_mtime);
    strftime(mtime,sizeof(mtime),"%a, %d %b %Y %H:%M:%S GMT",tm);
    cached = getenv("HTTP_IF_MODIFIED_SINCE");
    if (NULL != cached && 0 == strcmp(cached,mtime)) {
	/* shortcut -- browser has a up-to-date copy */
	printf("Status: 304 Image not modified\n"
	       "\n");
	fflush(stdout);
	return;
    }
    
    ed = exif_data_new_from_file(filename);
    if (!ed)
	panic(500,"file has no exif data\n");
    if (!ed->data)
	panic(500,"no exif thumbnail present");
    if (ed->data[0] != 0xff || ed->data[1] != 0xd8)
	panic(500,"exif thumbnail has no jpeg magic");

    
    printf("Status: 200 Thumbnail follows\n"
	   "Content-Type: image/jpeg\n"
	   "Content-Length: %d\n"
	   "Last-modified: %s\n"
	   "\n",
	   ed->size,mtime);
    fwrite(ed->data,ed->size,1,stdout);
    fflush(stdout);
}

/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char filename[1024];
    char *document_root;
    char *path_info;
    
    if (NULL == getenv("GATEWAY_INTERFACE")) {
	fprintf(stderr,"%s", shellhelp);
	fprintf(stderr,description,"$DOCUMENT_ROOT");
	exit(1);
    }

    document_root = getenv("DOCUMENT_ROOT");
    if (NULL == document_root)
	panic(500,"DOCUMENT_ROOT unset");

    path_info = getenv("PATH_INFO");
    if (NULL == path_info || 0 == strlen(path_info)) {
	printf("Content-type: text/plain\n"
	       "\n");
	printf(description,document_root);
	fflush(stdout);
	return 0;
    }

    if (NULL != strstr(path_info,".."))
	panic(403,"\"..\" not allowed in path");
    snprintf(filename,sizeof(filename),"%s/%s",document_root,path_info);
    dump_thumbnail(filename);
    return 0;
}
