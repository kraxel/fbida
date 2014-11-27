/*
 * some code to handle desktop files
 * http://www.freedesktop.org/Standards/desktop-entry-spec
 *
 * This is really very simple and basic: next to no locale handling,
 * no caching, no other clever tricks ...
 * ida + fbi only use the Comment= entry of .directory files.
 *
 * (c) 2004 Gerd Hoffmann <gerd@kraxel.org>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>

#include "list.h"
#include "desktop.h"

extern int debug;

/* ------------------------------------------------------------------------- */
/* desktop files are in utf-8                                                */

static int iconv_string(char *to, char *from,
			char *src, char *dst, size_t max)
{
    size_t ilen = strlen(src);
    size_t olen = max-1;
    iconv_t ic;

    ic = iconv_open(to,from);
    if (NULL == ic)
	return 0;

    while (ilen > 0) {
	if (-1 == iconv(ic,&src,&ilen,&dst,&olen)) {
	    /* skip + quote broken byte unless we are out of space */
	    if (E2BIG == errno)
		break;
	    if (olen < 4)
		break;
	    sprintf(dst,"\\x%02x",(int)(unsigned char)src[0]);
	    src  += 1;
	    dst  += 4;
	    ilen -= 1;
	    olen -= 4;
	}
    }
    dst[0] = 0;
    iconv_close(ic);
    return max-1 - olen;
}

int utf8_to_locale(char *src, char *dst, size_t max)
{
    char *codeset = nl_langinfo(CODESET);
    return iconv_string(codeset, "UTF-8", src, dst, max);
}

int locale_to_utf8(char *src, char *dst, size_t max)
{
    char *codeset = nl_langinfo(CODESET);
    return iconv_string("UTF-8", codeset, src, dst, max);
}

/* ------------------------------------------------------------------------- */
/* read/write desktop files                                                  */

struct desktop_line {
    struct list_head next;
    char line[1024];
};

static int read_file(char *filename, struct list_head *file)
{
    struct desktop_line *l;
    int len,count = 0;
    FILE *fp;

    INIT_LIST_HEAD(file);
    fp = fopen(filename,"r");
    if (NULL == fp) {
	if (debug)
	    fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return 0;
    }
    for (;;) {
	l = malloc(sizeof(*l));
	memset(l,0,sizeof(*l));
	if (NULL == fgets(l->line,sizeof(l->line),fp)) {
	    free(l);
	    break;
	}
	len = strlen(l->line);
	if (l->line[len-1] == '\n')
	    l->line[len-1] = 0;
	list_add_tail(&l->next,file);
	count++;
    }
    fclose(fp);
    return count;
}

static int write_file(char *filename, struct list_head *file)
{
    struct desktop_line *l;
    struct list_head *item;
    FILE *fp;

    fp = fopen(filename,"w");
    if (NULL == fp) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	return 0;
    }
    list_for_each(item,file) {
	l = list_entry(item, struct desktop_line, next);
	fprintf(fp,"%s\n",l->line);
    }
    fclose(fp);
    return 0;
}

static int dump_file(struct list_head *file)
{
    struct desktop_line *l;
    struct list_head *item;

    fprintf(stderr,"\n");
    fprintf(stderr,"+--------------------\n");
    list_for_each(item,file) {
	l = list_entry(item, struct desktop_line, next);
	fprintf(stderr,"| %s\n",l->line);
    }
    return 0;
}

static int free_file(struct list_head *file)
{
    struct desktop_line *l;

    while (!list_empty(file)) {
	l = list_entry(file->next, struct desktop_line, next);
	list_del(&l->next);
	free(l);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

static char* get_entry(struct list_head *file, char *entry)
{
    struct desktop_line *l;
    struct list_head *item;
    int in_desktop_entry = 0;
    int len = strlen(entry);

    list_for_each(item,file) {
	l = list_entry(item, struct desktop_line, next);
	if (0 == strcmp(l->line,"[Desktop Entry]")) {
	    in_desktop_entry = 1;
	    continue;
	}
	if (0 == strncmp(l->line,"[",1)) {
	    in_desktop_entry = 0;
	    continue;
	}
	if (!in_desktop_entry)
	    continue;
	if (0 == strncmp(l->line,entry,len))
	    return l->line+len;
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */

static int add_line(struct list_head *file, char *line)
{
    struct desktop_line *add;

    add = malloc(sizeof(*add));
    memset(add,0,sizeof(*add));
    snprintf(add->line,sizeof(add->line),"%s",line);
    list_add_tail(&add->next,file);
    return 0;
}

static int add_entry(struct list_head *file, char *entry, char *value)
{
    struct desktop_line *l,*add;
    struct list_head *item;

    list_for_each(item,file) {
	l = list_entry(item, struct desktop_line, next);
	if (0 != strcmp(l->line,"[Desktop Entry]"))
	    continue;
	add = malloc(sizeof(*add));
	memset(add,0,sizeof(*add));
	snprintf(add->line,sizeof(add->line),"%s%s",entry,value);
	list_add(&add->next,item);
	return 0;
    }
    return -1;
}

static int set_entry(struct list_head *file, char *type, char *entry, char *value)
{
    struct desktop_line *l;
    struct list_head *item;
    int in_desktop_entry = 0;
    int len = strlen(entry);

    list_for_each(item,file) {
	l = list_entry(item, struct desktop_line, next);
	if (0 == strcmp(l->line,"[Desktop Entry]")) {
	    in_desktop_entry = 1;
	    continue;
	}
	if (0 == strncmp(l->line,"[",1)) {
	    in_desktop_entry = 0;
	    continue;
	}
	if (!in_desktop_entry)
	    continue;
	if (0 == strncmp(l->line,entry,len)) {
	    snprintf(l->line,sizeof(l->line),"%s%s",entry,value);
	    return 0;
	}
    }
    if (0 != add_entry(file,entry,value)) {
	add_line(file,"[Desktop Entry]");
	add_entry(file,"Type=",type);
	add_entry(file,entry,value);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* public interface                                                          */

int desktop_read_entry(char *filename, char *entry, char *dest, size_t max)
{
    struct list_head file;
    char *value;
    int rc = 0;

    read_file(filename,&file);
    if (debug)
	dump_file(&file);
    value = get_entry(&file,entry);
    if (NULL != value) {
	rc = utf8_to_locale(value,dest,max);
	if (rc && debug)
	    fprintf(stderr,"# %s\n",dest);
    };
    free_file(&file);
    return rc;
}

int desktop_write_entry(char *filename, char *type, char *entry, char *value)
{
    struct list_head file;
    char utf8[1024];

    read_file(filename,&file);
    locale_to_utf8(value,utf8,sizeof(utf8));
    set_entry(&file,"Directory",entry,utf8);
    write_file(filename,&file);
    free_file(&file);
    return 0;
}
