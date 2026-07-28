//
// Created by adrian on 15/04/18.
//

#ifndef GRACTV2_TIME_INTERVAL_SUPPORT_ALGORITHM_HPP
#define GRACTV2_TIME_INTERVAL_SUPPORT_ALGORITHM_HPP

#include <stdint.h>
#include <geometry.hpp>
#include <spiral_matrix_coder.hpp>
#include <log_object.hpp>
#include <snapshot.hpp>
#include <global_constants.hpp>

namespace gract_ct {

    template <class index>
    bool _time_interval_expand_left(std::queue<rule_info> &rules, const uint64_t t_start, const uint64_t t_end,
                                    const uint64_t t_start_i, const uint64_t  t_end_i,
                                    const region &r_q, const uint64_t speed_max, const uint64_t max_x,
                                    const uint64_t max_y, const index* graCT){

        rule_info ri = rules.front();
        uint64_t nt = ri.m_value;
        uint64_t dist_max1 = (t_end_i - ri.m_time) * speed_max;
        uint64_t dist_max2 = (ri.m_time - t_start_i) * speed_max;
        region r_s1 = util::expand_point(ri.m_point, dist_max1, max_x, max_y);
        region r_s2 = util::expand_point(ri.m_point, dist_max2, max_x, max_y);
        if(util::touches_region(r_s1, r_q) || util::touches_region(r_s2, r_q)){
            if (nt >= graCT->alpha) {
                region mbr_rule = graCT->get_rule_mbr(nt);
                point p_rule = graCT->get_rule_point(nt);
                uint64_t t_rule = graCT->get_rule_time(nt);
                point p(ri.m_point.m_x + p_rule.m_x, ri.m_point.m_y + p_rule.m_y);
                region mbr(ri.m_point.m_x + mbr_rule.m_min_p.m_x, ri.m_point.m_y + mbr_rule.m_min_p.m_y,
                           ri.m_point.m_x + mbr_rule.m_max_p.m_x, ri.m_point.m_y + mbr_rule.m_max_p.m_y);
                uint64_t t = ri.m_time + t_rule;
                if(t_start <= t && t <= t_end && (util::contains_region(r_q, mbr) || util::contains_point(r_q, p))){
                    return true;
                }else{
                    uint64_t next_nt = graCT->get_left(nt);
                    rules.push(rule_info(next_nt, ri.m_time, ri.m_point));
                    if (next_nt >= graCT->alpha){
                        point next_p_rule = graCT->get_rule_point(next_nt);
                        uint64_t next_t_rule = graCT->get_rule_time(next_nt);
                        point next_p(ri.m_point.m_x + next_p_rule.m_x, ri.m_point.m_y + next_p_rule.m_y);
                        uint64_t next_t = ri.m_time + next_t_rule;
                        rules.push(rule_info(graCT->get_right(nt), next_t, next_p));
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(next_nt));
                        point next_p(ri.m_point.m_x + inc_p.first, ri.m_point.m_y + inc_p.second);
                        uint64_t next_t = ri.m_time + 1;
                        rules.push(rule_info(graCT->get_right(nt), next_t, next_p));
                    }
                }

            } else {
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
                point p(ri.m_point.m_x + inc_p.first, ri.m_point.m_y + inc_p.second);
                uint64_t t = ri.m_time + 1;
                if (util::contains_point(r_q, p) && t >= t_start && t <= t_end) {
                    return true;
                }
            }
            rules.pop();
            if(rules.empty()) return false;
            return _time_interval_expand_left(rules, t_start, t_end, t_start_i, t_end_i, r_q, speed_max, max_x, max_y, graCT);
        }
        return false;
    }

    template <class index>
    bool _time_interval_expand_right(std::queue<rule_info> &rules, const uint64_t t_start, const uint64_t t_end,
                                    const uint64_t t_start_i, const uint64_t  t_end_i,
                                    const region &r_q, const uint64_t speed_max, const uint64_t max_x,
                                    const uint64_t max_y, const index* graCT){

        rule_info ri = rules.front();
        uint64_t nt = ri.m_value;
        uint64_t dist_max1 = (t_end_i - ri.m_time) * speed_max;
        uint64_t dist_max2 = (ri.m_time - t_start_i) * speed_max;
        region r_s1 = util::expand_point(ri.m_point, dist_max1, max_x, max_y);
        region r_s2 = util::expand_point(ri.m_point, dist_max2, max_x, max_y);
        if(util::touches_region(r_s1, r_q) || util::touches_region(r_s2, r_q)){
            if (nt >= graCT->alpha) {
                region mbr_rule = graCT->get_rule_mbr(nt);
                point p_rule = graCT->get_rule_point(nt);
                uint64_t t_rule = graCT->get_rule_time(nt);
                point p(ri.m_point.m_x - p_rule.m_x, ri.m_point.m_y - p_rule.m_y);
                region mbr(p.m_x + mbr_rule.m_min_p.m_x, p.m_y + mbr_rule.m_min_p.m_y,
                           p.m_x + mbr_rule.m_max_p.m_x, p.m_y + mbr_rule.m_max_p.m_y);
                uint64_t t = ri.m_time - t_rule;
                if(t_start <= t && t <= t_end && (util::contains_region(r_q, mbr) || util::contains_point(r_q, p))){
                    return true;
                }else{
                    uint64_t next_nt = graCT->get_right(nt);
                    rules.push(rule_info(next_nt, ri.m_time, ri.m_point));
                    if (next_nt >= graCT->alpha){
                        point next_p_rule = graCT->get_rule_point(next_nt);
                        uint64_t next_t_rule = graCT->get_rule_time(next_nt);
                        point next_p(ri.m_point.m_x - next_p_rule.m_x, ri.m_point.m_y - next_p_rule.m_y);
                        uint64_t next_t = ri.m_time - next_t_rule;
                        rules.push(rule_info(graCT->get_left(nt), next_t, next_p));
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(next_nt));
                        point next_p(ri.m_point.m_x - inc_p.first, ri.m_point.m_y - inc_p.second);
                        uint64_t next_t = ri.m_time - 1;
                        rules.push(rule_info(graCT->get_left(nt), next_t, next_p));
                    }
                }

            } else {
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
                point p(ri.m_point.m_x - inc_p.first, ri.m_point.m_y - inc_p.second);
                uint64_t t = ri.m_time - 1;
                if (util::contains_point(r_q, p) && t >= t_start && t <= t_end) {
                    return true;
                }
            }
            rules.pop();
            if(rules.empty()) return false;
            return _time_interval_expand_right(rules, t_start, t_end, t_start_i, t_end_i, r_q, speed_max, max_x, max_y, graCT);
        }
        return false;
    }

    template<class index>
    bool _time_interval_object_left(const log_object* log_obj, const uint64_t t_start, const uint64_t t_end, const region &r_q,
                                    const uint64_t t_start_i, const uint64_t  t_end_i,
                                    uint64_t t, point p,
                                    uint64_t idx_log, uint64_t log_pos, uint64_t log_reap_movement_pos,
                                    uint64_t log_reap_time_pos,
                                    const uint64_t n_snapshot,
                                    const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                    const index* graCT){

        if(t_start <= t && t <= t_end && util::contains_point(r_q, p)) {
            return true;
        }else{
            uint64_t dist_max = (t_end_i - t) * speed_max;
            region r_s = util::expand_point(p, dist_max, max_x, max_y);
            if(!util::touches_region(r_s, r_q) || t > t_end_i || !log_obj->exist_log(idx_log, n_snapshot)
               || log_pos > log_obj->get_end_log(idx_log, n_snapshot)){
                return false;
            }else{
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
                    return _time_interval_object_left(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                      idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                      speed_max, max_x, max_y, graCT);
                }else{
                    if(log_value >= graCT->alpha){
                        uint64_t time = graCT->get_rule_time(log_value);
                        region mbr_rule = graCT->get_rule_mbr(log_value);
                        point p_rule = graCT->get_rule_point(log_value);
                        point p_i(p.m_x + p_rule.m_x, p.m_y + p_rule.m_y);
                        region mbr_i(p.m_x + mbr_rule.m_min_p.m_x, p.m_y + mbr_rule.m_min_p.m_y,
                                   p.m_x + mbr_rule.m_max_p.m_x, p.m_y + mbr_rule.m_max_p.m_y);
                        uint64_t t_i = t + time;
                        if(t_start_i <= t_i && t_i <= t_end_i &&
                                (util::contains_point(r_q, p_i) || util::contains_region(r_q, mbr_i))) {
                            return true;
                        }else if (util::touches_region(mbr_i, r_q) && t_i >= t_start_i) {
                            std::queue<rule_info> rules;
                            rules.push(rule_info((uint64_t) log_value, t, p));
                            if(_time_interval_expand_left(rules, t_start, t_end, t, t_i, r_q,
                                                          speed_max, max_x, max_y, graCT)) {
                                return true;
                            }
                        }
                        p = p_i;
                        t = t_i;
                        log_pos++;
                        return _time_interval_object_left(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT);
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        p.m_x += inc_p.first;
                        p.m_y += inc_p.second;
                        t++;
                        log_pos++;
                        return _time_interval_object_left(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT);
                    }
                }
            }
        }
    }

    template<class index>
    bool _time_interval_object_right(const log_object* log_obj, const uint64_t t_start, const uint64_t t_end, const region &r_q,
                                    const uint64_t t_start_i, const uint64_t  t_end_i,
                                    uint64_t t, point p,
                                    uint64_t idx_log, uint64_t log_pos, uint64_t log_reap_movement_pos,
                                    uint64_t log_reap_time_pos,
                                    const uint64_t n_snapshot,
                                    const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                    const index* graCT){

        if(t_start <= t && t <= t_end && util::contains_point(r_q, p)) {
            return true;
        }else{
            uint64_t dist_max = (t - t_start_i) * speed_max;
            region r_s = util::expand_point(p, dist_max, max_x, max_y);
            if(!util::touches_region(r_s, r_q) || t < t_start_i || !log_obj->exist_log(idx_log, n_snapshot)
               || log_pos < log_obj->get_start_log(idx_log)){
                return false;
            }else{
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
                    return _time_interval_object_right(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                      idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                      speed_max, max_x, max_y, graCT);
                }else{
                    if(log_value >= graCT->alpha){
                        uint64_t time = graCT->get_rule_time(log_value);
                        region mbr_rule = graCT->get_rule_mbr(log_value);
                        point p_rule = graCT->get_rule_point(log_value);
                        point p_i(p.m_x - p_rule.m_x, p.m_y - p_rule.m_y);
                        region mbr_i(p_i.m_x + mbr_rule.m_min_p.m_x, p_i.m_y + mbr_rule.m_min_p.m_y,
                                     p_i.m_x + mbr_rule.m_max_p.m_x, p_i.m_y + mbr_rule.m_max_p.m_y);
                        uint64_t t_i = t - time;
                        if(t_start_i <= t_i && t_i <= t_end_i &&
                           (util::contains_point(r_q, p_i) || util::contains_region(r_q, mbr_i))) {
                            return true;
                        }else if (util::touches_region(mbr_i, r_q) && t_i <= t_end_i) {
                            std::queue<rule_info> rules;
                            rules.push(rule_info((uint64_t) log_value, t, p));
                            if(_time_interval_expand_right(rules, t_start, t_end, t_i, t, r_q,
                                                          speed_max, max_x, max_y, graCT)) {
                                return true;
                            }
                        }
                        p = p_i;
                        t = t_i;
                        log_pos--;
                        return _time_interval_object_right(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT);
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        p.m_x -= inc_p.first;
                        p.m_y -= inc_p.second;
                        t--;
                        log_pos--;
                        return _time_interval_object_right(log_obj, t_start, t_end, r_q, t_start_i, t_end_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT);
                    }
                }
            }
        }
    }

    template<class index>
    bool _time_interval_first_rule_left(const log_object* log_obj, bool is_reappearance,
                                        const region &r_q, const uint64_t t_start, const uint64_t t_end,
                                        const uint64_t t_end_i,
                                        uint64_t &t, point &p,
                                        uint64_t idx_log, uint64_t &log_pos, uint64_t &log_reap_movement_pos,
                                        uint64_t &log_reap_time_pos, const uint64_t n_snapshot,
                                        const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                        const index* graCT){

        if(log_obj->exist_log(idx_log, n_snapshot)) {
            int64_t log_value = log_obj->get_log_value(log_pos);
            if(is_reappearance) {
                if (log_value == REAPPEARANCE) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                             d_t, d_x, d_y);
                    t =  t+ d_t;
                    p.m_x = d_x;
                    p.m_y = d_y;
                    log_reap_movement_pos += 2;
                    log_reap_time_pos++;
                    log_pos++;
                }
            }
            uint64_t end_log = log_obj->get_end_log(idx_log, n_snapshot);
            uint64_t dist_max = (t_end_i - t) * speed_max;
            region r_s = util::expand_point(p, dist_max, max_x, max_y);
            while(log_pos <= end_log && t < t_start && util::touches_region(r_s, r_q)) {
                log_value = log_obj->get_log_value(log_pos);
                if(log_value < 0){
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, false,
                                             d_t, d_x, d_y);
                    if (log_value == DISAPPEARANCE){
                        return false;
                    }else if(log_value == REAPPEARANCE) {
                        p.m_x = d_x;
                        p.m_y = d_y;
                        log_reap_movement_pos += 2;
                    }else if (log_value != REAP_NO_MOVE) {
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
                        if(t + time <= t_start) {
                            point p_rule = graCT->get_rule_point(log_value);
                            p.m_x += p_rule.m_x;
                            p.m_y += p_rule.m_y;
                            t = t + time;
                            log_pos++;
                        }else {
                            return true;
                        }
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        p.m_x += inc_p.first;
                        p.m_y += inc_p.second;
                        t++;
                        log_pos++;
                    }
                }
                dist_max = (t_end_i - t) * speed_max;
                r_s = util::expand_point(p, dist_max, max_x, max_y);
            }
            return (t <= t_end && util::touches_region(r_s, r_q));
        }
        return false;

    }

    template<class index>
    bool _time_interval_first_rule_right(const log_object* log_obj, bool is_reappearance,
                                        const region &r_q, const uint64_t t_start, const uint64_t t_end,
                                        const uint64_t t_start_i,
                                        uint64_t &t, point &p,
                                        uint64_t idx_log, uint64_t &log_pos, uint64_t &log_reap_movement_pos,
                                        uint64_t &log_reap_time_pos, const uint64_t n_snapshot,
                                        const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                        const index* graCT){

        if(log_obj->exist_log(idx_log, n_snapshot)) {
            int64_t log_value = log_obj->get_log_value(log_pos);
            if(is_reappearance) {
                if (log_value == DISAPPEARANCE) {
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                             d_t, d_x, d_y);
                    t =  t - d_t;
                    p.m_x = d_x;
                    p.m_y = d_y;
                    log_reap_movement_pos -= 2;
                    log_reap_time_pos--;
                    log_pos--;
                }
            }
            uint64_t start_log = log_obj->get_start_log(idx_log);
            uint64_t dist_max = (t - t_start_i) * speed_max;
            region r_s = util::expand_point(p, dist_max, max_x, max_y);
            while(log_pos >= start_log && t > t_end && util::touches_region(r_s, r_q)) {
                log_value = log_obj->get_log_value(log_pos);
                if(log_value < 0){
                    uint16_t d_t;
                    int64_t d_x, d_y;
                    log_obj->get_reappearance(log_reap_time_pos, log_reap_movement_pos, log_value, true,
                                             d_t, d_x, d_y);
                    if (log_value == REAPPEARANCE){
                        return false;
                    } else if (log_value == DISAPPEARANCE) {
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
                        if(t - time >= t_end) {
                            point p_rule = graCT->get_rule_point(log_value);
                            p.m_x -= p_rule.m_x;
                            p.m_y -= p_rule.m_y;
                            t = t - time;
                            log_pos--;
                        }else {
                            return true;
                        }
                    }else{
                        auto inc_p = spiral_matrix_coder::decode(graCT->translate(log_value));
                        p.m_x -= inc_p.first;
                        p.m_y -= inc_p.second;
                        t--;
                        log_pos--;
                    }
                }
                dist_max = (t - t_start_i) * speed_max;
                r_s = util::expand_point(p, dist_max, max_x, max_y);
            }
            return (t <= t_end && util::touches_region(r_s, r_q));
        }
        return false;

    }

    template <class index>
    std::vector<uint64_t> _time_interval_left(uint64_t idx_snap, const region& r_q,
                                              const uint64_t t_start, const uint64_t t_end,
                                           const uint64_t period_snapshot,
                                           const uint64_t n_snapshot, const uint64_t n_objects,
                                           const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                           const index* graCT){


        std::vector<uint64_t > results;
        sdsl::bit_vector solution(n_objects, 0);
        uint64_t t_snap = idx_snap * period_snapshot;
        while(t_snap <= t_end && results.size() < n_objects){
            uint64_t t_end_i = (idx_snap+1)* period_snapshot;
            uint64_t idx_log = idx_snap;
            if(t_end_i > t_end) t_end_i = t_end;
            uint64_t dist_max = (t_end_i - t_snap) * speed_max;
            region r_i = r_q.expand(dist_max, max_x, max_y);
            auto snap = graCT->get_snapshot(idx_snap);
            std::vector<id_point> candidates = snap->find_objects_in_region(r_i.m_min_p.m_x, r_i.m_max_p.m_x,
                                                                           r_i.m_min_p.m_y, r_i.m_max_p.m_y);
            sdsl::bit_vector checked(n_objects, 0);
            for(uint64_t i = 0; i < candidates.size(); i++){
                uint64_t oid = candidates[i].m_id;
                if(/*!checked[oid] &&*/ !solution[oid]){
                    auto log_obj = graCT->get_log_object(oid);
                    uint64_t log_pos = log_obj->get_start_log(idx_log);
                    uint64_t log_reap_movement_pos = log_obj->get_start_reap_movement(idx_log);
                    uint64_t log_reap_time_pos = log_obj->get_start_reap_time(idx_log);
                    uint64_t t = t_snap;
                    point p = candidates[i].m_point;
                    if(t_start <= t && t <= t_end && util::contains_point(r_q, p)){
                        results.push_back(oid);
                        solution[oid]=1;
                    }else if(_time_interval_first_rule_left(log_obj, false, r_q, t_start, t_end, t_end_i, t, p,
                                                      idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                      speed_max, max_x, max_y, graCT)
                       && _time_interval_object_left(log_obj, t_start, t_end, r_q, t_snap, t_end_i, t, p, idx_log,
                                                     log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                     speed_max, max_x, max_y, graCT)){
                        results.push_back(oid);
                        solution[oid]=1;
                    };
                    checked[oid] = 1;
                }
            }
            sdsl::int_vector<> reap_candidates = graCT->reap[idx_log];
            for(uint64_t i = 0; t_snap < t_end && i < reap_candidates.size(); i++){
                uint64_t oid = reap_candidates[i];
                if(/*!checked[oid] &&*/ !solution[oid]){
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
                        point p(d_x, d_y);
                        log_reap_movement_pos += 2;
                        log_reap_time_pos++;
                        log_pos++;
                        if(t_start <= t && t <= t_end && util::contains_point(r_q, p)){
                            results.push_back(oid);
                            solution[oid]=1;
                        }else if(_time_interval_first_rule_left(log_obj, false, r_q, t_start, t_end, t_end_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT)
                           && _time_interval_object_left(log_obj, t_start, t_end, r_q, t_snap, t_end_i, t, p, idx_log,
                                                         log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                         speed_max, max_x, max_y, graCT)){
                            results.push_back(oid);
                            solution[oid]=1;
                        };
                        checked[oid] = 1;
                    } else {
                        std::cout << "Error: search_object_left. oid = " << oid << " t = " << std::endl;
                        exit(20);
                    }
                }
            }
            idx_snap++;
            t_snap = idx_snap * period_snapshot;
        }
        return results;
    }

    template <class index>
    std::vector<uint64_t> _time_interval_right(uint64_t idx_snap, const region& r_q,
                                              const uint64_t t_start, const uint64_t t_end,
                                              const uint64_t period_snapshot,
                                              const uint64_t n_snapshot, const uint64_t n_objects,
                                              const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                              const index* graCT){

        std::vector<uint64_t > results;
        sdsl::bit_vector solution(n_objects, 0);
        uint64_t t_snap = idx_snap * period_snapshot;
        while(t_snap >= t_start && results.size() < n_objects){
            uint64_t t_start_i = (idx_snap-1)* period_snapshot;
            uint64_t idx_log = idx_snap-1;
            if(t_start_i < t_start) t_start_i = t_start;

            sdsl::bit_vector checked(n_objects, 0);
            uint64_t dist_max = (t_snap - t_start_i) * speed_max;
            region r_i = r_q.expand(dist_max, max_x, max_y);
            auto snap = graCT->get_snapshot(idx_snap);
            std::vector<id_point> candidates = snap->find_objects_in_region(r_i.m_min_p.m_x, r_i.m_max_p.m_x,
                                                                            r_i.m_min_p.m_y, r_i.m_max_p.m_y);
            for(uint64_t i = 0; i < candidates.size(); i++){
                uint64_t oid = candidates[i].m_id;
                if(/*!checked[oid] && */!solution[oid]){
                    auto log_obj = graCT->get_log_object(oid);
                    uint64_t log_pos = log_obj->get_end_log(idx_log, n_snapshot);
                    uint64_t log_reap_movement_pos = log_obj->get_end_reap_movement(idx_log, n_snapshot);
                    uint64_t log_reap_time_pos = log_obj->get_end_reap_time(idx_log, n_snapshot);
                    uint64_t t = t_snap;
                    point p = candidates[i].m_point;
                    if(t_start <= t && t <= t_end && util::contains_point(r_q, p)){
                        results.push_back(oid);
                        solution[oid]=1;
                    }else if(_time_interval_first_rule_right(log_obj, false, r_q, t_start, t_end, t_start_i, t, p,
                                                             idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                             speed_max, max_x, max_y, graCT)
                             && _time_interval_object_right(log_obj, t_start, t_end, r_q, t_start_i, t_snap, t, p, idx_log,
                                                            log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                            speed_max, max_x, max_y, graCT)){
                        results.push_back(oid);
                        solution[oid]=1;
                    };
                    checked[oid] = 1;
                }
            }

            sdsl::int_vector<> disap_candidates = graCT->disap[idx_log];
            for(uint64_t i = 0; t_snap > t_start && i < disap_candidates.size(); i++){
                uint64_t oid = disap_candidates[i];
                if( /*!checked[oid] && */ !solution[oid]){
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
                        point p(d_x, d_y);
                        log_reap_movement_pos -= 2;
                        log_reap_time_pos--;
                        log_pos--;
                        if(t_start <= t && t <= t_end && util::contains_point(r_q, p)){
                            results.push_back(oid);
                            solution[oid]=1;
                        }else if(_time_interval_first_rule_right(log_obj, true, r_q, t_start, t_end, t_start_i, t, p,
                                                          idx_log, log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                          speed_max, max_x, max_y, graCT)
                           && _time_interval_object_right(log_obj, t_start, t_end, r_q, t_start_i, t_snap, t, p, idx_log,
                                                         log_pos, log_reap_movement_pos, log_reap_time_pos, n_snapshot,
                                                         speed_max, max_x, max_y, graCT)){
                            results.push_back(oid);
                            solution[oid]=1;
                        };
                        checked[oid] = 1;
                    } else {
                        std::cout << "Error: search_object_left. oid = " << oid << " t = " << std::endl;
                        exit(20);
                    }
                }
            }

            idx_snap--;
            t_snap = idx_snap * period_snapshot;
        }
        return results;
    }


    template<class index>
    std::vector<uint64_t > time_interval_run(const region& r_q, const uint64_t t_start, const uint64_t t_end,
                                             const uint64_t period_snapshot,
                                         const uint64_t n_snapshot, const uint64_t n_objects,
                                         const uint64_t speed_max, const uint64_t max_x, const uint64_t max_y,
                                         const index* graCT){

        uint64_t idx_snap_left =  t_start / period_snapshot;
        uint64_t idx_snap_right =  t_end / period_snapshot + 1;
        uint64_t t_snap_left = idx_snap_left * period_snapshot;
        uint64_t t_snap_right = idx_snap_right * period_snapshot;
        if (idx_snap_right < n_snapshot && t_start - t_snap_left > t_snap_right - t_end) {
            return _time_interval_right(idx_snap_right, r_q, t_start, t_end, period_snapshot, n_snapshot, n_objects, speed_max,
                                        max_x, max_y, graCT);
        } else {
            return _time_interval_left(idx_snap_left, r_q, t_start, t_end, period_snapshot, n_snapshot, n_objects, speed_max,
                                       max_x, max_y, graCT);
        }

    }

}

#endif //GRACTV2_TIME_INTERVAL_SUPPORT_ALGORITHM_HPP
