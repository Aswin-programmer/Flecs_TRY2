#pragma once

#include <iostream>

class TransformTester
{
public:
	TransformTester() = default;
	TransformTester(int a, int b);
	~TransformTester();

	int AddTwoNumbers();
private:
	int x;
	int y;
};

