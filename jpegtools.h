
/* various flags */
#define JFLAG_TRANSFORM_IMAGE      0x0001
#define JFLAG_TRANSFORM_THUMBNAIL  0x0002
#define JFLAG_TRANSFORM_TRIM       0x0004

#define JFLAG_UPDATE_COMMENT       0x0010
#define JFLAG_UPDATE_ORIENTATION   0x0020
#define JFLAG_UPDATE_THUMBNAIL     0x0040

#define JFLAG_FILE_BACKUP          0x0100
#define JFLAG_FILE_KEEP_TIME       0x0200

/* functions */
int jpeg_transform_fp(FILE *in, FILE *out,
		      JXFORM_CODE transform,
		      unsigned char *comment,
		      char *thumbnail, int tsize,
		      unsigned int flags);
int jpeg_transform_files(char *infile, char *outfile,
			 JXFORM_CODE transform,
			 unsigned char *comment,
			 char *thumbnail, int tsize,
			 unsigned int flags);
int jpeg_transform_inplace(char *file,
			   JXFORM_CODE transform,
			   unsigned char *comment,
			   char *thumbnail, int tsize,
			   unsigned int flags);
