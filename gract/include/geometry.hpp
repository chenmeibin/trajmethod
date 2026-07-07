//
// Created by adrian on 24/04/17.
//

#ifndef SUCCINCTCT_GEOMETRY_HPP
#define SUCCINCTCT_GEOMETRY_HPP

#include <sdsl/int_vector.hpp>

namespace gract_ct {

    class point {
    public:
        typedef int64_t value_type;
        value_type m_x;
        value_type m_y;
        point(){};
        point(value_type x, value_type y){
            m_x = x;
            m_y = y;
        }
    };

    class region {
    public:
        typedef point::value_type value_type;
        typedef sdsl::int_vector<>::size_type size_type;
        point m_min_p;
        point m_max_p;
        region(){};
        region(value_type min_x, value_type min_y, value_type max_x, value_type max_y){
            m_min_p = point(min_x, min_y);
            m_max_p = point(max_x, max_y);
        }
        region(point min_p, point max_p){
            m_min_p = min_p;
            m_max_p = max_p;
        }

        region expand(value_type dist_max, value_type max_value_x, value_type max_value_y) const{
            uint64_t max_x = m_max_p.m_x + dist_max;
            uint64_t max_y = m_max_p.m_y + dist_max;
            uint64_t min_x = m_min_p.m_x - dist_max;
            uint64_t min_y = m_min_p.m_y - dist_max;
            if(m_min_p.m_x < dist_max){
                min_x = 0;
            }
            if(m_min_p.m_y < dist_max){
                min_y = 0;
            }
            if(max_x > max_value_x){
                max_x = max_value_x;
            }
            if(max_y > max_value_y){
                max_y = max_value_y;
            }
            return region(min_x, min_y, max_x, max_y);

        }
    };


    class id_point {
    public:
        typedef int64_t value_type;
        typedef uint64_t id_type;
        id_type m_id;
        point m_point;
        id_point(){};
        id_point(value_type id, point p){
            m_id = id;
            m_point = p;
        }
        id_point(value_type id, value_type x, value_type y){
            m_id = id;
            m_point.m_x = x;
            m_point.m_y = y;
        }

        bool operator< (const id_point &ip) const{
            return m_id < ip.m_id;
        }


    };

    class rule_info{
    public:
        uint64_t m_value;
        uint64_t m_time;
        point m_point;

        rule_info(uint64_t value, uint64_t time, point p){
            m_value = value;
            m_time = time;
            m_point = p;
        }
    };

    namespace util{

        typedef sdsl::int_vector<>::size_type size_type;

        size_type ceil_div(size_type x, size_type d){
            return (x + d - 1) / d;
        }

        /** Retrieve a given index from array A where every value uses len bits
	 * @param A Array
	 * @param len Length in bits of each field
	 * @param index Position to be retrieved
	 */
        inline uint get_field(const uint *A, const size_t len, const size_t index) {
            if(len==0) return 0;
            size_t i=index*len/64, j=index*len-64*i;
            uint result;
            if (j+len <= 64)
                result = (A[i] << (64-j-len)) >> (64-len);
            else {
                result = A[i] >> j;
                result = result | (A[i+1] << (64*64-j-len)) >> (64-len);
            }
            return result;
        }

        /** Store a given value in index into array A where every value uses len bits
         * @param A Array
         * @param len Length in bits of each field
         * @param index Position to store in
         * @param x Value to be stored
         */
        inline void set_field(uint *A, const size_t len, const size_t index, const uint x) {
            if(len==0) return;
            size_t i=index*len/64, j=index*len-i*64;
            uint mask = ((j+len) < 64 ? ~0u << (j+len) : 0)
                        | ((64-j) < 64 ? ~0u >> (64-j) : 0);
            A[i] = (A[i] & mask) | x << j;
            if (j+len>64) {
                mask = ((~0u) << (len+j-64));
                A[i+1] = (A[i+1] & mask)| x >> (64-j);
            }
        }

        void expand_coord(const size_type x, const size_type dist_max, point::value_type max_value_x, size_type &min_x, size_type &max_x){
            point::value_type max_x_aux = x + dist_max;
            point::value_type min_x_aux = x - dist_max;
            if(min_x_aux < 0){
                min_x = 0;
            }else{
                min_x = (size_type) min_x_aux;
            }
            if(max_x_aux > max_value_x){
                max_x = (size_type) max_value_x;
            }else{
                max_x = (size_type) max_x_aux;
            }
        }

        region expand_point(const point &p, uint64_t dist_max, uint64_t max_value_x,
                            uint64_t max_value_y){
            point::value_type max_x = p.m_x + dist_max;
            point::value_type max_y = p.m_y + dist_max;
            point::value_type min_x = p.m_x - dist_max;
            point::value_type min_y = p.m_y - dist_max;
            if(min_x < 0){
                min_x = 0;
            }
            if(min_y < 0){
                min_y = 0;
            }
            if(max_x > max_value_x){
                max_x = max_value_x;
            }
            if(max_y > max_value_y){
                max_y = max_value_y;
            }
            return region(min_x, min_y, max_x, max_y);

        }

        double_t distance(const region &r, const point &p){
            double_t x = 0, y = 0;
            if(p.m_x < r.m_min_p.m_x){
                x = r.m_min_p.m_x - p.m_x;
            }else if(p.m_x > r.m_max_p.m_x){
                x =p.m_x - r.m_max_p.m_x;
            }
            if(p.m_y < r.m_min_p.m_y){
                y = r.m_min_p.m_y - p.m_y;
            }else if(p.m_y > r.m_max_p.m_y){
                y = p.m_y - r.m_max_p.m_y;
            }
            return std::sqrt(x * x + y * y);
        }

        uint64_t distance_knn(const region &r, const point &p){
            int64_t x = 0, y = 0;
            if(p.m_x < r.m_min_p.m_x){
                x = r.m_min_p.m_x - p.m_x;
            }else if(p.m_x > r.m_max_p.m_x){
                x =p.m_x - r.m_max_p.m_x;
            }
            if(p.m_y < r.m_min_p.m_y){
                y = r.m_min_p.m_y - p.m_y;
            }else if(p.m_y > r.m_max_p.m_y){
                y = p.m_y - r.m_max_p.m_y;
            }
            return static_cast<uint64_t >(x * x + y * y);
        }

        uint64_t max_distance_knn(const region &r, const point &p){
            uint64_t x, y;
            if (p.m_x < r.m_min_p.m_x + (r.m_max_p.m_x - r.m_min_p.m_x) / 2) {
                x = r.m_max_p.m_x - p.m_x;
            } else {
                x = p.m_x - r.m_min_p.m_x;
            }
            if(p.m_y < r.m_min_p.m_y + (r.m_max_p.m_y - r.m_min_p.m_y)/2){
                y = r.m_max_p.m_y - p.m_y;
            }else{
                y = p.m_y - r.m_min_p.m_y;
            }
            return static_cast<uint64_t >(x * x + y * y);
        }

        double_t distance(const point &p, const point &p1){
            double_t x = 0, y = 0;
            x = std::abs(p1.m_x - p.m_x);
            y = std::abs(p1.m_y - p.m_y);
            return std::sqrt(x * x + y * y);
        }

        uint64_t distance_knn(const point &p, const point &p1){
            int64_t x = 0, y = 0;
            x = p1.m_x - p.m_x;
            y = p1.m_y - p.m_y;
            return static_cast<uint64_t >(x * x + y * y);
        }

        bool touches_coord(size_type p_min, size_type p_max, size_type r_min, size_type r_max){
            return !(p_min > r_max || p_max < r_min);
        }

        bool touches_region(const region &obj_region, const region &q_region){
            return !(obj_region.m_min_p.m_x > q_region.m_max_p.m_x || obj_region.m_max_p.m_x < q_region.m_min_p.m_x
                     || obj_region.m_min_p.m_y > q_region.m_max_p.m_y || obj_region.m_max_p.m_y < q_region.m_min_p.m_y);
        }

        bool contains_region(const region &m_region, const region &c_region){
            return (m_region.m_min_p.m_x <= c_region.m_min_p.m_x && c_region.m_min_p.m_x <= m_region.m_max_p.m_x
                    && m_region.m_min_p.m_x <= c_region.m_max_p.m_x && c_region.m_max_p.m_x <= m_region.m_max_p.m_x
                    && m_region.m_min_p.m_y <= c_region.m_min_p.m_y && c_region.m_min_p.m_y <= m_region.m_max_p.m_y
                    && m_region.m_min_p.m_y <= c_region.m_max_p.m_y && c_region.m_max_p.m_y <= m_region.m_max_p.m_y);
        }

        bool contains_point(const region &m_region, const point &c_point){
            return (m_region.m_min_p.m_x <= c_point.m_x && c_point.m_x <= m_region.m_max_p.m_x
                    && m_region.m_min_p.m_y <= c_point.m_y && c_point.m_y <= m_region.m_max_p.m_y);
        }

        bool contains_interval(const size_type &m_t_start, const size_type &m_t_end,
                               const size_type &c_t_start, const size_type &c_t_end){
            return (m_t_start <= c_t_start && m_t_end >= c_t_end);
        }

        bool touches_interval(const size_type &m_t_start, const size_type &m_t_end,
                              const size_type &i_t_start, const size_type &i_t_end){
            return !(m_t_start > i_t_end || m_t_end < i_t_start );
        }
    };


}

#endif //SUCCINCTCT_GEOMETRY_HPP
