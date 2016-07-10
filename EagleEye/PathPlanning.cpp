
#include "PathPlanning.h"

#include <cmath>

using namespace DerWeg;

PathPlanning::PathPlanning(const ConfigReader& cfg){
    cfg.get("PathPlanning::time_step", dt);
    cfg.get("PathPlanning::collision_penalty", collision_penalty);
    cfg.get("PathPlanning::max_depth", max_depth);
    cfg.get("LateralControl::axis_distance", axis_distance);
    cfg.get("PathPlanning::car_circle_radius", car_circle_radius);
}

Velocity PathPlanning::findPath(vector<Circle> obst) {
    obstacles = obst;
    Velocity maximizing_velocity;
    treeSearch(BBOARD->getState(), 0, maximizing_velocity);
    return maximizing_velocity;
}

double PathPlanning::treeSearch(const State state, const int depth, Velocity& maximizing) const {
    vector<Velocity> velocities = getVelocities(state);
    vector<double> values;

    for (unsigned int i=0; i<velocities.size(); i++) {
        // This copy of state will be modified within Path simulation to get the end state
        State state_copy = state;
        state_copy.velocity = velocities[i].velocity;
        state_copy.steer = velocities[i].steer;
        double distance = simulatePath(state_copy);
        if (distance > 0 && depth < max_depth) {
            values.push_back(distance + treeSearch(state_copy, depth + 1, velocities[i]));
        } else if (distance > 0) {
            values.push_back(distance);
        } else {
            values.push_back(distance - collision_penalty); // large negative value in collision case
        }
    }
    int arg_max = std::distance(values.begin(), std::max_element(values.begin(), values.end()));
    if (depth == 0) {
        maximizing = velocities[arg_max];
    }
    return values[arg_max];
}

vector<Velocity> PathPlanning::getVelocities(const State state) const {
    vector<Velocity> result;
    double delta = state.steer.get_deg_180();
    for (int i = -2; i <= 2; i++) {
        double new_delta = delta + i * 5;
        if (abs(new_delta) < 30) {
            Velocity v;
            v.velocity = state.velocity;
            v.steer = Angle::deg_angle(new_delta);
            result.push_back(v);
        }
    }
    return result;
}

double PathPlanning::simulatePath(State& state) const {
    int _direction_flag;
    if (state.steer.get_rad_pi() == 0) {
        _direction_flag = 0;
    } else if (state.steer.get_rad_pi() > 0) {
        _direction_flag = 1;
    } else {
        _direction_flag = -1;
    }
    const int direction_flag = _direction_flag;

    // Plot state position in AnicarViewer
    std::stringstream pos;
    pos << "think blue dot "
        << state.sg_position.x << " " << state.sg_position.y << std::endl;
    BBOARD->addPlotCommand(pos.str());

    // All calculations in millimetres and global coordinate system

    // driven distance within this motion step in millimetres
    double distance = state.velocity * dt * 1000;

    // Get circles covering car
    vector<Circle> car_circles = getCarCircles(state);
    // Up
    Vec ICM;
    // Update state and calculate ICM
    if (direction_flag == 0) {
        //Straight movement, so ICM is at infinity -> not used in calculate distance in this case
        Vec movement = distance * ( (state.sg_position - state.rear_position).normalize() );
        // Update state to end position
        state.sg_position += movement;
        state.rear_position += movement;
    } else {
        // Calculate ICM (momentanpol)
        double R_h = axis_distance / tan(state.steer.get_rad_pi());
        ICM = Vec(0, R_h); // local car coordinates
        ICM.s_rotate(state.orientation);
        ICM += state.rear_position; // glocal coordinates

        Angle alpha = Angle::rad_angle(distance / R_h); // angle covered on circle segment driven with the given velocity

        // update state to end position
        state.sg_position = (state.sg_position - ICM).rotate(alpha) + ICM;
        state.rear_position = (state.rear_position - ICM).rotate(alpha) + ICM;
        state.orientation += alpha;
    }

    // Get circles covering car at end position of this motion
    vector<Circle> end_car_circles = getCarCircles(state);

    double min_distance = 2 * collision_penalty; // arbitrary very high initial value

    for (unsigned int i=0; i<car_circles.size(); i++) {
        for (unsigned int j=0; j<obstacles.size(); j++) {
            double distance = calculateDistance(ICM, obstacles[j], car_circles[i], end_car_circles[i], direction_flag);
            min_distance = min(min_distance, distance);
        }
    }
    return min_distance;
}

double PathPlanning::calculateDistance(const Vec& ICM, const Circle& obstacle, const Circle& vehicle_point,
                                        const Circle& vehicle_point_end, const int direction_flag) const {
    if (direction_flag == 0) {
        // Straight movement
        // Determine, whether the orthogonal projection of obstacle onto the line through the two vehicle points
        // is inbetween the two vehicle points, or outside of this segment
        bool inbetween = (obstacle.center - vehicle_point.center) * (vehicle_point_end.center - vehicle_point.center) >= 0
                        && (obstacle.center - vehicle_point_end.center) * (vehicle_point.center - vehicle_point_end.center) >= 0;
        if (inbetween) {
            Vec line_unit_normal = (vehicle_point_end.center - vehicle_point.center).rotate_quarter().normalize();
            // Distance of line and obstacle center vis projection onto line
            double distance_obstacle_line = abs( (obstacle.center - vehicle_point.center) * line_unit_normal );
            //Assert Projektion gleich mit vehicle_end_point
            return distance_obstacle_line - obstacle.radius - vehicle_point.radius;
        } else {
            // If obstacle is outside line segment just check end points for closest distance
            return min(obstacle.distance(vehicle_point), obstacle.distance(vehicle_point_end));
        }
    } else {
        // Determine if obstacle is inside the circle sector (cone) or not
        bool inbetween;
        if (direction_flag > 0) {
            inbetween = (obstacle.center - ICM).inbetween(vehicle_point.center - ICM, vehicle_point_end.center - ICM);
        } else {
            inbetween = (obstacle.center - ICM).inbetween(vehicle_point_end.center - ICM, vehicle_point.center - ICM);
        }

        if (inbetween) {
            double R = (ICM - vehicle_point.center).length(); // Radius of vehicle point moving around ICM
            double d_obst = (obstacle.center - ICM).length(); // Distance from center of obstacle to ICM
            double min_distance = abs(R - d_obst) - obstacle.radius - vehicle_point.radius;
            return min_distance;
        } else {
            // If obstacle is outside circle segment just check end points for closest distance
            return min(obstacle.distance(vehicle_point), obstacle.distance(vehicle_point_end));
        }
    }
}


vector<Circle> PathPlanning::getCarCircles(const State state) const {
    vector<Circle> result;
    result.push_back(Circle(state.sg_position, car_circle_radius));
    result.push_back(Circle(state.rear_position, car_circle_radius));
    result.push_back(Circle(0.5 * (state.sg_position + state.rear_position), car_circle_radius));
    return result;
}