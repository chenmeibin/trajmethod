//
// Created by adrian on 17/04/18.
//

#ifndef GRACT3D_CLEAN_HPP
#define GRACT3D_CLEAN_HPP

#include <cmath>
#include <string>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <vector>
#include <geo_util.hpp>
#include <limits>

namespace gract_ct {


    class clean {


    private:

        static std::vector<std::string> read_next_row(std::ifstream &i_stream){
            std::string line;
            std::getline(i_stream, line);

            std::stringstream line_stream(line);
            std::string cell;

            std::vector<std::string> line_values;
            while(std::getline(line_stream, cell, ','))
            {
                line_values.push_back(cell);
            }
            if (!line_stream && cell.empty())
            {
                // If there was a trailing comma then add an empty element.
                line_values.push_back("");
            }
            return line_values;
        }

        static long to_seconds(const std::string date_string){
            std::istringstream ss(date_string);
            std::tm t{};
            ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
            return mktime(&t);

        }

    public:

        static void run(const std::string input_file, const std::string output_file){

            std::ifstream i_stream(input_file);
            std::ofstream o_stream(output_file);
            uint64_t t_s, t_e;
            std::string t_start, t_end;
            float lon_start, lat_start, lon_end, lat_end;
            while(i_stream){
                std::vector<std::string> values = read_next_row(i_stream);
                if(i_stream.eof()) break; //last-line toca narices
                t_start   = values[1];
                t_end     = values[2];
                for(uint64_t i = 0; i < values.size(); i++){
                    std::cout << "i: " << values[i] << std::endl;
                }
                lon_start = std::stof(values[5]);
                lat_start = std::stof(values[6]);
                lon_end   = std::stof(values[9]);
                lat_end   = std::stof(values[10]);
                t_s = static_cast<uint64_t >(to_seconds(t_start));
                t_e = static_cast<uint64_t >(to_seconds(t_end));
                o_stream << t_s << "," << t_e << "," << lon_start << ","
                << lat_start << "," << lon_end << "," << lat_end << std::endl;
            }
            i_stream.close();
            o_stream.close();
        }

    };
}

#endif //GRACT3D_DATASET_GRID_HPP
