//
// Created by adrian on 28/12/17.
//

#ifndef GRACTV2_LOG_OBJECT_HPP
#define GRACTV2_LOG_OBJECT_HPP

#include <global_constants.hpp>
#include <sdsl/int_vector.hpp>
#include <dac.hpp>
#include <spiral_matrix_coder.hpp>
#include <vector>

namespace gract_ct {

    class log_object {

    private:
        std::vector<int32_t> m_log;
        dac_vector_v2<16>  m_reap_movement;
        std::vector<uint16_t> m_reap_time;

        void copy(const log_object& p){
            m_log = p.m_log;
            m_reap_movement = p.m_reap_movement;
            m_reap_time = p.m_reap_time;
        }

    public:
        const dac_vector_v2<16>& reap_movement = m_reap_movement;

        log_object(){};

        template<class Container>
        log_object(const Container &log, const Container &reap_movement, const Container &reap_time){
            m_log = std::vector<int32_t >(log.size());
            for(uint64_t i = 0; i < log.size(); i++){
                m_log[i] = log[i];
            }
            m_reap_time = std::vector<uint16_t>(reap_time.size());
            for(uint64_t i = 0; i < reap_time.size(); i++){
                m_reap_time[i] = reap_time[i];
            }
            m_reap_movement = dac_vector_v2<16>(reap_movement);
        }

        uint64_t get_start_log(const uint64_t snap_idx) const{
            return m_log[snap_idx];
        }

        uint64_t get_end_log(const uint64_t snap_idx, const uint64_t n_snapshots) const{
            if(snap_idx < n_snapshots-1){
                return m_log[snap_idx+1]-1;
            }else{
                return m_log.size()-1;
            }
        }

        bool exist_log(const uint64_t snap_idx, const uint64_t n_snapshots) const{
            uint64_t last = 0;
            if(snap_idx < n_snapshots-1){
                last = m_log[snap_idx+1];
            }else{
                last = m_log.size();
            }
            return m_log[snap_idx] > 0 && (m_log[snap_idx] < last);
        }

        uint64_t get_start_reap_movement(const uint64_t snap_idx) const{
            return m_reap_movement[snap_idx];
        }

        uint64_t get_end_reap_movement(const uint64_t snap_idx, const uint64_t n_snapshots) const{
            if(snap_idx < n_snapshots) {
                return (uint64_t) (m_reap_movement[snap_idx+1]-1);
            }else{
                return m_reap_movement.size()-1;
            }
        }

        uint64_t  get_start_reap_time(const uint64_t snap_idx) const{
            return m_reap_time[snap_idx];
        }

        uint64_t get_end_reap_time(const uint64_t snap_idx, const uint64_t n_snapshots) const{
            if(snap_idx < n_snapshots) {
                return (uint64_t) (m_reap_time[snap_idx+1]-1);
            }else{
                return m_reap_time.size()-1;
            }
        }

        int64_t get_log_value(uint64_t pos_log) const{
            return (int32_t) m_log[pos_log];
        }

        void get_reappearance(const uint64_t pos_reap_time, const uint64_t pos_reap_movement,
                              const int64_t type_reappearance, const bool right,
                              uint16_t& d_t, int64_t& d_x, int64_t& d_y) const {
            d_t =  m_reap_time[pos_reap_time];
            if(type_reappearance == REAP_MOVE){
                auto move = spiral_matrix_coder::decode(m_reap_movement[pos_reap_movement]);
                d_x = move.first;
                d_y = move.second;
            }else if (type_reappearance == REAPPEARANCE || type_reappearance == DISAPPEARANCE){
                if(right){
                    d_x = m_reap_movement[pos_reap_movement-1];
                    d_y = m_reap_movement[pos_reap_movement];
                }else{
                    d_x = m_reap_movement[pos_reap_movement];
                    d_y = m_reap_movement[pos_reap_movement+1];
                }
            }
        }

        //! Assignment move operation
        log_object& operator=(log_object&& p) {
            if (this != &p) {
                m_log = std::move(p.m_log);
                m_reap_movement = std::move(p.m_reap_movement);
                m_reap_time = std::move(p.m_reap_time);
            }
            return *this;
        }

        //! Assignment operator
        log_object& operator=(const log_object& p)
        {
            if (this != &p) {
                copy(p);
            }
            return *this;
        }

        //! Copy constructor
        log_object(const log_object& p)
        {
            copy(p);
        }

        //! Move constructor
        log_object(log_object&& p)
        {
            *this = std::move(p);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(log_object& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            m_log.swap(p.m_log);
            m_reap_movement.swap(p.m_reap_movement);
            m_reap_time.swap(p.m_reap_time);

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
            sdsl::write_member(m_log.size(), out, child, "log_size");
            written_bytes += sdsl::serialize_vector(m_log, out, child, "log");
            //written_bytes += m_log.serialize(out, child, "log");
            written_bytes += m_reap_movement.serialize(out, child, "reap_movement");
            sdsl::write_member(m_reap_time.size(), out, child, "reap_size");
            written_bytes += sdsl::serialize_vector(m_reap_time, out, child, "reap_time");
            //written_bytes += m_reap_time.serialize(out, child, "reap_time");
            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }

        void load(std::istream &in){
            uint64_t m_log_size = 0, m_reap_size = 0;
            sdsl::read_member(m_log_size, in);
            m_log.resize(m_log_size);
            sdsl::load_vector(m_log, in);
            m_reap_movement.load(in);
            sdsl::read_member(m_reap_size, in);
            m_reap_time.resize(m_reap_size);
            sdsl::load_vector(m_reap_time, in);
        }



    };
}

#endif //GRACTV2_LOG_OBJECT_HPP
