//
// Created by adrian on 17/04/18.
//

#ifndef GRACT3D_DATASET_GRID_HPP
#define GRACT3D_DATASET_GRID_HPP

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


    class dataset_grid {


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

        static void init(std::ifstream &stream, double_t &x_o, double_t &y_o, uint64_t &t_start){
            x_o =  std::numeric_limits<double_t>::max();
            y_o = std::numeric_limits<double_t>::max();
            t_start =  std::numeric_limits<uint64_t>::max();
            while(stream) {
                std::vector<std::string> values = read_next_row(stream);
                if (stream.eof()) break; //last-line toca narices
                uint64_t t = std::strtoull(values[1].c_str(), NULL, 0);
                double_t x_degree = std::strtod(values[2].c_str(), NULL);
                double_t y_degree = std::strtod(values[3].c_str(), NULL);
                if(x_degree < x_o) x_o = x_degree;
                if(y_degree < y_o) y_o = y_degree;
                if(t < t_start) t_start = t;
            }
            stream.close();
        }

        static void stats(std::ifstream &stream){
            uint64_t id = 0, id_prev = -1, t, t_prev;
            uint64_t x, x_prev, y, y_prev;
            uint64_t max_x = 0, max_y = 0;
            uint64_t max_t = 0, ids = 0;
            double_t max_speed = 0, n_objects = 0;
            uint64_t line = 0;
            while(stream){
                stream >> id >> t >> x >> y;
                if(stream.eof()) break;
                if(id != id_prev) ++ids;
                if(t > max_t) max_t = t;
                if(x > max_x) max_x = x;
                if(y > max_y) max_y = y;
                if(line > 0 && id == id_prev){
                    double_t speed_x = std::abs(std::ceil((int64_t) (x-x_prev) / (double_t) (t - t_prev)));
                    if(max_speed < speed_x) max_speed = speed_x;
                    double_t speed_y = std::abs(std::ceil((int64_t) (y-y_prev) / (double_t) (t - t_prev)));
                    if(max_speed < speed_y) max_speed = speed_y;
                }
                if(line == 0 || id != id_prev) ++n_objects;
                id_prev = id;
                t_prev = t;
                x_prev = x;
                y_prev = y;
                line++;
            }
            std::cout << "---------------------------" << std::endl;
            std::cout << "           stats           " << std::endl;
            std::cout << "---------------------------" << std::endl;
            std::cout << "Maximum t: " << max_t << std::endl;
            std::cout << "Maximum x: " << max_x << std::endl;
            std::cout << "Maximum y: " << max_y << std::endl;
            std::cout << "Maximum s: " << max_speed << std::endl;
            std::cout << "Number ob: " << n_objects << std::endl;
            std::cout << "---------------------------" << std::endl;
            std::cout << std::endl;
        }

    public:

        static void transform(const std::string input_file, const std::string output_file,
                              const uint64_t t_interval, const uint64_t meters_x, const uint64_t meters_y){

            std::ifstream i_stream(input_file);
            std::ofstream o_stream(output_file);
            double_t x_o, y_o;
            uint64_t t_start;
            init(i_stream, x_o, y_o, t_start);
            i_stream.open(input_file);
            uint64_t t, t2=0;
            double x_degree, y_degree;
            bool first = true;
            uint64_t t_value, x_value, y_value, z_value, interval_value = 0;
            uint64_t t_p, x_p, y_p, z_p, id, id_p, line = 0;
            while(i_stream){
                std::vector<std::string> values = read_next_row(i_stream);
                if(i_stream.eof()) break; //last-line toca narices
                id = std::strtoull(values[0].c_str(), NULL, 0);
                t = std::strtoull(values[1].c_str(), NULL, 0);
                x_degree = std::strtod(values[2].c_str(), NULL);
                y_degree = std::strtod(values[3].c_str(), NULL);
                std::pair<double_t , double_t > dist = compute_distance(x_o, y_o, x_degree, y_degree);
                t_value = (t-t_start)/t_interval;
                if(line == 0 || id != id_p || t_value > interval_value){
                    interval_value = t_value;
                    x_value = (uint64_t) (std::ceil(dist.first/(double_t) meters_x));
                    y_value = (uint64_t) (std::ceil(dist.second/(double_t) meters_y));
                    o_stream << values[0] << " " << t_value << " " << x_value << " " << y_value << " " << std::endl;
                }
                id_p = id;
                line++;

            }
            i_stream.close();
            o_stream.close();
            std::cout << "t2: " << t2 << std::endl;
            std::cout << "tstart: " << t_start << std::endl;

            std::ifstream result_stream(output_file);
            stats(result_stream);
            result_stream.close();
        }

    };
}

#endif //GRACT3D_DATASET_GRID_HPP
