//
// Created by adrian on 29/12/17.
//

#ifndef GRACTV2_SEARCH_OBJECT_SUPPORT_ALGORITHM_HPP
#define GRACTV2_SEARCH_OBJECT_SUPPORT_ALGORITHM_HPP

#include <stdint.h>
#include <geometry.hpp>
#include <spiral_matrix_coder.hpp>
#include <log_object.hpp>
#include <global_constants.hpp>



namespace gract_ct {

        template<class index>
        bool _search_object_expand_left(const uint64_t nt, const uint64_t t_q,
                                        uint64_t t, int64_t x, int64_t y,
                                        point &p,
                                        const index* graCT) {

            if (nt >= graCT->alpha) {
                uint64_t v1 = graCT->get_left(nt);
                uint64_t v2 = graCT->get_right(nt);
                if (v1 < graCT->alpha) {
                    auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                    x = x + inc_p.first;
                    y = y + inc_p.second;
                    t++;
                    if (t < t_q) {
                        return _search_object_expand_left(v2, t_q, t, x, y, p, graCT);
                    } else if (t == t_q) {
                        //Queda sen expandir a da dereita
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
                        return _search_object_expand_left(v2, t_q, t, x, y, p, graCT);
                    } else {
                        //expando a da esquerda
                        return _search_object_expand_left(v1, t_q, t, x, y, p, graCT);
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
                    return false ;
                }
            }
        }

        template <class index>
        bool _search_object_expand_right(const uint64_t nt, const uint64_t t_q,
                                         uint64_t t, int64_t x, int64_t y,
                                         point &p,
                                         const index* graCT) {

            if (nt >= graCT->alpha) {
                uint64_t v1 = graCT->get_right(nt);
                uint64_t v2 = graCT->get_left(nt);
                if (v1 < graCT->alpha) {
                    auto inc_p = spiral_matrix_coder::decode(graCT->translate(v1));
                    x = x - inc_p.first;
                    y = y - inc_p.second;
                    t--;
                    if (t > t_q) {
                        return _search_object_expand_right(v2, t_q, t, x, y, p, graCT);
                    } else if (t == t_q) {
                        //Queda sen expandir a da dereita
                        p = point(x, y);
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
                        p= point(x, y);
                        return true;
                    } else if (t - inc_t > t_q) {
                        //expando a da dereita
                        point inc_p = graCT->get_rule_point(v1);
                        x = x - inc_p.m_x;
                        y = y - inc_p.m_y;
                        t = t - inc_t;
                        return _search_object_expand_right(v2, t_q, t, x, y, p, graCT);
                    } else {
                        //expando a da esquerda
                        return _search_object_expand_right(v1, t_q, t, x, y, p, graCT);
                        //Queda sen expandir a da dereita
                    }
                }
            } else {
                auto inc_p = spiral_matrix_coder::decode(graCT->translate(nt));
                x = x - inc_p.first;
                y = y - inc_p.second;
                t--;
                if (t == t_q) {
                    p = point(x, y);
                    return true;
                } else {
                    return false ;
                }
            }
        }

        template <class index>
        bool _search_object_left(const uint64_t oid, const uint64_t t_q, const uint64_t idx_snap, const uint64_t t_snap,
                                 const uint64_t period_snapshot, const uint64_t n_snapshot, point& p, const index* graCT) {

#if TIMER
            util_timer::function_timer snaphot_timer("snapshot.search_object");
#endif
            auto snap = graCT->get_snapshot(idx_snap);
            bool exist_snap = snap->find_object_position(oid, p);
#if TIMER
            snaphot_timer.end();
#endif
            if (t_q == t_snap) {
                return exist_snap;
            } else {
#if TIMER
                util_timer::function_timer log_timer("log.search_object");
#endif
                auto log_object = graCT->get_log_object(oid);
                uint64_t idx_log = t_q / period_snapshot;
                if (log_object->exist_log(idx_log, n_snapshot)) {
                    uint64_t t = t_snap;
                    int64_t x = 0, y = 0, log_value = 0;
                    uint64_t log_pos = log_object->get_start_log(idx_log);
                    uint64_t log_reap_movement_pos = log_object->get_start_reap_movement(idx_log);
                    uint64_t log_reap_time_pos = log_object->get_start_reap_time(idx_log);
                    if (exist_snap) {
                        x = p.m_x;
                        y = p.m_y;
                    } else {
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
                            std::cout << "Error: search_object_left. oid = " << oid << " t = " << t_q << std::endl;
                            exit(20);
                        }
                    }

                    uint64_t end_log = log_object->get_end_log(idx_log, n_snapshot);
                    while (log_pos <= end_log && t < t_q) {
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
                                point inc2_p = graCT->get_rule_point((uint64_t) log_value);
                                if (t + inc_t > t_q) {
                                    return _search_object_expand_left((uint64_t) log_value, t_q, t, x, y, p, graCT);
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
                        log_pos++;
                    }
                    if (t == t_q){
                        p = point(x, y);
#if TIMER
                        log_timer.end();
#endif
                        return true;
                    }

                }
#if TIMER
                log_timer.end();
#endif
                return false;
            }
        }

        template<class index>
        bool _search_object_right(const uint64_t oid, const uint64_t t_q, const uint64_t idx_snap, const uint64_t t_snap,
                                  const uint64_t period_snapshot, const uint64_t n_snapshot, point &p, const index* graCT) {

#if TIMER
            util_timer::function_timer snaphot_timer("snapshot.search_object");
#endif
            auto snap = graCT->get_snapshot(idx_snap);
            bool exist_snap = snap->find_object_position(oid, p);
#if TIMER
            snaphot_timer.end();
#endif
            if (t_q == t_snap) {
                return exist_snap;
            } else {
#if TIMER
                util_timer::function_timer log_timer("log.search_object");
#endif
                auto log_object = graCT->get_log_object(oid);
                uint64_t idx_log = t_q / period_snapshot;
                if (log_object->exist_log(idx_log, n_snapshot)) {
                    uint64_t t = t_snap;
                    int64_t x = 0, y = 0, log_value = 0;
                    uint64_t log_pos = log_object->get_end_log(idx_log, n_snapshot);
                    uint64_t log_reap_movement_pos = log_object->get_end_reap_movement(idx_log, n_snapshot);
                    uint64_t log_reap_time_pos = log_object->get_end_reap_time(idx_log, n_snapshot);
                    if (exist_snap) {
                        x = p.m_x;
                        y = p.m_y;
                    } else {
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
                            std::cout << "Error: search_object_right. oid = " << oid << " t = " << t_q << std::endl;
                            exit(20);
                        }
                    }

                    uint64_t start_log = log_object->get_start_log(idx_log);
                    while (log_pos >= start_log && t > t_q) {
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
                                if (t - inc_t < t_q) {
                                    return _search_object_expand_right((uint64_t) log_value, t_q, t, x, y, p, graCT);
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
                        log_pos--;
                    }
                    if (t == t_q) {
                        p = point(x, y);
#if TIMER
                        log_timer.end();
#endif
                        return true;
                    }

                }
#if TIMER
                log_timer.end();
#endif
                return false;
            }
        }

        template<class index>
        bool search_object_run(const uint64_t oid, const uint64_t t_q, const uint64_t period_snapshot,
                                            const uint64_t n_snapshot, point &p, const index* graCT){
            uint64_t t_after_snap = t_q % period_snapshot;
            uint64_t idx_snap = t_q / period_snapshot;
            uint64_t t_snap = idx_snap * period_snapshot;
            //return _search_object_left(oid, t_q, idx_snap, t_snap, period_snapshot, n_snapshot, p, graCT);
            //Para poder facer busqueda dende a dereita a snapshot da esquerda ten que ser menor que a ultima
            if (t_after_snap > period_snapshot/2 && idx_snap < n_snapshot - 1) {
                idx_snap++;
                t_snap = idx_snap * period_snapshot;
                return _search_object_right(oid, t_q, idx_snap, t_snap, period_snapshot, n_snapshot, p, graCT);
            } else {
                return _search_object_left(oid, t_q, idx_snap, t_snap, period_snapshot, n_snapshot, p, graCT);
            }
        }




}

#endif //GRACTV2_SEARCH_OBJECT_SUPPORT_ALGORITHM_HPP