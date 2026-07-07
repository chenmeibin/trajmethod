//
// Created by adrian on 12/11/18.
//

#include <iostream>
#include <index_ct.hpp>
#include <dataset_rw.hpp>

using namespace gract_ct;

void help(){

    std::cout << "GraCT_create_index: builds the GraCT" << std::endl;
    std::cout << "Usage: GraCT_create_index <dataset> <period> <index>" << std::endl;
    std::cout << "  <dataset>  input dataset file" << std::endl;
    std::cout << "  <period>   distance between snapshots" << std::endl;
    std::cout << "  <index>    output index file" << std::endl;
    exit(0);
}


int main(int argc, char **argv) {

    if(argc == 4){
        std::string dataset_name = argv[1];
        uint period_snapshot = (uint) atoi(argv[2]);
        std::string index_name = argv[3];

        std::cout << "Building index: " << index_name << std::endl;
        std::cout << "  -Dataset: " << dataset_name << std::endl;
        std::cout << "  -Period snapshot: " << period_snapshot << std::endl;
        std::cout << std::endl;
        dataset_rw m_dataset_rw(dataset_name);
        std::vector<data_traj> dataset = m_dataset_rw.read();
        index_ct<> m_index_ct(dataset, period_snapshot, false);
        sdsl::store_to_file(m_index_ct, index_name);
        sdsl::util::clear(m_index_ct);
        std::cout << "Done." << std::endl;
    }else{
        help();
    }

}