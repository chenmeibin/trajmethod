//
// Created by adrian on 8/01/18.
//

#ifndef SUCCINCTCT_UTIL_CT_HPP
#define SUCCINCTCT_UTIL_CT_HPP

#include <queue>
#include <vector>
#include <float.h>

namespace gract_ct {

    namespace util{




        class pq_knn_candidate_element {
        public:
            uint64_t id;
            uint64_t t;
            uint64_t x;
            uint64_t y;
            uint64_t weight;
            uint64_t distance;
            uint64_t logIdx = 0;
            uint64_t posLog = 0;
            uint64_t posMove = 0;
            uint64_t posTime = 0;

        private:

            void copy(const pq_knn_candidate_element &p) {
                id = p.id;
                t = p.t;
                x = p.x;
                y = p.y;
                distance = p.distance;
                weight = p.weight;
                logIdx = p.logIdx;
                posLog = p.posLog;
                posMove = p.posMove;
                posTime = p.posTime;
            }

        public:

            pq_knn_candidate_element() = default;

            pq_knn_candidate_element(uint64_t p_id, uint64_t p_t, uint64_t p_x, uint64_t p_y, uint64_t p_distance, uint64_t p_weight){
                id = p_id;
                t = p_t;
                x = p_x;
                y = p_y;
                distance = p_distance;
                weight = p_weight;
            }
            //! Copy constructor
            pq_knn_candidate_element(const pq_knn_candidate_element &p) {
                copy(p);
            }

            //! Move constructor
            pq_knn_candidate_element(pq_knn_candidate_element &&p) {
                *this = std::move(p);
            }

            //! Assignment move operation
            pq_knn_candidate_element &operator=(pq_knn_candidate_element &&p) {
                if (this != &p) {
                    id = std::move(p.id);
                    x = std::move(p.x);
                    y = std::move(p.y);
                    t = std::move(p.t);
                    weight = std::move(p.weight);
                    distance = std::move(p.distance);
                    posLog = std::move(p.posLog);
                    posMove = std::move(p.posMove);
                    posTime = std::move(p.posTime);

                }
                return *this;
            }

            //! Assignment operator
            pq_knn_candidate_element &operator=(const pq_knn_candidate_element &p) {
                if (this != &p) {
                    copy(p);
                }
                return *this;
            }

            //! Swap method
            /*! Swaps the content of the two data structure.
             *  You have to use set_vector to adjust the supported bit_vector.
             *  \param bp_support Object which is swapped.
             */
            void swap(pq_knn_candidate_element &p) {
                // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
                std::swap(id, p.id);
                std::swap(x, p.x);
                std::swap(y, p.y);
                std::swap(t, p.t);
                std::swap(distance, p.distance);
                std::swap(weight, p.weight);
                std::swap(posLog, p.posLog);
                std::swap(posMove, p.posMove);
                std::swap(posTime, p.posTime);
            }
        };

        class pq_knn_candidate_comparator {
        public:
            bool operator()(const pq_knn_candidate_element &lce, const pq_knn_candidate_element &rce){
                if(lce.weight == rce.weight){
                    return lce.distance > rce.distance;
                }
                return lce.weight > rce.weight;
            }
        };

        class pq_knn_region_element {
        private:
            uint64_t m_id;
            uint64_t m_minx;
            uint64_t m_miny;
            uint64_t m_level;
            uint64_t m_movements_point;
        public:
            const uint64_t &id = m_id;
            const uint64_t &minx = m_minx;
            const uint64_t &miny = m_miny;
            const uint64_t &level = m_level;
            const uint64_t &movements_point = m_movements_point;

        private:

            void copy(const pq_knn_region_element &p) {
                m_id = p.m_id;
                m_minx = p.m_minx;
                m_miny = p.m_miny;
                m_level = p.m_level;
                m_movements_point = p.m_movements_point;
            }

        public:
            pq_knn_region_element(){
                m_id = 0;
                m_minx = 0;
                m_miny = 0;
                m_level = 0;
                m_movements_point = UINT64_MAX;
            };

            pq_knn_region_element(uint64_t id, uint64_t minx, uint64_t miny, uint64_t level, uint64_t movements_point){
                m_id = id;
                m_minx = minx;
                m_miny = miny;
                m_level = level;
                m_movements_point = movements_point;
            }



            //! Copy constructor
            pq_knn_region_element(const pq_knn_region_element &p) {
                copy(p);
            }

            //! Move constructor
            pq_knn_region_element(pq_knn_region_element &&p) {
                *this = std::move(p);
            }

            //! Assignment move operation
            pq_knn_region_element &operator=(pq_knn_region_element &&p) {
                if (this != &p) {
                    m_id = std::move(p.m_id);
                    m_minx = std::move(p.m_minx);
                    m_miny = std::move(p.m_miny);
                    m_level = std::move(p.m_level);
                    m_movements_point = std::move(p.m_movements_point);
                }
                return *this;
            }

            //! Assignment operator
            pq_knn_region_element &operator=(const pq_knn_region_element &p) {
                if (this != &p) {
                    copy(p);
                }
                return *this;
            }

            //! Swap method
            /*! Swaps the content of the two data structure.
             *  You have to use set_vector to adjust the supported bit_vector.
             *  \param bp_support Object which is swapped.
             */
            void swap(pq_knn_region_element &p) {
                // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
                std::swap(m_id, p.m_id);
                std::swap(m_minx, p.m_minx);
                std::swap(m_miny, p.m_miny);
                std::swap(m_level, p.m_level);
                std::swap(m_movements_point, p.m_movements_point);
            }
        };

        class pq_knn_region_comparator{
        public:
            bool operator()(pq_knn_region_element &lre, pq_knn_region_element &rre) {
                return lre.movements_point > rre.movements_point;
            }
        };

        typedef std::priority_queue<pq_knn_region_element, std::vector<pq_knn_region_element>, pq_knn_region_comparator> pq_knn_region;
        typedef std::priority_queue<pq_knn_candidate_element, std::vector<pq_knn_candidate_element>, pq_knn_candidate_comparator> pq_knn_candidate;
        typedef std::priority_queue<uint64_t , std::vector<uint64_t >, std::less<uint64_t >> pq_knn_movement;
        typedef std::priority_queue<double_t , std::vector<double_t >, std::less<double_t >> pq_knn_distance;

    }

}

#endif //SUCCINCTCT_UTIL_CT_HPP
