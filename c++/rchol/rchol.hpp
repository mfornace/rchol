#ifndef rchol_hpp
#define rchol_hpp
#include <random>

#include "../sparse.hpp"

using rchol_rng = std::mt19937;

void rchol(rchol_rng &gen, const SparseCSR &A, SparseCSR &G);

#endif
