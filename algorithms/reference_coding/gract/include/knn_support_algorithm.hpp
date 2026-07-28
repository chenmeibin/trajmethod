//
// Created by adrian on 20/07/18.
//

#ifndef GRACTV2_KNN_SUPPORT_ALGORITHM_NEW_HPP
#define GRACTV2_KNN_SUPPORT_ALGORITHM_NEW_HPP

#include <global_constants.hpp>
#include <inttypes.h>
#include <geometry.hpp>
#include <spiral_matrix_coder.hpp>
#include <knn_support_helper.hpp>

using namespace gract_ct::knn_support_helper;

namespace gract_ct {



    knn_element recompute_priority(const uint32_t k, const point &p_q, const uint64_t t_q,
                                                       knn_element &knn_e, const uint64_t speed_max,
                                                       const uint64_t max_x, const uint64_t max_y){
        uint64_t dist_max = std::abs((int64_t) (t_q - knn_e.t)) * speed_max;
        region r_i = util::expand_point(knn_e.min, dist_max, max_x, max_y);
        uint64_t distance = util::distance_knn(r_i, p_q);
        uint64_t distance_max = util::max_distance_knn(r_i, p_q);
        knn_e.distance = distance;
        knn_e.distance_max = distance_max;
        return knn_e;
    }

    void insert_result(knn_element &candidate, pq_knn_element_type &pq_results, pq_knn_movement_type &pq_distances, uint64_t k){
        pq_results.push(candidate);
        if (pq_distances.size() < k) {
            pq_distances.push(candidate.distance);
        } else if (candidate.distance < pq_distances.top()) {
            pq_distances.pop();
            pq_distances.push(candidate.distance);
        }
    }

    template <class index>
    bool knn_expand_left(const uint64_t nt, const uint64_t t_q ,
                          uint64_t t, int64_t x, int64_t y, point &p,
                          const index* graCT){

        if (nt >= graCT->alpha) {
            uint64_t v1 = graCT->get_left(nt);
            uint64_t v2 = graCT->get_right(nt);
            if (v1 < graCT->alpha) {
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                x = x + inc_p.first;
                y = y + inc_p.second;
                t++;
                if (t < t_q) {
                    return knn_expand_left(v2, t_q, t, x, y, p, graCT);
                } else if (t == t_q) {
                    p =  point(x, y);
                    return true;
                } else {
                    return false;
                }
            } else {
                uint64_t inc_t = graCT->get_rule_time(v1);
                if (t + inc_t == t_q) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x + inc_p.m_x;
                    y = y + inc_p.m_y;
                    //Queda sen expandir a da dereita
                    p = point(x, y);
                    return true;
                } else if (t + inc_t < t_q) {
                    //expando a da dereita
                    point inc_p = graCT->get_rule_point(v1);
                    x = x + inc_p.m_x;
                    y = y + inc_p.m_y;
                    t = t + inc_t;
                    return knn_expand_left(v2, t_q, t, x, y, p, graCT);
                } else {
                    //expando a da esquerda
                    return knn_expand_left(v1, t_q, t, x, y, p, graCT);
                    //Queda sen expandir a da dereita
                }
            }
        } else {
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x + inc_p.first;
            y = y + inc_p.second;
            t++;
            if (t == t_q) {
                p =  point(x, y);
                return true;
            } else {
                return false;
            }
        }
    }


    template <class index>
    bool knn_expand_right(const uint64_t nt, const uint64_t t_q ,
                          uint64_t t, int64_t x, int64_t y, point &p,
                          const index* graCT){

        if (nt >= graCT->alpha) {
            uint64_t v1 = graCT->get_right(nt);
            uint64_t v2 = graCT->get_left(nt);
            if (v1 < graCT->alpha) {
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                x = x - inc_p.first;
                y = y - inc_p.second;
                t--;
                if (t > t_q) {
                    return knn_expand_right(v2, t_q, t, x, y, p, graCT);
                } else if (t == t_q) {
                    p =  point(x, y);
                    return true;
                } else {
                    return false;
                }
            } else {
                uint64_t inc_t = graCT->get_rule_time(v1);
                if (t - inc_t == t_q) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x - inc_p.m_x;
                    y = y - inc_p.m_y;
                    //Queda sen expandir a da dereita
                    p = point(x, y);
                    return true;
                } else if (t - inc_t > t_q) {
                    //expando a da dereita
                    point inc_p = graCT->get_rule_point(v1);
                    x = x - inc_p.m_x;
                    y = y - inc_p.m_y;
                    t = t - inc_t;
                    return knn_expand_right(v2, t_q, t, x, y, p, graCT);
                } else {
                    //expando a da esquerda
                    return knn_expand_right(v1, t_q, t, x, y, p, graCT);
                    //Queda sen expandir a da dereita
                }
            }
        } else {
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x - inc_p.first;
            y = y - inc_p.second;
            t--;
            if (t == t_q) {
                p =  point(x, y);
                return true;
            } else {
                return false;
            }
        }
    }

    template<class index, class t_array>
    void init_left(const uint64_t t_q, const point &p_q, pq_knn_element_type &knn_pq, const t_array &reap,
                          const uint64_t snap_id, const index* graCT,
                          const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y){
        std::vector<knn_element> elements;
        for(const auto &oid : reap){
            auto log_obj = graCT->get_log_object(oid);
            uint64_t log_pos = log_obj->get_start_log(snap_id);
            int64_t log_value = log_obj->get_log_value(log_pos);
            if (log_value == REAPPEARANCE) {
                uint64_t log_reap_movement_pos = log_obj->get_start_reap_movement(snap_id);
                uint64_t log_reap_time_pos = log_obj->get_start_reap_time(snap_id);
                uint16_t d_t;
                int64_t d_x, d_y;
                log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                         d_t, d_x, d_y);
                uint64_t t = snap_id * graCT->period_snapshot + d_t;
                if(t <= t_q){
                    uint64_t dist_max = (t_q - t) * speed_max;
                    point p(d_x, d_y);
                    region r_i = util::expand_point(p, dist_max, max_x, max_y);
                    uint64_t distance = util::distance_knn(r_i, p_q);
                    uint64_t distance_max = util::max_distance_knn(r_i, p_q);
                    knn_element knn_e(oid, p, distance, distance_max, t, snap_id, log_pos+1, log_reap_movement_pos+2, log_reap_time_pos+1, false, true);
                    elements.push_back(knn_e);
                }
            }
        }
        knn_element knn_e(0, point(0,0), point(0,0), 0, 0, snap_id * graCT->period_snapshot, 0);
        elements.push_back(knn_e);
        knn_pq = pq_knn_element_type(elements.begin(), elements.end());
    };

    template<class index, class t_array>
    void init_right(const uint64_t t_q, const point &p_q, pq_knn_element_type &knn_pq,
                             const t_array &disap,
                             const uint64_t snap_id, const index* graCT,
                             const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                             const uint64_t n_snapshot){
        std::vector<knn_element> elements;
        for(const auto &oid : disap){
            auto log_obj = graCT->get_log_object(oid);
            uint64_t log_pos = log_obj->get_end_log(snap_id-1, n_snapshot);
            int64_t log_value = log_obj->get_log_value(log_pos);
            if (log_value == DISAPPEARANCE) {
                uint64_t log_reap_movement_pos = log_obj->get_end_reap_movement(snap_id-1, n_snapshot);
                uint64_t log_reap_time_pos = log_obj->get_end_reap_time(snap_id-1, n_snapshot);
                uint16_t d_t;
                int64_t d_x, d_y;
                log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                         d_t, d_x, d_y);
                uint64_t t = snap_id * graCT->period_snapshot - d_t;
                if(t >= t_q){
                    point p(d_x, d_y);
                    uint64_t dist_max = (t - t_q) * speed_max;
                    region r_i = util::expand_point(p, dist_max, max_x, max_y);
                    uint64_t distance = util::distance_knn(r_i, p_q);
                    uint64_t distance_max = util::max_distance_knn(r_i, p_q);
                    knn_element knn_e(oid, p, distance, distance_max, t, snap_id-1, log_pos-1, log_reap_movement_pos-2, log_reap_time_pos-1, false, true);
                    elements.push_back(knn_e);
                }
            }
        }
        knn_element knn_e(0, point(0,0), point(0,0), 0, 0, snap_id * graCT->period_snapshot, 0);
        elements.push_back(knn_e);
        knn_pq = pq_knn_element_type(elements.begin(), elements.end());
    };

    template<class index>
    bool knn_left(knn_element& candidate, const uint32_t k, const point &p_q, const uint64_t t_q, const uint64_t steps,
                  pq_knn_element_type &knn_objects, const uint64_t sid, const index* graCT,
                  const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                  const uint64_t n_snapshots){

        uint64_t oid = candidate.id;
        auto log_obj = graCT->get_log_object(oid);
        uint64_t end_log = log_obj->get_end_log(sid, n_snapshots);
        candidate.log_pos = candidate.log_pos ? candidate.log_pos : log_obj->get_start_log(sid);
        candidate.tim_pos = candidate.tim_pos ? candidate.tim_pos : log_obj->get_start_reap_time(sid);
        candidate.mov_pos = candidate.mov_pos ? candidate.mov_pos : log_obj->get_start_reap_movement(sid);
        if(log_obj->exist_log(sid, n_snapshots)) {
            uint64_t step = 0;
            while (step < steps && candidate.t < t_q && candidate.log_pos <= end_log) {
                int64_t log_value = log_obj->get_log_value(candidate.log_pos);
                if (log_value < 0) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(candidate.tim_pos, candidate.mov_pos, log_value, false, d_t, d_x,
                                              d_y);
                    if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                        candidate.min.m_x = d_x;
                        candidate.min.m_y = d_y;
                        candidate.mov_pos += 2;
                    } else if (log_value != REAP_NO_MOVE) {
                        candidate.min.m_x += d_x;
                        candidate.min.m_y += d_y;
                        candidate.mov_pos++;
                    }
                    candidate.t = candidate.t + d_t;
                    candidate.tim_pos++;
                } else {

                    if (log_value >= graCT->alpha) {
                        uint64_t time = graCT->get_rule_time(log_value);
                        if (candidate.t + time > t_q) {
                            point p;
                            knn_expand_left(log_value, t_q, candidate.t, candidate.min.m_x, candidate.min.m_y, p, graCT);
                            candidate.t = t_q;
                            candidate.min.m_x = p.m_x;
                            candidate.min.m_y = p.m_y;
                        } else {
                            point inc_p = graCT->get_rule_point(log_value);
                            candidate.min.m_x += inc_p.m_x;
                            candidate.min.m_y += inc_p.m_y;
                            candidate.t += time;
                        }
                    } else {
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        candidate.min.m_x += inc_p.first;
                        candidate.min.m_y += inc_p.second;
                        candidate.t++;
                    }
                }
                candidate.log_pos++;
                ++step;
            }
            if(candidate.t > t_q) return false;
            recompute_priority(k, p_q, t_q, candidate, speed_max, max_x, max_y);
            if(candidate.t == t_q) return true;
            knn_objects.push(candidate);
        }
        return false;

    }




    template<class index>
    bool knn_right(knn_element& candidate, const uint32_t k, const point &p_q, const uint64_t t_q, const uint64_t steps,
                   pq_knn_element_type &knn_objects, const uint64_t sid, const index* graCT,
                   const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                   const uint64_t n_snapshots){


        uint64_t oid = candidate.id;
        auto log_obj = graCT->get_log_object(oid);
        uint64_t start_log = log_obj->get_start_log(candidate.log_idx);
        candidate.log_pos = candidate.log_pos ? candidate.log_pos : log_obj->get_end_log(candidate.log_idx, n_snapshots);
        candidate.tim_pos = candidate.tim_pos ? candidate.tim_pos : log_obj->get_end_reap_time(candidate.log_idx, n_snapshots);
        candidate.mov_pos = candidate.mov_pos ? candidate.mov_pos : log_obj->get_end_reap_movement(candidate.log_idx, n_snapshots);
        if(log_obj->exist_log(candidate.log_idx, n_snapshots)){
            uint64_t step = 0;
            while(step < steps && candidate.t > t_q && candidate.log_pos >= start_log){
                int64_t log_value = log_obj->get_log_value(candidate.log_pos);
                if(log_value < 0){
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(candidate.tim_pos, candidate.mov_pos, log_value, true, d_t, d_x, d_y);
                    if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                        candidate.min = point(d_x, d_y);
                        candidate.mov_pos -= 2;
                    } else if (log_value != REAP_NO_MOVE) {
                        candidate.min.m_x -= d_x;
                        candidate.min.m_y -= d_y;
                        candidate.mov_pos--;
                    }
                    candidate.t = candidate.t - d_t;
                    candidate.tim_pos--;
                }else{

                    if(log_value >= graCT->alpha){
                        uint64_t time = graCT->get_rule_time(log_value);
                        if(candidate.t - time < t_q){
                            point p;
                            knn_expand_right(log_value, t_q, candidate.t, candidate.min.m_x, candidate.min.m_y, p, graCT);
                            candidate.t = t_q;
                            candidate.min.m_x = p.m_x;
                            candidate.min.m_y = p.m_y;
                        }else{
                            point inc_p = graCT->get_rule_point(log_value);
                            candidate.min.m_x -= inc_p.m_x;
                            candidate.min.m_y -= inc_p.m_y;
                            candidate.t -= time;
                        }
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        candidate.min.m_x -= inc_p.first;
                        candidate.min.m_y -= inc_p.second;
                        candidate.t--;
                    }
                }
                candidate.log_pos--;
                ++step;
            }
            if(candidate.t < t_q) return false;
            recompute_priority(k, p_q, t_q, candidate, speed_max, max_x, max_y);
            if(candidate.t == t_q) return true;
            knn_objects.push(candidate);
        }
        return false;
    }


    template <class index>
    std::vector<uint64_t > knn_run(const uint32_t k, const point &p_q, const uint64_t t_q, const uint64_t steps,
                                    const uint64_t period_snapshot,
                                   const uint64_t n_snapshot, const uint64_t n_objects, const uint64_t speed_max,
                                   const uint64_t max_x, const uint64_t max_y, const index* graCT, const bool test){

        pq_knn_element_type pq_results;
        pq_knn_movement_type pq_distances;


        uint64_t sid = t_q / period_snapshot;
        uint64_t snap_t = sid * period_snapshot;
        uint64_t d_t = t_q - snap_t;
        if (d_t == 0) {
            pq_knn_element_type candidates;
            knn_element knn_e(0, point(0,0), point(0,0), 0, 0, snap_t, 0);
            candidates.push(knn_e);
            auto snap = graCT->get_snapshot(sid);
            while(!candidates.empty() && !(pq_results.size() >= k && candidates.top().distance >= pq_distances.top())){
                auto candidate = candidates.top(); //copy is necessary
                candidates.pop();
                if(candidate.is_leaf){
                    for(const auto &o : snap->get_objects(candidate.id)){
                        knn_element knn_child(o, candidate.min, candidate.distance, candidate.distance_max, snap_t, candidate.log_idx,  0, 0, 0, false, true);
                        insert_result(knn_child, pq_results, pq_distances, k);
                    }
                }else{
                    snap->enqueue_children(candidate, candidates, p_q, speed_max, sid, snap_t, d_t, max_x, max_y);
                }
            }
        } else {
            //Detectar se vamos pola esquerda ou pola dereita
            if (sid < n_snapshot - 1 && d_t > period_snapshot / 2) {
                //dereita
                sid++;
                snap_t += period_snapshot;
                d_t = snap_t - t_q;
                auto snap = graCT->get_snapshot(sid);
                pq_knn_element_type candidates;
                init_right(t_q, p_q, candidates, graCT->disap[sid-1], sid, graCT, speed_max, max_x, max_y, n_snapshot);
                while(!candidates.empty() && !(pq_results.size() >= k && candidates.top().distance >= pq_distances.top())){
                    auto candidate = candidates.top(); //copy is necessary
                    candidates.pop();
                    if(candidate.is_point){
                        if(knn_right(candidate, k, p_q, t_q, steps, candidates, sid, graCT, speed_max, max_x, max_y, n_snapshot)){
                            insert_result(candidate, pq_results, pq_distances, k);
                            //std::cout << "inserta" << std::endl;
                        };
                        /*std::cout << "id: " << candidate.id << " point: (" << candidate.min.m_x << ", "
                                  << candidate.min.m_y << ") weight: " << candidate.weight << std::endl;*/
                    }else if (candidate.is_leaf) {
                        for(const auto &o : snap->get_objects(candidate.id)){
                            knn_element knn_child(o, candidate.min, candidate.distance, candidate.distance_max, snap_t,
                                                  candidate.log_idx,  0, 0, 0, false, true);
                            candidates.push(knn_child);
                        }
                    }else{
                        snap->enqueue_children(candidate, candidates, p_q, speed_max, sid-1, snap_t, d_t, max_x, max_y);
                    }
                }
            } else {
                //esquerda
                auto snap = graCT->get_snapshot(sid);
                pq_knn_element_type candidates;
                init_left(t_q, p_q, candidates, graCT->reap[sid], sid, graCT, speed_max, max_x, max_y);
                while(!candidates.empty() && !(pq_results.size() >= k && candidates.top().distance >= pq_distances.top())){
                    auto candidate = candidates.top(); //copy is necessary
                    candidates.pop();
                    if(candidate.is_point) {
                        if (knn_left(candidate, k, p_q, t_q, steps, candidates, sid, graCT, speed_max, max_x, max_y,
                                     n_snapshot)) {
                            insert_result(candidate, pq_results, pq_distances, k);
                        };
                    }else if (candidate.is_leaf){
                        for(const auto &o : snap->get_objects(candidate.id)){
                            knn_element knn_child(o, candidate.min, candidate.distance, candidate.distance_max, snap_t,
                                                  candidate.log_idx,  0, 0, 0, false, true);
                            candidates.push(knn_child);
                        }
                    }else{
                        snap->enqueue_children(candidate, candidates, p_q, speed_max, sid, snap_t, d_t, max_x, max_y);
                    }
                }
            }
        }

        std::vector<uint64_t > resultado;
        double_t k_distance = 0;
        for (uint i = 0; i < k; i++) {
            auto sol = pq_results.top();
            k_distance = sol.distance;
            resultado.push_back(sol.id);
            pq_results.pop();
        }
        if(test){
            while(!pq_results.empty() && pq_results.top().distance == k_distance){
                auto sol = pq_results.top();
                resultado.push_back(sol.id);
                pq_results.pop();
            }
        }
        return resultado;

    }




}

#endif //GRACTV2_KNN_SUPPORT_ALGORITHM_NEW_HPP
