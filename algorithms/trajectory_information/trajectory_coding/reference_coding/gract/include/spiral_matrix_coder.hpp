//
// Created by adrian on 26/12/17.
//

#ifndef GRACTV2_SPIRAL_MATRIX_CODER_HPP
#define GRACTV2_SPIRAL_MATRIX_CODER_HPP

namespace gract_ct {

    class spiral_matrix_coder {

    public:

        static uint64_t encode(int64_t x, int64_t y){
            //If the object is not moving, return 0
            if(x == 0 && y == 0){
                return 0;
            }

            //If the object is moving:
            uint64_t max = std::max(std::abs(x), std::abs(y));
            //Square side length
            //long length = 2*max + 1;
            uint64_t length = (max << 1) + 1;
            //Initial position
            //long initialPosition = static_cast<long>(pow(length - 2, 2));
            uint64_t initialPosition = (length - 2) * (length -2);
            //std::cout << initialPosition << "," << length << "," << max << std::endl;
            if(x == max){ //If we are on the right
                return initialPosition + max - y;
            }else if(x == -max){ //If we are on the left
                return initialPosition + max + 2*(length -1) + y;
            }else if(y == max){ //If we are on the top
                return initialPosition + max + 3*(length -1) + x;
            }else{  //(y == -max) If we are on the bottom
                return initialPosition + max + (length -1) - x;
            }
        }

        static std::pair<int64_t, int64_t> decode(uint64_t code){
            if(code == 0){
                return std::pair<int64_t, int64_t>(0, 0);
            }

            //If the object is moving:
            uint64_t sqrt = (uint64_t) std::floor(std::sqrt(code));
            //Square side length
            uint64_t length = (sqrt % 2) + 1 + sqrt;
            //Max distance
            uint64_t max = (length - 1) >> 1;
            //Initial position
            uint64_t initialPosition = (length-2) * (length-2);
            //Difference between position and inititalposition
            uint64_t diff = code - initialPosition;
            //Calculate zone
            uint64_t zone = diff / (length-1);
            if(zone == 0){ //If we are on the right
                return std::pair<int64_t, int64_t>(max, max - diff);
            }else if(zone == 1){ //If we are on the bottom
                return std::pair<int64_t, int64_t>((length -1)+ max - diff, -max);
            }else if(zone == 2){ //If we are on the left
                return std::pair<int64_t, int64_t>(-max, diff - max - 2*(length -1));
            }else {//(zone == 3) If we are on the top
                return std::pair<int64_t, int64_t>(diff - max - 3*(length -1), max);
            }
        };
    };
}

#endif //GRACTV2_SPIRAL_MATRIX_CODER_HPP
