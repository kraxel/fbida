struct op_map_parm_ch {
    float gamma;
    int   bottom;
    int   top;
    int   left;
    int   right;
};
struct op_map_parm {
    struct op_map_parm_ch red;
    struct op_map_parm_ch green;
    struct op_map_parm_ch blue;
};

extern struct op_map_parm_ch op_map_nothing;
extern struct ida_op desc_map;
