#include <iostream>
#include <fstream>
#include <string>
#include "arc.h"
#include "lru.h"
//#include"TDC.h"  // TDC算法已注释
//#include"score.h"  // SCORE算法已注释
#include "TraceLine.h"
#include <vector>
#include <chrono>
#include"tiercache.h"
#include <thread>
#include"tdc2.h"



// 定义一个用于存储 trace_line 记录的容器
std::vector<trace_line> trace_records;
int main(int argc, char** argv) { // 第一个参数是

    std::cout << "=== Entering main ===" << std::endl;
    std::cout << "argc = " << argc << std::endl;
    for (int i = 0; i < argc; ++i) {
        std::cout << "argv[" << i << "] = " << argv[i] << std::endl;
    }
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <c> <trace_file>\n"
            << "       <c>           -- cache_size\n"
            << "       <trace_file>  -- path of trace_file" << std::endl;
        return 1;
    }

    int c = std::stoi(argv[1]);
    FILE* pFile = nullptr;
    if (fopen_s(&pFile, argv[2], "r") != 0) {
        std::cerr << "can't not find trace_file" << std::endl;
        return -1;
    }

    LRUCache lru_cache(c, argv[2]);
    ARCCache arc_cache(c, argv[2]);
    //SCORECache score_cache(c, argv[2]);  // SCORE算法已注释
    //TDCCache tdc_cache(c, argv[2]);  // TDC算法已注释
    trace_line l;
    int access_counter = 0;
    // 在主循环中添加一个计数器
    //int requestCounter = 0;
    // 定义一个用于存储 trace_line 记录的容器
    std::vector<trace_line> trace_records;
    // 添加行计数器，用于进度输出
    int line_count = 0;
    // 记录程序开始时间，用于计算耗时
    auto start_time = std::chrono::steady_clock::now();
    //最外层的 while 循环用于从文件中读取 trace 数据。在每次迭代中，它读取一行数据，将这一行的信息存储在 trace_line 结构体中，并进行相应的处理。
    //具体来说，每次迭代中，while 循环从文件中读取一个 trace 数据，该数据包括起始块、块数、忽略标志和请求号。这些信息被存储在 trace_line 结构体中的对应成员中。
    while (fscanf_s(pFile, "%d %d %d %d\n",
        &l.starting_block, &l.size_of_blocks, &l.ignore, &l.request_number) != EOF) {
        line_count++;  // 每处理一行，计数器+1
        // 更新每个 trace 数据的目前访问时间和最后访问时间戳
        // 计算对象大小
        int size = l.size_of_blocks * 4096;
        l.current_time = time(nullptr);  // 使用系统当前时间
        l.access_count = 0;  // 初始化访问次数为0
        // 获取相应的数据
        //int n = 1; // 初始化周期计数器（TDC算法已注释，不再需要）
        //int requestCounter = 0; // 请求计数器（TDC算法已注释，不再需要）
        //内部的 for 循环则对从文件中读取的每个 trace 数据进行缓存访问的模拟。在每次迭代中，它对当前 trace 数据中描述的块范围进行循环，调用 LRUCache 和 ARCCache 类的 get 方法来模拟从缓存中获取数据。
        //在这个循环内，针对每个块，它执行了一些断言检查，确保缓存访问的正确性。
        for (auto i = l.starting_block; i < (l.starting_block + l.size_of_blocks); ++i) {
            // 增加计数器
            
            auto res1 = lru_cache.get(i);
            assert(res1 != -1);
            auto res2 = arc_cache.get(i);
            assert(res2 != -1);

            // SCORE算法相关代码已注释
            // 获取系统当前时间
            //auto getCurrentTime = []() {
            //    return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            //        std::chrono::system_clock::now().time_since_epoch()
            //    ).count());
            //};
            //
            //// 创建一个新的 trace_line 对象
            //trace_line new_trace;
            //new_trace.starting_block = l.starting_block;
            //new_trace.size_of_blocks = l.size_of_blocks;
            //new_trace.ignore = l.ignore;
            //new_trace.request_number = l.request_number;
            //new_trace.access_count = l.access_count;
            //new_trace.current_time = getCurrentTime();
            //// 将新的 trace_line 对象添加到 trace_records 容器中
            ////trace_records.push_back(new_trace);
            //SCOREParams scoreparam{ i, trace_records };
            //auto res3 = score_cache.get(scoreparam);
            //assert(res3 != -1);
            // TDC算法相关代码已注释
            // 判断是否达到一个周期
            //if (requestCounter % 160000 == 0) {
            //    ++n;
            //}
            //TDCParams tdcParams{ i, n, static_cast<double>(size) };//i对象 n是周期 size缓存大小
            //auto res4 = tdc_cache.get(tdcParams);
            //assert(res4 != -1);
        }
        
        // 每100行输出一次进度信息和耗时信息，便于对比时间提升情况
        if (line_count % 100 == 0) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            auto elapsed_minutes = elapsed / 60;
            auto elapsed_seconds = elapsed % 60;
            std::cout << "Processed " << line_count << " lines... "
                      << "Elapsed time: " << elapsed_minutes << "m " << elapsed_seconds << "s\n";
        }
        
        // 注释掉详细的每行输出，以提升性能
        //std::cout << "Starting Block: " << l.starting_block << "\n"
        //    << "Number of Blocks: " << l.size_of_blocks << "\n"
        //    << "Ignore: " << l.ignore << "\n"
        //    << "Request Number: " << l.request_number << "\n"
        //    << "Current Access Time: " << l.current_time << "\n"
        //    << "------------------------\n";

        trace_records.push_back(l);
    }

    //std::unordered_map<int, TemperatureRecord> temperatureTable = lru_cache.calculateTemperature(trace_records);
    // 打印温度表

    std::cout << lru_cache.statics();
    std::cout << arc_cache.statics();
    //std::cout << score_cache.statics();  // SCORE算法已注释
    //std::cout << tdc_cache.statics();  // TDC算法已注释
    return 0;
}

