//
// Created by adrian on 24/04/17.
//

#ifndef GRACT_KTREE_REPRESENTATION_LITE_HPP
#define GRACT_KTREE_REPRESENTATION_LITE_HPP

#include <global_constants.hpp>
#include <sys/types.h>
#include <string>
#include <stdexcept>
#include <sdsl/int_vector.hpp>

namespace gract_ct{

    template <class data_t = uint>
    class node_lite{


    public:
        typedef sdsl::int_vector<>::value_type  value_type;
        typedef sdsl::int_vector<>::size_type  size_type;
        value_type data = 0;
        uint32_t start_child = 0;
        std::vector<std::vector<value_type>> label;

    private:

        void copy(const node_lite& p){
            data = p.data;
            start_child = p.start_child;
            label = p.label;
        }

    public:
        node_lite(){};

        //! Assignment move operation
        node_lite& operator=(node_lite&& p) {
            if (this != &p) {
                data = std::move(p.data);
                start_child = std::move(p.start_child);
                label = std::move(p.label);
            }
            return *this;
        }

        //! Assignment operator
        node_lite& operator=(const node_lite& p)
        {
            if (this != &p) {
                copy(p);
            }
            return *this;
        }

        //! Copy constructor
        node_lite(const node_lite& p)
        {
            copy(p);
        }

        //! Move constructor
        node_lite(node_lite&& p)
        {
            *this = std::move(p);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(node_lite& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            data = p.data;
            start_child = p.start_child;
            label = p.label;
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
            written_bytes += sdsl::serialize_vector(label, out, child, "label");
            written_bytes += sdsl::write_member(data, out, child, "data");
            written_bytes += sdsl::write_member(start_child, out, child, "n_cells_objects");

            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }


    };

    //TODO define size_type, value_type
    template <uint64_t k,
            class t_size = sdsl::bit_vector::size_type,
            class t_data = uint>
    class k2_tree_representation_lite {

    public:
        std::vector<node_lite<>> nodes;
        t_size n_leaves; //Nodos folla que conten algun obxecto (bits do penultimo nivel a 1)
        t_size n_cells_objects; //Numero de celas que teñen obxectos
        t_size n_total_leaves; //Bits do penultimo nivel
        t_size n_nodes; //Nodos
        t_size maxLevel;
        t_size K;

    public:

        typedef sdsl::int_vector<>::value_type  value_type;
        typedef sdsl::int_vector<>::size_type  size_type;


        k2_tree_representation_lite(){
            nodes.push_back(node_lite<>());
            this->n_leaves = 0;
            this->n_cells_objects = 0;
            this->n_nodes = 0;
            this->n_total_leaves = 0;
            this->maxLevel = 0;
            this->K = k;
        }

        k2_tree_representation_lite(uint64_t m){
            nodes.push_back(node_lite<>());
            this->n_leaves = 0;
            this->n_cells_objects = 0;
            this->n_nodes = 0;
            this->n_total_leaves = 0;
            this->maxLevel = m;
            this->K = k;
        }


    private:

        void copy(const k2_tree_representation_lite& p){
            nodes = p.nodes;
            n_leaves = p.n_leaves;
            n_cells_objects = p.n_cells_objects;
            n_nodes = p.n_nodes;
            n_total_leaves = p.n_total_leaves;
            maxLevel = p.maxLevel;
            K = p.K;
        }

    public:

        void insertObject(uint x, uint y, t_data id) {
            t_size index_node = 0;
            t_size level = 0;
            t_size levelDivision = 0;
            t_size nodeNumber = 0;
            //y = std::pow(K, maxLevel) - 1 - y;
            while (level <= maxLevel) {
                //Compute the divisions
                levelDivision = 1;
                for (t_size exp = 0; exp < (maxLevel - level); ++exp) {
                    levelDivision *= K;
                }
                if (levelDivision == 0) {
                    throw std::runtime_error(
                        "k2_tree_representation_lite::insertObject computed levelDivision=0"
                        " K=" + std::to_string(K) +
                        " maxLevel=" + std::to_string(maxLevel) +
                        " level=" + std::to_string(level) +
                        " x=" + std::to_string(x) +
                        " y=" + std::to_string(y)
                    );
                }
                //Number of node where we insert the object
                nodeNumber = (y / levelDivision) * K + (x / levelDivision);
                if (level == maxLevel) {
                    if (nodes[index_node].data == 0) {
                        n_leaves++;
                    }
                    //Adding bit 1 to the position of the object
                    n_cells_objects++;
                    nodes[index_node].data = nodes[index_node].data | (0x1 << nodeNumber);
                    if(nodes[index_node].label.empty()){
                        nodes[index_node].label.resize(K*K, std::vector<value_type>());
                    }
                } else {

                    if (nodes[index_node].start_child == 0) {
                        if (level < maxLevel - 1) {
                            n_nodes += K * K;
                        } else {
                            n_total_leaves += K * K;
                        }
                        nodes[index_node].start_child = (uint32_t) nodes.size();
                        nodes.resize(nodes.size()+ K*K);
                    }
                    index_node = nodes[index_node].start_child + nodeNumber;
                }
                x = x % levelDivision;
                y = y % levelDivision;
                level++;

            }

            //Adding ids
            nodes[index_node].label[nodeNumber].push_back(id);
        }

        //! Assignment move operation
        k2_tree_representation_lite& operator=(k2_tree_representation_lite&& p) {
            if (this != &p) {
                nodes = std::move(p.nodes);
                n_leaves = std::move(p.n_leaves);
                n_cells_objects = std::move(p.n_cells_objects);
                n_nodes = std::move(p.n_nodes);
                n_total_leaves = std::move(p.n_total_leaves);
                maxLevel = std::move(p.maxLevel);
                K = std::move(p.K);
            }
            return *this;
        }

        //! Assignment operator
        k2_tree_representation_lite& operator=(const k2_tree_representation_lite& p)
        {
            if (this != &p) {
                copy(p);
            }
            return *this;
        }

        //! Copy constructor
        k2_tree_representation_lite(const k2_tree_representation_lite& p)
        {
            copy(p);
        }

        //! Move constructor
        k2_tree_representation_lite(k2_tree_representation_lite&& p)
        {
            *this = std::move(p);
        }

        //! Swap method
        /*! Swaps the content of the two data structure.
         *  You have to use set_vector to adjust the supported bit_vector.
         *  \param bp_support Object which is swapped.
         */
        void swap(k2_tree_representation_lite& p)
        {
            // m_bp.swap(bp_support.m_bp); use set_vector to set the supported bit_vector
            copy(p);
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
            written_bytes += sdsl::serialize_vector(nodes, out, child, "nodes");
            written_bytes += sdsl::write_member(n_leaves, out, child, "n_leaves");
            written_bytes += sdsl::write_member(n_cells_objects, out, child, "n_cells_objects");
            written_bytes += sdsl::write_member(n_nodes, out, child, "n_nodes");
            written_bytes += sdsl::write_member(n_total_leaves, out, child, "n_total_leaves");
            written_bytes += sdsl::write_member(maxLevel, out, child, "maxLevel");
            written_bytes += sdsl::write_member(K, out, child, "K");

            sdsl::structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }
    };
}

#endif //SUCCINCTCT_KTREE_SHAPE_HPP
