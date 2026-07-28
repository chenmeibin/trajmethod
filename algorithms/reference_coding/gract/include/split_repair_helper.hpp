//
// Created by adrian on 26/12/17.
//

#ifndef GRACTV2_SPLIT_REPAIR_HELPER_HPP
#define GRACTV2_SPLIT_REPAIR_HELPER_HPP

#include <vector>
#include <map>

namespace gract_ct {

    class split_repair_helper{

    private:
        std::map<int64_t, uint64_t> m_prev_map;
        std::map<int64_t, uint64_t> m_succ_map;
        std::vector<int64_t>* m_repair_string;
        std::vector<int64_t>* m_terminals;
        std::map<int64_t, char>* m_map_terminals;
        typedef std::map<int64_t, uint64_t>::iterator it_type;

    public:
        split_repair_helper(std::vector<int64_t>* repair_string, std::vector<int64_t>* terminals,
                            std::map<int64_t, char>* map_terminals){
            m_repair_string = repair_string;
            m_terminals = terminals;
            m_map_terminals = map_terminals;

        };

        void add_split_at(uint64_t i){

            if(m_repair_string->size() == 0) return;

            bool exist_prev = (i > 0);
            bool exist_succ = (i < m_repair_string->size()-1);
            it_type it_prev = m_prev_map.end(), it_succ = m_succ_map.end();
            uint64_t prev_split = 1, succ_split = 1;
            int64_t prev_value, succ_value;
            if(exist_prev){
                prev_value = m_repair_string->at(i-1);
                if((it_prev = m_prev_map.find(prev_value)) != m_prev_map.end()){
                    prev_split = it_prev->second + 1;
                }
            }
            if(exist_succ){
                succ_value = m_repair_string->at(i+1);
                if((it_succ = m_succ_map.find(succ_value)) != m_succ_map.end()){
                    succ_split = it_succ->second + 1;
                }
            }
            uint64_t max = std::max(prev_split, succ_split);
            if(exist_prev){
                if(it_prev != m_prev_map.end()){
                    m_prev_map.erase(it_prev);
                }
                m_prev_map.insert(std::pair<int64_t, uint64_t >(prev_value, max));

            }
            if(exist_succ){
                if(it_succ != m_succ_map.end()){
                    m_succ_map.erase(it_succ);
                }
                m_succ_map.insert(std::pair<int64_t, uint64_t >(succ_value, max));
            }
            m_repair_string->at(i) = -max;
            if(m_map_terminals->find(-max) == m_map_terminals->end()){
                m_terminals->push_back(-max);
                m_map_terminals->insert(std::pair<int64_t, char>(-max, 'a'));
            }

        }
    };
}
#endif //GRACTV2_SPLIT_REPAIR_HELPER_HPP
