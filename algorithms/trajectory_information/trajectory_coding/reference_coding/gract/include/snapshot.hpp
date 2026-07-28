//
// Created by adrian on 24/04/17.
//

#ifndef SUCCINCTCT_K2_TREE_HPP
#define SUCCINCTCT_K2_TREE_HPP

#include <global_constants.hpp>
#include "k2_tree_representation_lite.hpp"
#include <permutation_labels.hpp>
#include <geometry.hpp>
#include <queue>
#include <cmath>
#include <util_ct.hpp>

namespace gract_ct {

    template<uint64_t k = 2>
    class snapshot{
    public:
        typedef sdsl::bit_vector::size_type size_type;
        typedef sdsl::int_vector<>::value_type value_type;

    private:
        size_type m_max_level;
        size_type m_total_objects;
        sdsl::bit_vector bn;
        sdsl::bit_vector bt;
        sdsl::bit_vector bl;
        sdsl::rank_support_v<> m_rank_bl;
        sdsl::rank_support_v<> m_rank_bt;
        sdsl::rank_support_v<> m_rank_bn;
        sdsl::select_support_mcl<> m_select_bl;
        sdsl::select_support_mcl<> m_select_bt;
        sdsl::select_support_mcl<> m_select_bn;
        permutation_labels m_permutation_labels;
        size_type m_n_cells_objects = 0;
        std::vector<uint64_t> div_level_table;

    private:
        void copy(const snapshot& p){
            m_max_level = p.m_max_level;
            m_total_objects = p.m_total_objects;
            bn = p.bn;
            bt = p.bt;
            bl = p.bl;
            m_rank_bl = p.m_rank_bl;
            m_rank_bl.set_vector(&bl);
            m_rank_bt = p.m_rank_bt;
            m_rank_bt.set_vector(&bt);
            m_rank_bn = p.m_rank_bn;
            m_rank_bn.set_vector(&bn);
            m_select_bl = p.m_select_bl;
            m_select_bl.set_vector(&bl);
            m_select_bt = p.m_select_bt;
            m_select_bt.set_vector(&bt);
            m_select_bn = p.m_select_bn;
            m_select_bn.set_vector(&bn);
            m_permutation_labels = p.m_permutation_labels;
            m_n_cells_objects = p.m_n_cells_objects;
            div_level_table = p.div_level_table;
        }

    public:
        snapshot(){};


        snapshot(const k2_tree_representation_lite<k> &k2t_repr, const size_type total_objects){

            m_total_objects = total_objects;
            m_n_cells_objects = k2t_repr.n_cells_objects;
            m_max_level = k2t_repr.maxLevel;


            size_type bits_BT_len = k2t_repr.n_nodes;
            size_type bits_BN_len = k2t_repr.n_total_leaves;
            size_type bits_LI_len = k2t_repr.n_leaves * k2t_repr.K * k2t_repr.K;

            sdsl::bit_vector bits_BT(bits_BT_len, 0);
            sdsl::bit_vector bits_BN(bits_BN_len, 0);
            sdsl::bit_vector bits_LI(bits_LI_len, 0);

            uint jj, j, queuecont, conttmp, m_node;
            uint pos = 0;
            int i;
            char isroot = 1;
            std::queue<uint32_t > q;
            q.push(0);
            queuecont = 1;

            //Add bits to bT
            for (i = 0; i < k2t_repr.maxLevel; i++) {
                //Iterate up to antepenultimate level
                conttmp = 0;
                for (jj = 0; jj < queuecont; jj++) {

                    uint32_t index = q.front();
                    q.pop();
                    if (k2t_repr.nodes[index].start_child != 0) {
                        for (j = 0; j < k2t_repr.K * k2t_repr.K; j++) {
                            conttmp++;
                            q.push(k2t_repr.nodes[index].start_child + j);
                        }
                        if (!isroot){
                            bits_BT[pos] = 1;
                        }
                    }
                    if (!isroot){
                        pos++;
                    }
                    isroot = 0;
                }
                queuecont = conttmp;
            }

            pos = 0;
            uint pos_inf = 0;
            std::vector<std::vector<value_type>> labels(k2t_repr.n_cells_objects, std::vector<value_type>());
            /* std::vector<value_type> *labels =
                     (std::vector<value_type> *) malloc(sizeof(std::vector<value_type>) * k2t_repr.n_cells_objects);
             for(uint jj = 0; jj < k2t_repr.n_cells_objects; jj++){
                 labels[jj] =  std::vector<value_type>();
             }*/
            uint cellIndex = 0;
            //Going through the penultimate level
            while (!q.empty()) {
                uint32_t index = q.front();
                q.pop();
                if (k2t_repr.nodes[index].data != 0) {
                    //If there are objects, set to 1. Penultimate level
                    bits_BN[pos]=1;

                    //Add the objects of the cells
                    for (i = 0; i < k2t_repr.K * k2t_repr.K; i++) {
                        if (k2t_repr.nodes[index].data & (0x1 << i)) {
                            //If there are objects, set to 1. Last level
                            bits_LI[pos_inf] = 1;
                            //Adding object's ids
                            //delete labels[cellIndex];
                            labels[cellIndex] = k2t_repr.nodes[index].label[i];
                            cellIndex++;
                        }
                        pos_inf++;
                    }
                }
                pos++;
            }

            //Building Bt, Bn and Bl sequences
            bt = bits_BT;
            bn = bits_BN;
            bl = bits_LI;

            sdsl::util::init_support(m_rank_bl, &bl);
            sdsl::util::init_support(m_select_bl, &bl);
            sdsl::util::init_support(m_rank_bt, &bt);
            sdsl::util::init_support(m_select_bt, &bt);
            sdsl::util::init_support(m_rank_bn, &bn);
            sdsl::util::init_support(m_select_bn, &bn);


            //Building the permutation with the identifiers of the objects
            m_permutation_labels = permutation_labels(labels, m_total_objects, k2t_repr.n_cells_objects);

            //Building division level table
            div_level_table = std::vector<uint64_t>(m_max_level + 1);
            for ( i = 0; i <= m_max_level; i++){
                div_level_table[i] = std::pow(k, m_max_level - i);
            }

            //Free
            labels.clear();
        }

        std::queue<size_type> _path_levels_object(value_type oid) const {
            std::queue<size_type> path;
            //Find the position in the leaf

            size_type pos;
            bool exist = m_permutation_labels.cell_of_object(oid, pos);
            if(exist){
                size_t x = m_select_bl(pos+1); //p-th leaf
                size_t i = x % (k * k); //i-th element in the node. Leaf level
                path.push(i);

                x = m_select_bn(x/(k*k)+1);
                x = x + bt.size(); //parent position
                while (x > 0){
                    i = x % (k * k);
                    path.push(i);
                    x = (x < (k*k) ? 0 : m_select_bt(x / (k * k)));
                }
            }
            return path;

        }

        bool find_object_position(value_type id, point &p) const{
            if(id > m_total_objects){
                return false;
            }else{
                std::queue<size_type > path = _path_levels_object(id);
                if(path.empty()){
                    return false;
                }
                size_type i;
                size_type column = 0, row = 0;
                size_type columnLevel = 0, rowLevel = 0;
                int  level = 0;
                //Compute the position with the path
                while (!path.empty()) {
                    i = path.front();
                    columnLevel = i % k;
                    rowLevel = i / k;
                    column = (size_type) pow(k, level)*columnLevel + column;
                    row = (size_type) pow(k, level)*rowLevel + row;
                    level++;
                    path.pop();
                }
                p = point(column, row);
                return true;
            }
        }

        void _find_objects_in_region_rec(size_type p1, size_type p2, size_type q1, size_type q2,
                                         size_type dp, size_type dq, long x, long l, std::vector<id_point> &result) const {
            size_type i = 0, j, leaf;
            size_type y, p1new, p2new, q1new, q2new;
            unsigned long int divlevel;
            if (l == m_max_level) {
                //Reading the last level
                leaf = (uint) x;
                if (bl[leaf]) {
                    //dp + i => Y axis
                    //dq + j => X axis
                    std::vector<value_type> objects = m_permutation_labels.objects_into_cell(m_rank_bl(leaf+1)-1);
                    for(uint ii = 0; ii < objects.size(); ii++){
                        result.push_back(id_point(objects[ii], dq, dp));
                    }
                }

            }

            if ((l == m_max_level - 1) && (bn[x - bt.size()])) {
                //Reading the last-1 level
                y = (m_rank_bn(x - bt.size()+1) - 1) * k * k; //children position in L
                for (i = p1; i <= p2; i++) {
                    for (j = q1; j <= q2; j++) {
                        _find_objects_in_region_rec(0, 0, 0, 0, dp + i, dq + j,
                                                    y + k * i + j, l + 1, result);
                    }
                }

            }
            //X=-1: first step
            //Reading the intermidate levels
            if ((x == -1) || ((l < m_max_level - 1) && (bt[x]))) {
                y = (x == -1) ? 0 : m_rank_bt(x+1) * k * k; //children position
                //side of every submatrix in the actual level
                divlevel = div_level_table[l + 1];
                //for each position in the range of the query
                for (i = p1 / divlevel; i <= p2 / divlevel; i++) {
                    p1new = 0;
                    //(0, divlevel-1) except the leftest region (p1 % divlevel, divlevel-1) and
                    //rightest region (0, p2 % divlevel)
                    if (i == p1 / divlevel)
                        p1new = p1 % divlevel;
                    p2new = divlevel - 1;
                    if (i == p2 / divlevel)
                        p2new = p2 % divlevel;
                    for (j = q1 / divlevel; j <= q2 / divlevel; j++) {
                        q1new = 0;
                        if (j == q1 / divlevel)
                            q1new = q1 % divlevel;
                        q2new = divlevel - 1;
                        if (j == q2 / divlevel)
                            q2new = q2 % divlevel;
                        //i and j zones in Y-axis and X-axis respecitively
                        //dp + divLevel*i position where starts the region in Y-axis
                        //dq + divLevel*j position where starts the region in X-axis
                        //x (position in T) = y + k*i + j -> position of the region in T
                        //l = l+1 seguinte nivel
                        _find_objects_in_region_rec(p1new, p2new, q1new, q2new,
                                                    dp + divlevel * i, dq + divlevel * j,
                                                    y + k * i + j, l + 1, result);
                    }
                }

            }
        }

        std::vector<id_point> find_objects_in_region(uint minX, uint maxX, uint minY, uint maxY) const {
            std::vector<id_point> results;
            if(m_n_cells_objects == 0){
                return  results;
            }
            _find_objects_in_region_rec(minY, maxY, minX, maxX, 0, 0, -1, -1, results);
            return results;
        }




        inline std::vector<value_type> get_objects(const uint64_t leaf) const{
            return m_permutation_labels.objects_into_cell(leaf);
        }

        template <class element, class priority_queue>
        void enqueue_children(const element &knn_e, priority_queue &pq, const point p_q,
                              const uint64_t max_speed, const uint64_t log_idx,
                              const uint64_t snap_t, const uint64_t d_t,
                              const uint64_t max_x, const uint64_t max_y) const {

            if(knn_e.level == m_max_level){
                auto leaf = m_rank_bl(knn_e.id); //m_rank_bl(knn_e.id)+1-1 -> +1 next; -1 index
                for(size_type i = knn_e.id; i < k * k + knn_e.id; ++i){
                    if(bl[i]){
                        //leaf = m_rank_bl(i+1)-1;
                        auto j = i - knn_e.id;
                        size_type x = (j % k)  + knn_e.min.m_x;
                        size_type y = (j / k)  + knn_e.min.m_y;
                        point p(x, y);
                        //std::vector<value_type> objects = m_permutation_labels.objects_into_cell(leaf);
                        region r_expanded = util::expand_point(p, d_t * max_speed, max_x, max_y);
                        auto distance = util::distance_knn(r_expanded, p_q);
                        auto distance_max = util::max_distance_knn(r_expanded, p_q);
                        /*for(const auto &id : objects){
                            element knn_child(id, p, distance, snap_t, log_idx,  0, 0, 0);
                            pq.push(knn_child);
                        }*/
                        element knn_child(leaf, p, distance, distance_max, snap_t, log_idx,  0, 0, 0, true, false);
                        pq.push(knn_child);
                        ++leaf;
                    }
                }
            } else if(knn_e.level == m_max_level-1) {
                auto div_level = div_level_table[knn_e.level];
                size_type bn_index = knn_e.id - bt.size();
                size_type p_leaf = m_rank_bn(bn_index) * k * k; //(rank(bn_index)+1-1)*k*k: +1 next; -1 index
                for (size_type i = knn_e.id; i < k * k + knn_e.id; i++) {
                    //Add regions to a priority queue
                    if (bn[bn_index]) {
                        //p_leaf = (m_rank_bn(bn_index+1)-1)*k*k;
                        auto j = i - knn_e.id;
                        size_type m_x = (j % k) * div_level + knn_e.min.m_x;
                        size_type m_y = (j / k) * div_level + knn_e.min.m_y;
                        size_type M_x = m_x + div_level - 1;
                        size_type M_y = m_y + div_level - 1;
                        region r(m_x, m_y, M_x, M_y);
                        region r_expanded = r.expand(d_t * max_speed, max_x, max_y);
                        auto distance = util::distance_knn(r_expanded, p_q);
                        auto distance_max = util::max_distance_knn(r_expanded, p_q);
                        element knn_region(p_leaf, point(m_x, m_y), point(M_x, M_y), distance, distance_max, snap_t, knn_e.level+1);
                        pq.push(knn_region);
                        p_leaf += k*k;
                    }
                    ++bn_index;
                }
            } else {
                auto div_level = div_level_table[knn_e.level];
                size_type p_region = (m_rank_bt(knn_e.id)+1) * k * k;
                for (size_type i = knn_e.id; i < k * k + knn_e.id; i++) {
                    //Add regions to a priority queue
                    if (bt[i]) {
                        //p_region = m_rank_bt(i+1) * k * k;
                        auto j = i - knn_e.id;
                        size_type m_x = (j % k) * div_level + knn_e.min.m_x;
                        size_type m_y = (j / k) * div_level + knn_e.min.m_y;
                        size_type M_x = m_x + div_level - 1;
                        size_type M_y = m_y + div_level - 1;
                        region r(m_x, m_y, M_x, M_y);
                        region r_expanded = r.expand(d_t * max_speed, max_x, max_y);
                        auto distance = util::distance_knn(r_expanded, p_q);
                        auto distance_max = util::max_distance_knn(r_expanded, p_q);
                        element knn_region(p_region, point(m_x, m_y), point(M_x, M_y), distance, distance_max,
                                            snap_t, knn_e.level+1);
                        pq.push(knn_region);
                        p_region += k*k;
                    }
                }
            }
        }




        //! Assignment move operation
        snapshot& operator=(snapshot&& p) {
            if (this != &p) {
                m_max_level = std::move(p.m_max_level);
                m_total_objects = std::move(p.m_total_objects);
                bn = std::move(p.bn);
                bt = std::move(p.bt);
                bl = std::move(p.bl);
                m_rank_bl = std::move(p.m_rank_bl);
                m_rank_bl.set_vector(&bl);
                m_rank_bt = std::move(p.m_rank_bt);
                m_rank_bt.set_vector(&bt);
                m_rank_bn = std::move(p.m_rank_bn);
                m_rank_bn.set_vector(&bn);
                m_select_bl = std::move(p.m_select_bl);
                m_select_bl.set_vector(&bl);
                m_select_bt = std::move(p.m_select_bt);
                m_select_bt.set_vector(&bt);
                m_select_bn = std::move(p.m_select_bn);
                m_select_bn.set_vector(&bn);
                m_permutation_labels = std::move(p.m_permutation_labels);
                m_n_cells_objects = std::move(p.m_n_cells_objects);
                div_level_table = std::move(p.div_level_table);
            }
            return *this;
        }

        //! Assignment operator
        snapshot& operator=(const snapshot& snapshot)
        {
            if (this != &snapshot) {
                copy(snapshot);
            }
            return *this;
        }

        //! Copy constructor
        snapshot(const snapshot& snapshot)
        {
            copy(snapshot);
        }

        //! Move constructor
        snapshot(snapshot&& snapshot)
        {
            *this = std::move(snapshot);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(snapshot& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            std::swap(m_total_objects, p.m_total_objects);
            std::swap(m_max_level, p.m_max_level);
            bn.swap(p.bn);
            bt.swap(p.bt);
            bl.swap(p.bl);
            m_rank_bl.swap(p.m_rank_bl);
            m_rank_bt.swap(p.m_rank_bt);
            m_rank_bn.swap(p.m_rank_bn);
            m_select_bl.swap(p.m_select_bl);
            m_select_bt.swap(p.m_select_bt);
            m_select_bn.swap(p.m_select_bn);
            m_permutation_labels = std::move(p.m_permutation_labels);
            std::swap(m_n_cells_objects, p.m_n_cells_objects);
            std::swap(div_level_table, p.div_level_table);

        }

        //! Serializes the snapshot to a stream.
        /*!
         * \param out The outstream to which the data structure is written.
         * \return The number of bytes written to out.
         */
        size_type serialize(std::ostream& out, sdsl::structure_tree_node* v=nullptr, std::string name="")const
        {
            sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
            size_type written_bytes = 0;
            sdsl::write_member(div_level_table.size(), out, child, "div_level_size");
            written_bytes += sdsl::serialize_vector(div_level_table, out, child, "div_level_table");
            written_bytes += bn.serialize(out, child, "bn");
            written_bytes += bt.serialize(out, child, "bt");
            written_bytes += bl.serialize(out, child, "bl");
            written_bytes += m_rank_bl.serialize(out, child, "rank_bl");
            written_bytes += m_rank_bt.serialize(out, child, "rank_bt");
            written_bytes += m_rank_bn.serialize(out, child, "rank_bn");
            written_bytes += m_select_bl.serialize(out, child, "select_bl");
            written_bytes += m_select_bt.serialize(out, child, "select_bt");
            written_bytes += m_select_bn.serialize(out, child, "select_bn");
            written_bytes += m_permutation_labels.serialize(out, child, "permutation_labels");
            written_bytes += sdsl::write_member(m_n_cells_objects, out, child, "n_cells_objects");
            written_bytes += sdsl::write_member(m_total_objects, out, child, "total_objects");
            written_bytes += sdsl::write_member(m_max_level, out, child, "max_level");

            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }

        void load(std::istream &in){
            size_type div_level_size;
            sdsl::read_member(div_level_size, in);
            div_level_table.resize(div_level_size);
            sdsl::load_vector(div_level_table, in);
            bn.load(in);
            bt.load(in);
            bl.load(in);
            m_rank_bl.load(in);
            m_rank_bl.set_vector(&bl);
            m_rank_bt.load(in);
            m_rank_bt.set_vector(&bt);
            m_rank_bn.load(in);
            m_rank_bn.set_vector(&bn);
            m_select_bl.load(in);
            m_select_bl.set_vector(&bl);
            m_select_bt.load(in);
            m_select_bt.set_vector(&bt);
            m_select_bn.load(in);
            m_select_bn.set_vector(&bn);
            m_permutation_labels.load(in);
            sdsl::read_member(m_n_cells_objects, in);
            sdsl::read_member(m_total_objects, in);
            sdsl::read_member(m_max_level, in);
        }

    };



}

#endif //SUCCINCTCT_K2_TREE_HPP

