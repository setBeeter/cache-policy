
#include <cassert>
#include <list>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <sstream>
//LruType ö�٣��о��˲�ͬ���͵� LRU���������ʹ�ã��б���T1��B1��T2��B2��None��
enum LruType {
    T1,
    B1,
    T2,
    B2,
    None,
};
//ArcEntry �ṹ�壺��ʾ�����е�һ����Ŀ������Ŀ�ꡢ��ַ��LRU ���ͺ��б��ĵ�����
struct ArcEntry;
/*ArcEntryPtr ���������� ArcEntry ���������ָ�롣��ͨ������ȷ���ڲ�����Ҫ�ö���ʱ��ȷ�ͷ��ڴ棬�������ֶ����� delete���� ArcEntryPtr ���󳬳���Χʱ�������Զ����������������ͷŹ������ڴ档*/
using ArcEntryPtr = std::shared_ptr<ArcEntry>;

struct ArcEntry {
    int target;
    int addr;
    LruType lru_type;
    std::list<ArcEntryPtr>::iterator iter;
};
//ARCCache �ࣺʵ���� ARC �����㷨
class ARCCache {

public:
    //// ���캯������ʼ��������������ݽṹ
    explicit ARCCache(int c, std::string file_name) : _c(c), _p(0),
        _file_name(file_name), _hit_count(0), _get_count(0),_miss_count(0) {

        _list_table[T1] = &_t1;
        _list_table[T2] = &_t2;
        _list_table[B1] = &_b1;
        _list_table[B2] = &_b2;
    }
    //// ���ÿ������캯���͸�ֵ�������ȷ����һʵ��
    ARCCache(const ARCCache&) = delete;
    ARCCache& operator=(const ARCCache&) = delete;
    // ��������
    ~ARCCache() {}

public:
    // ��ȡ������ָ��Ŀ�������
    int get(int target);
    // ���ػ����ͳ����Ϣ
    std::string statics();

private:
    // ����Ŀ�ƶ���ָ���� LRU �б�
    inline void move_to_lru(const ArcEntryPtr& entry, const LruType& new_type) {
        auto src_list = _list_table[entry->lru_type];
        auto dst_list = _list_table[new_type];
        assert(src_list != nullptr && dst_list != nullptr);

        auto it = src_list->begin();
        for (; it != src_list->end(); ++it) {
            if (it->get() == entry.get()) {
                break;
            }
        }
        assert(it != src_list->end());

        dst_list->splice(dst_list->begin(), *src_list, it);
        entry->lru_type = new_type;
        entry->iter = dst_list->begin();
    }
    // ִ���滻����
    void replace(bool in_b2);
    // ������������黺���С�Ƿ�����涨
    inline void assert_c() {
        assert(_t1.size() + _t2.size() <= _c);
        assert(_t1.size() + _b1.size() <= _c);
        assert(_t2.size() + _b2.size() <= _c * 2);
        assert(_t1.size() + _b1.size() + _t2.size() + _b2.size() <= _c * 2);
    }

private:
    // ��ͬ LRU �б�
    std::list<ArcEntryPtr> _t1;
    std::list<ArcEntryPtr> _b1;
    std::list<ArcEntryPtr> _t2;
    std::list<ArcEntryPtr> _b2;
    // ӳ�䲻ͬ LRU �б�������
    std::unordered_map<LruType, std::list<ArcEntryPtr>*> _list_table;
    std::unordered_map<int, ArcEntryPtr> _table;
    // ��������
    int _c;
    // P ����
    double _p;
    // ���д����ͷ��ʴ���
    unsigned int _hit_count;
    unsigned int _get_count;
    int _miss_count;  // �����ӵ�δ���м�����

    // �ļ���
    std::string _file_name;

};
