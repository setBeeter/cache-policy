#include <set>
//更像是一个简单的集合（std::set）包装器,来确切地存储和查询元素
class BloomFilter {
public:
	BloomFilter(int size) {
		capacity = size;//实现中指的是过滤器可以存储的元素数量上限
	}
	void setBit(unsigned int count) {
		s.insert(count);
		--capacity;//实际上它只是简单地添加元素到 std::set 中
	}

	bool checkBit(unsigned int count) {
		return s.find(count) != s.end();//检查给定的元素（count）是否已经存在于集合 s 中
	}
	// bool operator<(const BloomFilter& b) const{
	// 	return this->vec.size() > b.vec.size();
	// }

	int remain_capacity() {
		return capacity;//返回布隆过滤器当前的剩余容量，即还可以添加多少个元素
	}
private:
	std::set<int> s;//用于存储添加到过滤器中的元素
	unsigned int capacity;//记录过滤器的剩余容量
};
