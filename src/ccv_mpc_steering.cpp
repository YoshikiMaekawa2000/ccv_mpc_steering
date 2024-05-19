/*
#include <ccv_mpc_steering/ccv_mpc_steering.h>

using CppAD::AD;


MpcPathTracker::MpcPathTracker():local_nh_("~"), tf_listener_(tf_buffer_)
{
    // param
    local_nh_.param("hz", HZ_, {10});
    local_nh_.param("horizon_t", HORIZON_T_, {5});
    local_nh_.param("world_frame_id", world_frame_id_, {"odom"});
    local_nh_.param("robot_frame_id", robot_frame_id_, {"base_link"});
    local_nh_.param("tread", TREAD_, {0.5});
    local_nh_.param("wheel_radius", WHEEL_RADIUS_, {0.15});
    local_nh_.param("goal_border", GOAL_BORDER_, {0.3});
    local_nh_.param("pitch_offset", PITCH_OFFSET_, {3.0 * M_PI / 180.0});
    local_nh_.param("vref", VREF_, {1.2});
    local_nh_.param("resolution", RESOLUTION_, {0.1});
    //制約
    local_nh_.param("x_bound", X_BOUND_, {1.0e19});
    local_nh_.param("y_bound", Y_BOUND_, {1.0e19});
    local_nh_.param("yaw_bound", YAW_BOUND_, {1.0e19});
    local_nh_.param("v_bound", V_BOUND_, {1.5});
    local_nh_.param("omega_bound", OMEGA_BOUND_, {1.0});
    local_nh_.param("omega_r_bound", OMEGA_R_BOUND_, {3.14});
    local_nh_.param("omega_l_bound", OMEGA_L_BOUND_, {3.14}); 
    local_nh_.param("steer_r_bound", STEER_R_BOUND_, {20*M_PI/180}); //要確認
    local_nh_.param("steer_l_bound", STEER_L_BOUND_, {20*M_PI/180});
    local_nh_.param("domega_r_bound", DOMEGA_R_BOUND_, {3.14});
    local_nh_.param("domega_l_bound", DOMEGA_L_BOUND_, {3.14});
    local_nh_.param("dsteer_r_bound", DSTEER_R_BOUND_, {3.14});
    local_nh_.param("dsteer_l_bound", DSTEER_L_BOUND_, {3.14});


    // subscriber
    sub_path_ = nh_.subscribe("/local_path", 10, &MpcPathTracker::path_callback, this, ros::TransportHints().tcpNoDelay());
    sub_joint_states_ = nh_.subscribe("/sq2_ccv/joint_states", 10, &MpcPathTracker::joint_states_callback, this);
    // publisher
    pub_cmd_vel_ = nh_.advertise<geometry_msgs::Twist>("/local/cmd_vel", 1);
    pub_cmd_pos_ = nh_.advertise<ccv_dynamixel_msgs::CmdPoseByRadian>("/local/cmd_pos", 1);

    path_x = Eigen::VectorXd::Zero(HORIZON_T_);
    path_y = Eigen::VectorXd::Zero(HORIZON_T_);
    path_yaw = Eigen::VectorXd::Zero(HORIZON_T_);

    //states
    mpc_state.push_back("x");
    mpc_state.push_back("y");
    mpc_state.push_back("yaw");
    mpc_state.push_back("v");
    mpc_state.push_back("omega");
    mpc_state.push_back("omega_r");
    mpc_state.push_back("omega_l");
    mpc_state.push_back("steer_r");
    mpc_state.push_back("steer_l");
    //inputs
    mpc_input.push_back("domega_r");
    mpc_input.push_back("domega_l");
    mpc_input.push_back("dsteer_r");
    mpc_input.push_back("dsteer_l");
    //bound
    mpc_bound.push_back(X_BOUND_);
    mpc_bound.push_back(Y_BOUND_);
    mpc_bound.push_back(YAW_BOUND_);
    mpc_bound.push_back(V_BOUND_);
    mpc_bound.push_back(OMEGA_BOUND_);
    mpc_bound.push_back(OMEGA_R_BOUND_);
    mpc_bound.push_back(OMEGA_L_BOUND_);
    mpc_bound.push_back(STEER_R_BOUND_);
    mpc_bound.push_back(STEER_L_BOUND_);
    mpc_bound.push_back(DOMEGA_R_BOUND_);
    mpc_bound.push_back(DOMEGA_L_BOUND_);
    mpc_bound.push_back(DSTEER_R_BOUND_);
    mpc_bound.push_back(DSTEER_L_BOUND_);
    
    
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

FG_eval::FG_eval(Eigen::VectorXd ref_x, Eigen::VectorXd ref_y, Eigen::VectorXd ref_yaw)
{
    this->ref_x = ref_x; 
    this->ref_y = ref_y;
    this->ref_yaw = ref_yaw;
}


void MpcPathTracker::path_callback(const nav_msgs::PathConstPtr &msg)
{
    std::cout << "path callback" << std::endl;
    have_received_path_ = true;
    path_ = *msg;
}

void MpcPathTracker::joint_states_callback(const sensor_msgs::JointState::ConstPtr &msg)
{
    //std::cout << "joint states callback" << std::endl;
    if (right_steering_joint_idx == std::numeric_limits<size_t>::max() || left_steering_joint_idx == std::numeric_limits<size_t>::max() ||
        right_wheel_joint_idx == std::numeric_limits<size_t>::max() || left_wheel_joint_idx == std::numeric_limits<size_t>::max())
    {
        for (size_t i = 0; i < msg->name.size(); i++)
        {
            if (msg->name[i] == "right_steering_joint") right_steering_joint_idx = i;
            else if (msg->name[i] == "left_steering_joint") left_steering_joint_idx = i;
            else if(msg->name[i] == "right_wheel_joint") right_wheel_joint_idx = i;
            else if(msg->name[i] == "left_wheel_joint") left_wheel_joint_idx = i;
        }
    }
    else
    {
        steering_angle_r = msg->position[right_steering_joint_idx];
        steering_angle_l = msg->position[left_steering_joint_idx];
        omega_r = msg->velocity[right_wheel_joint_idx];
        omega_l = msg->velocity[left_wheel_joint_idx];
    }
}
bool MpcPathTracker::update_current_pose()
{
    bool transformed = false;
    geometry_msgs::PoseStamped pose;
    try
    {
        geometry_msgs::TransformStamped tf = tf_buffer_.lookupTransform(world_frame_id_, robot_frame_id_, ros::Time(0));
        current_pose.header = tf.header;
        current_pose.pose.position.x = tf.transform.translation.x;
        current_pose.pose.position.y = tf.transform.translation.y;
        current_pose.pose.orientation = tf.transform.rotation;
        pose.header = current_pose.header;
        pose.pose.position.x = 0;
        pose.pose.position.y = 0;
        pose.pose.orientation = tf.transform.rotation;
        transformed = true;
    }
    catch (tf2::TransformException &ex)
    {
        std::cout << ex.what() << std::endl;
    }
    return transformed;
}

void MpcPathTracker::update_current_state(Eigen::VectorXd& current_state){
    double current_time = ros::Time::now().toSec();
    double dt = current_time - last_time;
    last_time = current_time;
    double dx = current_pose.pose.position.x - previous_pose.pose.position.x;
    double dy = current_pose.pose.position.y - previous_pose.pose.position.y;
    double dyaw = tf2::getYaw(current_pose.pose.orientation) - tf2::getYaw(previous_pose.pose.orientation);
    double v = sqrt(dx * dx + dy * dy) / dt;
    double omega = dyaw / dt;
    //x, y, yaw, v, omega, omega_l, omega_r, steer_l, steer_r
    current_state << 0, 0, tf2::getYaw(current_pose.pose.orientation), v, omega, omega_l, omega_r, steering_angle_l, steering_angle_r;
}

void MpcPathTracker::path_to_vector(void){
    int m = VREF_ * DT_ / RESOLUTION_ + 1;// TODO:delete 1
    int index = 0;
    for(int i=0;i<HORIZON_T_;i++){
        if(i*m<path_.poses.size()){
            index = i*m;
            path_x[i] = path_.poses[index].pose.position.x;
            path_y[i] = path_.poses[index].pose.position.y;
            path_yaw[i] = tf2::getYaw(path_.poses[index].pose.orientation);
        }else{
            path_x[i] = path_.poses[path_.poses.size() - 1].pose.position.x;
            path_y[i] = path_.poses[path_.poses.size() - 1].pose.position.y;
            path_yaw[i] = tf2::getYaw(path_.poses[path_.poses.size() - 1].pose.orientation);
        }
    }
}



std::vector<double> MPC::solve(Eigen::VectorXd current_state, Eigen::VectorXd ref_x, Eigen::VectorXd ref_y, Eigen::VectorXd ref_yaw)
{
    //current_state = {x, y, yaw, v, omega, omega_l, omega_r, steer_l, steer_r} 計9
    bool ok = true;

    if(!(current_state.size() == state.size())) ROS_WARN_STREAM("Different number of STATE");
    
    size_t n_variables = state.size() * T + input.size() * (T-1);
    size_t n_constraints = state.size() * T; 
    
    //varsを宣言（全予測ホライゾンの状態と入力を格納）
    Dvector vars(n_variables);
    for(int i=0;i<n_variables;i++){
        vars[i] = 0.0;
    }

    //各状態の上下限を定義
    Dvector vars_lower_bound(n_variables);
    Dvector vars_upper_bound(n_variables);
    for(size_t i=0; i<state.size(); i++){
        for(size_t j=0; j<T; j++){
            vars_upper_bound[i*T + j] = bound[i];
            vars_lower_bound[i*T + j] = -bound[i];
        }
    }
    for(size_t i=0; i<input.size(); i++){
        for(size_t j=0; j<T-1; j++){
            vars_upper_bound[state.size()*T + i*(T-1) + j]=bound[state.size()+i];
            vars_lower_bound[state.size()*T + i*(T-1) + j]=-bound[state.size()+i];
        }
    }

    // 等式制約
    Dvector constraints_lower_bound(n_constraints);
    Dvector constraints_upper_bound(n_constraints);
    for(size_t i=0;i<n_constraints;i++){
        constraints_lower_bound[i] = 0.0;
        constraints_upper_bound[i] = 0.0;
    }
    // t=0の設定
    for(size_t i=0; i<current_state.size(); i++){
        constraints_upper_bound[i*T] = current_state[i];
        constraints_lower_bound[i*T] = current_state[i];
    }
    
    
    //目的関数と制約を評価するオブジェクト
    FG_eval fg_eval(ref_x, ref_y, ref_yaw);
    

    std::cout << "======HERE=====" << std::endl;
    ////////////////////////////////////////////////////////////////////////////
    int x_start = 0;
    int y_start = x_start + T;
    int yaw_start = y_start + T;
    int v_start = yaw_start + T;
    int omega_start = v_start + T;
    int omega_r_start = omega_start + T;
    int omega_l_start = omega_r_start + T;
    int steer_r_start = omega_l_start + T;
    int steer_l_start = steer_r_start + T;
    int domega_r_start = steer_l_start + T;
    int domega_l_start = domega_r_start + T-1;
    int dsteer_r_start = domega_l_start + T-1;
    int dsteer_l_start = dsteer_r_start + T-1;
    std::string options;
    options += "Integer print_level  0\n";

    options += "Sparse  true                forward\n";
    options += "Sparse  true                reverse\n";

    options += "Numeric max_cpu_time                    0.5\n";

    
    
    CppAD::ipopt::solve_result<Dvector> solution;

    std::cout << "optimization start" << std::endl;
    CppAD::ipopt::solve<Dvector, FG_eval>(
            options, vars, vars_lower_bound, vars_upper_bound, constraints_lower_bound,
            constraints_upper_bound, fg_eval, solution);
    

    std::cout << "optimization end" << std::endl;
    ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;
    std::cout << solution.status << std::endl;
    std::cout << ok << std::endl;

    auto cost = solution.obj_value;
    std::cout << "Cost " << cost << std::endl;

    if(ok){
        std::cout<<"===OK==="<<std::endl;
        failure_count = 0;
        result.clear();
        // 何故か0だとうまく行かない
        result.push_back(solution.x[v_start+1]);
        result.push_back(solution.x[omega_start+1]);
        //予測軌道
        for(int i = 0; i < T-1; i++){
            result.push_back(solution.x[x_start+i+1]);
            result.push_back(solution.x[y_start+i+1]);
            result.push_back(solution.x[yaw_start+i+1]);
        }
    }else{
        if(failure_count < T - 1){
            failure_count++;
        }
        result.push_back(solution.x[v_start+1+failure_count]);
        result.push_back(solution.x[omega_start+1+failure_count]);
        result[v_start] = result[v_start + failure_count];
        result[omega_start] = result[omega_start + failure_count];

        //予測軌道
        for(int i = failure_count; i < T-1; i++){
            
            result.push_back(solution.x[x_start+i+1]);
            result.push_back(solution.x[y_start+i+1]);
            result.push_back(solution.x[yaw_start+i+1]);
            
        }
    }
    return result;
}


void FG_eval::operator()(ADvector& fg, const ADvector& vars){
    std::cout << "FG_eval() start" << std::endl;
    // cost
    fg[0] = 0;
    // state
    int x_start = 0;
    int y_start = x_start + T;
    int yaw_start = y_start + T;
    int v_start = yaw_start + T;
    int omega_start = v_start + T;
    int omega_r_start = omega_start + T;
    int omega_l_start = omega_r_start + T;
    int steer_r_start = omega_l_start + T;
    int steer_l_start = steer_r_start + T;
    int domega_r_start = steer_l_start + T;
    int domega_l_start = domega_r_start + T-1;
    int dsteer_r_start = domega_l_start + T-1;
    int dsteer_l_start = dsteer_r_start + T-1;

    for(int i=0;i<T-1;i++){
        // pathとの距離
        fg[0] += 0.2 * (CppAD::pow(vars[x_start + i] - ref_x[i], 2) + CppAD::pow(vars[y_start + i] - ref_y[i], 2));
        // 向き
        //fg[0] += 0.1 * CppAD::pow(vars[yaw_start + i] - ref_yaw[i], 2);
        // 速度
        fg[0] += 100 * CppAD::pow(VREF - vars[v_start + i], 2);
        // 角加速度
        fg[0] += 0.1 * CppAD::pow(vars[omega_start + i] - vars[omega_start + i+ 1], 2);
    }

    std::cout << "constrains start" << std::endl;
    //constraint
    //初期状態
    fg[1 + x_start] = vars[x_start];
    fg[1 + y_start] = vars[y_start];
    fg[1 + yaw_start] = vars[yaw_start];
    fg[1 + v_start] = vars[v_start];
    fg[1 + omega_start] = vars[omega_start];
    fg[1 + omega_r_start] = vars[omega_r_start];
    fg[1 + omega_l_start] = vars[omega_l_start];

    std::cout << "constraints loop start" << std::endl;

    for(int i=0;i<T-1;i++){
        //t+1
        AD<double> x1 = vars[x_start + i + 1];
        AD<double> y1 = vars[y_start + i + 1];
        AD<double> yaw1 = vars[yaw_start + i + 1];
        AD<double> v1 = vars[v_start + i + 1];
        AD<double> omega1 = vars[omega_start + i + 1];
        AD<double> omega_r1 = vars[omega_r_start + i + 1];
        AD<double> omega_l1 = vars[omega_l_start + i + 1];
        //t
        AD<double> x0 = vars[x_start + i];
        AD<double> y0 = vars[y_start + i];
        AD<double> yaw0 = vars[yaw_start + i];
        AD<double> v0 = vars[v_start + i];
        AD<double> omega0 = vars[omega_start + i];
        AD<double> omega_r0 = vars[omega_r_start + i];
        AD<double> omega_l0 = vars[omega_l_start + i];
        //入力ホライゾンはt+1を考慮しない
        AD<double> domega_r0 = vars[domega_r_start + i];
        AD<double> domega_l0 = vars[domega_l_start + i];

        //制約
        fg[2 + omega_r_start + i] = omega_r1 - (omega_r0 + domega_r0 * DT);
        fg[2 + omega_l_start + i] = omega_l1 - (omega_l0 + domega_l0 * DT);
        fg[2 + v_start + i] = v1 - (WHEEL_RADIUS / 2.0) * (omega_r1 + omega_l1);
        fg[2 + omega_start + i] = omega1 - (WHEEL_RADIUS / TREAD) * (omega_r1 - omega_l1);
        fg[2 + x_start + i] = x1 - (x0 + v0 * CppAD::cos(yaw0) * DT);
        fg[2 + y_start + i] = y1 - (y0 + v0 * CppAD::sin(yaw0) * DT);
        fg[2 + yaw_start + i] = yaw1 - (yaw0 + omega0 * DT);
    }
    std::cout << "FG_eval() end" << std::endl;
}


void MpcPathTracker::process()
{
    ros::Rate loop_rate(HZ_);

    while (ros::ok())
    {
        if (have_received_path_)
        {
            bool transformed = update_current_pose();

            if (transformed && !path_.poses.empty())
            {
                // std::cout << "=== ccv mpc steering ===" << std::endl;
                if (first_transform)
                {
                    last_time = ros::Time::now().toSec();
                    first_transform = false;
                }
                else
                {
                    double current_time = ros::Time::now().toSec();
                    double dt = current_time - last_time;
                    last_time = current_time;

                    Eigen::VectorXd current_state(9);
                    update_current_state(current_state);
                    path_to_vector();
                    // std::cout<<"path_x :\n"<<path_x<<std::endl;
                    // std::cout<<"path_y :\n"<<path_y<<std::endl;
                    // std::cout<<"path_yaw:\n"<<path_yaw<<std::endl;

                    //debug
                    MPC mpc(HORIZON_T_, mpc_state, mpc_input, mpc_bound);
                    // std::cout << "solving" << std::endl;
                    // std::cout << "Type of current_state: " << typeid(current_state).name() << std::endl;
                    // std::cout << "Type of path_x: " << typeid(path_x).name() << std::endl;
                    // std::cout << "Type of path_y: " << typeid(path_y).name() << std::endl;
                    // std::cout << "Type of path_yaw: " << typeid(path_yaw).name() << std::endl;
                    auto result=mpc.solve(current_state, path_x, path_y, path_yaw);
                    // std::cout << "solved" << std::endl;
                    geometry_msgs::Twist velocity;
                    // velocity.linear.x = result[0];
                    // velocity.angular.z = result[1];
                    // std::cout << velocity << std::endl;
                    // pub_cmd_vel_.publish(velocity);


                }
                previous_pose = current_pose;
            }
        }
        else
            ROS_WARN_STREAM("cannot receive path");
        ros::spinOnce();
        std::cout << "==============================" << std::endl;
        loop_rate.sleep();
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "mpc_path_tracker");
    MpcPathTracker mpc_path_tracker;
    mpc_path_tracker.process();
    return 0;
}

*/

//ros
#include <ros/ros.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>
#include <tf/tf.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/PoseArray.h>

//ipopt
#include <Eigen/Core>
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>

using CppAD::AD;

class MPC{
public:
    MPC();

    // state, ref_x, ref_y, ref_yaw
    std::vector<double> solve(Eigen::VectorXd, Eigen::VectorXd, Eigen::VectorXd, Eigen::VectorXd);

};

class FG_eval{
public:
    FG_eval(Eigen::VectorXd, Eigen::VectorXd, Eigen::VectorXd);

    typedef CPPAD_TESTVECTOR(AD<double>) ADvector;

    void operator()(ADvector&, const ADvector&);

private:
    Eigen::VectorXd ref_x;
    Eigen::VectorXd ref_y;
    Eigen::VectorXd ref_yaw;

};

class MPCPathTracker
{
public:
    MPCPathTracker(void);

    void path_callback(const nav_msgs::PathConstPtr&);

    void process(void);
    void path_to_vector(void);

private:
    ros::NodeHandle nh;
    ros::Publisher velocity_pub;
    ros::Publisher path_pub;
    ros::Subscriber path_sub;
    MPC mpc;
    nav_msgs::Path path;
    tf::TransformListener listener;
    geometry_msgs::PoseStamped current_pose;
    geometry_msgs::PoseStamped previous_pose;
    tf::StampedTransform _transform;
    geometry_msgs::TransformStamped transform;
    Eigen::VectorXd path_x;
    Eigen::VectorXd path_y;
    Eigen::VectorXd path_yaw;
    bool first_transform = true;
    double last_time;

};

// ホライゾン長さ
int T = 15;
// 周期
double DT = 0.1;// [s]
const double HZ = 10;
// 目標速度
double VREF;// [m/s]
// 最大速度
double MAX_VELOCITY; // [m/s]
// 最大角速度
double MAX_ANGULAR_VELOCITY;// [rad/s]
// ホイール角加速度
double WHEEL_ANGULAR_ACCELERATION_LIMIT;// [rad/s^2]
// ホイール角速度
double WHEEL_ANGULAR_VELOCITY_LIMIT;// [rad/s]
// ホイール半径
double WHEEL_RADIUS;// [m]
// トレッド
double TREAD;// [m]
// グリッドマップ分解能
double RESOLUTION;// [m]

std::string WORLD_FRAME;
std::string ROBOT_FRAME;
std::string VELOCITY_TOPIC_NAME;
std::string INTERMEDIATE_PATH_TOPIC_NAME;

// state
size_t x_start = 0;
size_t y_start = x_start + T;
size_t yaw_start = y_start + T;
size_t v_start = yaw_start + T;
size_t omega_start = v_start + T;
size_t omega_r_start = omega_start + T;
size_t omega_l_start = omega_r_start + T;
// input
size_t domega_r_start = omega_l_start + T;
size_t domega_l_start = domega_r_start + T - 1;

// 最適化失敗時は最後の成功データを使う
int failure_count = 0;
std::vector<double> result;

double min_distance(nav_msgs::Path&, geometry_msgs::PoseStamped&);
double get_distance(geometry_msgs::PoseStamped&, geometry_msgs::PoseStamped&);

int main(int argc, char** argv)
{
    ros::init(argc, argv, "ccv_mpc_steering");
    ros::NodeHandle local_nh("~");

    // local_nh.getParam("HORIZON_T", T);
    // local_nh.getParam("/dynamic_avoidance/VREF", VREF);
    // local_nh.getParam("/dynamic_avoidance/MAX_ANGULAR_VELOCITY", MAX_ANGULAR_VELOCITY);
    // local_nh.getParam("/diff_drive/MAX_WHEEL_ANGULAR_ACCELERATION", WHEEL_ANGULAR_ACCELERATION_LIMIT);
    // local_nh.getParam("/diff_drive/MAX_WHEEL_ANGULAR_VELOCITY", WHEEL_ANGULAR_VELOCITY_LIMIT);
    // local_nh.getParam("/diff_drive/WHEEL_RADIUS", WHEEL_RADIUS);
    // local_nh.getParam("/diff_drive/TREAD", TREAD);
    // local_nh.getParam("/diff_drive/MAX_VELOCITY", MAX_VELOCITY);
    // local_nh.getParam("/dynamic_avoidance/RESOLUTION", RESOLUTION);
    // local_nh.getParam("/dynamic_avoidance/ROBOT_FRAME", ROBOT_FRAME);
    // local_nh.getParam("/dynamic_avoidance/WORLD_FRAME", WORLD_FRAME);
    // local_nh.getParam("/dynamic_avoidance/VELOCITY_TOPIC_NAME", VELOCITY_TOPIC_NAME);
    // local_nh.getParam("/dynamic_avoidance/INTERMEDIATE_PATH_TOPIC_NAME", INTERMEDIATE_PATH_TOPIC_NAME);

    local_nh.param("HORIZON_T", T, {15});
    local_nh.param("/dynamic_avoidance/WORLDT_FRAME", WORLD_FRAME, {"odom"});
    local_nh.param("/dynamic_avoidance/ROBOT_FRAME", ROBOT_FRAME, {"base_link"});
    local_nh.param("/diff_drive/TREAD", TREAD, {0.5});
    local_nh.param("/diff_drive/WHEEL_RADIUS", WHEEL_RADIUS,{0.15});
    //local_nh_.param("goal_border", GOAL_BORDER_, {0.3});
    //local_nh_.param("pitch_offset", PITCH_OFFSET_, {3.0 * M_PI / 180.0});
    local_nh.param("/dynamic_avoidance/VREF", VREF, {1.2});
    //local_nh_.param("resolution", RESOLUTION_, {0.1});
    local_nh.param("/dynamic_avoidance/MAX_ANGULAR_VELOCITY", MAX_ANGULAR_VELOCITY, {3.14});
    local_nh.getParam("/diff_drive/MAX_WHEEL_ANGULAR_ACCELERATION", WHEEL_ANGULAR_ACCELERATION_LIMIT);

    std::cout << "T: " << T << std::endl;
    std::cout << "VREF: " << VREF << std::endl;
    std::cout << "MAX_VELOCITY: " << MAX_VELOCITY << std::endl;
    std::cout << "MAX_ANGULAR_VELOCITY: " << MAX_ANGULAR_VELOCITY << std::endl;
    std::cout << "WHEEL_ANGULAR_VELOCITY_LIMIT: " << WHEEL_ANGULAR_VELOCITY_LIMIT << std::endl;
    std::cout << "WHEEL_ANGULAR_ACCELERATION_LIMIT: " << WHEEL_ANGULAR_ACCELERATION_LIMIT << std::endl;
    std::cout << "WHEEL_RADIUS: " << WHEEL_RADIUS << std::endl;
    std::cout << "TREAD: " << TREAD << std::endl;
    std::cout << "RESOLUTION: " << RESOLUTION << std::endl;
    std::cout << "ROBOT_FRAME: " << ROBOT_FRAME << std::endl;
    std::cout << "WORLD_FRAME: " << WORLD_FRAME << std::endl;
    std::cout << "VELOCITY_TOPIC_NAME: " << VELOCITY_TOPIC_NAME << std::endl;
    std::cout << "INTERMEDIATE_PATH_TOPIC_NAME: " << INTERMEDIATE_PATH_TOPIC_NAME << std::endl;


    
    MPCPathTracker mpc_path_tracker;

    ros::Rate loop_rate(HZ);

    while(ros::ok()){
        mpc_path_tracker.process();

        ros::spinOnce();
        loop_rate.sleep();
    }
    return 0;
}

MPC::MPC(){}

std::vector<double> MPC::solve(Eigen::VectorXd state, Eigen::VectorXd ref_x, Eigen::VectorXd ref_y, Eigen::VectorXd ref_yaw)
{
    /*
     * state:x, y, yaw, v, omega, omega_r, omega_l
     */
    bool ok = true;
    size_t i;
    typedef CPPAD_TESTVECTOR(double) Dvector;

    double x = state[0];
    double y = state[1];
    double yaw = state[2];
    double v = state[3];
    double omega = state[4];
    double omega_r = state[5];
    double omega_l = state[6];


    /*
    std::cout << "--- state ---" << std::endl;
    std::cout << state << std::endl;
    std::cout << "--- path_x ---" << std::endl;
    std::cout << ref_x << std::endl;
    std::cout << "--- path_y ---" << std::endl;
    std::cout << ref_y << std::endl;
    std::cout << "--- path_yaw ---" << std::endl;
    std::cout << ref_yaw << std::endl;
    */

    // 7(x, y, yaw, v, omega, omega_r, omega_l), 2(domega_r, domega_l)
    size_t n_variables = 7 * T + 2 * (T - 1);

    size_t n_constraints = 7 * T;

    Dvector vars(n_variables);
    for(int i=0;i<n_variables;i++){
        vars[i] = 0.0;
    }
    
    vars[x_start] = x;
    vars[y_start] = y;
    vars[yaw_start] = yaw;
    vars[v_start] = v;
    vars[omega_start] = omega;
    vars[omega_r_start] = omega_r;
    vars[omega_l_start] = omega_l;

    Dvector vars_lower_bound(n_variables);
    Dvector vars_upper_bound(n_variables);
    //std::cout<<"- - -HERE- - -"<<std::endl;
    Dvector constraints_lower_bound(n_constraints);
    Dvector constraints_upper_bound(n_constraints);

    for(int i=0;i<v_start;i++){
        // x, y, yaw
        vars_lower_bound[i] = -1.0e19;
        vars_upper_bound[i] = 1.0e19;
    }
    for(int i=v_start;i<omega_start;i++){
        // v
        vars_lower_bound[i] = 0;
        vars_upper_bound[i] = MAX_VELOCITY;
    }
    for(int i=omega_start;i<omega_r_start;i++){
        // omega
        vars_lower_bound[i] = -MAX_ANGULAR_VELOCITY;
        vars_upper_bound[i] = MAX_ANGULAR_VELOCITY;
    }
    for(int i=omega_r_start;i<domega_r_start;i++){
        // omega_r, omega_l
        vars_lower_bound[i] = -WHEEL_ANGULAR_VELOCITY_LIMIT;
        vars_upper_bound[i] = WHEEL_ANGULAR_VELOCITY_LIMIT;
    }
    for(int i=omega_start;i<n_variables;i++){
        // domega_r, domega_l
        vars_lower_bound[i] = -WHEEL_ANGULAR_ACCELERATION_LIMIT;
        vars_upper_bound[i] = WHEEL_ANGULAR_ACCELERATION_LIMIT;
    }

    // 等式制約
    // Dvector constraints_lower_bound(n_constraints);
    // Dvector constraints_upper_bound(n_constraints);

    for(int i=0;i<n_constraints;i++){
        constraints_lower_bound[i] = 0.0;
        constraints_upper_bound[i] = 0.0;
    }
    

    // t=0の設定
    //std::cout << "--- HERE ---" << std::endl;
    constraints_lower_bound[x_start] = x;
    constraints_lower_bound[y_start] = y;
    constraints_lower_bound[yaw_start] = yaw;
    constraints_lower_bound[v_start] = v;
    constraints_lower_bound[omega_start] = omega;
    constraints_lower_bound[omega_r_start] = omega_r;
    constraints_lower_bound[omega_l_start] = omega_l;

    constraints_upper_bound[x_start] = x;
    constraints_upper_bound[y_start] = y;
    constraints_upper_bound[yaw_start] = yaw;
    constraints_upper_bound[v_start] = v;
    constraints_upper_bound[omega_start] = omega;
    constraints_upper_bound[omega_r_start] = omega_r;
    constraints_upper_bound[omega_l_start] = omega_l;
    //std::cout << "--- HERE ---" << std::endl;

    FG_eval fg_eval(ref_x, ref_y, ref_yaw);

    std::string options;
    options += "Integer print_level  0\n";

    options += "Sparse  true                forward\n";
    options += "Sparse  true                reverse\n";

    options += "Numeric max_cpu_time                    0.5\n";

    CppAD::ipopt::solve_result<Dvector> solution;

    std::cout << "optimization start" << std::endl;
    CppAD::ipopt::solve<Dvector, FG_eval>(
            options, vars, vars_lower_bound, vars_upper_bound, constraints_lower_bound,
            constraints_upper_bound, fg_eval, solution);

    std::cout << "optimization end" << std::endl;
    ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;
    std::cout << "solutio  status "<< solution.status << std::endl;
    std::cout << "ok "<< ok << std::endl;

    auto cost = solution.obj_value;
    std::cout << "Cost " << cost << std::endl;

    if(ok){
        failure_count = 0;
        result.clear();
        // 何故か0だとうまく行かない
        result.push_back(solution.x[v_start+1]);
        result.push_back(solution.x[omega_start+1]);
        //予測軌道
        for(int i = 0; i < T-1; i++){
            result.push_back(solution.x[x_start+i+1]);
            result.push_back(solution.x[y_start+i+1]);
            result.push_back(solution.x[yaw_start+i+1]);
        }
    }else{
        if(failure_count < T - 1){
            failure_count++;
        }
        result.push_back(solution.x[v_start+1+failure_count]);
        result.push_back(solution.x[omega_start+1+failure_count]);
        result[v_start] = result[v_start + failure_count];
        result[omega_start] = result[omega_start + failure_count];

        //予測軌道
        for(int i = failure_count; i < T-1; i++){
            /*
            result.push_back(solution.x[x_start+i+1]);
            result.push_back(solution.x[y_start+i+1]);
            result.push_back(solution.x[yaw_start+i+1]);
            */
        }
    }
    /*
    std::cout << "--- result ---" << std::endl;
    for(int i=0;i<result.size();i++){
        std::cout << result[i] << std::endl;
    }
    */
    return result;
}

FG_eval::FG_eval(Eigen::VectorXd ref_x, Eigen::VectorXd ref_y, Eigen::VectorXd ref_yaw)
{
    this->ref_x = ref_x;
    this->ref_y = ref_y;
    this->ref_yaw = ref_yaw;
}

void FG_eval::operator()(ADvector& fg, const ADvector& vars)
{
    std::cout << "FG_eval() start" << std::endl;
    // cost
    fg[0] = 0;
    // state
    for(int i=0;i<T-1;i++){
        // pathとの距離
        fg[0] += 0.2 * (CppAD::pow(vars[x_start + i] - ref_x[i], 2) + CppAD::pow(vars[y_start + i] - ref_y[i], 2));
        // 向き
        //fg[0] += 0.1 * CppAD::pow(vars[yaw_start + i] - ref_yaw[i], 2);
        // 速度
        fg[0] += 100 * CppAD::pow(VREF - vars[v_start + i], 2);
        // 角加速度
        fg[0] += 0.1 * CppAD::pow(vars[omega_start + i] - vars[omega_start + i+ 1], 2);
    }
    // input
    for(int i=0;i<T-2;i++){
    }

    std::cout << "constrains start" << std::endl;
    //constraint
    //初期状態
    fg[1 + x_start] = vars[x_start];
    fg[1 + y_start] = vars[y_start];
    fg[1 + yaw_start] = vars[yaw_start];
    fg[1 + v_start] = vars[v_start];
    fg[1 + omega_start] = vars[omega_start];
    fg[1 + omega_r_start] = vars[omega_r_start];
    fg[1 + omega_l_start] = vars[omega_l_start];

    std::cout << "constraints loop start" << std::endl;

    for(int i=0;i<T-1;i++){
        //t+1
        AD<double> x1 = vars[x_start + i + 1];
        AD<double> y1 = vars[y_start + i + 1];
        AD<double> yaw1 = vars[yaw_start + i + 1];
        AD<double> v1 = vars[v_start + i + 1];
        AD<double> omega1 = vars[omega_start + i + 1];
        AD<double> omega_r1 = vars[omega_r_start + i + 1];
        AD<double> omega_l1 = vars[omega_l_start + i + 1];
        //t
        AD<double> x0 = vars[x_start + i];
        AD<double> y0 = vars[y_start + i];
        AD<double> yaw0 = vars[yaw_start + i];
        AD<double> v0 = vars[v_start + i];
        AD<double> omega0 = vars[omega_start + i];
        AD<double> omega_r0 = vars[omega_r_start + i];
        AD<double> omega_l0 = vars[omega_l_start + i];
        //入力ホライゾンはt+1を考慮しない
        AD<double> domega_r0 = vars[domega_r_start + i];
        AD<double> domega_l0 = vars[domega_l_start + i];

        //制約
        fg[2 + omega_r_start + i] = omega_r1 - (omega_r0 + domega_r0 * DT);
        fg[2 + omega_l_start + i] = omega_l1 - (omega_l0 + domega_l0 * DT);
        fg[2 + v_start + i] = v1 - (WHEEL_RADIUS / 2.0) * (omega_r1 + omega_l1);
        fg[2 + omega_start + i] = omega1 - (WHEEL_RADIUS / TREAD) * (omega_r1 - omega_l1);
        fg[2 + x_start + i] = x1 - (x0 + v0 * CppAD::cos(yaw0) * DT);
        fg[2 + y_start + i] = y1 - (y0 + v0 * CppAD::sin(yaw0) * DT);
        fg[2 + yaw_start + i] = yaw1 - (yaw0 + omega0 * DT);
    }
    std::cout << "FG_eval() end" << std::endl;
}

MPCPathTracker::MPCPathTracker(void)
{
    velocity_pub = nh.advertise<geometry_msgs::Twist>("/local/cmd_vel", 1);
    path_pub = nh.advertise<geometry_msgs::PoseArray>("/mpc_path", 1);
    path_sub = nh.subscribe("/local_path", 1, &MPCPathTracker::path_callback, this);
    path_x = Eigen::VectorXd::Zero(T);
    path_y = Eigen::VectorXd::Zero(T);
    path_yaw = Eigen::VectorXd::Zero(T);
    std::cout << "=== mpc_path_tracker ===" << std::endl;
}

void MPCPathTracker::path_callback(const nav_msgs::PathConstPtr& msg)
{
    std::cout << "path callback" << std::endl;
    path = *msg;
}

void MPCPathTracker::process(void)
{
    bool transformed = false;
    geometry_msgs::PoseStamped pose;
    try{
        listener.lookupTransform(WORLD_FRAME, ROBOT_FRAME, ros::Time(0), _transform);
        tf::transformStampedTFToMsg(_transform, transform);
        current_pose.header = transform.header;
        current_pose.pose.position.x = transform.transform.translation.x;
        current_pose.pose.position.y = transform.transform.translation.y;
        current_pose.pose.orientation = transform.transform.rotation;
        pose.header = current_pose.header;
        pose.pose.position.x = 0;
        pose.pose.position.y = 0;
        pose.pose.orientation = transform.transform.rotation;
        transformed = true;
    }catch(tf::TransformException &ex){
        std::cout << ex.what() << std::endl;
    }

    if(!path.poses.empty() && transformed){
        std::cout << "=== diff drive mpc ===" << std::endl;
        ros::Time start_time = ros::Time::now();
        if(first_transform){
            last_time = ros::Time::now().toSec();
            first_transform = false;
        }else{
            //std::cout << current_pose << std::endl;
            double current_time = ros::Time::now().toSec();
            double dt = current_time - last_time;
            last_time = current_time;
            double dx = current_pose.pose.position.x - previous_pose.pose.position.x;
            double dy = current_pose.pose.position.y - previous_pose.pose.position.y;
            double dyaw = tf::getYaw(current_pose.pose.orientation) - tf::getYaw(previous_pose.pose.orientation);
            double v = sqrt(dx * dx + dy * dy) / dt;
            double omega = dyaw / dt;
            double omega_r = (v + omega * TREAD / 2.0) / WHEEL_RADIUS;
            double omega_l = (v - omega * TREAD / 2.0) / WHEEL_RADIUS;

            Eigen::VectorXd state(7);
            state << pose.pose.position.x, pose.pose.position.y, tf::getYaw(pose.pose.orientation), v, omega, omega_r, omega_l;
            std::cout << "path to vector" << std::endl;
            path_to_vector();
            std::cout << "solving" << std::endl;
            auto result = mpc.solve(state, path_x, path_y, path_yaw);
            std::cout << "solved" << std::endl;
            geometry_msgs::Twist velocity;
            velocity.linear.x = result[0];
            velocity.angular.z = result[1];
            std::cout << velocity << std::endl;
            velocity_pub.publish(velocity);
            // mpc表示
            geometry_msgs::PoseArray mpc_path;
            mpc_path.header.frame_id = ROBOT_FRAME;
            double yaw0 = tf::getYaw(pose.pose.orientation);
            for(int i=0;i<T-1;i++){
                geometry_msgs::Pose temp;
                temp.position.x = result[2+3*i] * cos(-yaw0) - result[3+3*i] * sin(-yaw0);
                temp.position.y = result[2+3*i] * sin(-yaw0) + result[3+3*i] * cos(-yaw0);
                temp.orientation = tf::createQuaternionMsgFromYaw(result[4+3*i] - yaw0);
                mpc_path.poses.push_back(temp);
            }
            path_pub.publish(mpc_path);
            // ~mpc表示
            path.poses.erase(path.poses.begin());
        }
        std::cout << ros::Time::now() - start_time << "[s]" << std::endl;
    }
    previous_pose = current_pose;
}

void MPCPathTracker::path_to_vector(void)
{
    int m = VREF * DT / RESOLUTION + 1;// TODO:delete 1
    int index = 0;
    for(int i=0;i<T;i++){
        if(i*m<path.poses.size()){
            index = i*m;
            path_x[i] = path.poses[index].pose.position.x;
            path_y[i] = path.poses[index].pose.position.y;
            path_yaw[i] = tf::getYaw(path.poses[index].pose.orientation);
        }else{
            path_x[i] = path.poses[path.poses.size() - 1].pose.position.x;
            path_y[i] = path.poses[path.poses.size() - 1].pose.position.y;
            path_yaw[i] = tf::getYaw(path.poses[path.poses.size() - 1].pose.orientation);
        }
    }
}

double min_distance(nav_msgs::Path& path, geometry_msgs::PoseStamped& pose)
{
    int length = path.poses.size();
    double min_distance = 100;
    for(int i=0;i<length;i++){
        double distance = get_distance(path.poses[i], pose);
        if(min_distance > distance){
            min_distance = distance;
        }
    }
    return min_distance;
}

double get_distance(geometry_msgs::PoseStamped& pose0, geometry_msgs::PoseStamped& pose1)
{
    return sqrt((pose0.pose.position.x - pose1.pose.position.x) * (pose0.pose.position.x - pose1.pose.position.x) + (pose0.pose.position.y - pose1.pose.position.y) * (pose0.pose.position.y - pose1.pose.position.y));
}
