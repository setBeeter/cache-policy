#include <iostream>
#include <fstream>
#include <string>
#include <ctime>      // 用于 time() 函数
#include <cassert>   // 用于 assert() 宏
#include "arc.h"
#include "lru.h"
// #include"score.h"
#include"iscore.h"
#include "TraceLine.h"
#include <vector>
#include <chrono>
// #include"tiercache.h"  // Ceph/TierCache 相关，当前实验不使用（避免引入其头文件依赖）
// #include "TDC.h"  // TDC算法
#include <sstream>     // 用于字符串流
#include <iomanip>     // 用于日期格式化

//whc测试

// ========== 计时全局变量（定义） ==========
double g_t_parse_ns = 0.0;
double g_t_get_ns = 0.0;
uint64_t g_request_count = 0;

int main(int argc, char** argv) {

    std::cout << "=== Entering main ===" << std::endl;
    std::cout << "argc = " << argc << std::endl;
    for (int i = 0; i < argc; ++i) {
        std::cout << "argv[" << i << "] = " << argv[i] << std::endl;
    }

    // ========== 编译模式检查 ==========
#ifdef NDEBUG
    std::cout << "[BUILD] NDEBUG=1 (Release)" << std::endl;
#else
    std::cout << "[BUILD] NDEBUG not defined (Debug - SLOW!)" << std::endl;
#endif
#ifdef _ITERATOR_DEBUG_LEVEL
    std::cout << "[BUILD] _ITERATOR_DEBUG_LEVEL=" << _ITERATOR_DEBUG_LEVEL << std::endl;
#endif

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

    // ========== 缓存算法初始化 ==========
    LRUCache lru_cache(c, argv[2]);
    ARCCache arc_cache(c, argv[2]);
    // ========== ISCORE 参数说明 ==========
    // lambda = 0.001    温度衰减系数（越大衰减越快）
    // c_smooth = 1.0    age 平滑常数（避免除零，>0 即可）
    // evict_k = 1       每次淘汰数量（k=1 最精准，k 越大速度越快但命中率可能下降）
    ISCORECache iscore_cache(c, argv[2], /*lambda=*/0.001, /*c_smooth=*/1.0, /*evict_k=*/1);

    trace_line l;
    int line_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    // 主循环：读取 trace 数据
    while (true) {
        // ========== t_parse 计时开始 ==========
        auto t0_parse = std::chrono::steady_clock::now();
        int ret = fscanf_s(pFile, "%d %d %d %d\n",
            &l.starting_block, &l.size_of_blocks, &l.ignore, &l.request_number);
        auto t1_parse = std::chrono::steady_clock::now();
        g_t_parse_ns += std::chrono::duration<double, std::nano>(t1_parse - t0_parse).count();
        // ========== t_parse 计时结束 ==========

        if (ret == EOF) break;

        line_count++;

        // 对每个 block 进行缓存访问
        for (auto i = l.starting_block; i < (l.starting_block + l.size_of_blocks); ++i) {
            // ========== t_get 计时开始 ==========
            auto t0_get = std::chrono::steady_clock::now();

            // LRU 算法
            auto res1 = lru_cache.get(i);
            assert(res1 != -1);

            // ARC 算法
            auto res2 = arc_cache.get(i);
            assert(res2 != -1);

            // 新版 ISCORE：只需要 block_id 和 size，不再需要 trace_records
            ISCOREParams iscore_param{ i, 4096 };  // block size = 4096 bytes
            auto res_is = iscore_cache.get(iscore_param);
            assert(res_is != -1);

            auto t1_get = std::chrono::steady_clock::now();
            g_t_get_ns += std::chrono::duration<double, std::nano>(t1_get - t0_get).count();
            ++g_request_count;
            // ========== t_get 计时结束 ==========
        }
        
        // 每500行输出一次进度信息
        if (line_count % 500 == 0) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            auto elapsed_minutes = elapsed / 60;
            auto elapsed_seconds = elapsed % 60;
            std::cout << "Processed " << line_count << " lines... "
                      << "Elapsed time: " << elapsed_minutes << "m " << elapsed_seconds << "s\n";
        }
    }

    fclose(pFile);

    // ========== 计时结果输出 ==========
    auto end_time = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(end_time - start_time).count();
    double t_parse_sec = g_t_parse_ns / 1e9;
    double t_get_sec = g_t_get_ns / 1e9;
    double t_evict_sec = g_t_evict_ns / 1e9;
    double qps = (total_sec > 0) ? g_request_count / total_sec : 0;

    std::cout << "\n========== PERFORMANCE TIMING ==========\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "elapsed_total:       " << total_sec << " sec\n";
    std::cout << "t_parse:             " << t_parse_sec << " sec (" << std::setprecision(1) << (100.0 * t_parse_sec / total_sec) << "%)\n";
    std::cout << std::setprecision(3);
    std::cout << "t_get:               " << t_get_sec << " sec (" << std::setprecision(1) << (100.0 * t_get_sec / total_sec) << "%)\n";
    std::cout << std::setprecision(3);
    std::cout << "t_evict:             " << t_evict_sec << " sec (" << std::setprecision(1) << (100.0 * t_evict_sec / total_sec) << "%)\n";
    std::cout << std::setprecision(3);
    std::cout << "t_evict_traverse:    " << (g_t_evict_traverse_ns / 1e9) << " sec (" << std::setprecision(1) << (100.0 * g_t_evict_traverse_ns / 1e9 / total_sec) << "%)\n";
    std::cout << "requests:            " << g_request_count << "\n";
    std::cout << "cache_size:          " << c << "\n";
    std::cout << "evict_cnt:           " << g_evict_count << "\n";
    std::cout << "QPS:                 " << std::setprecision(1) << qps << " req/sec\n";
    if (g_evict_count > 0) {
        std::cout << "avg_evict_time:      " << std::setprecision(6) << (t_evict_sec / g_evict_count * 1000.0) << " ms/evict\n";
    }
    std::cout << "=========================================\n";

    // 准备输出结果到文件
    std::string trace_file_path(argv[2]);
    std::string dataset_name;
    size_t last_slash = trace_file_path.find_last_of("\\/");
    std::string filename = (last_slash == std::string::npos) ? trace_file_path : trace_file_path.substr(last_slash + 1);
    size_t last_dot = filename.find_last_of(".");
    dataset_name = (last_dot == std::string::npos) ? filename : filename.substr(0, last_dot);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &time_t);
    std::ostringstream date_stream;
    date_stream << std::setfill('0') << std::setw(4) << (local_tm.tm_year + 1900)
                << std::setw(2) << (local_tm.tm_mon + 1)
                << std::setw(2) << local_tm.tm_mday;
    std::string date_str = date_stream.str();
    
    std::ostringstream filename_stream;
    filename_stream << "res/" << dataset_name << "+" << argv[1] << "+" << date_str << ".txt";
    std::string output_filename = filename_stream.str();
    
    std::ofstream output_file(output_filename);
    if (!output_file.is_open()) {
        std::cerr << "Warning: Cannot open output file: " << output_filename << std::endl;
        std::cerr << "Results will only be printed to console." << std::endl;
    }
    
    auto output_to_both = [&output_file](const std::string& content) {
        std::cout << content;
        if (output_file.is_open()) {
            output_file << content;
        }
    };
    
    output_to_both(lru_cache.statics());
    output_to_both(arc_cache.statics());
    output_to_both(iscore_cache.statics());
    
    if (output_file.is_open()) {
        output_file.close();
        std::cout << "\nResults saved to: " << output_filename << std::endl;
    }
    
    return 0;
}
