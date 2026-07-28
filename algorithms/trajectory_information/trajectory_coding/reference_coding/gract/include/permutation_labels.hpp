//
// Created by adrian on 24/04/17.
//

#ifndef GRACT_PERMUTATION_LABELS_HPP
#define GRACT_PERMUTATION_LABELS_HPP

#include <vector>
#include <hash_map.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/rank_support.hpp>
#include <sdsl/select_support.hpp>
#include <sdsl/inv_perm_support.hpp>

namespace gract_ct {

    class permutation_labels {


    public:
        typedef sdsl::int_vector<>::value_type value_type;
        typedef sdsl::int_vector<>::size_type size_type;
        
    private:
        size_type m_size_total;
        sdsl::bit_vector m_bq;
        sdsl::rank_support_v<0> m_rank_zero_q;
        sdsl::select_support_mcl<0> m_select_zero_q;
        sdsl::int_vector<> m_perm;
        sdsl::inv_perm_support<5> m_inv_perm_support;

    private:
        void copy(const permutation_labels& p){
            m_size_total = p.m_size_total;
            m_bq = p.m_bq;
            m_rank_zero_q = p.m_rank_zero_q;
            m_select_zero_q = p.m_select_zero_q;
            m_perm = p.m_perm;
            m_inv_perm_support = p.m_inv_perm_support;
        }
    public:
        permutation_labels(){};
        permutation_labels(const std::vector<std::vector<value_type>> &labels, const size_type total_objects,
                           const size_type n_cells_objects){

            //bitmap Q
            sdsl::bit_vector bq_aux(total_objects, 0);

            hash_map* m_hash_map = new hash_map();
            //building the permutation with all objects
            sdsl::int_vector<> ids(total_objects);
            uint pos = 0;

            m_size_total = 0; //number of elements which there are in the tree
            //Add the objects which there are in the tree to the permutation
            for(uint i = 0; i < n_cells_objects; i++){
                std::vector<value_type > labels_cell = labels[i];
                for(uint j=0; j< labels_cell.size();j++){
                    value_type id = labels_cell[j];
                    ids[pos] = id;
                    m_hash_map->put(id);
                    m_size_total++;
                    if(j != labels_cell.size()-1){
                        bq_aux[pos]=1;
                    }
                    pos++;
                }
            }
            //Adding the rest of the objects to the permutation
            uint id = 0;
            while(pos < total_objects){
                while(m_hash_map->get(id)!=-1){
                    id++;
                }
                ids[pos] = id;
                m_hash_map->put(id);
                pos++;
            }

            m_bq = bq_aux;
            m_perm = ids;
            sdsl::util::init_support(m_rank_zero_q, &m_bq);
            sdsl::util::init_support(m_select_zero_q, &m_bq);
            //Build the permutation
            sdsl::util::bit_compress(m_perm);
            m_inv_perm_support = sdsl::inv_perm_support<5>(&m_perm);

            delete m_hash_map;

        }

        bool cell_of_object(const value_type id, size_type &pos) const {
            value_type k = m_inv_perm_support[id];
            //Check if exists in the tree
            if(k < m_size_total){
                pos = m_rank_zero_q(k);
                return true;
            }else{
                return false;
            }


        }

        std::vector<value_type > objects_into_cell(const size_type cell) const {

            //Starting and last position of the cell in perm
            size_type p_start = (cell) ? m_select_zero_q(cell)+1 : 0;
            size_type p_end = m_select_zero_q(cell+1);

            //Adding the ids of the cell
            std::vector<value_type > ids;
            for(size_type p = p_start; p <= p_end; p++){
                value_type id = m_perm[p];
                ids.push_back(id);
            }
            return ids;

        }

        //! Assignment move operation
        permutation_labels& operator=(permutation_labels&& p) {
            if (this != &p) {
                m_size_total   = std::move(p.m_size_total);
                m_bq           = std::move(p.m_bq);
                m_rank_zero_q  = std::move(p.m_rank_zero_q);
                m_rank_zero_q.set_vector(&m_bq);
                m_select_zero_q = std::move(p.m_select_zero_q);
                m_select_zero_q.set_vector(&m_bq);
                m_perm = std::move(p.m_perm);
                m_inv_perm_support = std::move(p.m_inv_perm_support);
                m_inv_perm_support.set_vector(&m_perm);
            }
            return *this;
        }


        //! Assignment operator
        permutation_labels& operator=(const permutation_labels& permutation_labels)
        {
            if (this != &permutation_labels) {
                copy(permutation_labels);
            }
            return *this;
        }

        //! Copy constructor
        permutation_labels(const permutation_labels& permutation_labels)
        {
            copy(permutation_labels);
        }

        //! Move constructor
        permutation_labels(permutation_labels&& permutation_labels)
        {
            *this = std::move(permutation_labels);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(permutation_labels& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            std::swap(m_size_total, p.m_size_total);
            m_bq.swap(p.m_bq);
            m_rank_zero_q.swap(p.m_rank_zero_q);
            m_select_zero_q.swap(p.m_select_zero_q);
            m_perm.swap(p.m_perm);
            m_inv_perm_support.swap(p.m_inv_perm_support);

        }

        //! Serializes the permutation_labels to a stream.
        /*!
         * \param out The outstream to which the data structure is written.
         * \return The number of bytes written to out.
         */
        size_type serialize(std::ostream& out, sdsl::structure_tree_node* v=nullptr, std::string name="")const
        {
            sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
            size_type written_bytes = 0;
            written_bytes += sdsl::write_member(m_size_total, out, child, "size_total");
            written_bytes += m_bq.serialize(out, child, "bq");
            written_bytes += m_rank_zero_q.serialize(out, child, "rank_zero_q");
            written_bytes += m_select_zero_q.serialize(out, child, "select_zero_q");
            written_bytes += m_perm.serialize(out, child, "perm");
            written_bytes += m_inv_perm_support.serialize(out, child, "inv_perm");

            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }

        void load(std::istream& in){
            sdsl::read_member(m_size_total, in);
            m_bq.load(in);
            m_rank_zero_q.load(in);
            m_rank_zero_q.set_vector(&m_bq);
            m_select_zero_q.load(in);
            m_select_zero_q.set_vector(&m_bq);
            m_perm.load(in);
            m_inv_perm_support.load(in);
            m_inv_perm_support.set_vector(&m_perm);
        }

    };
}

#endif //SUCCINCTCT_PERMUTATION_LABELS_HPP
