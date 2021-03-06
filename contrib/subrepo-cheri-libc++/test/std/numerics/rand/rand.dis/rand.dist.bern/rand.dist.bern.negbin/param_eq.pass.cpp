//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <random>

// template<class IntType = int>
// class negative_binomial_distribution
// {
//     class param_type;

#include <random>
#include <limits>
#include <cassert>

int main(int, char**)
{
    {
        typedef std::negative_binomial_distribution<> D;
        typedef D::param_type param_type;
        param_type p1(3, 0.75);
        param_type p2(3, 0.75);
        assert(p1 == p2);
    }
    {
        typedef std::negative_binomial_distribution<> D;
        typedef D::param_type param_type;
        param_type p1(3, 0.75);
        param_type p2(3, 0.5);
        assert(p1 != p2);
    }

  return 0;
}
