#include "navigation_geo.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double point_to_segment_distance_m(double lat, double lon,
                                          double lat1, double lon1,
                                          double lat2, double lon2)
{
    const double R = 6371000.0;
    double lat0 = lat * M_PI / 180.0;
    double x = (lon - lon1) * M_PI / 180.0 * cos(lat0) * R;
    double y = (lat - lat1) * M_PI / 180.0 * R;
    double x2 = (lon2 - lon1) * M_PI / 180.0 * cos(lat0) * R;
    double y2 = (lat2 - lat1) * M_PI / 180.0 * R;

    double seg_len2 = x2 * x2 + y2 * y2;
    if (seg_len2 < 1e-6)
        return sqrt(x * x + y * y);

    double t = (x * x2 + y * y2) / seg_len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    double proj_x = x2 * t;
    double proj_y = y2 * t;
    double dx = x - proj_x;
    double dy = y - proj_y;
    return sqrt(dx * dx + dy * dy);
}

double nav_mercator_to_lat(double y)
{
    return atan(sinh(M_PI * (1.0 - 2.0 * y))) * 180.0 / M_PI;
}

double nav_mercator_to_lon(double x)
{
    return x * 360.0 - 180.0;
}

double nav_haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi / 2) * sin(dphi / 2) + cos(phi1) * cos(phi2) * sin(dlambda / 2) * sin(dlambda / 2);
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

double nav_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;
    double y = sin(dlambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda);
    double b = atan2(y, x) * 180.0 / M_PI;
    return fmod(b + 360.0, 360.0);
}

double nav_shortest_angle_diff(double from, double to)
{
    return fmod(to - from + 540.0, 360.0) - 180.0;
}

double nav_min_distance_to_route_m(const double *path_lat, const double *path_lon, int route_point_count,
                                   double lat, double lon)
{
    if (!path_lat || !path_lon || route_point_count < 2)
        return 1e9;

    double best = 1e9;
    for (int i = 0; i < route_point_count - 1; i++)
    {
        double d = point_to_segment_distance_m(lat, lon,
                                               path_lat[i], path_lon[i],
                                               path_lat[i + 1], path_lon[i + 1]);
        if (d < best) best = d;
    }
    return best;
}
