struct ARGS {
    Boolean        debug;
    Boolean        help;
    Boolean        testload;
};

extern struct ARGS        args;
extern unsigned int       pcd_res;

extern Widget             app_shell;
extern XtAppContext       app_context;
extern Display            *dpy;
extern struct ida_viewer  *ida;

void action_cb(Widget widget, XtPointer clientdata, XtPointer call_data);
void destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data);

void ptr_register(Widget widget);
void ptr_unregister(Widget widget);
void ptr_busy(void);
void ptr_idle(void);

void do_save_print(void);
void resize_shell(void);
char* load_tmpfile(char *base);
void new_file(char *name, int complain);
