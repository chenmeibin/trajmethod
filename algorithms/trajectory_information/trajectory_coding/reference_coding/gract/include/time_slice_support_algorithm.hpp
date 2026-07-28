//
// Created by adrian on 13/04/18.
//

#ifndef GRACTV2_TIME_SLICE_SUPPORT_ALGORITHM_HPP
#define GRACTV2_TIME_SLICE_SUPPORT_ALGORITHM_HPP

#include <stdint.h>
#include <geometry.hpp>
#include <spiral_matrix_coder.hpp>
#include <log_object.hpp>
#include <snapshot.hpp>
#include <global_constants.hpp>

namespace gract_ct{


    template <class index>
    bool _time_slice_expand_left(const uint64_t nt, const uint64_t t_q, const region &r_q,
                                 uint64_t t, int64_t x, int64_t y,
                                 point &p,
                                 const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                 const index* graCT){

        uint64_t dist_max = (t_q - t) * speed_max;
        region r_s = util::expand_point(point(x, y), dist_max, max_x, max_y);
        if(util::touches_region(r_s, r_q)){
            if (nt >= graCT->alpha) {
                uint64_t v1 = graCT->get_left(nt);
                uint64_t v2 = graCT->get_right(nt);
                if (v1 < graCT->alpha) {
                    auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                    x = x + inc_p.first;
                    y = y + inc_p.second;
                    t++;
                    if (t < t_q) {
                        return _time_slice_expand_left(v2, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
                    } else if (t == t_q) {
                        p =  point(x, y);
                        return util::contains_point(r_q, p);
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
                        return util::contains_point(r_q, p);
                    } else if (t + inc_t < t_q) {
                        //expando a da dereita
                        point inc_p = graCT->get_rule_point(v1);
                        x = x + inc_p.m_x;
                        y = y + inc_p.m_y;
                        t = t + inc_t;
                        return _time_slice_expand_left(v2, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
                    } else {
                        //expando a da esquerda
                        return _time_slice_expand_left(v1, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
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
                    return util::contains_point(r_q, p);;
                } else {
                    return false;
                }
            }
        }
        return false;
    }

    template <class index>
    bool _time_slice_expand_right(const uint64_t nt, const uint64_t t_q, const region &r_q,
                                 uint64_t t, int64_t x, int64_t y,
                                 point &p,
                                 const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                 const index* graCT){

        uint64_t dist_max = (t - t_q) * speed_max;
        region r_s = util::expand_point(point(x, y), dist_max, max_x, max_y);
        if(util::touches_region(r_s, r_q)){
            if (nt >= graCT->alpha) {
                uint64_t v1 = graCT->get_right(nt);
                uint64_t v2 = graCT->get_left(nt);
                if (v1 < graCT->alpha) {
                    auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                    x = x - inc_p.first;
                    y = y - inc_p.second;
                    t--;
                    if (t > t_q) {
                        return _time_slice_expand_right(v2, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
                    } else if (t == t_q) {
                        p =  point(x, y);
                        return util::contains_point(r_q, p);
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
                        return util::contains_point(r_q, p);
                    } else if (t - inc_t > t_q) {
                        //expando a da dereita
                        point inc_p = graCT->get_rule_point(v1);
                        x = x - inc_p.m_x;
                        y = y - inc_p.m_y;
                        t = t - inc_t;
                        return _time_slice_expand_right(v2, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
                    } else {
                        //expando a da esquerda
                        return _time_slice_expand_right(v1, t_q, r_q, t, x, y, p, speed_max, max_x, max_y, graCT);
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
                    return util::contains_point(r_q, p);
                } else {
                    return false;
                }
            }
        }
        return false;
    }

    template<class index>
    bool _time_slice_object_left(const log_object* log_obj, const uint64_t t_q, const region &r_q,
                          uint64_t t, int64_t x, int64_t y,
                          point &p,
                          uint64_t idx_log, uint64_t log_pos, uint64_t log_reap_movement_pos,
                          uint64_t log_reap_time_pos,
                          const uint64_t n_snapshot,
                          const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                          const index* graCT){

        p = point(x, y);
        if(t == t_q && util::contains_point(r_q, p)){
            return true;
        }else {
            if(log_obj->exist_log(idx_log, n_snapshot)){
                uint64_t dist_max = (t_q - t) * speed_max;
                region r_s = util::expand_point(point(x, y), dist_max, max_x, max_y);
                uint64_t end_log = log_obj->get_end_log(idx_log, n_snapshot);
                while(log_pos <= end_log && t < t_q && util::touches_region(r_s, r_q)){
                    int64_t log_value = log_obj->get_log_value(log_pos);
                    if(log_value < 0){
                        uint16_t d_t;
                        int64_t d_x, d_y;
                        log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                                 d_t, d_x, d_y);
                        if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                            p.m_x = d_x;
                            p.m_y = d_y;
                            log_reap_movement_pos += 2;
                        } else if (log_value != REAP_NO_MOVE) {
                            p.m_x += d_x;
                            p.m_y += d_y;
                            log_reap_movement_pos++;
                        }
                        t = t + d_t;
                        log_pos++;
                        log_reap_time_pos++;
                    }else{
                        if(log_value >= graCT->alpha){
                            uint64_t time = graCT->get_rule_time(log_value);
                            if(t + time > t_q) {
                                region mbr_rule = graCT->get_rule_mbr(log_value);
                                region mbr(p.m_x + mbr_rule.m_min_p.m_x, p.m_y + mbr_rule.m_min_p.m_y,
                                           p.m_x + mbr_rule.m_max_p.m_x, p.m_y + mbr_rule.m_max_p.m_y);
                                if(util::touches_region(mbr, r_q)) {
                                    return _time_slice_expand_left((uint64_t) log_value, t_q, r_q, t, p.m_x, p.m_y, p, speed_max, max_x, max_y, graCT);
                                }else {
                                    return false;
                                }
                            }else {
                                point inc_p = graCT->get_rule_point(log_value);
                                p.m_x = p.m_x + inc_p.m_x;
                                p.m_y = p.m_y + inc_p.m_y;
                                t = t + time;
                                log_pos++;
                            }
                        }else{
                            auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                            p.m_x += inc_p.first;
                            p.m_y += inc_p.second;
                            t++;
                            log_pos++;
                        }
                    }
                    dist_max = (t_q - t) * speed_max;
                    r_s = util::expand_point(p, dist_max, max_x, max_y);
                }
                return (t == t_q && util::contains_point(r_q, p));
            }
        }
        return false;

    }

    template<class index>
    bool _time_slice_object_right(const log_object* log_obj, const uint64_t t_q, const region &r_q,
                          uint64_t t, int64_t x, int64_t y,
                          point &p,
                          uint64_t idx_log, uint64_t log_pos, uint64_t log_reap_movement_pos,
                          uint64_t log_reap_time_pos,
                          const uint64_t n_snapshot,
                          const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                          const index* graCT){

        p = point(x, y);
        if(t == t_q && util::contains_point(r_q, p)){
            return true;
        }else {
            if(log_obj->exist_log(idx_log, n_snapshot)){
                uint64_t dist_max = (t - t_q) * speed_max;
                region r_s = util::expand_point(point(x, y), dist_max, max_x, max_y);
                uint64_t start_log = log_obj->get_start_log(idx_log);
                while(log_pos >= start_log && t > t_q && util::touches_region(r_s, r_q)){
                    int64_t log_value = log_obj->get_log_value(log_pos);
                    if(log_value < 0){
                        uint16_t d_t;
                        int64_t d_x, d_y;
                        log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                                 d_t, d_x, d_y);
                        if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                            p.m_x = d_x;
                            p.m_y = d_y;
                            log_reap_movement_pos -= 2;
                        } else if (log_value != REAP_NO_MOVE) {
                            p.m_x -= d_x;
                            p.m_y -= d_y;
                            log_reap_movement_pos--;
                        }
                        t = t - d_t;
                        log_pos--;
                        log_reap_time_pos--;
                    }else{
                        if(log_value >= graCT->alpha){
                            uint64_t time = graCT->get_rule_time(log_value);
                            if(t - time < t_q) {
                                point p_rule = graCT->get_rule_point(log_value);
                                region mbr_rule = graCT->get_rule_mbr(log_value);
                                region mbr((p.m_x-p_rule.m_x) + mbr_rule.m_min_p.m_x, (p.m_y-p_rule.m_y) + mbr_rule.m_min_p.m_y,
                                           (p.m_x-p_rule.m_x) + mbr_rule.m_max_p.m_x, (p.m_y-p_rule.m_y) + mbr_rule.m_max_p.m_y);
                                if(util::touches_region(mbr, r_q)) {
                                    return _time_slice_expand_right((uint64_t) log_value, t_q, r_q, t, p.m_x, p.m_y, p, speed_max, max_x, max_y, graCT);
                                }else {
                                    return false;
                                }
                            }else {
                                point inc_p = graCT->get_rule_point(log_value);
                                p.m_x = p.m_x - inc_p.m_x;
                                p.m_y = p.m_y - inc_p.m_y;
                                t = t - time;
                                log_pos--;
                            }
                        }else{
                            auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                            p.m_x -= inc_p.first;
                            p.m_y -= inc_p.second;
                            t--;
                            log_pos--;
                        }
                    }
                    dist_max = (t - t_q) * speed_max;
                    r_s = util::expand_point(p, dist_max, max_x, max_y);
                }
                return (t == t_q && util::contains_point(r_q, p));
            }
        }
        return false;

    }

    template <class index>
    std::vector<id_point> _time_slice_left(const uint64_t idx_snap, const region& r_q, const uint64_t t_q,
                                           const uint64_t period_snapshot,
                                          const uint64_t n_snapshot, const uint64_t n_objects,
                                          const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                          const index* graCT){
        std::vector<id_point> results;
        uint64_t t_snap = idx_snap * period_snapshot;
        int64_t x, y;
        uint64_t dist_max = (t_q - t_snap) * speed_max;
        region r_i = r_q.expand(dist_max, max_x, max_y);
        sdsl::bit_vector checked(n_objects, 0);
        uint64_t idx_log = t_q / period_snapshot;
        sdsl::int_vector<> reap_candidates = graCT->reap[idx_log];
        for(uint64_t i = 0; i < reap_candidates.size(); i++){
            uint64_t oid = reap_candidates[i];
            if(!checked[oid]){
                uint64_t t = t_snap;
                auto log_obj = graCT->get_log_object(oid);
                uint64_t log_pos = log_obj->get_start_log(idx_log);
                uint64_t log_reap_movement_pos = log_obj->get_start_reap_movement(idx_log);
                uint64_t log_reap_time_pos = log_obj->get_start_reap_time(idx_log);
                int64_t log_value = log_obj->get_log_value(log_pos);
                if (log_value == REAPPEARANCE) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                             d_t, d_x, d_y);
                    t = t + d_t;
                    x = d_x;
                    y = d_y;
                    log_reap_movement_pos += 2;
                    log_reap_time_pos++;
                    log_pos++;
                    point p;
                    if(_time_slice_object_left(log_obj, t_q, r_q, t, x, y, p, idx_log, log_pos, log_reap_movement_pos,
                                               log_reap_time_pos, n_snapshot, speed_max, max_x, max_y, graCT)){
                        results.push_back(id_point(oid, p));
                    };
                    checked[oid] = 1;
                } else {
                    std::cout << "Error: search_object_left. oid = " << oid << " t = " << t_q << std::endl;
                    exit(20);
                }
            }
        }
        auto snap = graCT->get_snapshot(idx_snap);
        std::vector<id_point> candidates = snap->find_objects_in_region(r_i.m_min_p.m_x, r_i.m_max_p.m_x,
                                                                       r_i.m_min_p.m_y, r_i.m_max_p.m_y);
        for(uint64_t i = 0; i < candidates.size(); i++){
            uint64_t oid = candidates[i].m_id;
            //if(!checked[oid]){
                auto log_obj = graCT->get_log_object(oid);
                uint64_t log_pos = log_obj->get_start_log(idx_log);
                uint64_t log_reap_movement_pos = log_obj->get_start_reap_movement(idx_log);
                uint64_t log_reap_time_pos = log_obj->get_start_reap_time(idx_log);
                uint64_t t = t_snap;
                x = candidates[i].m_point.m_x;
                y = candidates[i].m_point.m_y;
                point p;
                if(_time_slice_object_left(log_obj, t_q, r_q, t, x, y, p, idx_log, log_pos, log_reap_movement_pos,
                                           log_reap_time_pos, n_snapshot, speed_max, max_x, max_y, graCT)){
                    results.push_back(id_point(oid, p));
                };
                checked[oid] = 1;
            //}
        }
        return results;
    }

    template <class index>
    std::vector<id_point> _time_slice_right(const uint64_t idx_snap, const region& r_q, const uint64_t t_q,
                                           const uint64_t period_snapshot,
                                           const uint64_t n_snapshot, const uint64_t n_objects,
                                           const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                           const index* graCT){
        std::vector<id_point> results;
        uint64_t t_snap = idx_snap * period_snapshot;
        int64_t x, y;
        uint64_t dist_max = (t_snap - t_q) * speed_max;
        region r_i = r_q.expand(dist_max, max_x, max_y);
        sdsl::bit_vector checked(n_objects, 0);
        uint64_t idx_log = t_q / period_snapshot;
        sdsl::int_vector<> disap_candidates = graCT->disap[idx_log];
        for(uint64_t i = 0; i < graCT->disap[idx_log].size(); i++){
            uint64_t oid = graCT->disap[idx_log][i];
           // if(!checked[oid]){
                uint64_t t = t_snap;
                auto log_obj = graCT->get_log_object(oid);
                uint64_t log_pos = log_obj->get_end_log(idx_log, n_snapshot);
                uint64_t log_reap_movement_pos = log_obj->get_end_reap_movement(idx_log, n_snapshot);
                uint64_t log_reap_time_pos = log_obj->get_end_reap_time(idx_log, n_snapshot);
                int64_t log_value = log_obj->get_log_value(log_pos);
                if (log_value == DISAPPEARANCE) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                             d_t, d_x, d_y);
                    t = t - d_t;
                    x = d_x;
                    y = d_y;
                    log_reap_movement_pos -= 2;
                    log_reap_time_pos--;
                    log_pos--;
                    point p;
                    if(_time_slice_object_right(log_obj, t_q, r_q, t, x, y, p, idx_log, log_pos, log_reap_movement_pos,
                                               log_reap_time_pos, n_snapshot, speed_max, max_x, max_y, graCT)){
                        results.push_back(id_point(oid, p));
                    };
                    checked[oid] = 1;
                } else {
                    std::cout << "Error: search_object_right. oid = " << oid << " t = " << t_q << std::endl;
                    exit(20);
                }
          //  }
        }
        auto snap = graCT->get_snapshot(idx_snap);
        std::vector<id_point> candidates = snap->find_objects_in_region(r_i.m_min_p.m_x, r_i.m_max_p.m_x,
                                                                       r_i.m_min_p.m_y, r_i.m_max_p.m_y);
        for(uint64_t i = 0; i < candidates.size(); i++){
            uint64_t oid = candidates[i].m_id;
           // if(!checked[oid]){
                auto log_obj = graCT->get_log_object(oid);
                uint64_t log_pos = log_obj->get_end_log(idx_log, n_snapshot);
                uint64_t log_reap_movement_pos = log_obj->get_end_reap_movement(idx_log, n_snapshot);
                uint64_t log_reap_time_pos = log_obj->get_end_reap_time(idx_log, n_snapshot);
                uint64_t t = t_snap;
                x = candidates[i].m_point.m_x;
                y = candidates[i].m_point.m_y;
                point p;
                if(_time_slice_object_right(log_obj, t_q, r_q, t, x, y, p, idx_log, log_pos, log_reap_movement_pos,
                                           log_reap_time_pos, n_snapshot, speed_max, max_x, max_y, graCT)){
                    results.push_back(id_point(oid, p));
                };
                checked[oid] = 1;
           // }
        }
        return results;
    }

    template<class index>
    std::vector<id_point> time_slice_run(const region& r_q, const uint64_t t_q, const uint64_t period_snapshot,
                                          const uint64_t n_snapshot, const uint64_t n_objects,
                                          const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                          const index* graCT){
        uint64_t idx_snap =  t_q / period_snapshot;
        if(t_q % period_snapshot == 0){
            auto snap = graCT->get_snapshot(idx_snap);
            return snap->find_objects_in_region(r_q.m_min_p.m_x, r_q.m_max_p.m_x, r_q.m_min_p.m_y, r_q.m_max_p.m_y);
        }else{
            if(idx_snap < n_snapshot-1 && t_q % period_snapshot > period_snapshot/2){
                idx_snap++;
                return _time_slice_right(idx_snap, r_q, t_q, period_snapshot, n_snapshot, n_objects, speed_max, max_x, max_y, graCT);
            }else{
                return _time_slice_left(idx_snap, r_q, t_q, period_snapshot, n_snapshot, n_objects, speed_max, max_x, max_y, graCT);
            }

        }
    }


}

#endif //GRACTV2_TIME_SLICE_SUPPORT_ALGORITHM_HPP
