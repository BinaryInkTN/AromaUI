#ifndef NAVIGATION_GEO_H
#define NAVIGATION_GEO_H

double nav_mercator_to_lat(double y);
double nav_mercator_to_lon(double x);
double nav_haversine_m(double lat1, double lon1, double lat2, double lon2);
double nav_bearing_deg(double lat1, double lon1, double lat2, double lon2);
double nav_shortest_angle_diff(double from, double to);
double nav_min_distance_to_route_m(const double *path_lat, const double *path_lon, int route_point_count,
                                   double lat, double lon);

#endif
