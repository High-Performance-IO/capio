#include "../src/capio_file.hpp"

#include <iostream>
#include <set>
#include <vector>

void test1() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.print();
}

void test2() {
	std::vector<int> nums = {1, 3, 5, 7, 9, 11};
	Capio_file c_file;
	for (size_t i = 0; i < nums.size(); i+=2)
		c_file.insert_sector(nums[i], nums[i + 1]);
	c_file.print();
}

void test3() {
	Capio_file c_file;
	c_file.insert_sector(1, 4);
	c_file.insert_sector(2, 3);
	c_file.print();
}

void test4() {
	Capio_file c_file;
	c_file.insert_sector(5, 7);
	c_file.insert_sector(1, 3);
	c_file.print();
}

void test5() {
	Capio_file c_file;
	c_file.insert_sector(1, 4);
	c_file.insert_sector(1, 3);
	c_file.print();
}

void test6() {
	Capio_file c_file;
	c_file.insert_sector(1, 4);
	c_file.insert_sector(2, 4);
	c_file.print();
}

void test7() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.insert_sector(5, 7);
	c_file.insert_sector(0, 10);
	c_file.print();
}

void test8() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.insert_sector(5, 7);
	c_file.insert_sector(2, 6);
	c_file.print();
}


void test9() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.insert_sector(5, 7);
	c_file.insert_sector(3, 5);
	c_file.print();
}

void test10() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.insert_sector(5, 7);
	c_file.insert_sector(3, 4);
	c_file.print();
}

void test11() {
	Capio_file c_file;
	c_file.insert_sector(1, 3);
	c_file.insert_sector(5, 7);
	c_file.insert_sector(4, 5);
	c_file.print();
}

void test12() {
	Capio_file c_file;
	c_file.insert_sector(2, 3);
	c_file.insert_sector(1, 4);
	c_file.print();
}

void test13() {
	Capio_file c_file;
	off64_t sector_end = c_file.get_sector_end(10);
	std::cout << "sector end :" << sector_end << std::endl;
}

void test14() {
	Capio_file c_file;
	c_file.insert_sector(2, 4);
	off64_t sector_end = c_file.get_sector_end(1);
	std::cout << "sector end :" << sector_end << std::endl;
	sector_end = c_file.get_sector_end(2);
	std::cout << "sector end :" << sector_end << std::endl;
	sector_end = c_file.get_sector_end(4);
	std::cout << "sector end :" << sector_end << std::endl;
	sector_end = c_file.get_sector_end(5);
	std::cout << "sector end :" << sector_end << std::endl;
	c_file.insert_sector(6, 8);
	sector_end = c_file.get_sector_end(5);
	std::cout << "sector end :" << sector_end << std::endl;
	sector_end = c_file.get_sector_end(7);
	std::cout << "sector end :" << sector_end << std::endl;
	sector_end = c_file.get_sector_end(9);
	std::cout << "sector end :" << sector_end << std::endl;
}


void test_seek_data() {
	Capio_file c_file;
	off_t res = c_file.seek_data(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(2);
	std::cout << "res " << res << std::endl;

	c_file.insert_sector(2, 4);
	res = c_file.seek_data(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(2);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(3);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(4);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(5);
	std::cout << "res " << res << std::endl;


	c_file.insert_sector(6, 8);
	res = c_file.seek_data(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(2);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(3);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(4);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(5);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(6);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(7);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(8);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_data(9);
	std::cout << "res " << res << std::endl;
}

void test_seek_hole() {
	Capio_file c_file;
	off_t res = c_file.seek_hole(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(2);
	std::cout << "res " << res << std::endl;

	c_file.insert_sector(2, 4);
	res = c_file.seek_hole(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(2);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(3);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(4);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(5);
	std::cout << "res " << res << std::endl;


	c_file.insert_sector(6, 8);
	res = c_file.seek_hole(0);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(2);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(3);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(4);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(5);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(6);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(7);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(8);
	std::cout << "res " << res << std::endl;
	res = c_file.seek_hole(9);
	std::cout << "res " << res << std::endl;
}
int main(int argc, char** argv) {
	std::cout << "test 1" << std::endl;
	test1();
	std::cout << "test 2" << std::endl;
	test2();
	std::cout << "test 3" << std::endl;
	test3();
	std::cout << "test 4" << std::endl;
	test4();
	std::cout << "test 5" << std::endl;
	test5();
	std::cout << "test 6" << std::endl;
	test6();
	std::cout << "test 7" << std::endl;
	test7();
	std::cout << "test 8" << std::endl;
	test8();
	std::cout << "test 9" << std::endl;
	test9();
	std::cout << "test 10" << std::endl;
	test10();
	std::cout << "test 11" << std::endl;
	test11();
	std::cout << "test 12" << std::endl;
	test12();
	std::cout << "test 13" << std::endl;
	test13();
	std::cout << "test 14" << std::endl;
	test14();
	std::cout << "test 15" << std::endl;
	test_seek_data();
	std::cout << "test 16" << std::endl;
	test_seek_hole();

	return 0;
}
