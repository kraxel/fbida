extern Atom XA_TARGETS, XA_DONE;
extern Atom _MOTIF_EXPORT_TARGETS;
extern Atom _MOTIF_CLIPBOARD_TARGETS;
extern Atom _MOTIF_DEFERRED_CLIPBOARD_TARGETS;
extern Atom _MOTIF_SNAPSHOT;
extern Atom _MOTIF_LOSE_SELECTION;
extern Atom XA_FILE_NAME, XA_FILE;
extern Atom _NETSCAPE_URL;
extern Atom MIME_TEXT_URI_LIST;

Atom sel_unique_atom(Widget widget);

void selection_dest(Widget  w, XtPointer ignore, XtPointer call_data);
void selection_convert(Widget widget, XtPointer ignore, XtPointer call_data);
void ipc_ac(Widget widget, XEvent *event, String *argv, Cardinal *argc);
void dnd_add(Widget widget);
void ipc_init(void);

