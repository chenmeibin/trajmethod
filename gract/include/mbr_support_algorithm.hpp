//
// Created by adrian on 13/04/18.
//

#ifndef GRACTV2_SEARCH_MBR_SUPPORT_ALGORITHM_HPP
#define GRACTV2_SEARCH_MBR_SUPPORT_ALGORITHM_HPP

#include <stdint.h>
#include <geometry.hpp>
#include <spiral_matrix_coder.hpp>
#include <log_object.hpp>
#include <global_constants.hpp>
#include <data_traj.hpp>

using namespace std::chrono;

namespace gract_ct {


    void check_mbr(region &mbr_result, uint64_t min_x, uint64_t min_y, uint64_t max_x, uint64_t max_y){
        if(mbr_result.m_min_p.m_x > min_x) mbr_result.m_min_p.m_x = min_x;
        if(mbr_result.m_min_p.m_y > min_y) mbr_result.m_min_p.m_y = min_y;
        if(mbr_result.m_max_p.m_x < max_x) mbr_result.m_max_p.m_x = max_x;
        if(mbr_result.m_max_p.m_y < max_y) mbr_result.m_max_p.m_y = max_y;
    }
    template <class index>
    bool _search_mbr_expand_first_rule_left(const uint64_t nt, const uint64_t t_start,
                                              uint64_t t, int64_t x, int64_t y,
                                              point &p, std::stack<uint64_t> &rules,
                                              const index* graCT){

        if (nt >= graCT->alpha) {
            uint64_t v1 = graCT->get_left(nt);
            uint64_t v2 = graCT->get_right(nt);
            if(v1 < graCT->alpha){
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                x = x + inc_p.first;
                y = y + inc_p.second;
                t++;
                if(t < t_start){
                    return _search_mbr_expand_first_rule_left(v2, t_start, t, x, y, p, rules, graCT);
                }else if (t == t_start){
                    rules.push(v2);
                    p =  point(x, y);
                    return true;
                }else{
                    return false;
                }
            }else{
                uint64_t inc_t = graCT->get_rule_time(v1);
                if (t + inc_t == t_start) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x + inc_p.m_x;
                    y = y + inc_p.m_y;
                    rules.push(v2);
                    p =  point(x, y);
                    return true;
                    //regras pendentes v2
                }else if (t + inc_t < t_start) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x + inc_p.m_x;
                    y = y + inc_p.m_y;
                    return _search_mbr_expand_first_rule_left(v2, t_start, t + inc_t, x, y, p, rules, graCT);
                }else {
                    //regras pendentes v2
                    rules.push(v2);
                    return _search_mbr_expand_first_rule_left(v1, t_start, t, x, y, p, rules, graCT);
                }
            }
        }else {
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x + inc_p.first;
            y = y + inc_p.second;
            t++;
            if (t == t_start) {
                p =  point(x, y);
                return true;
            } else {
                return false;
            }
        }

    }

    template <class index>
    bool _search_mbr_expand_first_rule_right(const uint64_t nt, const uint64_t t_start,
                                                   uint64_t t, int64_t x, int64_t y,
                                                   point &p, std::stack<uint64_t> &rules,
                                                   const index* graCT){

        if (nt >= graCT->alpha) {
            uint64_t v1 = graCT->get_right(nt);
            uint64_t v2 = graCT->get_left(nt);
            if(v1 < graCT->alpha){
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                x = x - inc_p.first;
                y = y - inc_p.second;
                t--;
                if(t > t_start){
                    rules.push(v1);
                    return _search_mbr_expand_first_rule_right(v2, t_start, t, x, y, p, rules, graCT);
                }else if (t == t_start){
                    rules.push(v1);
                    p =  point(x, y);
                    return true;
                }else{
                    return false;
                }
            }else{
                uint64_t inc_t = graCT->get_rule_time(v1);
                if (t - inc_t == t_start) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x - inc_p.m_x;
                    y = y - inc_p.m_y;
                    rules.push(v1);
                    p =  point(x, y);
                    return true;
                    //regras pendentes v2
                }else if (t - inc_t > t_start) {
                    point inc_p = graCT->get_rule_point(v1);
                    x = x - inc_p.m_x;
                    y = y - inc_p.m_y;
                    rules.push(v1);
                    return _search_mbr_expand_first_rule_right(v2, t_start, t - inc_t, x, y, p, rules, graCT);
                }else {
                    return _search_mbr_expand_first_rule_right(v1, t_start, t, x, y, p, rules, graCT);
                }
            }
        }else {
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x - inc_p.first;
            y = y - inc_p.second;
            t--;
            if (t == t_start) {
                p =  point(x, y);
                return true;
            } else {
                return false;
            }
        }

    }

    template <class index>
    bool _search_mbr_expand_last_rule(const uint64_t nt, const uint64_t t_end,
                                            uint64_t t, int64_t x, int64_t y,
                                            region &mbr_result, const index* graCT){

        if (nt >= graCT->alpha) {
            uint64_t v1 = graCT->get_left(nt);
            uint64_t v2 = graCT->get_right(nt);
            if(v1 < graCT->alpha){
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                x = x + inc_p.first;
                y = y + inc_p.second;
                t++;
                if(t <= t_end) check_mbr(mbr_result, x, y, x, y);
                if(t < t_end){
                    _search_mbr_expand_last_rule(v2, t_end, t, x, y, mbr_result, graCT);
                }
            }else{
                uint64_t inc_t = graCT->get_rule_time(v1);
                if (t + inc_t == t_end) {
                    auto rule_mbr = graCT->get_rule_mbr(v1);
                    region mbr(rule_mbr.m_min_p.m_x +x, rule_mbr.m_min_p.m_y + y, rule_mbr.m_max_p.m_x + x, rule_mbr.m_max_p.m_y + y);
                    check_mbr(mbr_result, mbr.m_min_p.m_x, mbr.m_min_p.m_y, mbr.m_max_p.m_x, mbr.m_max_p.m_y);
                }else if (t + inc_t < t_end) {
                    auto rule_mbr = graCT->get_rule_mbr(v1);
                    region mbr(rule_mbr.m_min_p.m_x +x, rule_mbr.m_min_p.m_y + y, rule_mbr.m_max_p.m_x + x, rule_mbr.m_max_p.m_y + y);
                    check_mbr(mbr_result, mbr.m_min_p.m_x, mbr.m_min_p.m_y, mbr.m_max_p.m_x, mbr.m_max_p.m_y);
                    point inc_p = graCT->get_rule_point(v1);
                    x = x + inc_p.m_x;
                    y = y + inc_p.m_y;
                    _search_mbr_expand_last_rule(v2, t_end, t + inc_t, x, y, mbr_result, graCT);
                }else {
                    _search_mbr_expand_last_rule(v1, t_end, t, x, y, mbr_result, graCT);
                }
            }
        }else {
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x + inc_p.first;
            y = y + inc_p.second;
            t++;
            if(t <= t_end) check_mbr(mbr_result, x, y, x, y);
        }

    }

    template <class index>
    uint64_t _search_mbr_expand(region &mbr_result, uint64_t nt,
                                       uint64_t &t, int64_t &x, int64_t &y,
                                       uint64_t t_end,
                                       const uint64_t period_snapshot,
                                                    const index* graCT){

        if(nt < graCT->alpha){
            auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
            x = x + inc_p.first;
            y = y + inc_p.second;
            t++;
            if(t <= t_end) check_mbr(mbr_result, x, y, x, y);
        }else{
            region rule_mbr = graCT->get_rule_mbr(nt); //relative mbr of the rule
            auto inc_t = graCT->get_rule_time(nt);
            if(t + inc_t > t_end){
                _search_mbr_expand_last_rule(nt, t_end, t, x, y, mbr_result, graCT);
                t = t_end;
            }else{
                region mbr(rule_mbr.m_min_p.m_x +x, rule_mbr.m_min_p.m_y + y, rule_mbr.m_max_p.m_x + x,
                           rule_mbr.m_max_p.m_y + y); //current mbr
                check_mbr(mbr_result, mbr.m_min_p.m_x, mbr.m_min_p.m_y, mbr.m_max_p.m_x, mbr.m_max_p.m_y);
                point p = graCT->get_rule_point(nt);
                x += p.m_x;
                y += p.m_y;
                t += inc_t;
            }

        }
    }

    template <class index>
    void _search_mbr_expand_all(const log_object* log_obj, region &mbr_result,
                                       const uint64_t oid, const uint64_t t_end,uint64_t t, int64_t x, int64_t y,
                                       uint64_t idx_log, uint64_t log_pos, uint64_t log_reap_movement_pos,
                                       uint64_t log_reap_time_pos, const uint64_t period_snapshot,
                                       const uint64_t n_snapshot, const index* graCT){

        data_traj data;
        bool data_is_null = true;
        while (t < t_end) {
            if (log_pos > log_obj->get_end_log(idx_log, n_snapshot)) {
                bool exist_snap = false;
                bool exist_log = false;
                point p;
                while((idx_log+1) * period_snapshot <= t_end && (idx_log+1) < n_snapshot && !exist_snap && !exist_log){
                    ++idx_log;
                    auto snap = graCT->get_snapshot(idx_log);
                    exist_snap = snap->find_object_position(oid, p);
                    exist_log = log_obj->exist_log(idx_log, n_snapshot);
                    t = idx_log * period_snapshot;
                };
                if(!exist_snap && !exist_log) return;
                log_reap_movement_pos = log_obj->get_start_reap_movement(idx_log);
                log_reap_time_pos = log_obj->get_start_reap_time(idx_log);
                if(exist_snap){
                    x = p.m_x;
                    y = p.m_y;
                    check_mbr(mbr_result, x, y, x, y);
                }else if(exist_log){
                    int64_t log_value = log_obj->get_log_value(log_pos);
                    if (log_value == REAPPEARANCE) {
                        uint16_t d_t;
                        int64_t d_x, d_y;
                        log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                                    d_t, d_x, d_y);
                        t =  t+ d_t;
                        x = d_x;
                        y = d_y;
                        log_reap_movement_pos += 2;
                        log_reap_time_pos++;
                        log_pos++;
                        if(t > t_end) return;
                        check_mbr(mbr_result, x, y, x, y);
                    }
                }

            } else {
                int64_t log_value = log_obj->get_log_value(log_pos);
                if (log_value < 0){
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                                d_t, d_x, d_y);
                    if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                        x = d_x;
                        y = d_y;
                        log_reap_movement_pos += 2;
                    } else if (log_value != REAP_NO_MOVE) {
                        x += d_x;
                        y += d_y;
                        log_reap_movement_pos++;
                    }
                    log_pos++;
                    log_reap_time_pos++;

                    if(log_value != DISAPPEARANCE){
                        t += d_t;
                        if(t > t_end) return;
                        check_mbr(mbr_result, x, y, x, y);
                    }
                }else{
                    if(log_value >= graCT->alpha){
                        graCT->get_rule_mbr(log_value);
                        uint64_t inc_t = graCT->get_rule_time(log_value);
                        if (t + inc_t <= t_end) {
                            region rule_mbr = graCT->get_rule_mbr(log_value); //relative mbr of the rule
                            region mbr(rule_mbr.m_min_p.m_x + x, rule_mbr.m_min_p.m_y + y, rule_mbr.m_max_p.m_x +x,
                                       rule_mbr.m_max_p.m_y+y);
                            check_mbr(mbr_result, mbr.m_min_p.m_x, mbr.m_min_p.m_y, mbr.m_max_p.m_x, mbr.m_max_p.m_y);
                            point inc_p = graCT->get_rule_point(log_value);
                            x = x + inc_p.m_x;
                            y = y + inc_p.m_y;
                            t = t + inc_t;
                        }else{

                            _search_mbr_expand_last_rule(log_value, t_end, t,x,y, mbr_result, graCT);
                            return;
                        }
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        x += inc_p.first;
                        y += inc_p.second;
                        t++;
                        check_mbr(mbr_result, x, y, x, y);

                    }
                    log_pos++;
                }
            }
        }

    }

    template <class index>
    bool _search_mbr_left(const uint64_t oid, const uint64_t t_start, const uint64_t t_end, uint64_t idx_snap,
                                 const uint64_t t_snap, const uint64_t period_snapshot, const uint64_t n_snapshot,
                                 region &mbr_result, const index* graCT) {

#if TIMER
        util_timer::function_timer snaps_timer("snapshot.search_trajectory");
#endif
        auto snap = graCT->get_snapshot(idx_snap);
        point p;
        bool exist_snap = snap->find_object_position(oid, p);
#if TIMER
        snaps_timer.end();
#endif
        uint64_t idx_log = idx_snap;
        auto log_object = graCT->get_log_object(oid);
        uint64_t t = t_snap;
        int64_t x = 0, y = 0, log_value = 0;
        uint64_t log_pos = log_object->get_start_log(idx_log);
        uint64_t log_reap_movement_pos = log_object->get_start_reap_movement(idx_log);
        uint64_t log_reap_time_pos = log_object->get_start_reap_time(idx_log);
        if (exist_snap) {
            x = p.m_x;
            y = p.m_y;
        } else if (log_object->exist_log(idx_log, n_snapshot)) {
#if TIMER
            util_timer::function_timer log_timer("log.search_trajectory");
#endif
            log_value = log_object->get_log_value(log_pos);
            if (log_value == REAPPEARANCE) {
                uint16_t d_t;
                int64_t d_x, d_y;
                log_object->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                            d_t, d_x, d_y);
                t = t + d_t;
                x = d_x;
                y = d_y;
                log_reap_movement_pos += 2;
                log_reap_time_pos++;
                log_pos++;
            } else {
                std::cout << "Error: search_trajectory_left. oid = " << oid << " t_start = " << t_start << std::endl;
                exit(20);
            }
#if TIMER
            log_timer.end();
#endif
        }

        while(t < t_start) {

            if (log_pos > log_object->get_end_log(idx_log, n_snapshot)) {
                idx_snap++;
                idx_log++;
#if TIMER
                util_timer::function_timer snap2_timer("snapshot.search_trajectory");
#endif
                snap = graCT->get_snapshot(idx_snap);
                exist_snap = snap->find_object_position(oid, p);
#if TIMER
                snap2_timer.end();
#endif
                t = idx_snap * period_snapshot;
                if (exist_snap) {
                    x = p.m_x;
                    y = p.m_y;
                }
            }
            if (log_object->exist_log(idx_log, n_snapshot)) {
#if TIMER
                util_timer::function_timer log_timer("log.search_trajectory");
#endif
                log_value = log_object->get_log_value(log_pos);
                if (log_value < 0) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_object->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                                d_t, d_x, d_y);
                    if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                        x = d_x;
                        y = d_y;
                        log_reap_movement_pos += 2;
                    } else if (log_value != REAP_NO_MOVE) {
                        x += d_x;
                        y += d_y;
                        log_reap_movement_pos++;
                    }
                    t += d_t;
                    log_reap_time_pos++;
                } else {
                    if (log_value >= graCT->alpha) {
                        uint64_t inc_t = graCT->get_rule_time((uint64_t) log_value);
                        if (t + inc_t > t_start) {
                            std::stack<uint64_t> rules;
                            bool exist_first = _search_mbr_expand_first_rule_left((uint64_t) log_value, t_start,
                                                                                         t, x, y, p, rules, graCT);
                            if (exist_first) {
                                //Update result
                                mbr_result.m_max_p = p;
                                mbr_result.m_min_p = p;
                                t = t_start;
                                x = p.m_x;
                                y = p.m_y;
                                while (!rules.empty() && t < t_end) {
                                    uint64_t r = rules.top();
                                    _search_mbr_expand(mbr_result, r, t, x, y, t_end, period_snapshot, graCT);
                                    rules.pop();
                                }
                                _search_mbr_expand_all(log_object, mbr_result, oid, t_end, t, x, y, idx_log, log_pos+1,
                                                              log_reap_movement_pos, log_reap_time_pos, period_snapshot,
                                                              n_snapshot, graCT);
                                return true;
                            }
                        } else {
                            point inc_p = graCT->get_rule_point((uint64_t) log_value);
                            t += inc_t;
                            x += inc_p.m_x;
                            y += inc_p.m_y;
                        }

                    } else {
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        x += inc_p.first;
                        y += inc_p.second;
                        t++;
                    }
                }
#if TIMER
                log_timer.end();
#endif
            }
            log_pos++;
        }
        if(t >= t_start && t <= t_end){
#if TIMER
            util_timer::function_timer log_timer("log.search_trajectory");
#endif
            mbr_result.m_max_p.m_x = x;
            mbr_result.m_min_p.m_x = x;
            mbr_result.m_max_p.m_y = y;
            mbr_result.m_min_p.m_y = y;
            _search_mbr_expand_all(log_object, mbr_result, oid, t_end, t, x, y, idx_log, log_pos,
                                          log_reap_movement_pos, log_reap_time_pos, period_snapshot,
                                          n_snapshot, graCT);
            return true;
#if TIMER
            log_timer.end();
#endif
        }
        return false;
    }

    template <class index>
    bool _search_mbr_right(const uint64_t oid, const uint64_t t_start, const uint64_t t_end, uint64_t idx_snap,
                                 const uint64_t t_snap, const uint64_t period_snapshot, const uint64_t n_snapshot,
                                 region &mbr_result, const index* graCT) {

#if TIMER
        util_timer::function_timer snaphot_timer("snapshot.search_trajectory");
#endif
        auto snap = graCT->get_snapshot(idx_snap);
        point p;
        bool exist_snap = snap->find_object_position(oid, p);
#if TIMER
        snaphot_timer.end();
#endif
        uint64_t idx_log = idx_snap -1;
        auto log_object = graCT->get_log_object(oid);
        uint64_t t = t_snap;
        int64_t x = 0, y = 0, log_value = 0;
        uint64_t log_pos = log_object->get_end_log(idx_log, n_snapshot);
        uint64_t log_reap_movement_pos = log_object->get_end_reap_movement(idx_log, n_snapshot);
        uint64_t log_reap_time_pos = log_object->get_end_reap_time(idx_log, n_snapshot);
        if (exist_snap) {
            x = p.m_x;
            y = p.m_y;
        } else if (log_object->exist_log(idx_log, n_snapshot)) {
#if TIMER
            util_timer::function_timer log_timer("log.search_trajectory");
#endif
            log_value = log_object->get_log_value(log_pos);
            if (log_value == DISAPPEARANCE) {
                uint16_t d_t;
                int64_t d_x, d_y;
                log_object->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                            d_t, d_x, d_y);
                t = t - d_t;
                x = d_x;
                y = d_y;
                log_reap_movement_pos -= 2;
                log_reap_time_pos--;
                log_pos--;
            } else {
                std::cout << "Error: search_trajectory_left. oid = " << oid << " t_start = " << t_start << std::endl;
                exit(20);
            }
#if TIMER
            log_timer.end();
#endif
        }

        while(t > t_start) {

            if (log_pos < log_object->get_start_log(idx_log)) {
                idx_snap--;
                idx_log--;
#if TIMER
                util_timer::function_timer snaphot_timer2("snapshot.search_trajectory");
#endif
                snap = graCT->get_snapshot(idx_snap);
                exist_snap = snap->find_object_position(oid, p);
#if TIMER
                snaphot_timer2.end();
#endif
                t = idx_snap * period_snapshot;
                if (exist_snap) {
                    x = p.m_x;
                    y = p.m_y;
                }
            }
            if (log_object->exist_log(idx_log, n_snapshot)) {
#if TIMER
                util_timer::function_timer log_timer("log.search_trajectory");
#endif
                log_value = log_object->get_log_value(log_pos);
                if (log_value < 0) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_object->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                                d_t, d_x, d_y);
                    if (log_value == REAPPEARANCE || log_value == DISAPPEARANCE) {
                        x = d_x;
                        y = d_y;
                        log_reap_movement_pos -= 2;
                    } else if (log_value != REAP_NO_MOVE) {
                        x -= d_x;
                        y -= d_y;
                        log_reap_movement_pos--;
                    }
                    t -= d_t;
                    log_reap_time_pos--;
                } else {
                    if (log_value >= graCT->alpha) {
                        uint64_t inc_t = graCT->get_rule_time((uint64_t) log_value);
                        if (t - inc_t < t_start) {
                            std::stack<uint64_t> rules;
                            bool exist_first = _search_mbr_expand_first_rule_right((uint64_t) log_value, t_start,
                                                                                         t, x, y, p, rules, graCT);
                            if (exist_first) {
                                mbr_result.m_max_p = p;
                                mbr_result.m_min_p = p;
                                t = t_start;
                                x = p.m_x;
                                y = p.m_y;
                                while (!rules.empty() && t < t_end) {
                                    uint64_t r = rules.top();
                                    _search_mbr_expand(mbr_result, r, t, x, y, t_end, period_snapshot, graCT);
                                    rules.pop();
                                }
                                _search_mbr_expand_all(log_object, mbr_result, oid, t_end, t, x, y, idx_log, log_pos+1,
                                                              log_reap_movement_pos+1, log_reap_time_pos+1, period_snapshot,
                                                              n_snapshot, graCT);
                                return true;
                            }
                        } else {
                            point inc_p = graCT->get_rule_point((uint64_t) log_value);
                            t -= inc_t;
                            x -= inc_p.m_x;
                            y -= inc_p.m_y;
                        }

                    } else {
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        x -= inc_p.first;
                        y -= inc_p.second;
                        t--;
                    }
                }
#if TIMER
                log_timer.end();
#endif
            }
            log_pos--;

        }
        if(t >= t_start && t <= t_end){
            mbr_result.m_max_p.m_x = x;
            mbr_result.m_min_p.m_x = x;
            mbr_result.m_max_p.m_y = y;
            mbr_result.m_min_p.m_y = y;
#if TIMER
            util_timer::function_timer log_timer("log.search_trajectory");
#endif
            _search_mbr_expand_all(log_object, mbr_result, oid, t_end, t, x, y, idx_log, log_pos+1,
                                          log_reap_movement_pos+1, log_reap_time_pos+1, period_snapshot,
                                          n_snapshot, graCT);
            return true;
#if TIMER
            log_timer.end();
#endif
        }
        return false;
    }



    template<class index>
    bool search_mbr_run(const uint64_t oid, const uint64_t t_start, const uint64_t t_end,
                               const uint64_t period_snapshot, const uint64_t n_snapshot,
                               const index* graCT, region &mbr_result){

        uint64_t t_after_snap = t_start % period_snapshot;
        uint64_t idx_snap = t_start / period_snapshot;
        uint64_t t_snap = idx_snap * period_snapshot;

        //return _search_object_left(oid, t_q, idx_snap, t_snap, period_snapshot, n_snapshot, p, graCT);
        //Para poder facer busqueda dende a dereita a snapshot da esquerda ten que ser menor que a ultima
        if (t_after_snap > period_snapshot/2 && idx_snap < n_snapshot - 1) {
            idx_snap++;
            t_snap = idx_snap * period_snapshot;
            return _search_mbr_right(oid, t_start, t_end, idx_snap, t_snap, period_snapshot, n_snapshot, mbr_result, graCT);
        } else {
            return _search_mbr_left(oid, t_start, t_end, idx_snap, t_snap, period_snapshot, n_snapshot, mbr_result, graCT);
        }
    }
}

#endif //GRACTV2_SEARCH_MBR_SUPPORT_ALGORITHM_HPP
