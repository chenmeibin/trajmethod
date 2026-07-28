//
// Created by adrian on 26/12/17.
//

#ifndef GRACTV2_ALTERNATIVE_CODER_HPP
#define GRACTV2_ALTERNATIVE_CODER_HPP

#include <cmath>
#include <global_constants.hpp>

namespace gract_ct {

    class alternative_coder {

    public:

        static uint64_t encode(int64_t v){
            uint absValue =(uint) std::abs(v)*2;
            if(v < 0){
                return absValue-1;
            }
            return absValue;
        }

        static int64_t decode(uint64_t code){
            int64_t  absValue =(int64_t) std::ceil((double) code / 2);
            uint64_t negative = code % 2;
            if(negative){
                return -absValue;
            }
            return absValue;
        };
    };
}

#endif //GRACTV2_SPIRAL_MATRIX_CODER_HPP
