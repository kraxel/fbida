
struct op_3x3_parm {
    int f1[3];
    int f2[3];
    int f3[3];
    int mul,div,add;
};

struct op_sharpe_parm {
    int factor;
};

struct op_resize_parm {
    int width;
    int height;
    int dpi;
};

struct op_rotate_parm {
    int angle;
};

extern struct ida_op desc_grayscale;
extern struct ida_op desc_3x3;
extern struct ida_op desc_sharpe;
extern struct ida_op desc_resize;
extern struct ida_op desc_rotate;
