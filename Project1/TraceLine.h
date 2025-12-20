#pragma once
// TraceLine.h

#include <ctime>

struct trace_line {
    int starting_block;      // 起始块号
    int size_of_blocks;      // 块数量
    int ignore;
    int request_number;
    int access_count;        // 访问次数
    time_t current_time;     // 当前访问时间
};
