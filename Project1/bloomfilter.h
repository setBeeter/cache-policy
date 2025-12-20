#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <set>

// 简单的集合过滤器，使用 std::set 封装
class BloomFilter {
public:
	BloomFilter(int size) {
		capacity = size;//ʵ����ָ���ǹ��������Դ洢��Ԫ����������
	}
	void setBit(unsigned int count) {
		s.insert(count);
		--capacity;//ʵ������ֻ�Ǽ򵥵�����Ԫ�ص� std::set ��
	}

	bool checkBit(unsigned int count) {
		return s.find(count) != s.end();//��������Ԫ�أ�count���Ƿ��Ѿ������ڼ��� s ��
	}
	// bool operator<(const BloomFilter& b) const{
	// 	return this->vec.size() > b.vec.size();
	// }

	int remain_capacity() {
		return capacity;//���ز�¡��������ǰ��ʣ�������������������Ӷ��ٸ�Ԫ��
	}
private:
	std::set<int> s;           // 用于存储添加的元素
	unsigned int capacity;     // 记录过滤器的剩余容量
};

#endif  // BLOOMFILTER_H
