extern int xdnd_debug;
void XdndDropSink(Widget widget);
void XdndAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params);
void XdndDropFinished(Widget widget, XmSelectionCallbackStruct *scs);
