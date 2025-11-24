
#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>
#include <chrono>
struct temp {
    int n;  // Ψһʶ
    double temperature; // ǰ¶
    int size; // С
    std::chrono::system_clock::time_point last_access_time; // 最后访问时间
};
//TDCParams ṹ壺Ψһʶһӳ䣬ӳ佫ڱӳ䵽Ӧ temp ʵ
struct TDCParams {
    int target;  // Ψһʶ
    int n; // ǰ
    double size;
    std::unordered_map<int, std::unordered_map<int, temp>> temperatureTable; //µ¶ȱ
};
//temp ṹ壺ڴ洢ڵ¶ȡСʱ䡣


class TDCCache {

public:
    explicit TDCCache(int c, std::string file_name) :
        _capacity(c), _file_name(file_name), _hit_count(0), _get_count(0) {}

    TDCCache(const TDCCache&) = delete;
    TDCCache& operator=(const TDCCache&) = delete;

    ~TDCCache() {}

public:
    int get(const TDCParams& params);
    std::string statics();
    //double calculateTemperature(int target);
    int currentCycleAccessCount;  //¼ǰڵķʴ
   //ʹһϣ std::unordered_map<int, TDCParams>ڴ洢ÿ TDCParams ʵ
    std::unordered_map<int, std::unordered_map<int,temp>> temperatureTable;
    // 洢¶ԼڵǶ׹ϣ
    //std::unordered_map<int, double> densityTable;
private:
    //һ洢ݵ _items
    std::list<std::pair<int, int>> _items; // (target, cache_addr)
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;//ϣ洢ÿλõĹϣ _table
    int _capacity;
    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;
};
