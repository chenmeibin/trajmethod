//
// Created by adrian on 12/12/17.
//

#ifndef SUCCINCTCT_DATA_TRAJ_HPP
#define SUCCINCTCT_DATA_TRAJ_HPP

namespace gract_ct{

    class data_traj {
    public:
        typedef uint32_t value_type;
        typedef uint32_t time_type;
        value_type m_id;
        time_type m_t;
        value_type m_x;
        value_type m_y;
        data_traj(value_type id, time_type t, value_type x, value_type y){
            m_id = id;
            m_t = t;
            m_x = x;
            m_y = y;
        }

        data_traj(){}
    };
}

#endif //SUCCINCTCT_DATA_TRAJ_HPP
