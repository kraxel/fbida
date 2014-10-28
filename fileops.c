#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#include "list.h"
#include "ida.h"
#include "readers.h"
#include "filebutton.h"
#include "fileops.h"

#include <jpeglib.h>
#include "transupp.h"		/* Support routines for jpegtran */
#include "jpegtools.h"

/*----------------------------------------------------------------------*/

struct jobqueue {
    struct list_head       list;
    char                   *op;
    char                   *filename;
    char                   *args;
};
static LIST_HEAD(jobs);
static XtWorkProcId jobproc;

/*----------------------------------------------------------------------*/

static Boolean job_worker(XtPointer clientdata)
{
    struct jobqueue *job;
    unsigned int flags =
	JFLAG_TRANSFORM_IMAGE     |
	JFLAG_TRANSFORM_THUMBNAIL |
	JFLAG_TRANSFORM_TRIM      |
	JFLAG_UPDATE_ORIENTATION;
    
    if (list_empty(&jobs)) {
	/* nothing to do */
	jobproc = 0;
	return TRUE;
    }
    job = list_entry(jobs.next, struct jobqueue, list);

    /* process job */
    if (debug)
	fprintf(stderr,"job worker: %s %s\n",job->op,job->filename);
    ptr_busy();
    if (0 == strcmp(job->op,"rotexif")) {
	jpeg_transform_inplace(job->filename, -1/*auto*/,
			       NULL, NULL, 0, flags);

    } else if (0 == strcmp(job->op,"rotcw")) {
	jpeg_transform_inplace(job->filename, JXFORM_ROT_90,
			       NULL, NULL, 0, flags);

    } else if (0 == strcmp(job->op,"rotccw")) {
	jpeg_transform_inplace(job->filename, JXFORM_ROT_270,
			       NULL, NULL, 0, flags);

    } else if (0 == strcmp(job->op,"comment")) {
	jpeg_transform_inplace(job->filename, JXFORM_NONE, job->args,
			       NULL, 0, 
			       JFLAG_UPDATE_COMMENT);

    } else {
	fprintf(stderr,"job: \"%s\" is *unknown*\n",job->op);
    }
    ptr_idle();
    fileinfo_invalidate(job->filename);

    /* cleanup */
    list_del(&job->list);
    free(job->filename);
    free(job->op);
    if (job->args)
	free(job->args);
    free(job);
    return FALSE;
}

/*----------------------------------------------------------------------*/

void job_submit(char *op, char *filename, char *args)
{
    struct jobqueue *job;

    job = malloc(sizeof(*job));
    memset(job,0,sizeof(*job));
    job->op       = strdup(op);
    job->filename = strdup(filename);
    if (args)
	job->args = strdup(args);
    list_add_tail(&job->list,&jobs);
    if (debug)
	fprintf(stderr,"job submit: %s %s\n",job->op,job->filename);
    if (0 == jobproc)
	jobproc = XtAppAddWorkProc(app_context,job_worker,NULL);
}
