//
// Created by adrian on 10/05/18.
//

#ifndef GRACT3D_GEO_UTIL_HPP
#define GRACT3D_GEO_UTIL_HPP

namespace gract_ct {

    typedef long double distance_t;

    static double radians(double degrees){
        return degrees * M_PI / 180;
    }

    static distance_t haversine_formule(double x_o, double y_o, double x_p, double y_p){
        int R = 6371000; // Earth’s mean radius in meter
        distance_t dLat = radians(y_p - y_o);
        distance_t dLong = radians(x_p - x_o);
        distance_t a = std::sin(dLat / 2) * std::sin(dLat / 2) + std::cos(radians(y_o)) * std::cos(radians(y_p)) *
                                                             std::sin(dLong / 2) * std::sin(dLong / 2);
        distance_t c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
        distance_t d = R * c; //meters
        return std::ceil(d);
    }

    static std::pair<distance_t, distance_t> compute_distance(double x_o, double y_o, double x_p, double y_p){
        distance_t x_meters =  haversine_formule(x_o, y_o, x_p, y_o);
        distance_t y_meters = haversine_formule(x_o, y_o, x_o, y_p);
        return std::pair<distance_t , distance_t >(x_meters, y_meters);
    }

}

#endif //GRACT3D_GEO_UTIL_HPP
