#include "../Elementary/KogmoThread.h"
#include "../Elementary/PluginFactory.h"
#include "../Blackboard/Blackboard.h"
#include "../Elementary/Angle.h"
#include "BezierCurve.h"
#include <cmath>
#include <iostream>
#include <sstream>

using namespace std;

namespace DerWeg {

    struct ControllerInput {
        double distance;        ///< Distance to reference curve
        Angle diff_angle;        ///< Angle difference between ego and curve
        double curvature;       ///< Curvature of reference curve

        ControllerInput(double dist, Angle angle, double curv) : distance(dist), diff_angle(angle), curvature(curv) {;} /// Constructor
    };



  /** LateralControl */
  class LateralControl : public KogmoThread {

    private:
        double lastProjectionParameter;
        BezierCurve bc;

        double newton_tolerance;
        int newton_max_iter;

        double precontrol_k;
        double stanley_k0, stanley_k1;
        double axis_distance;

        double v_max;
        double v_min;
        double a_lateral_max;
        // curvature, we always assume to have -> prevents the car to drive move than a certain velocity and
        // and also prevents dividing by zero
        double virtual_min_kappa;

        //if set to one, set velocity manually, if set to zero, car automatically accelerates
        bool manual_velocity;

    public:
        LateralControl () : lastProjectionParameter(0) {;}
        ~LateralControl () {;}


	void init(const ConfigReader& cfg) {
        cfg.get("LateralControl::newton_tolerance", newton_tolerance);
        cfg.get("LateralControl::newton_max_iter", newton_max_iter);
        cfg.get("LateralControl::precontrol_k", precontrol_k);
        cfg.get("LateralControl::stanley_k0", stanley_k0);
        cfg.get("LateralControl::stanley_k1", stanley_k1);
        cfg.get("LateralControl::axis_distance", axis_distance);

        cfg.get("LongitudinalControl::v_max", v_max);
        cfg.get("LongitudinalControl::v_min", v_min);
        cfg.get("LongitudinalControl::a_lateral_max", a_lateral_max);
        cfg.get("LongitudinalControl::manual_velocity", manual_velocity);

        virtual_min_kappa = a_lateral_max / pow(v_max, 2);
	}

        void execute () {
          try{
            while (true) {

              //If path changed set estimate for newton algo to zero, else use the previous result
              if (bc != BBOARD->getReferenceTrajectory().path) {
                  //LOUT("New curve detected, by LateralControl" << endl);
                bc = BBOARD->getReferenceTrajectory().path;
                lastProjectionParameter = 0;
              }

              ControllerInput input = calculate_curve_data(BBOARD->getState());


              //get current steering angle and velocity
              Velocity dv = BBOARD->getDesiredVelocity();


              //Stanley-Controller here
              double u = precontrol_k*input.curvature - stanley_k0 * input.distance - stanley_k1 * input.diff_angle.get_rad_pi();
              double delta = atan(axis_distance * u);
              // set steering angle
              dv.steer = Angle::rad_angle(delta);


              //Velocity control
              double max_velocity;
              if (!manual_velocity) {
                  // calculate maximal velocity from curvature
                  double kappa = max(virtual_min_kappa, abs(input.curvature));
                  // get maximal velocity for the current curvature to not exceed given lateral acceleration
                  max_velocity = max(v_min, pow(a_lateral_max / kappa, 0.5));
              } else {
                  // if velocity is set manually, use last velocity from blackboard
                  max_velocity = dv.velocity;
              }
              // Get v_max from TrajectoryGenerator (could be reduced because of a traffic light)
              dv.velocity = min(max_velocity, BBOARD->getReferenceTrajectory().v_max);

              // set steering angle and velocity
              BBOARD->setDesiredVelocity(dv);

              boost::this_thread::sleep(boost::posix_time::milliseconds(10));
              boost::this_thread::interruption_point();
            }
          }catch(boost::thread_interrupted&){;}
        }

        /* Calculates the distance from the current position to the bezier curve, the angle of the vehicle with respect to the curve,
        and the curvature of the curve. Those values are needed for the controller. */
        ControllerInput calculate_curve_data(const State& state)  {
            Vec pos = state.rear_position;

            stringstream pos_point;
            pos_point << "thick black dot " << pos.x << " " << pos.y;
            BBOARD->addPlotCommand(pos_point.str());

            lastProjectionParameter = bc.project(pos, lastProjectionParameter,
                                                 newton_tolerance, newton_max_iter);
            //LOUT("Projected Parameter: " << lastProjectionParameter << endl);

            //evaluate bezier curve and derivatives at the projection parameter
            Vec f = bc(lastProjectionParameter);

            stringstream project_point;
            project_point << "thick green dot " << f.x << " " << f.y;
            BBOARD->addPlotCommand(project_point.str());

            Vec df = bc.prime(lastProjectionParameter);
            //Vec ddf = bc.double_prime(lastProjectionParameter);

            //Calculate distance form current position to curve
            //The difference vector is normal to the tangent in the projection point
            //We use this to assign a sign to the distance:
            //The distance is positive, if the actual position is left of the curve
            // and negative if it's right of the curve (regarding moving direction)
            Vec diff = pos - f;
            double distance = diff.length();
            //If the point is right of df, let the distance have a negative sign
            if (diff * df.rotate_quarter() < 0) {
                distance *= -1;
            }

            //Calculate difference angle between vehicle and curve
//            double phi;
//            if (df.x != 0 && abs(df.y/df.x) < 1) {
//                //atan2 takes the direction into account
//                phi = atan2(df.y, df.x);
//            } else {
//                //avoid singularities by rotating coordinate system for 2nd and 4th quadrant
//                phi = M_PI/2 + atan2(-df.x, df.y);
//            }
            Angle diff_angle = state.orientation - bc.orientation(df);

            //Calculate the curvature of the bezier curve
//            double curvature_numerator = ddf.y * df.x - ddf.x * df.y;
//            double curvature_denominator = pow(df.squared_length(), 3.0/2);
//            double curvature = curvature_numerator / curvature_denominator;
            double curvature = bc.curvature(lastProjectionParameter, df);

            /*
            LOUT("Distance: " << distance << endl);
            LOUT("Diff_Angle: " << diff_angle.get_deg_180() << endl);
            LOUT("Curvature: " << curvature << endl);
            LOUT("Position: " << state.position.x << ", " << state.position.y << endl);
            */

            return ControllerInput(distance, diff_angle, curvature);
        }

  };

} // namespace DerWeg

namespace {

  // Plugin bei der Factory anmelden
  static DerWeg::PluginBuilder<DerWeg::KogmoThread, DerWeg::LateralControl> application ("LateralControl");

}
