//
// Created by adrian on 12/11/18.
//

#include <iostream>
#include <index_ct.hpp>
#include <dataset_rw.hpp>

using namespace gract_ct;

void help(){

    std::cout << "GraCT_run_queries: run queries over a GraCT index" << std::endl;
    std::cout << "Usage: GraCT_run_queries <index> <queries>" << std::endl;
    std::cout << "  <index>     input index file" << std::endl;
    std::cout << "  <queries>   input queries file" << std::endl;
    exit(0);
}




int main(int argc, char **argv) {


    if(argc == 3){
        std::string index_name = argv[1];
        std::string queries = argv[2];

        std::cout << "Loading index: " << index_name << std::endl;
        std::cout << "  -Queries: " << queries << std::endl;
        std::cout << std::endl;

        index_ct<> m_index_ct;
        sdsl::load_from_file(m_index_ct, index_name);


        std::ifstream finQ(queries);
        int64_t type_query;
        uint64_t id, maxX, maxY, minX, minY, k, tstart, tend;
        point p(0, 0);

        //-1
        auto run_search_object = [&](){
            finQ >> id >> tstart >> type_query;
            std::cout << "so" << std::endl;
            m_index_ct.search_object(id, tstart, p);
        };

        //-2
        auto run_search_trajectory = [&](){
            std::vector<data_traj> trajectory;
            finQ >> id >> tstart >> tend >> type_query;
            std::cout << "traj" << std::endl;
            m_index_ct.search_trajectory(id, tstart, tend, trajectory);
        };

        //-3
        auto run_time_slice = [&](){
            finQ >> tstart >> minX >> maxX >> minY >> maxY >> type_query;
            std::cout << "ts" << std::endl;
            m_index_ct.time_slice(tstart, region(minX, minY, maxX, maxY));
        };

        //-4
        auto run_time_interval = [&](){
            finQ >> tstart >> tend >> minX >> maxX >> minY >> maxY >> type_query;
            std::cout << "ti" << std::endl;
            m_index_ct.time_interval(tstart, tend, region(minX, minY, maxX, maxY));
        };

        //-5
        auto run_knn = [&](){
            finQ >> tstart >> minX >> minY >> k >> type_query;
            std::cout << "knn" << std::endl;
            m_index_ct.find_knn(k, point(minX, minY), tstart, 10);
        };

        //-6
        auto run_mbr = [&]() {
           finQ >> id >> tstart >> tend >> type_query;
           std::cout << "mbr" << std::endl;
           region mbr;
           m_index_ct.search_mbr(id, tstart, tend, mbr);
        };


        while(finQ){

           finQ >> type_query;
           switch(type_query){
               case -1:
                   run_search_object();
                   break;
               case -2:
                   run_search_trajectory();
                   break;
               case -3:
                   run_time_slice();
                   break;
               case -4:
                   run_time_interval();
                   break;
               case -5:
                   run_knn();
                   break;
               case -6:
                   run_mbr();
                   break;
               default:
                   std::cout << "Query not implemented" << std::endl;
                   break;
           }
        }

        finQ.close();
        sdsl::util::clear(m_index_ct);
        std::cout << "Done." << std::endl;
    }else{
        help();
    }

}