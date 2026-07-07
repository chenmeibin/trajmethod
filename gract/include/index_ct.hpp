//
// Created by adrian on 26/12/17.
//

#ifndef GRACTV2_INDEX_CT_HPP
#define GRACTV2_INDEX_CT_HPP

#include <vector>
#include <geometry.hpp>
#include <data_traj.hpp>
#include <snapshot.hpp>
#include <log_object.hpp>
#include <global_constants.hpp>
#include <spiral_matrix_coder.hpp>
#include <split_repair_helper.hpp>
#include <alternative_coder.hpp>
#include <repair.hpp>
#include "sys/types.h"
#include <util_mem.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/util.hpp>
#include <dac.hpp>


#include <search_object_support_algorithm.hpp>
#include <time_slice_support_algorithm.hpp>
#include <time_interval_support_algorithm.hpp>
#include <search_trajectory_support_algorithm.hpp>
#include <knn_support_algorithm.hpp>
#include <mbr_support_algorithm.hpp>

#define SIZE_INT_VECTOR 64
#define PRINT_STATS 0


namespace gract_ct {

    template<uint32_t k = 2>
    class index_ct{

    public:
        typedef uint64_t size_type;
        typedef uint64_t value_type;
        typedef snapshot<k> snapshot_type;
    private:

        size_type m_period_snapshot;
        size_type m_total_objects;
        size_type m_t_max;
        size_type m_x_max;
        size_type m_y_max;
        size_type m_speed_max;
        size_type m_level_max;
        std::vector<snapshot<k>> m_snapshots;
        std::vector<sdsl::int_vector<>> m_reap;
        std::vector<sdsl::int_vector<>> m_disap;
        uint32_t m_alpha;
        uint32_t m_zero_value;
        std::vector<uint64_t> m_translate_table;
        sdsl::int_vector<32> m_rules;
        dac_vector_dp_v2<> m_extra_info_time;
        dac_vector_dp_v2<> m_extra_info_pos;
        std::vector<log_object> m_log_object;

    public:
        const std::vector<sdsl::int_vector<>>& reap = m_reap;
        const std::vector<sdsl::int_vector<>>& disap = m_disap;
        const size_type &period_snapshot = m_period_snapshot;
        const size_type &total_objects = m_total_objects;
        const size_type &t_max = m_t_max;
        const size_type &x_max = m_x_max;
        const size_type &y_max = m_y_max;
        const size_type &speed_max = m_speed_max;
        const size_type &level_max = m_level_max;

    private:

        void copy(const index_ct& p){
            m_period_snapshot = p.m_period_snapshot;
            m_total_objects = p.m_total_objects;
            m_t_max = p.m_t_max;
            m_x_max = p.m_x_max;
            m_y_max = p.m_y_max;
            m_speed_max = p.m_speed_max;
            m_level_max = p.m_level_max;
            m_snapshots = p.m_snapshots;
            m_reap = p.m_reap;
            m_disap = p.m_disap;
            m_alpha = p.m_alpha;
            m_zero_value = p.m_zero_value;
            m_translate_table = p.m_translate_table;
            m_rules = p.m_rules;
            m_extra_info_pos = p.m_extra_info_pos;
            m_extra_info_time = p.m_extra_info_time;
            m_log_object = p.m_log_object;
        }


        void _get_stats(const std::vector<data_traj> &dataset){
            m_total_objects = 0, m_speed_max = 0, m_t_max= 0, m_x_max = 0, m_y_max = 0;
            value_type prev_id = -1;
            size_type prev_x, prev_y, prev_t;
            for(size_type i = 0; i < dataset.size(); ++i){
                auto v = dataset[i];
                if(prev_id != v.m_id) ++m_total_objects;
                if(i > 0){
                    if(prev_id == v.m_id){
                        //std::cout << v.m_id << " " << prev_t << " " << v.m_t << std::endl;
                        uint64_t x_diff = std::abs((int64_t) (v.m_x - prev_x)) / (v.m_t - prev_t);
                        uint64_t y_diff = std::abs((int64_t) (v.m_y - prev_y)) / (v.m_t - prev_t);
                        if(x_diff > m_speed_max) {
                            m_speed_max = x_diff;
                        }
                        if(y_diff > m_speed_max) {
                            m_speed_max = y_diff;
                        }
                    }
                }
                if(v.m_x > m_x_max) m_x_max = v.m_x;
                if(v.m_y > m_y_max) m_y_max = v.m_y;
                if(v.m_t > m_t_max) m_t_max = v.m_t;
                prev_x = v.m_x;
                prev_y = v.m_y;
                prev_id = v.m_id;
                prev_t = v.m_t;
            }
            m_x_max++; m_y_max++;
            size_type max_xy = std::max(m_x_max, m_y_max);
            m_level_max = (size_type) std::ceil(log2(max_xy)/log2(k));
#if PRINT_STATS
            std::cout << "Total objects: " << m_total_objects << std::endl;
            std::cout << "Max speed: " << m_speed_max << std::endl;
            std::cout << "Max x: " << m_x_max << std::endl;
            std::cout << "Max y: " << m_y_max << std::endl;
            std::cout << "Max t: " << m_t_max << std::endl;
            std::cout << "Level max: " << m_level_max << std::endl;
#endif
        }

        void _prepare_log(const std::vector<data_traj> &dataset, const size_type n_snapshots,
                          std::vector<k2_tree_representation_lite<k> > &trees,
                          std::vector<int64_t> &log,
                          std::vector<std::vector<int64_t>> &ref_objects,
                          std::vector<std::vector<int64_t>> &reap_movement_objects,
                          std::vector<std::vector<int64_t>> &reap_time_objects,
                          std::vector<sdsl::bit_vector> &exist_log){

            std::cout << "preparing log" << std::endl;
            size_type snap_prev_idx = 0, t_prev = 0, oid_prev = 1, x_prev = 0, y_prev = 0;
            size_type snap_idx = 0, t = 0, oid = 0, x = 0, y = 0;

            m_reap = std::vector<sdsl::int_vector<>>(n_snapshots);
            m_disap = std::vector<sdsl::int_vector<>>(n_snapshots);
            std::vector<std::vector<uint64_t>> reap_temp(n_snapshots);
            std::vector<std::vector<uint64_t>> disap_temp(n_snapshots);
            reap_movement_objects = std::vector<std::vector<int64_t>>(total_objects, std::vector<int64_t>(n_snapshots, 0));
            reap_time_objects = std::vector<std::vector<int64_t>>(total_objects, std::vector<int64_t>(n_snapshots, 0));


            auto log_disappearance = [&](){
                log.push_back(DISAPPEARANCE);
                ref_objects[oid_prev].push_back(DISAPPEARANCE);
                reap_movement_objects[oid_prev].push_back(x_prev);
                reap_movement_objects[oid_prev].push_back(y_prev);
                reap_time_objects[oid_prev].push_back((snap_prev_idx+1)*period_snapshot-t_prev);
                if(snap_prev_idx+1 < n_snapshots){
                    reap_movement_objects[oid_prev][snap_prev_idx+1] = reap_movement_objects[oid_prev].size();
                    reap_time_objects[oid_prev][snap_prev_idx+1] = reap_time_objects[oid_prev].size();
                }
            };

            auto log_reappearance = [&](){
                log.push_back(REAPPEARANCE);
                ref_objects[oid].push_back(REAPPEARANCE);
                reap_movement_objects[oid][snap_idx] = reap_movement_objects[oid].size();
                reap_movement_objects[oid].push_back(x);
                reap_movement_objects[oid].push_back(y);
                reap_time_objects[oid][snap_idx] = reap_time_objects[oid].size();
                reap_time_objects[oid].push_back(t - snap_idx*period_snapshot);
            };

            auto log_reap_move = [&](){
                log.push_back(REAP_MOVE);
                ref_objects[oid].push_back(REAP_MOVE);
                auto code  = spiral_matrix_coder::encode(x-x_prev, y-y_prev);
                if(reap_movement_objects[oid][snap_idx]==0){
                    reap_movement_objects[oid][snap_idx] = reap_movement_objects[oid].size();
                }
                if(reap_time_objects[oid][snap_idx]==0){
                    reap_time_objects[oid][snap_idx] = reap_time_objects[oid].size();
                }
                reap_movement_objects[oid].push_back(code);
                reap_time_objects[oid].push_back(t - t_prev);
            };

            auto log_reap_no_move = [&](){
                log.push_back(REAP_NO_MOVE);
                ref_objects[oid].push_back(REAP_NO_MOVE);
                if(reap_time_objects[oid][snap_idx]==0){
                    reap_time_objects[oid][snap_idx] = reap_time_objects[oid].size();
                }
                reap_time_objects[oid].push_back(t - t_prev);
            };

            auto log_object = [&](){
                log.push_back(OBJECT);
                ref_objects[oid].push_back(OBJECT);
            };

            auto log_snapshot = [&](){
                log.push_back(SNAPSHOT);
                ref_objects[oid].push_back(SNAPSHOT);
                reap_movement_objects[oid][snap_idx] = reap_movement_objects[oid].size();
                reap_time_objects[oid][snap_idx] = reap_time_objects[oid].size();
            };


            for(size_type i = 0; i < dataset.size(); i++) {

                oid = dataset[i].m_id;
                t = dataset[i].m_t;
                x = dataset[i].m_x;
                y = dataset[i].m_y;
                //std::cout << "oid: " << oid << " t:" << t << " x:" << dataset[i].m_x << " y:" << dataset[i].m_y << std::endl;
                snap_idx = t / period_snapshot;
                snap_prev_idx = t_prev / period_snapshot;

                //Adding information to the log
                if(oid_prev != oid){
                    if(i>0 && t_prev % period_snapshot > 0){
                        //Add the info to the disappeareance
                        exist_log[oid_prev][snap_prev_idx]=1;
                        log_disappearance();
                        disap_temp[snap_prev_idx].push_back(oid_prev);
                    }
                    log_object();
                    //If the time instant belongs to a snapshot we add it
                    if(t % period_snapshot == 0){ //1.2 Add to snapshot
                        trees[snap_idx].insertObject(x, y, oid);
                        log_snapshot();
                        exist_log[oid][snap_idx]=1;
                    }else{
                        //Mark in bitmap
                        exist_log[oid][snap_idx]=1;
                        log_reappearance();
                        reap_temp[snap_idx].push_back(oid);
                    }
                }else{
                    if(t - t_prev > 1){ //Disappearance
                        if(snap_idx > snap_prev_idx){ //Disappearance in different logs
                            if(t_prev % period_snapshot > 0){ //Check if the information should be stored in a snapshot
                                exist_log[oid][snap_prev_idx]=1;
                                log_disappearance();
                                if(t != (snap_prev_idx+1) * m_period_snapshot){
                                    //The object is not contained in the next snapshot
                                    disap_temp[snap_prev_idx].push_back(oid_prev);
                                }
                            }
                            if(t % period_snapshot > 0){  //Check if the information should be stored in a snapshot
                                exist_log[oid][snap_idx]=1;
                                log_reappearance();
                                reap_temp[snap_idx].push_back(oid);
                            }else{
                                trees[snap_idx].insertObject(x, y, oid);
                                log_snapshot();
                                exist_log[oid][snap_idx]=1;
                            }
                        }else{
                            if(x == x_prev && y == y_prev){
                                log_reap_no_move();
                            }else{
                                log_reap_move();
                            }
                            exist_log[oid][snap_idx]=1;
                        }
                    }else{
                        auto code  = spiral_matrix_coder::encode(x-x_prev, y-y_prev);
                        log.push_back(code);
                        if(t % period_snapshot == 0){
                            trees[snap_idx].insertObject(x, y, oid);
                            log_snapshot();
                        }
                        exist_log[oid][snap_idx]=1;
                    }
                }
                x_prev = x; y_prev = y; t_prev = t;
                oid_prev = oid;
            }
            if(t_prev % period_snapshot > 0){
                //Add the info to the disappeareance
                exist_log[oid_prev][snap_prev_idx]=1;
                log_disappearance();
                disap_temp[snap_prev_idx].push_back(oid_prev);
            }

            for(size_type i = 0; i < n_snapshots; i++){
                m_disap[i].resize(disap_temp[i].size());
                m_reap[i].resize(reap_temp[i].size());
                size_type j = 0;
                for(const auto &v : reap_temp[i]){
                    m_reap[i][j] = v;
                    ++j;
                }
                j = 0;
                for(const auto &v : disap_temp[i]){
                    m_disap[i][j] = v;
                    ++j;
                }
                sdsl::util::bit_compress(m_reap[i]);
                sdsl::util::bit_compress(m_disap[i]);
            }
            disap_temp.clear();
            reap_temp.clear();

        }



        void _add_splits(std::vector<int64_t> &log, std::vector<int64_t> &terminals, std::map<int64_t, char> &map_terminals){
            split_repair_helper split_helper(&log, &terminals, &map_terminals);
            for(size_type i = 0; i < log.size(); i++){
                if(log[i]<0){
                    split_helper.add_split_at(i);
                }else{
                    if(map_terminals.find(log[i]) == map_terminals.end()){
                        terminals.push_back(log[i]);
                        map_terminals.insert(std::pair<int64_t, u_char>(log[i], 'a'));
                    }
                }
            }
        }

        uint32_t _zero_value(const std::vector<int64_t> &terminals){
            for(uint32_t i = 0; i < terminals.size(); i++){
                if(terminals[i] == 0) return i;
            }
            return 0;
        }

        uint32_t _translate_log(const std::vector<int64_t> &log, const std::vector<int64_t> &terminals,
                                 int* &log_repair){

            std::map<int64_t, int32_t> map_terminals;
            for(int32_t i = 0; i < terminals.size(); i++){
                map_terminals.insert(std::pair<int64_t, int32_t>(terminals[i], i));

            }

            std::map<int64_t, int32_t>::iterator it_map_terminals;
            for(size_type i = 0; i < log.size(); i++){
                it_map_terminals = map_terminals.find(log[i]);
                if(it_map_terminals != map_terminals.end()){
                    log_repair[i] = it_map_terminals->second;
                }else{
                    std::cout << "error1" << std::endl;
                    exit(20);
                }
            }

            uint32_t zero_value = 0;
            it_map_terminals = map_terminals.find(0);
            if(it_map_terminals != map_terminals.end()){
                zero_value = (uint32_t) it_map_terminals->second;
            }else{
                std::cout << "error2" << std::endl;
                exit(20);
            }
            return zero_value;
        }



        template <class Container>
        void _adjust_header_info(Container &log_objects, const uint64_t n_snapshots){
            for(uint64_t oid = 0; oid < total_objects; oid++){
                //Skip first zeroes
                uint64_t lower_bound = 0, upper_bound = 0;
                while(lower_bound < n_snapshots && log_objects[oid][lower_bound]==0){
                    lower_bound++;
                    upper_bound++;
                }
                while(lower_bound < n_snapshots){
                    //Looking for the next zero
                    while(log_objects[oid][lower_bound] > 0 && lower_bound < n_snapshots){
                        lower_bound++;
                        upper_bound++;
                    }
                    //Looking for the next number distinct to zero
                    while(log_objects[oid][upper_bound] == 0 && upper_bound < n_snapshots){
                        upper_bound++;
                    }
                    int64_t upper_value = log_objects[oid].size();
                    if(upper_bound < n_snapshots){
                        upper_value = log_objects[oid][upper_bound];
                    }
                    while(lower_bound < upper_bound){
                        log_objects[oid][lower_bound] = upper_value;
                        lower_bound++;
                    }
                }
            }
        }

        void _build_log_objects( const std::vector<std::vector<int64_t>> &ref_objects,
                                 const std::vector<sdsl::bit_vector> &exist_log,
                                 const repair &m_repair_algo,
                                 const uint64_t n_snapshots,
                                 std::vector<std::vector<int64_t>> &log_objects){
            size_type oid = 0;
            size_type ref_index = 1, exist_index = 0, log_index = 0;
            bool change = false;
            for(size_type i = 1; i < m_repair_algo.lenC && oid < total_objects; i++){
                //std::cout << "c: " << (int32_t) (m_repair_algo.c[i]) << std::endl;
                if(m_repair_algo.c[i] < m_zero_value){
                    if(ref_index >= ref_objects[oid].size()){
                        ref_index = 1;
                        exist_index = 0;
                        oid++;
                        //if(oid == 236) exit(20);
                        change = false;
                    }else{
                        if(ref_objects[oid][ref_index] == SNAPSHOT){
                            if(change){
                                exist_index++;
                            }
                            while(!exist_log[oid][exist_index]){
                                exist_index++;
                            }
                            change = true;
                        }else if (ref_objects[oid][ref_index] == REAPPEARANCE){
                            //std::cout << "REAP" << std::endl;
                            if(change){ /*Careful when appears a snapshot and there are no movement*/
                                exist_index++;
                            }
                            while(!exist_log[oid][exist_index]){
                                exist_index++;
                            }
                            log_objects[oid][exist_index] = log_objects[oid].size();
                            change = false;
                            exist_index++;
                            //Add to log
                            log_objects[oid].push_back(ref_objects[oid][ref_index]);
                        }else if (ref_objects[oid][ref_index] < REAPPEARANCE){
                            //Add to log
                            if(change){
                                log_objects[oid][exist_index] = log_objects[oid].size();
                                change = false;
                                exist_index++;
                            }
                            log_objects[oid].push_back(ref_objects[oid][ref_index]);
                        }
                        ref_index++;
                    }
                }else{
                    if(change){
                        log_objects[oid][exist_index] = log_objects[oid].size();
                        change = false;
                        exist_index++;
                    }
                    //Add to log
                    log_objects[oid].push_back(m_repair_algo.c[i]);
                }
            }
        }


        void _build_translate_table(const std::vector<int64_t >& values){
            m_translate_table = std::vector<uint64_t>(values.begin()+ m_zero_value, values.end());
        }

        void _get_extra_info_rule(int i, const repair &m_repair_algo, uint64_t &t, int64_t &x, int64_t &y,
                         int64_t &m_x, int64_t &m_y, int64_t &M_x, int64_t &M_y) {
            while (i >= m_alpha) {
                _get_extra_info_rule(m_repair_algo.rules[i-m_alpha].left, m_repair_algo,
                                           t, x, y, m_x, m_y, M_x, M_y);
                i =  m_repair_algo.rules[i-m_alpha].right;
            }
            //Leaves
            if(i < m_alpha){
                //Increase time
                t++;
                //Translate and decode the positions
                auto point = spiral_matrix_coder::decode(m_translate_table[i-m_zero_value]);
                //Compute the movement
                int64_t p_x = point.first;
                int64_t p_y = point.second;
                x = x + p_x;
                y = y + p_y;
                //Compute MBR
                if(m_x > x){
                    m_x = x;
                }
                if(m_y > y){
                    m_y = y;
                }
                if(M_x < x){
                    M_x = x;
                }
                if(M_y < y){
                    M_y = y;
                }
            }
        }

        void _add_extra_info_rules(const repair &m_repair_algo){

            std::vector<uint64_t> extra_info_time(m_repair_algo.lenR);
            std::vector<uint64_t> extra_info_pos(m_repair_algo.lenR * 6);
            m_rules.resize(m_repair_algo.lenR*2);
            for(uint32_t i = 0; i<m_repair_algo.lenR; i++){
                uint64_t t = 0;
                int64_t x = 0, y = 0, m_x = 0, m_y = 0, M_x = 0, M_y = 0;
                int32_t rule = i +  m_alpha;
                m_rules[2*i]= (uint32_t) m_repair_algo.rules[i].left;
                m_rules[2*i+1] = (uint32_t) m_repair_algo.rules[i].right;
                _get_extra_info_rule(rule, m_repair_algo, t, x, y, m_x, m_y, M_x, M_y);
                extra_info_time[i] = t;
                extra_info_pos[6*i] = alternative_coder::encode(x);
                extra_info_pos[6*i+1] = alternative_coder::encode(y);
                extra_info_pos[6*i+2] = alternative_coder::encode(m_x);
                extra_info_pos[6*i+3] = alternative_coder::encode(M_x);
                extra_info_pos[6*i+4] = alternative_coder::encode(m_y);
                extra_info_pos[6*i+5] = alternative_coder::encode(M_y);
            }
            m_extra_info_pos = dac_vector_dp_v2<>(extra_info_pos);
            m_extra_info_time = dac_vector_dp_v2<>(extra_info_time);
        }

        template <class Container>
        uint64_t size_vector(Container &vector){
            uint64_t size = 0;
            for(auto v : vector){
                size += sdsl::size_in_bytes(v);
            }
            return size;
        }

    public:

        const uint32_t &alpha = m_alpha;
        const uint32_t &zero_value = m_zero_value;
        const std::vector<uint64_t> &translate_table = m_translate_table;
        //struct sysinfo memInfo;

        index_ct(){};

        index_ct(std::vector<data_traj> &dataset, const size_type period_snapshot,
                 const bool clean_dataset = true){

            m_period_snapshot = period_snapshot;
            _get_stats(dataset);
            size_type n_snapshots = util::ceil_div(t_max, period_snapshot);
            std::vector<int64_t> log, terminals;
            std::map<int64_t, char> map_terminals;
            int* log_repair;
            std::vector<k2_tree_representation_lite<k> > trees(n_snapshots);
            for(size_type i = 0; i < trees.size(); ++i){
                trees[i].maxLevel = m_level_max;
            }
            std::vector<sdsl::bit_vector> exist_log(total_objects, sdsl::bit_vector(n_snapshots, 0));
            std::vector<std::vector<int64_t>> ref_objects(total_objects, std::vector<int64_t>());
            std::vector<std::vector<int64_t>> reap_movement_objects(total_objects, std::vector<int64_t>());
            std::vector<std::vector<int64_t>> reap_time_objects(total_objects, std::vector<int64_t>());
            std::vector<std::vector<int64_t>> log_objects(total_objects, std::vector<int64_t>(n_snapshots, 0));
            //1. We prepare the log with the different types of marks
            _prepare_log(dataset, n_snapshots, trees, log, ref_objects, reap_movement_objects,
                         reap_time_objects, exist_log);
            if(clean_dataset) {
                sdsl::util::clear(dataset);
            }
            m_snapshots = std::vector<snapshot<k>>(n_snapshots);
            for(size_type i = 0; i < n_snapshots; i++) {
                m_snapshots[i] = snapshot<k>(trees[i], total_objects);
            }
            trees.clear();

            //2. We add splits, avoiding the creation of new non-terminals with disappearances, reappearances,
            // snapshots and end of log.
            _add_splits(log, terminals, map_terminals);
            //map_terminals.clear();
            sort(terminals.begin(), terminals.end());
            log_repair = new int[log.size()];
            size_type length_log = log.size();
            m_zero_value = _translate_log(log, terminals, log_repair);

            log.clear();
            //3. We apply repair over the log
            repair m_repair_algo;
            /*sysinfo(&memInfo);
            std::cout << memInfo.freeram << std::endl;
            int total_MB = (int) ((memInfo.freeram  >> 20) * 0.8);*/
            int total_MB = ::util::memory::total_memory_megabytes() * 0.8;
            /*std::ofstream out_log_repair("log_repair.txt");
            for(int i = 0; i < length_log; ++i){
                out_log_repair.write((char*) &(log_repair[i]), sizeof(int));
            }
            out_log_repair.close();*/

            m_repair_algo.run(log_repair, length_log, terminals, total_MB);
            m_alpha =  (uint32_t) m_repair_algo.alpha;
            //4. We build the temporal logs
            _build_log_objects(ref_objects, exist_log, m_repair_algo,  n_snapshots, log_objects);
            _adjust_header_info(log_objects, n_snapshots);
            _adjust_header_info(reap_movement_objects, n_snapshots);
            _adjust_header_info(reap_time_objects, n_snapshots);
            //5. Create translate table
            _build_translate_table(terminals);


            //6. We add extra information to the rules
            _add_extra_info_rules(m_repair_algo);
            //7. Compress data
            for(size_type oid = 0; oid < total_objects; oid++){
                m_log_object.push_back(log_object(log_objects[oid], reap_movement_objects[oid], reap_time_objects[oid]));
            }
            log_objects.clear();
            reap_movement_objects.clear();
            reap_time_objects.clear();
            m_repair_algo.clear();
        }

        bool search_object(const uint64_t oid, const uint64_t t_q, point &p){
            return search_object_run(oid, t_q, period_snapshot, m_snapshots.size(), p, this);
        }

        void search_trajectory(const uint64_t oid, const uint64_t t_start, const uint64_t t_end, std::vector<data_traj> &traj){
             search_trajectory_run(oid, t_start, t_end, period_snapshot, m_snapshots.size(), this, traj);
        }

        void search_mbr(const uint64_t oid, const uint64_t t_start, const uint64_t t_end, region &mbr_result){
            search_mbr_run(oid, t_start, t_end, period_snapshot, m_snapshots.size(), this, mbr_result);
        }

        std::vector<id_point> time_slice(const uint64_t t_q, const region &r){
            return time_slice_run(r, t_q, period_snapshot, m_snapshots.size(), total_objects, speed_max, x_max, y_max, this);
        }

        std::vector<uint64_t> time_interval(const uint64_t t_start, const uint64_t t_end, const region &r){
           return time_interval_run(r, t_start, t_end, period_snapshot, m_snapshots.size(), total_objects, speed_max, x_max, y_max, this);
        }

        std::vector<uint64_t> find_knn(const uint32_t k_param, const point &p_q, const uint64_t t_q, const uint64_t steps){
          return knn_run(k_param, p_q, t_q, steps, period_snapshot, m_snapshots.size(), total_objects, speed_max, x_max, y_max, this, false);
        }


        const snapshot<k>* get_snapshot(const uint64_t idx){
            return &(m_snapshots[idx]);
        }

        const snapshot<k>* get_snapshot(const uint64_t idx) const{
            return &(m_snapshots[idx]);
        }

        uint64_t translate(const uint32_t value) const{
            return m_translate_table[value - m_zero_value];
        }


        const log_object* get_log_object(const uint64_t oid){
            return &(m_log_object[oid]);
        }

        const log_object* get_log_object(const uint64_t oid) const{
            return &(m_log_object[oid]);
        }

        uint32_t get_left(const uint64_t rule) const {
            uint64_t pos = (rule - m_alpha) * 2;
            return (uint32_t) m_rules[pos];
        }

        uint32_t get_right(const uint64_t rule) const {
            uint64_t pos = (rule - m_alpha) * 2 + 1;
            return (uint32_t) m_rules[pos];
        }

        uint32_t get_rule_time(const uint64_t rule) const{
            return (uint32_t) m_extra_info_time[rule - m_alpha];
        }

        point get_rule_point(const uint64_t rule) const{
            uint64_t pos = (rule - m_alpha) * 6;
            return point(alternative_coder::decode(m_extra_info_pos[pos]), alternative_coder::decode(m_extra_info_pos[pos + 1]));
        }

        region get_rule_mbr(const uint64_t rule) const{
            uint64_t pos = (rule - m_alpha) * 6;
            return region(alternative_coder::decode(m_extra_info_pos[pos+2]), alternative_coder::decode(m_extra_info_pos[pos+4]),
                          alternative_coder::decode(m_extra_info_pos[pos+3]), alternative_coder::decode(m_extra_info_pos[pos+5]));
        }


        //! Assignment move operation
        index_ct& operator=(index_ct&& p) {
            if (this != &p) {
                m_period_snapshot = std::move(p.m_period_snapshot);
                m_total_objects = std::move(p.m_total_objects);
                m_t_max = std::move(p.m_t_max);
                m_x_max = std::move(p.m_x_max);
                m_y_max = std::move(p.m_y_max);
                m_speed_max = std::move(p.m_speed_max);
                m_level_max = std::move(p.m_level_max);
                m_snapshots = std::move(p.m_snapshots);
                m_reap = std::move(p.m_reap);
                m_disap = std::move(p.m_disap);
                m_alpha = std::move(p.m_alpha);
                m_zero_value = std::move(p.m_zero_value);
                m_rules = std::move(p.m_rules);
                m_translate_table = std::move(p.m_translate_table);
                m_extra_info_pos = std::move(p.m_extra_info_pos);
                m_extra_info_time = std::move(p.m_extra_info_time);
                m_log_object = std::move(p.m_log_object);
            }
            return *this;
        }

        //! Assignment operator
        index_ct& operator=(const index_ct& p)
        {
            if (this != &p) {
                copy(p);
            }
            return *this;
        }

        //! Copy constructor
        index_ct(const index_ct& p)
        {
            copy(p);
        }

        //! Move constructor
        index_ct(index_ct&& p)
        {
            *this = std::move(p);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(index_ct& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            std::swap(m_period_snapshot, p.m_period_snapshot);
            std::swap(m_total_objects, p.m_total_objects);
            std::swap(m_t_max, p.m_t_max);
            std::swap(m_x_max, p.m_x_max);
            std::swap(m_y_max, p.m_y_max);
            std::swap(m_speed_max, p.m_speed_max);
            std::swap(m_level_max, p.m_level_max);
            m_snapshots.swap(p.m_snapshots);
            m_reap.swap(p.m_reap);
            m_disap.swap(p.m_disap);
            m_alpha = p.m_alpha;
            m_zero_value = p.m_zero_value;
            m_rules.swap(p.m_rules);
            m_translate_table.swap(p.m_translate_table);
            m_extra_info_pos.swap(p.m_extra_info_pos);
            m_extra_info_time.swap(p.m_extra_info_time);
            m_log_object.swap(p.m_log_object);

        }

        //! Serializes the snapshot to a stream.
        /*!
         * \param out The outstream to which the data structure is written.
         * \return The number of bytes written to out.
         */
        uint64_t serialize(std::ostream& out, sdsl::structure_tree_node* v=nullptr, std::string name="")const
        {
            sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
            uint64_t written_bytes = 0;
            sdsl::write_member(m_snapshots.size(), out, child, "snap_size");
            written_bytes += sdsl::serialize_vector(m_snapshots, out, child, "snapshots");
            written_bytes += sdsl::serialize_vector(m_reap, out, child, "reaps");
            written_bytes += sdsl::serialize_vector(m_disap, out, child, "disaps");
            written_bytes += sdsl::write_member(m_alpha, out, child, "alpha");
            written_bytes += sdsl::write_member(m_zero_value, out, child, "zero_value");
            written_bytes += m_rules.serialize(out, child, "rules");
            written_bytes += m_extra_info_pos.serialize(out, child, "extra_info_pos");
            written_bytes += m_extra_info_time.serialize(out, child, "extra_info_time");
            sdsl::write_member(m_translate_table.size(), out, child, "translate_size");
            written_bytes += sdsl::serialize_vector(m_translate_table, out, child, "translate");
            sdsl::write_member(m_log_object.size(), out, child, "log_size");
            for(size_type i = 0; i < m_log_object.size(); i++){
                written_bytes += m_log_object[i].serialize(out, child, "log_objects");
            }
            written_bytes += sdsl::write_member(m_period_snapshot, out, child, "period_snapshot");
            written_bytes += sdsl::write_member(m_total_objects, out, child, "total_objects");
            written_bytes += sdsl::write_member(m_t_max, out, child, "t_max");
            written_bytes += sdsl::write_member(m_x_max, out, child, "x_max");
            written_bytes += sdsl::write_member(m_y_max, out, child, "y_max");
            written_bytes += sdsl::write_member(m_speed_max, out, child, "speed_max");
            written_bytes += sdsl::write_member(m_level_max, out, child, "level_max");
            //written_bytes += sdsl::serialize_vector(m_log_object, out, child, "log_objects");
            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }

        void load(std::istream &in){
            uint64_t m_snap_size = 0;
            sdsl::read_member(m_snap_size, in);
            m_snapshots.resize(m_snap_size);
            m_reap.resize(m_snap_size);
            m_disap.resize(m_snap_size);
            sdsl::load_vector(m_snapshots, in);
            sdsl::load_vector(m_reap, in);
            sdsl::load_vector(m_disap, in);
            sdsl::read_member(m_alpha, in);
            sdsl::read_member(m_zero_value, in);
            m_rules.load(in);
            m_extra_info_pos.load(in);
            m_extra_info_time.load(in);
            uint64_t m_translate_size = 0;
            sdsl::read_member(m_translate_size, in);
            m_translate_table.resize(m_translate_size);
            sdsl::load_vector(m_translate_table, in);
            uint64_t m_log_size = 0;
            sdsl::read_member(m_log_size, in);
            m_log_object.resize(m_log_size);
            for(uint64_t i = 0; i < m_log_object.size(); ++i){
                m_log_object[i].load(in);
            }
            sdsl::read_member(m_period_snapshot, in);
            sdsl::read_member(m_total_objects, in);
            sdsl::read_member(m_t_max, in);
            sdsl::read_member(m_x_max, in);
            sdsl::read_member(m_y_max, in);
            sdsl::read_member(m_speed_max, in);
            sdsl::read_member(m_level_max, in);
        }

    };
}

#endif //GRACTV2_INDEX_CT_HPP
