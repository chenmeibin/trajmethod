//
// Created by adrian on 27/04/17.
//

#ifndef GRACT_DATASET_READER_HPP
#define GRACT_DATASET_READER_HPP

#include <string>
#include <vector>
#include <fstream>
#include "data_traj.hpp"

namespace gract_ct {

    class dataset_rw {

    private:
        std::string m_dataset;
    public:
        dataset_rw(std::string file){
            m_dataset = file;
        };

        std::vector<data_traj> read(){
            std::ifstream fin(m_dataset);
            uint64_t t;
            uint32_t id, x, y;
            std::vector<data_traj> result;
            while (fin) {
                //File format
                fin >> id >> t >> x >> y ;
                if(fin.eof()) break; //last-line toca narices
                //if(id < 4){
                    result.push_back(data_traj(id, t, x ,y));
                //}
            }
            fin.close();
            return result;
        }

    };
};

#endif //GRACT_DATASET_READER_HPP
