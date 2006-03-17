/*
 * config file parser
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "list.h"
#include "misc.h"
#include "parseconfig.h"

struct cfg_entry {
    struct list_head  next;
    char              *name;
    unsigned int      flags;
    char              *value;
};

struct cfg_section {
    struct list_head  next;
    char              *name;
    unsigned int      flags;
    struct list_head  entries;
};

struct cfg_domain {
    struct list_head  next;
    char              *name;
    struct list_head  sections;
};

LIST_HEAD(domains);

/* ------------------------------------------------------------------------ */
/* internal stuff                                                           */

static struct cfg_domain  *d_last;
static struct cfg_section *s_last;
static struct cfg_entry   *e_last;

static struct cfg_domain*
cfg_find_domain(char *dname)
{
    struct list_head *item;
    struct cfg_domain *domain;

    if (d_last && 0 == strcmp(d_last->name,dname))
	return d_last;
    d_last = NULL;
    s_last = NULL;
    e_last = NULL;

    list_for_each(item,&domains) {
	domain = list_entry(item, struct cfg_domain, next);
	if (0 == strcasecmp(domain->name,dname)) {
	    d_last = domain;
	    return domain;
	}
    }
    return NULL;
}

static struct cfg_domain*
cfg_get_domain(char *dname)
{
    struct cfg_domain *domain;

    domain = cfg_find_domain(dname);
    if (NULL == domain) {
	domain = malloc(sizeof(*domain));
	memset(domain,0,sizeof(*domain));
	domain->name = strdup(dname);
	INIT_LIST_HEAD(&domain->sections);
	list_add_tail(&domain->next,&domains);
    }
    d_last = domain;
    return domain;
}

static struct cfg_section*
cfg_find_section(struct cfg_domain *domain, char *sname)
{
    struct list_head *item;
    struct cfg_section *section;

    if (s_last && 0 == strcmp(s_last->name,sname))
	return s_last;
    s_last = NULL;
    e_last = NULL;

    list_for_each(item,&domain->sections) {
	section = list_entry(item, struct cfg_section, next);
	if (0 == strcasecmp(section->name,sname)) {
	    s_last = section;
	    return section;
	}
    }
    return NULL;
}

static struct cfg_section*
cfg_get_section(struct cfg_domain *domain, char *sname)
{
    struct cfg_section *section;

    section = cfg_find_section(domain,sname);
    if (NULL == section) {
	section = malloc(sizeof(*section));
	memset(section,0,sizeof(*section));
	section->name = strdup(sname);
	INIT_LIST_HEAD(&section->entries);
	list_add_tail(&section->next,&domain->sections);
    }
    s_last = section;
    return section;
}

static struct cfg_entry*
cfg_find_entry(struct cfg_section *section, char *ename)
{
    struct list_head *item;
    struct cfg_entry *entry;

    if (e_last && 0 == strcmp(e_last->name,ename))
	return e_last;
    e_last = NULL;

    list_for_each(item,&section->entries) {
	entry = list_entry(item, struct cfg_entry, next);
	if (0 == strcasecmp(entry->name,ename)) {
	    e_last = entry;
	    return entry;
	}
    }
    return NULL;
}

static struct cfg_entry*
cfg_get_entry(struct cfg_section *section, char *ename)
{
    struct cfg_entry *entry;

    entry = cfg_find_entry(section,ename);
    if (NULL == entry) {
	entry = malloc(sizeof(*entry));
	memset(entry,0,sizeof(*entry));
	entry->name = strdup(ename);
	list_add_tail(&entry->next,&section->entries);
    }
    e_last = entry;
    return entry;
}

static void
cfg_set_entry(struct cfg_section *section, char *name, const char *value)
{
    struct cfg_entry *entry;
    
    entry = cfg_get_entry(section,name);
    if (entry->value)
	free(entry->value);
    entry->value = strdup(value);
}

static struct cfg_section*
cfg_get_sec(char *dname, char *sname)
{
    struct cfg_domain  *domain;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;
    return cfg_find_section(domain,sname);
}

static struct cfg_entry*
cfg_get_ent(char *dname, char *sname, char *ename)
{
    struct cfg_section *section;

    section = cfg_get_sec(dname,sname);
    if (NULL == section)
	return NULL;
    return cfg_find_entry(section,ename);
}

/* ------------------------------------------------------------------------ */
/* import / add / del config data                                           */

int
cfg_parse_file(char *dname, char *filename)
{
    struct cfg_domain  *domain  = NULL;
    struct cfg_section *section = NULL;
    char line[256],tag[64],value[192];
    FILE *fp;
    int nr;
    
    if (NULL == (fp = fopen(filename,"r")))
	return -1;

    nr = 0;
    domain = cfg_get_domain(dname);
    while (NULL != fgets(line,255,fp)) {
	nr++;
	if (1 == sscanf(line,"# include \"%[^\"]\"",value)) {
	    /* includes */
	    char *h,*inc;
	    inc = malloc(strlen(filename)+strlen(value));
	    strcpy(inc,filename);
	    h = strrchr(inc,'/');
	    if (h)
		h++;
	    else
		h = inc;
	    strcpy(h,value);
	    cfg_parse_file(dname,inc);
	    free(inc);
	    continue;
	}
	if (line[0] == '\n' || line[0] == '#' || line[0] == '%')
	    continue;
	if (1 == sscanf(line,"[%99[^]]]",value)) {
	    /* [section] */
	    section = cfg_get_section(domain,value);
	} else if (2 == sscanf(line," %63[^= ] = %191[^\n]",tag,value)) {
	    /* foo = bar */
	    if (NULL == section) {
		fprintf(stderr,"%s:%d: error: no section\n",filename,nr);
	    } else {
		char *c = value + strlen(value)-1;
		while (c > value  &&  (*c == ' ' || *c == '\t'))
		    *(c--) = 0;
		cfg_set_entry(section,tag,value);
	    }
	} else {
	    /* Huh ? */
	    fprintf(stderr,"%s:%d: syntax error\n",filename,nr);
	}
    }
    fclose(fp);
    return 0;
}

void
cfg_set_str(char *dname, char *sname, char *ename, const char *value)
{
    struct cfg_domain  *domain  = NULL;
    struct cfg_section *section = NULL;

    if (NULL == value) {
	cfg_del_entry(dname, sname, ename);
    } else {
	domain  = cfg_get_domain(dname);
	section = cfg_get_section(domain,sname);
	cfg_set_entry(section,ename,value);
    }
}

void
cfg_set_int(char *dname, char *sname, char *ename, int value)
{
    char str[32];

    snprintf(str,sizeof(str),"%d",value);
    cfg_set_str(dname,sname,ename,str);
}

void
cfg_set_bool(char *dname, char *sname, char *ename, int value)
{
    cfg_set_str(dname,sname,ename, value ? "true" : "false");
}

void
cfg_del_section(char *dname, char *sname)
{
    struct cfg_section  *section;
    struct cfg_entry    *entry;

    section= cfg_get_sec(dname,sname);
    if (!section)
	return;
    list_del(&section->next);
    while (!list_empty(&section->entries)) {
	entry = list_entry(section->entries.next, struct cfg_entry, next);
	list_del(&entry->next);
	free(entry->name);
	free(entry->value);
	free(entry);
    }
    s_last = NULL;
    e_last = NULL;
    free(section->name);
    free(section);
}

void
cfg_del_entry(char *dname, char *sname, char *ename)
{
    struct cfg_entry *entry;

    entry = cfg_get_ent(dname,sname,ename);
    if (!entry)
	return;
    e_last = NULL;
    list_del(&entry->next);
    free(entry->name);
    free(entry->value);
    free(entry);
}

void
cfg_parse_cmdline(int *argc, char **argv, struct cfg_cmdline *opt)
{
    int i,j,o,shift,len;
    char sopt,*lopt;

    for (i = 1; i < *argc;) {
	if (argv[i][0] != '-') {
	    i++;
	    continue;
	}
	if (argv[i][1] == 0) {
	    i++;
	    continue;
	}

	sopt = 0;
	lopt = NULL;
	if (argv[i][1] != '-' &&
	    argv[i][2] == 0) {
	    /* short option: -f */
	    sopt = argv[i][1];
	}
	if (argv[i][1] != '-') {
	    /* long option: -foo */
	    lopt = argv[i]+1;
	} else {
	    /* also accept gnu-style: --foo */
	    lopt = argv[i]+2;
	}
	
	for (shift = 0, o = 0;
	     0 == shift && opt[o].cmdline != NULL;
	     o++) {
	    len = strlen(opt[o].cmdline);

	    if (opt[o].yesno && sopt && sopt == opt[o].letter) {
		/* yesno: -f */
		cfg_set_bool(opt[o].option.domain,
			     opt[o].option.section,
			     opt[o].option.entry,
			     1);
		shift = 1;

	    } else if (opt[o].needsarg && sopt && sopt == opt[o].letter &&
		       i+1 < *argc) {
		/* arg: -f bar */
		cfg_set_str(opt[o].option.domain,
			    opt[o].option.section,
			    opt[o].option.entry,
			    argv[i+1]);
		shift = 2;
		
	    } else if (opt[o].value && sopt && sopt == opt[o].letter) {
		/* -f sets fixed value */
		cfg_set_str(opt[o].option.domain,
			    opt[o].option.section,
			    opt[o].option.entry,
			    opt[o].value);
		shift = 1;

	    } else if (opt[o].yesno && lopt && 
		       0 == strcmp(lopt,opt[o].cmdline)) {
		/* yesno: -foo */
		cfg_set_bool(opt[o].option.domain,
			     opt[o].option.section,
			     opt[o].option.entry,
			     1);
		shift = 1;

	    } else if (opt[o].yesno && lopt && 
		       0 == strncmp(lopt,"no",2) &&
		       0 == strcmp(lopt+2,opt[o].cmdline)) {
		/* yesno: -nofoo */
		cfg_set_bool(opt[o].option.domain,
			     opt[o].option.section,
			     opt[o].option.entry,
			     0);
		shift = 1;

	    } else if (opt[o].needsarg && lopt && 
		       0 == strcmp(lopt,opt[o].cmdline) &&
		       i+1 < *argc) {
		/* arg: -foo bar */
		cfg_set_str(opt[o].option.domain,
			    opt[o].option.section,
			    opt[o].option.entry,
			    argv[i+1]);
		shift = 2;

	    } else if (opt[o].needsarg && lopt && 
		       0 == strncmp(lopt,opt[o].cmdline,len) &&
		       0 == strncmp(lopt+len,"=",1)) {
		/* arg: -foo=bar */
		cfg_set_str(opt[o].option.domain,
			    opt[o].option.section,
			    opt[o].option.entry,
			    argv[i]+2+len);
		shift = 1;

	    } else if (opt[o].value && lopt &&
		       0 == strcmp(lopt,opt[o].cmdline)) {
		/* -foo sets some fixed value */
		cfg_set_str(opt[o].option.domain,
			    opt[o].option.section,
			    opt[o].option.entry,
			    opt[o].value);
		shift = 1;
	    }
	}

	if (shift) {
	    /* remove processed args */
	    for (j = i; j < *argc+1-shift; j++)
		argv[j] = argv[j+shift];
	    (*argc) -= shift;
	} else
	    i++;
    }
}

void
cfg_help_cmdline(FILE *fp, struct cfg_cmdline *opt, int w1, int w2, int w3)
{
    char *val;
    int o,len;
    
    for (o = 0; opt[o].cmdline != NULL; o++) {
	fprintf(fp,"%*s",w1,"");
	if (opt[o].letter) {
	    fprintf(fp,"-%c  ",opt[o].letter);
	} else {
	    fprintf(fp,"    ");
	}

	if (opt[o].yesno) {
	    len = fprintf(fp,"-(no)%s ",opt[o].cmdline);
	} else if (opt[o].needsarg) {
	    len = fprintf(fp,"-%s <arg> ",opt[o].cmdline);
	} else {
	    len = fprintf(fp,"-%s ",opt[o].cmdline);
	}
	if (len < w2)
	    fprintf(fp,"%*s",w2-len,"");

	len = fprintf(fp,"%s ",opt[o].desc);

	if (w3) {
	    if (len < w3)
		fprintf(fp,"%*s",w3-len,"");
	    val = cfg_get_str(opt[o].option.domain,
			      opt[o].option.section,
			      opt[o].option.entry);
	    if (val)
		fprintf(fp,"[%s]",val);
	}
 	fprintf(fp,"\n");
    }
}

/* ------------------------------------------------------------------------ */
/* export config data                                                       */

static int cfg_mkdir(char *filename)
{
    char *h;
    int rc;

    h = strrchr(filename,'/');
    if (!h  ||  h == filename)
	return -1;
    *h = '\0';
    rc = mkdir(filename,0777);
    if (-1 == rc && ENOENT == errno) {
	cfg_mkdir(filename);
	rc = mkdir(filename,0777);
    }
    if (-1 == rc)
	fprintf(stderr,"mkdir(%s): %s\n",filename,strerror(errno));
    *h = '/';
    return rc;
}

int
cfg_write_file(char *dname, char *filename)
{
    struct list_head   *item1,*item2;
    struct cfg_domain  *domain;
    struct cfg_section *section;
    struct cfg_entry   *entry;
    char *bfile, *tfile;
    int len;
    FILE *fp;

    len = strlen(filename)+10;
    bfile = malloc(len);
    tfile = malloc(len);
    sprintf(bfile,"%s~",filename);
    sprintf(tfile,"%s.$$$",filename);

    fp = fopen(tfile,"w");
    if (NULL == fp  &&  ENOENT == errno) {
	cfg_mkdir(tfile);
	fp = fopen(tfile,"w");
    }
    if (NULL == fp) {
	fprintf(stderr,"open(%s): %s\n",tfile,strerror(errno));
	return -1;
    }

    domain = cfg_find_domain(dname);
    if (NULL != domain) {
	list_for_each(item1,&domain->sections) {
	    section = list_entry(item1, struct cfg_section, next);
	    fprintf(fp,"[%s]\n",section->name);
	    list_for_each(item2,&section->entries) {
		entry = list_entry(item2, struct cfg_entry, next);
		fprintf(fp,"%s = %s\n",entry->name,entry->value);
	    }
	    fprintf(fp,"\n");
	}
    }
    fclose(fp);

    if (-1 == unlink(bfile) && ENOENT != errno) {
	fprintf(stderr,"unlink(%s): %s\n",bfile,strerror(errno));
	return -1;
    }
    if (-1 == rename(filename,bfile) && ENOENT != errno) {
	fprintf(stderr,"rename(%s,%s): %s\n",filename,bfile,strerror(errno));
	return -1;
    }
    if (-1 == rename(tfile,filename)) {
	fprintf(stderr,"rename(%s,%s): %s\n",tfile,filename,strerror(errno));
	return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* list / search config data                                                */

char*
cfg_sections_first(char *dname)
{
    struct list_head   *item;
    struct cfg_domain  *domain;
    struct cfg_section *section;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;
    
    item = &domain->sections;
    if (item->next == &domain->sections)
	return NULL;
    section = list_entry(item->next, struct cfg_section, next);
    s_last  = section;
    e_last  = NULL;
    return section->name;
}

char*
cfg_sections_next(char *dname, char *current)
{
    struct list_head   *item;
    struct cfg_domain  *domain;
    struct cfg_section *section;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;
    section = cfg_find_section(domain,current);
    if (NULL == section)
	return NULL;
    item = &section->next;

    if (item->next == &domain->sections)
	return NULL;
    section = list_entry(item->next, struct cfg_section, next);
    s_last  = section;
    e_last  = NULL;
    return section->name;
}

char*
cfg_sections_prev(char *dname, char *current)
{
    struct list_head   *item;
    struct cfg_domain  *domain;
    struct cfg_section *section;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;
    section = cfg_find_section(domain,current);
    if (NULL == section)
	return NULL;
    item = &section->next;
    
    if (item->prev == &domain->sections)
	return NULL;
    section = list_entry(item->prev, struct cfg_section, next);
    s_last  = section;
    e_last  = NULL;
    return section->name;
}

unsigned int cfg_sections_count(char *dname)
{
    struct list_head   *item;
    struct cfg_domain  *domain;
    int count = 0;

    domain = cfg_find_domain(dname);
    if (NULL != domain)
	list_for_each(item,&domain->sections)
	    count++;
    return count;
}

char* cfg_sections_index(char *dname, int i)
{
    struct list_head   *item;
    struct cfg_domain  *domain;
    struct cfg_section *section;
    int count = 0;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;

    list_for_each(item,&domain->sections) {
	if (i == count) {
	    section = list_entry(item, struct cfg_section, next);
	    s_last  = section;
	    e_last  = NULL;
	    return section->name;
	}
	count++;
    }
    return NULL;
}

char*
cfg_entries_first(char *dname, char *sname)
{
    struct list_head   *item;
    struct cfg_section *section;
    struct cfg_entry   *entry;

    section = cfg_get_sec(dname,sname);
    if (NULL == section)
	return NULL;
    
    item = &section->entries;
    if (item->next == &section->entries)
	return NULL;
    entry  = list_entry(item->next, struct cfg_entry, next);
    e_last = entry;
    return entry->name;
}

char*
cfg_entries_next(char *dname, char *sname, char *current)
{
    struct list_head   *item;
    struct cfg_section *section;
    struct cfg_entry   *entry;

    section = cfg_get_sec(dname,sname);
    if (NULL == section)
	return NULL;
    entry = cfg_find_entry(section,current);
    if (NULL == entry)
	return NULL;
    item = &entry->next;

    if (item->next == &section->entries)
	return NULL;
    entry  = list_entry(item->next, struct cfg_entry, next);
    e_last = entry;
    return entry->name;
}

char*
cfg_entries_prev(char *dname, char *sname, char *current)
{
    struct list_head   *item;
    struct cfg_section *section;
    struct cfg_entry   *entry;

    section = cfg_get_sec(dname,sname);
    if (NULL == section)
	return NULL;
    entry = cfg_find_entry(section,current);
    if (NULL == entry)
	return NULL;
    item = &entry->next;

    if (item->prev == &section->entries)
	return NULL;
    entry  = list_entry(item->prev, struct cfg_entry, next);
    e_last = entry;
    return entry->name;
}

unsigned int cfg_entries_count(char *dname, char *sname)
{
    struct list_head   *item;
    struct cfg_section *section;
    int count = 0;

    section = cfg_get_sec(dname,sname);
    if (NULL != section)
	list_for_each(item,&section->entries)
	    count++;
    return count;
}

char* cfg_entries_index(char *dname, char *sname, int i)
{
    struct list_head   *item;
    struct cfg_section *section;
    struct cfg_entry   *entry;
    int count = 0;

    section = cfg_get_sec(dname,sname);
    if (NULL == section)
	return NULL;
    
    list_for_each(item,&section->entries) {
	if (i == count) {
	    entry  = list_entry(item, struct cfg_entry, next);
	    e_last = entry;
	    return entry->name;
	}
	count++;
    }
    return NULL;
}

char* cfg_search(char *dname, char *sname, char *ename, char *value)
{
    struct list_head   *item1,*item2;
    struct cfg_domain  *domain;
    struct cfg_section *section;
    struct cfg_entry   *entry;

    domain = cfg_find_domain(dname);
    if (NULL == domain)
	return NULL;
    list_for_each(item1,&domain->sections) {
	section = list_entry(item1, struct cfg_section, next);
	if (sname && 0 != strcasecmp(section->name,sname))
	    continue;
	if (!ename)
	    return section->name;
	list_for_each(item2,&section->entries) {
	    entry = list_entry(item2, struct cfg_entry, next);
	    if (0 != strcasecmp(entry->name,ename))
		continue;
	    if (0 == strcasecmp(entry->value,value))
		return section->name;
	}
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* get config data                                                          */

char*
cfg_get_str(char *dname, char *sname, char *ename)
{
    struct cfg_entry   *entry;

    entry = cfg_get_ent(dname, sname, ename);
    if (NULL == entry)
	return NULL;
    return entry->value;
}

unsigned int
cfg_get_int(char *dname, char *sname, char *ename, unsigned int def)
{
    char *val;

    val = cfg_get_str(dname,sname,ename);
    if (NULL == val)
	return def;
    return atoi(val);
}

signed int
cfg_get_signed_int(char *dname, char *sname, char *ename, signed int def)
{
    char *val;

    val = cfg_get_str(dname,sname,ename);
    if (NULL == val)
	return def;
    return atoi(val);
}

float
cfg_get_float(char *dname, char *sname, char *ename, float def)
{
    char *val;

    val = cfg_get_str(dname,sname,ename);
    if (NULL == val)
	return def;
    return atof(val);
}

int
cfg_get_bool(char *dname, char *sname, char *ename, int def)
{
    static char *yes[] = { "true",  "yes", "on",  "1" };
    char *val;
    int i;
    int retval = 0;

    val = cfg_get_str(dname,sname,ename);
    if (NULL == val)
	return def;
    for (i = 0; i < sizeof(yes)/sizeof(yes[0]); i++)
	if (0 == strcasecmp(val,yes[i]))
	    retval = 1;
    return retval;
}

/* ------------------------------------------------------------------------ */
/* get/set flags                                                            */

unsigned int cfg_get_sflags(char *dname, char *sname)
{
    struct cfg_section *section;

    section = cfg_get_sec(dname, sname);
    if (NULL == section)
	return 0;
    return section->flags;
}

unsigned int cfg_get_eflags(char *dname, char *sname, char *ename)
{
    struct cfg_entry   *entry;

    entry = cfg_get_ent(dname, sname, ename);
    if (NULL == entry)
	return 0;
    return entry->flags;
}

unsigned int cfg_set_sflags(char *dname, char *sname,
			    unsigned int mask, unsigned int bits)
{
    struct cfg_section *section;

    section = cfg_get_sec(dname, sname);
    if (NULL == section)
	return 0;
    section->flags &= ~mask;
    section->flags |= bits;
    return section->flags;
}

unsigned int cfg_set_eflags(char *dname, char *sname, char *ename,
			    unsigned int mask, unsigned int bits)
{
    struct cfg_entry   *entry;

    entry = cfg_get_ent(dname, sname, ename);
    if (NULL == entry)
	return 0;
    entry->flags &= ~mask;
    entry->flags |= bits;
    return entry->flags;
}
