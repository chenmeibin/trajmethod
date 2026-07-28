/***
BSD 2-Clause License

Copyright (c) 2018, Adrián
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/


//
// Created by Adrián on 09/03/2019.
//

#ifndef GRACT_KNN_SUPPORT_HELPER_HPP
#define GRACT_KNN_SUPPORT_HELPER_HPP

#include <geometry.hpp>
#include <queue>

namespace gract_ct {

    namespace knn_support_helper {

        typedef uint64_t size_type;
        typedef uint64_t distance_type;

        struct knn_element {

            bool is_point = false;
            bool is_leaf = false;

            size_type id;
            point min;
            point max;
            distance_type distance;
            distance_type distance_max;

            size_type t;
            size_type log_idx = 0;
            size_type log_pos = 0;
            size_type mov_pos = 0;
            size_type tim_pos = 0;
            size_type level = 0;

            knn_element(size_type _id, point _p, distance_type _dist, distance_type _dist_max,
                        size_type _t, size_type _log_idx, size_type _log_pos,
                        size_type _mov_pos, size_type _tim_pos, bool _leaf, bool _obj){
                is_point = _obj;
                is_leaf = _leaf;
                id = _id;
                min = _p;
                distance = _dist;
                distance_max = _dist_max;
                t = _t;
                log_idx = _log_idx;
                log_pos = _log_pos;
                mov_pos = _mov_pos;
                tim_pos = _tim_pos;
            }

            knn_element(size_type _id, point _min, point _max, distance_type _dist,
                        distance_type _dist_max, size_type _t, size_type _level){
                is_point = false;
                is_leaf = false;
                id = _id;
                min = _min;
                max = _max;
                distance = _dist;
                distance_max = _dist_max;
                t = _t;
                level = _level;
            }
        };

        class knn_element_comparator {
        public:
            bool operator()(const knn_element &lce, const knn_element &rce){
                if(lce.distance == rce.distance) return lce.distance_max > rce.distance_max;
                return lce.distance > rce.distance;
            }
        };

        typedef std::priority_queue<knn_element, std::vector<knn_element>, knn_element_comparator> pq_knn_element_type;
        typedef std::priority_queue<uint64_t , std::vector<uint64_t >, std::less<uint64_t >> pq_knn_movement_type;


    }

}

#endif //GRACT_KNN_SUPPORT_HELPER_HPP
