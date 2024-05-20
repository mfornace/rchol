#ifndef rchol_lap_hpp
#define rchol_lap_hpp

#include <random>
#include <vector>

using rchol_rng = std::mt19937;

void rchol_lap(rchol_rng &gen, std::vector<size_t>&, std::vector<size_t>&, std::vector<double>&, 
    size_t*&, size_t*&, double*&, size_t&, std::vector<size_t>&);


#endif

