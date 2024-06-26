#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <typeinfo>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sys/resource.h>
#include <string>
#include <sstream>
#include <future>
#include <thread>
#include <vector>
#include "rchol_lap.hpp"
#include "spcol.hpp"


struct Sparse_storage_input {
    std::vector<size_t> *colPtr; 
    std::vector<size_t> *rowIdx; 
    std::vector<double> *val;
};


struct Sparse_storage_output {
    size_t *colPtr; 
    size_t *rowIdx; 
    double *val;
    size_t N;
};

/* used for random sampling */
struct Sample {
    size_t row;
    double data;
    Sample(size_t arg0, double arg1)
    {
        row = arg0;
        data = arg1;
    }
    Sample()
    {
        
    }
    bool operator<(Sample other) const
    {
        return row < other.row;
    }
};

/* for keeping track of edges in separator and other special use */
struct Edge_info {
    double val;
    size_t row;
    size_t col;
    Edge_info(double arg0, size_t arg1, size_t arg2)
    {
        val = arg0;
        row = arg1;
        col = arg2;
    }
    Edge_info()
    {
        
    }
    bool operator<(Edge_info other) const
    {
        return col < other.col;
    }
};



/* functions set up */
void process_array(const Sparse_storage_input *input, std::vector<size_t> &result_idx, size_t depth, size_t target, std::vector<gsl_spmatrix *> &lap, size_t start, size_t total_size, int core_begin, int core_end); // read in matlab array to gsl sparray
std::vector<Edge_info> & recursive_calculation(rchol_rng &gen, std::vector<size_t> &result_idx, size_t depth, std::vector<gsl_spmatrix *> &lap, double *diagpt, size_t start, size_t total_size, size_t target, int core_begin, int core_end);
/* functions for factoring Cholesky */
void cholesky_factorization(rchol_rng &gen, std::vector<gsl_spmatrix *> &lap, std::vector<size_t> &result_idx, Sparse_storage_output *output);

void linear_update(gsl_spmatrix *b);
/* sampling algorithm */
double random_sampling0(rchol_rng &gen, gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound);
double random_sampling1(gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound);
double random_sampling2(gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound);

bool compare (Sample i, Sample j);
/* clean up memory */
void clear_memory(std::vector<gsl_spmatrix *> &lap);

int NUM_THREAD = 0;




void rchol_lap(rchol_rng &gen, Sparse_storage_input *input, Sparse_storage_output *output, std::vector<size_t> &result_idx, int thread)
{
    NUM_THREAD = thread;
    // cpu_set_t cpuset; 

    //the CPU we whant to use
    int cpu = 0;

    // CPU_ZERO(&cpuset);       //clears the cpuset
    // CPU_SET( cpu , &cpuset); //set CPU 2 on cpuset
    /*
    * cpu affinity for the calling thread 
    * first parameter is the pid, 0 = calling thread
    * second parameter is the size of your cpuset
    * third param is the cpuset in which your thread will be
    * placed. Each bit represents a CPU
    */
    // sched_setaffinity(0, sizeof(cpuset), &cpuset);
    

    std::vector<gsl_spmatrix *> *lap_val = new std::vector<gsl_spmatrix *>(input->colPtr->size() - 1);
    std::vector<gsl_spmatrix *> &lap = *lap_val;
    process_array(input, result_idx, 1, (size_t)(std::log2(NUM_THREAD) + 1), lap, 0, result_idx.size() - 1, 0, NUM_THREAD);
    

    auto start = std::chrono::steady_clock::now();
    cholesky_factorization(gen, lap, result_idx, output);
    auto end = std::chrono::steady_clock::now();
    // std::cout << "chol time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "\n";


    // clear memory
    
    clear_memory(lap);

    

}



void rchol_lap(rchol_rng &gen, std::vector<size_t> &rowPtrA, std::vector<size_t> &colIdxA, 
    std::vector<double> &valA, size_t* &colPtrG, size_t* &rowIdxG, double* &valG, size_t &sizeG, std::vector<size_t> &idx) {
  Sparse_storage_input input;
  input.colPtr = &rowPtrA;
  input.rowIdx = &colIdxA;
  input.val = &valA;
  Sparse_storage_output output;
  rchol_lap(gen, &input, &output, idx, idx.size() / 2);
  colPtrG = output.colPtr;
  rowIdxG = output.rowIdx;
  valG = output.val;
  sizeG = output.N;
}




/* clear memory */
void clear_memory(std::vector<gsl_spmatrix *> &lap)
{
    size_t i;
    // clear lap
    for(i = 0; i < lap.size(); i++)
    {
        gsl_spmatrix_free(lap.at(i));
    }
    
    delete &lap;
    

}

// #include<immintrin.h>

bool uni(Sample a, Sample b) 
{ 
    // Checking if both the arguments are same and equal 
    // to 'G' then only they are considered same 
    // and duplicates are removed 
    if (a.row == b.row) { 
        return 1; 
    } else { 
        return 0; 
    } 
} 




std::vector<Edge_info> & recursive_calculation(rchol_rng &gen, std::vector<size_t> &result_idx, size_t depth, std::vector<gsl_spmatrix *> &lap, double *diagpt, size_t start, size_t total_size, size_t target, int core_begin, int core_end)
{

    int core_id = (core_begin + core_end) / 2;
    // if(sched_getcpu() != core_begin)
    // {
    //     cpu_set_t cpuset;
    //     CPU_ZERO(&cpuset);
    //     CPU_SET(core_begin, &cpuset);
    //     sched_setaffinity(0, sizeof(cpuset), &cpuset);
    // }
    
    

    /* base case */
    if(target == depth)
    {
        

        //std::cout << "thread id: " << omp_get_thread_num() << "\n";
		double density = 0;
        // int cpu_num = sched_getcpu();
        auto time_s = std::chrono::steady_clock::now();
        auto time_e = std::chrono::steady_clock::now();
        auto elapsed = time_s - time_e;

        std::vector<Edge_info> *pt = new std::vector<Edge_info>();
        std::vector<Edge_info> &sep_edge = *pt;
        
        time_s = std::chrono::steady_clock::now();
        
        for (size_t i = result_idx.at(start); i < result_idx.at(start + total_size); i++)
        {
            
            size_t current = i;
            gsl_spmatrix *b = lap.at(current);
            if(b->nz - b->split > 0)
            {

                linear_update(b);
                
            }
        
            diagpt[current] = random_sampling0(gen, lap.at(current), lap, current, sep_edge, result_idx.at(start), result_idx.at(start + total_size));
            density += lap.at(current)->nz;
        }

        time_e = std::chrono::steady_clock::now();
        elapsed += time_e - time_s;

        // std::cout << "depth: " << depth << " thread " << std::this_thread::get_id() << " cpu: " << cpu_num << " time: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "\n";
        // std::cout << "depth: " << depth << " length: " << result_idx.at(start) << " nztotal " << density << " density: " << density / (result_idx.at(start + total_size) - result_idx.at(start)) << "\n";
        // std::cout << "depth: " << depth << "\n";
        //std::cout << omp_proc_bind_master << "  " << omp_get_proc_bind << "\n";
        return sep_edge;
    }
    else
    {
        auto time_s = std::chrono::steady_clock::now();
        auto time_e = std::chrono::steady_clock::now();
        auto elapsed = time_s - time_e;

        /* recursive call */
        std::vector<Edge_info> *l_pt;
        std::vector<Edge_info> *r_pt;

        // create new thread
        // auto a1 = std::async(std::launch::async, recursive_calculation, gen, std::ref(result_idx), depth + 1, std::ref(lap), diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_id, core_end);
        // auto a1 = std::async(std::launch::async, recursive_calculation, gen, std::ref(result_idx), depth + 1, std::ref(lap), diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_id, core_end);
        
        //#pragma omp task shared(lap, r_pt)
        //{
        //    r_pt = &recursive_calculation(result_idx, depth + 1, lap, diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_id, core_end);
        //}
        // run its own job
        l_pt = &recursive_calculation(gen, result_idx, depth + 1, lap, diagpt, start, (total_size - 1) / 2, target, core_begin, core_id);
        //r_pt = &recursive_calculation(result_idx, depth + 1, lap, diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_id, core_end);
        
        /* synchronize at this point */
        //#pragma omp taskwait
        // r_pt = &a1.get();
        r_pt = &recursive_calculation(gen, std::ref(result_idx), depth + 1, std::ref(lap), diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_id, core_end);

        //auto a1 = std::async(std::launch::async, recursive_calculation, std::ref(result_idx), depth + 1, std::ref(lap), diagpt, start, (total_size - 1) / 2, target, core_id, core_end);
        //r_pt = &recursive_calculation(result_idx, depth + 1, lap, diagpt, (total_size - 1) / 2 + start, (total_size - 1) / 2, target, core_begin, core_id);
        //l_pt = &a1.get();
        
        std::vector<Edge_info> &l_edge = *l_pt;
        std::vector<Edge_info> &r_edge = *r_pt;
        
        /* process edges in separator on the current level */
        std::vector<Edge_info> *pt = new std::vector<Edge_info>();
        std::vector<Edge_info> &sep_edge = *pt;

        size_t l_bound = result_idx.at(start + total_size - 1);
        size_t r_bound = result_idx.at(start + total_size);
        for (size_t i = 0; i < l_edge.size(); i++)
        {

            Edge_info &temp = l_edge.at(i);
            if(temp.col >= l_bound && temp.col < r_bound)
            {
                gsl_spmatrix_set(lap.at(temp.col), temp.row, 0, temp.val);
            }
            else
            {
                sep_edge.push_back(Edge_info(temp.val, temp.row, temp.col));
            }
        }
        
        for (size_t i = 0; i < r_edge.size(); i++)
        {
            
            Edge_info &temp = r_edge.at(i);
            if(temp.col >= l_bound && temp.col < r_bound)
            {
                gsl_spmatrix_set(lap.at(temp.col), temp.row, 0, temp.val);
            }
            else
            {
                sep_edge.push_back(Edge_info(temp.val, temp.row, temp.col));
            }
        }
        delete &l_edge;
        delete &r_edge;

        /* separator portion */
        double density = 0;
        double before_density = 0;
/*
        for (size_t i = result_idx.at(start + total_size - 1); i < result_idx.at(start + total_size); i++)
        {
            
            std::vector<Sample> v2(relation.at(i)->receive);
            for(int j = 0; j < lap.at(i)->nz; j++)
            {
                v2.push_back(Sample(lap.at(i)->i[j], lap.at(i)->data[j]));
            }
            std::sort(v2.begin(), v2.end());
            auto ip = std::unique(v2.begin(), v2.end(), uni); 
            //assert(v2.size() == (ip - v2.begin()));
            before_density += (ip - v2.begin());
        }
*/

		
        time_s = std::chrono::steady_clock::now();
        for (size_t i = result_idx.at(start + total_size - 1); i < result_idx.at(start + total_size); i++)
        {
            
            size_t current = i;
            gsl_spmatrix *b = lap.at(current);
            if(b->nz - b->split > 0)
            {
                
                linear_update(b);
            }

            
            diagpt[current] = random_sampling0(gen, lap.at(current), lap, current, sep_edge, l_bound, r_bound);
            density += lap.at(i)->nz;
        }
        time_e = std::chrono::steady_clock::now();
        elapsed = time_e - time_s;
        // int cpu_num = sched_getcpu();
        // std::cout << "depth(separator): " << depth << " thread " << std::this_thread::get_id() << " cpu: " << cpu_num  << " time: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " length: " << result_idx.at(start + total_size) - result_idx.at(start + total_size - 1) << " nztotal " << density << " before: " << before_density / (result_idx.at(start + total_size) - result_idx.at(start + total_size - 1)) << " density: " << density / (result_idx.at(start + total_size) - result_idx.at(start + total_size - 1)) << "\n";
		
        return sep_edge;
    }
    
}


void coalesce(std::vector<gsl_spmatrix *> &lap, size_t *cpt, size_t *rpt, double *datapt, double *diagpt)
{
    size_t counter = 0;
    size_t m = lap.size() - 1;
    cpt[0] = 0;
    
    
    for (size_t i = 0; i < m; i++)
    {
        gsl_spmatrix *toCopy = lap.at(i);
        size_t nz = toCopy->nz;
        size_t *irow = toCopy->i;
        double *data = toCopy->data;
        
        for (size_t j = 0; j < nz; j++)
        {
            if (irow[j] == m)
                continue;
            datapt[counter] = data[j];
            rpt[counter] = irow[j];
            if(rpt[counter] != i)
                datapt[counter] = datapt[counter] * -1;
            datapt[counter] *= std::sqrt(diagpt[i]);
            counter++;
        }
        cpt[i + 1] = counter;

    }

}



auto start1 = std::chrono::steady_clock::now();
auto end1 = std::chrono::steady_clock::now();
auto elapsed1 = end1 - start1;

/* main routine for Cholesky */

void cholesky_factorization(rchol_rng &gen, std::vector<gsl_spmatrix *> &lap, std::vector<size_t> &result_idx, Sparse_storage_output *output)
{
    // calculate nonzeros and create lower triangular matrix
    size_t m = lap.size();
    double *diagpt = new double[m]();
    
    auto start = std::chrono::steady_clock::now();
    auto end = std::chrono::steady_clock::now();
    auto elapsed = end - start;
    
    // std::cout << NUM_THREAD << "\n";


    start = std::chrono::steady_clock::now();

    /* recursive call */
    //#pragma omp parallel
    //#pragma omp single
    recursive_calculation(gen, result_idx, 1, lap,
        diagpt, 0, result_idx.size() - 1, (size_t)(std::log2(NUM_THREAD) + 1), 0, NUM_THREAD);
    // recursive_calculation(result_idx, 1, lap,
    //     diagpt, 0, result_idx.size() - 1, 3);
    end = std::chrono::steady_clock::now();
    elapsed = end - start;
    // std::cout << "factor time: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "\n";
    size_t nzmax = 0;
    size_t edge = 0;
    for (size_t i = 0; i < lap.size(); i++)
    {
        nzmax += lap.at(i)->nz;
	    edge += lap.at(i)->split;
    }
    // std::cout<< "nzmax: " << nzmax <<"\n";
    // std::cout<< "edge: " << edge <<"\n";
    // std::cout << "size of matrix length: " << m - 1 << "\n";


    // return results back 

    size_t *cpt = new size_t[m]();
    size_t *rpt = new size_t[nzmax]();
    double *datapt = new double[nzmax]();
    
    coalesce(lap, cpt, rpt, datapt, diagpt);
    output->colPtr = cpt;
    output->rowIdx = rpt;
    output->val = datapt;
    output->N = m - 1;
    delete[] diagpt;

    //mkl_sparse_d_create_csr(L, SPARSE_INDEX_BASE_ZERO, m - 1, m - 1, pointerB, pointerE, rpt, datapt);


}


double random_sampling2(rchol_rng &gen, gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound)
{

    double *data = cur->data;
    size_t *row = cur->i;
    double sum = 0.0;
    size_t nz = cur->nz;
    // find sum of column
    for (size_t i = 1; i < nz; i++)
    {
        sum += data[i];
    }
    // run only if at least 3 elements, including diagonal

    
    if (nz > 2)
    {
        
        int size = nz - 1;

        // create sample vector for sampling
        Sample sample[size];
        // cumulative sum
        double cumsum[size];
        double csum = 0.0;
        for (size_t i = 0; i < size; i++)
        {
            sample[i].row = row[i + 1];
            sample[i].data = data[i + 1];
        }

        // sort first based on value
        std::sort(sample, sample + size, compare);
        for (size_t i = 0; i < size; i++)
        {
            csum += sample[i].data;
            cumsum[i] = csum;
        }
        
        
        // sampling
        // random number and edge values
        int num_sample = (size - 1);
        for (int i = 0; i < num_sample; i++)
        {
            // sample based on discrete uniform
            std::uniform_int_distribution<int> discrete_dis(0, size - 2);
            int uniform = discrete_dis(gen);

            // sample based on weight
            std::uniform_real_distribution<> uniform_dis(0.0, 1.0);
            double tar = cumsum[uniform];
            double search_num = uniform_dis(gen) * (csum - tar) + tar;
            double *weight = std::lower_bound (cumsum + uniform, cumsum + size, search_num);

            
            
            // edge weight
            size_t minl = std::min(sample[weight - cumsum].row, sample[uniform].row);
            size_t maxl = std::max(sample[weight - cumsum].row, sample[uniform].row);

            // if(uniform > size - 1 || (weight - cumsum) > size - 1)
            // {

            //     std::cout << "uniform: " << uniform << "\n";
            //     std::cout << "prev: " << cumsum[size - 2] << "\n";
            //     std::cout << "last: " << sample[size - 1].data << "\n";
            //     std::cout << "other: " << cumsum[size - 1] << "\n";
            //     std::cout << "search: " << search_num << "\n";
            //     std::cout << "size: " << size << "\n";
            //     assert(cumsum[size - 1] < search_num);
            //     assert(false);
            // }
            

            double setval = sample[uniform].data * (csum - cumsum[uniform]) / csum * (double)(size - 1) / (double)(num_sample);
            if(minl >= l_bound && minl < r_bound)
            {
                gsl_spmatrix_set(lap.at(minl), maxl, 0, setval);
            }
            else
            {
                sep_edge.push_back(Edge_info(setval, maxl, minl));
            }
            
        }
        
        
    }
    else
    {
        if(nz == 1)
            sum = -data[0];
    }
       
    
    // update column
    gsl_spmatrix_scale(cur, 1.0 / sum);
    cur->data[0] = 1.0;
    
    return sum;
}



double random_sampling1(rchol_rng &gen, gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound)
{

    double *data = cur->data;
    size_t *row = cur->i;
    double sum = 0.0;
    size_t nz = cur->nz;
    // find sum of column
    for (size_t i = 1; i < nz; i++)
    {
        sum += data[i];
    }
    // run only if at least 3 elements, including diagonal

    
    if (nz > 2)
    {
        
        int size = nz - 1;

        // create sample vector for sampling
        Sample sample[size];
        // cumulative sum
        double cumsum[size];
        double csum = 0.0;
        for (size_t i = 0; i < size; i++)
        {
            sample[i].row = row[i + 1];
            sample[i].data = data[i + 1];
            csum += data[i + 1];
            cumsum[i] = csum;
        }
        
        
        // sampling
        // random number and edge values
        int num_sample = size;
        for (int i = 0; i < num_sample; i++)
        {
            // sample 1 based on weight
            std::uniform_real_distribution<> uniform_dis(0.0, csum);
            double *weight1 = std::lower_bound (cumsum, cumsum + size, uniform_dis(gen));

            // sample 2 based on weight
            double *weight2 = std::lower_bound (cumsum, cumsum + size, uniform_dis(gen));
            
            // edge weight
            size_t minl = std::min(sample[weight1 - cumsum].row, sample[weight2 - cumsum].row);
            size_t maxl = std::max(sample[weight1 - cumsum].row, sample[weight2 - cumsum].row);

            if (minl == maxl)
                continue;

            if(minl >= l_bound && minl < r_bound)
            {
                
                gsl_spmatrix_set(lap.at(minl), maxl, 0, csum / double((2 * num_sample)));
            }
            else
            {
                sep_edge.push_back(Edge_info(csum / double((2 * num_sample)), maxl, minl));
            }
            
        }
        
        
    }
    else
    {
        if(nz == 1)
           sum = -data[0];
    }
      


    // update column
    gsl_spmatrix_scale(cur, 1.0 / sum);
    cur->data[0] = 1.0;
    
    return sum;
}

double random_sampling0(rchol_rng &gen, gsl_spmatrix *cur, std::vector<gsl_spmatrix *> &lap, size_t curcol, std::vector<Edge_info> &sep_edge, size_t l_bound, size_t r_bound)
{

    double *data = cur->data;
    size_t *row = cur->i;
    double sum = 0.0;
    size_t nz = cur->nz;
    // find sum of column
    for (size_t i = 1; i < nz; i++)
    {
        sum += data[i];
    }
    // run only if at least 3 elements, including diagonal

    
    if (nz > 2)
    {
        
        int size = nz - 1;

        // create sample vector for sampling
        Sample sample[size];
        // cumulative sum
        double cumsum[size];
        double csum = 0.0;
        for (size_t i = 0; i < size; i++)
        {
            sample[i].row = row[i + 1];
            sample[i].data = data[i + 1];
        }
        // sort first based on value
        std::sort(sample, sample + size, compare);
        for (size_t i = 0; i < size; i++)
        {
            csum += sample[i].data;
            cumsum[i] = csum;
        }
        
        
        
        // sampling
        // random number and edge values
        
        for (size_t i = 0; i < size - 1; i++)
        {
            std::uniform_real_distribution<> dis(0.0, 1.0);
            double tar = cumsum[i];
            double r = dis(gen) * (csum - tar) + tar;
            double *low = std::lower_bound (cumsum + i, cumsum + size, r);
            
            // edge weight
            size_t minl = std::min(sample[low - cumsum].row, sample[i].row);
            size_t maxl = std::max(sample[low - cumsum].row, sample[i].row);

            // if(curcol >= 0 && curcol < 275)
            // {
            //     if((minl >= 275 && minl < 550))
            //     {
            //         std::cout << "wrong: " << minl << "self: " << curcol << " \n";
            //         assert(0);
            //     }
                
            // }
                

            if(minl >= l_bound && minl < r_bound)
            {
                
                gsl_spmatrix_set(lap.at(minl), maxl, 0, sample[i].data * (csum - cumsum[i]) / csum);
            }
            else
            {
                sep_edge.push_back(Edge_info(sample[i].data * (csum - cumsum[i]) / csum, maxl, minl));
            }
            
        }
        
        
    }
    
    // update column
    gsl_spmatrix_scale(cur, 1.0 / sum);
    cur->data[0] = 1.0;
    
    return sum;
}


// uses int for some loops
void linear_update(gsl_spmatrix *b)
{
    // sort
    size_t size = b->nz;
    Sample sample[size];
    for (size_t i = 0; i < size; i++)
    {
        sample[i].row = b->i[i];
        sample[i].data = b->data[i];
    }

    std::sort(sample, sample + size);



    // override original vector
    size_t rowp = sample[0].row;
    size_t addidx = 0;
    set_element(b, 0, sample[0].row, sample[0].data);
    for (size_t i = 1; i < size; i++)
    {
        if(sample[i].row != rowp)
        {
            addidx++;
            rowp = sample[i].row;
            set_element(b, addidx, sample[i].row, sample[i].data);
        }
        else
        {
            b->data[addidx] += sample[i].data;
        }
    }

    // set nonzero and split
    
    b->split = b->nz - b->split; 
    b->nz = addidx + 1;
}

bool compare1 (Edge_info *i, Edge_info *j)
{
    return (i->col < j->col);
}
/* read in matlab array */

void process_array(const Sparse_storage_input *input, std::vector<size_t> &result_idx, size_t depth, size_t target, std::vector<gsl_spmatrix *> &lap, size_t start, size_t total_size, int core_begin, int core_end)
{
    int core_id = (core_begin + core_end) / 2;
    // cpu_set_t cpuset;
    // CPU_ZERO(&cpuset);
    // CPU_SET(core_begin, &cpuset);
    // sched_setaffinity(0, sizeof(cpuset), &cpuset);
    
    // bottom level
    if(target == depth)
    {
        size_t i, j;
        size_t N = input->colPtr->size() - 1;
        std::vector<size_t> &cpt = *(input->colPtr);
        std::vector<size_t> &rpt = *(input->rowIdx);
        std::vector<double> &datapt = *(input->val);
        for(i = result_idx.at(start); i < result_idx.at(start + total_size); i++)
        {
            size_t start = cpt[i];
            size_t last = cpt[i + 1];
            gsl_spmatrix *one_col = gsl_spmatrix_alloc_nzmax(N, 1, 40); // CSC format
            size_t count = 0;
            for (j = start; j < last; j++)
            {
                
                if(rpt[j] == lap.size() - 1 && datapt[j] < 0 && i != lap.size() - 1)
                {
                    //std::cout << "set to 0 at i: " << i << "\n";
                    count++;
                    continue;
                }
                gsl_spmatrix_set(one_col, rpt[j], 0, datapt[j]);
            }
            one_col->split = last - start - count;
            lap.at(i) = one_col;
        }
    }
    else
    {
        /* code */   
        auto future = std::async(std::launch::async, process_array, input, std::ref(result_idx), depth + 1, target, std::ref(lap), (total_size - 1) / 2 + start, (total_size - 1) / 2, core_id, core_end); 
        process_array(input, result_idx, depth + 1, target, lap, start, (total_size - 1) / 2, core_begin, core_id);

        // synchronize
        future.wait();

        // separator
        size_t i, j;
        size_t N = input->colPtr->size() - 1;
        std::vector<size_t> &cpt = *(input->colPtr);
        std::vector<size_t> &rpt = *(input->rowIdx);
        std::vector<double> &datapt = *(input->val);
        for (i = result_idx.at(start + total_size - 1); i < result_idx.at(start + total_size); i++)
        {
            size_t start = cpt[i];
            size_t last = cpt[i + 1];
            gsl_spmatrix *one_col = gsl_spmatrix_alloc_nzmax(N, 1, 90); // CSC format
            size_t count = 0;
            for (j = start; j < last; j++)
            {
                
                if(rpt[j] == lap.size() - 1 && datapt[j] < 0 && i != lap.size() - 1)
                {
                    //std::cout << "set to 0 at i: " << i << "\n";
                    count++;
                    continue;
                }
                    
                gsl_spmatrix_set(one_col, rpt[j], 0, datapt[j]);
            }
            one_col->split = last - start - count;
            lap.at(i) = one_col;
        }

        
    }
    
}




bool compare (Sample i, Sample j)
{
    return (i.data < j.data);
}




