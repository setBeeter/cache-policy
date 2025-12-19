#include <iostream>
#include <fstream>
#include <string>
#include <ctime>      // 用于 time() 函数
#include <cassert>   // 用于 assert() 宏
#include "arc.h"
#include "lru.h"
#include"TDC.h"
#include"score.h"
#include"iscore.h"
#include "TraceLine.h"
#include <vector>
#include <chrono>
#include"tiercache.h"  // TDC相关，暂时不使用
#include <thread>
// #include"tdc2.h"  // TDC算法暂时不使用


//whc测试


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
    SCORECache score_cache(c, argv[2]);
    ISCORECache iscore_cache(c, argv[2]);
    // TDCCache tdc_cache(c, argv[2]);  // TDC算法暂时不使用
    trace_line l;
    int access_counter = 0;
    // 在主循环中添加一个计数器
    // int requestCounter = 0;  // TDC算法请求计数器（已注释）
    // 定义一个用于存储 trace_line 记录的容器
    std::vector<trace_line> trace_records;
    // 添加行计数器，用于进度输出
    int line_count = 0;
    // TDC算法周期计数器（已注释）
    // int n = 1;  // 初始化周期计数器
    // 记录程序开始时间，用于计算耗时
    auto start_time = std::chrono::steady_clock::now();
    //最外层的 while 循环用于从文件中读取 trace 数据。在每次迭代中，它读取一行数据，将这一行的信息存储在 trace_line 结构体中，并进行相应的处理。
    //具体来说，每次迭代中，while 循环从文件中读取一个 trace 数据，该数据包括起始块、块数、忽略标志和请求号。这些信息被存储在 trace_line 结构体中的对应成员中。
    while (fscanf_s(pFile, "%d %d %d %d\n",
        &l.starting_block, &l.size_of_blocks, &l.ignore, &l.request_number) != EOF) {
        line_count++;  // 每处理一行，计数器+1
        // 更新每个 trace 数据的目前访问时间和最后访问时间戳
        // 计算对象大小
        // int size = l.size_of_blocks * 4096;  // TDC算法对象大小（已注释）
        l.current_time = time(nullptr);  // 使用系统当前时间
        l.access_count = 0;  // 初始化访问次数为0
        // 获取相应的数据
        //内部的 for 循环则对从文件中读取的每个 trace 数据进行缓存访问的模拟。在每次迭代中，它对当前 trace 数据中描述的块范围进行循环，调用 LRUCache 和 ARCCache 类的 get 方法来模拟从缓存中获取数据。
        //在这个循环内，针对每个块，它执行了一些断言检查，确保缓存访问的正确性。
        for (auto i = l.starting_block; i < (l.starting_block + l.size_of_blocks); ++i) {
            // 增加计数器
            
            auto res1 = lru_cache.get(i);
            assert(res1 != -1);
            auto res2 = arc_cache.get(i);
            assert(res2 != -1);

            // SCORE算法：使用逻辑时间戳（request_number）
            SCOREParams scoreparam{ i, static_cast<uint32_t>(l.request_number), l.size_of_blocks };
            auto res3 = score_cache.get(scoreparam);
            assert(res3 != -1);
<<<<<<< HEAD

            // ISCORE 使用与 SCORE 相同的逻辑时间和 block_id 作为 key
            ISCOREParams iscore_param{ i, static_cast<uint32_t>(l.request_number), l.size_of_blocks };
            auto res_is = iscore_cache.get(iscore_param);
            assert(res_is != -1);

            // TDC算法相关代码（已注释）
            // // 判断是否达到一个周期
            // if (requestCounter % 160000 == 0) {
            //     ++n;
            // }
            // TDCParams tdcParams{ i, n, static_cast<double>(size), tdc_cache.temperatureTable };//i对象 n是周期 size缓存大小
            // auto res4 = tdc_cache.get(tdcParams);
            // assert(res4 != -1);
            // requestCounter++;
=======
            
            // TDC算法相关代码
            // 判断是否达到一个周期
            if (requestCounter % 160000 == 0) {
                ++n;
            }
            TDCParams tdcParams{ i, n, static_cast<double>(size), tdc_cache.temperatureTable };//i对象 n是周期 size缓存大小
            auto res4 = tdc_cache.get(tdcParams);
            assert(res4 != -1);
            requestCounter++;
>>>>>>> f31377255e60c4430aae1f6d87701cb4fa484c74
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

        trace_records.push_back(l);
    }

    //std::unordered_map<int, TemperatureRecord> temperatureTable = lru_cache.calculateTemperature(trace_records);
    // 打印温度表

    std::cout << lru_cache.statics();
    std::cout << arc_cache.statics();
    std::cout << score_cache.statics();
    std::cout << iscore_cache.statics();
    // std::cout << tdc_cache.statics();  // TDC算法已注释
    return 0;
}

