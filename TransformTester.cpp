#include "TransformTester.h"

TransformTester::TransformTester(int a, int b) :
	x(a),
	y(b)
{
	std::cout << "The constructor of TransformTester is being invoked!" << std::endl;
}

TransformTester::~TransformTester()
{
	std::cout << "The destructor of TransformTester is being invoked!" << std::endl;
}

int TransformTester::AddTwoNumbers()
{
	return x + y;
}
