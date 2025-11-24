#pragma once
// TraceLine.h



#include <ctime>

struct trace_line {
    int starting_block;//oid 这个变量表示一个数据块（或者称为对象）的起始块。在存储系统或文件系统的上下文中，一个对象可能由多个相邻的块组成。starting_block 指定了这个对象的第一个块的编号或地址。
    int size_of_blocks;//size它定义了对象的大小
    int ignore;
    int request_number;
    int access_count;  // 新增：记录访问次数
    time_t current_time;  // 添加目前访问时间
};

