#pragma once

#include "damiao.h"
#ifndef _WIN32
#include <unistd.h>
#endif
#include <cmath>
#include <iostream>
#include <vector>
#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <functional> // ���F�ϥ� std::function



// ��줽��
#define L1 25.0f
#define L2 31.0f
#define L1_M L1/100.0
#define L2_M L2/100.0



// ���ȵ��c��
struct MoveTask {
    float target_x;
    float target_z;
    float speed;
    float target_m4;
    float m4_speed;
    float force_x;
    float force_z;
    int relay_action = -1;
};

// �f�ѵ��G���c��
struct IKResult {
    float theta;
    float alpha;
    float beta;
    bool  valid;
};

class PalletizerController {
public:
    // �غc���G��l�ư��F�]�w
    PalletizerController(const std::string& serial_port = "/dev/ttyACM0")
        : M2(damiao::DM4340_48V, 0x02, 0x22),
        M3(damiao::DM4340_48V, 0x03, 0x33),
        M4(damiao::DM4340_48V, 0x04, 0x44),
        is_running(false)
    {
        serial = std::make_shared<SerialPort>(serial_port, B921600);
        dm = std::make_unique<damiao::Motor_Control>(serial);

        dm->addMotor(&M2);
        dm->addMotor(&M3);
        dm->addMotor(&M4);
    }

    // �Ѻc���G�w������������P���F
    ~PalletizerController() {
        stop();
    }

    // ��l�ƻP�Ұʰ��F
    bool init() {
        dm->disable(M2);
        dm->disable(M3);
        dm->disable(M4);

        bool success = true;
        if (dm->switchControlMode(M2, damiao::MIT_MODE)) std::cout << "M2 Switch MIT Success\n"; else success = false;
        if (dm->switchControlMode(M3, damiao::MIT_MODE)) std::cout << "M3 Switch MIT Success\n"; else success = false;
        if (dm->switchControlMode(M4, damiao::MIT_MODE)) std::cout << "M4 Switch MIT Success\n"; else success = false;

        dm->disable(M2);
        dm->disable(M3);
        dm->disable(M4);

        return success;
    }

    // �Ұ� 500Hz ��������
    void start() {
        if (!is_running) {
            is_running = true;
            std::cout << "�Ұ� 500Hz ���F��������..." << std::endl;
            control_thread = std::thread(&PalletizerController::motor_control_loop, this);
        }
    }

    void zero_set()
    {
        dm->disable(M2);
        dm->disable(M3);
        dm->disable(M4);
        dm->control_mit(M2, 0, 0, 0, 0, 0);
        dm->control_mit(M3, 0, 0, 0, 0, 0);
        dm->control_mit(M4, 0, 0, 0, 0, 0);


        dm->set_zero_position(M2);
        dm->set_zero_position(M3);
        dm->set_zero_position(M4);
    }

    // ���������P���F
    void stop() {
        if (is_running) {
            is_running = false;
            if (control_thread.joinable()) {
                control_thread.join();
            }
            dm->disable(M2);
            dm->disable(M3);
            dm->disable(M4);
            std::cout << "���������w����A���F�w�����C" << std::endl;
        }
    }

    void unlock()
    {


        dm->control_mit(M2, 0, 0, 0, 0, 0);
        dm->control_mit(M3, 0, 0, 0, 0, 0);
        dm->control_mit(M4, 0, 0, 0, 0, 0);

        dm->enable(M2);
        dm->enable(M3);
        dm->enable(M4);
        
        this->clear_queue();

        // �Ѽƨ̧ǡG(�ؼ�X, �ؼ�Z, �t��, M4����, M4�t��, X�b���ON, Z�b���ON)
        this->push_task(10, 20, 5.0f, 0, 2.0, 0.5f, 0.0f, 0);


    }

    void lock()
    {
        dm->disable(M2);
        dm->disable(M3);
        dm->disable(M4);

    }

    void set_relay_callback(std::function<void(int)> callback) {
        relay_callback = callback;
    }


    // �[�J�s���Ȧܦ�C
    void push_task1(float target_x, float target_z, float speed,
        float target_m4 = 0.0f, float m4_speed = 2.0f,
        float force_x = 0.0f, float force_z = 0.0f) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        //task_queue.push({ target_x, target_z, speed, target_m4, m4_speed, force_x, force_z });
    }

    void push_task(float target_x, float target_z, float speed,
        float target_m4 = 0.0f, float m4_speed = 2.0f,
        float force_x = 0.0f, float force_z = 0.0f,
        int relay_action = -1) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push({ target_x, target_z, speed, target_m4, m4_speed, force_x, force_z, relay_action });
    }

    // �ˬd���Ȧ�C�O�_����
    bool is_queue_empty() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return task_queue.empty();
    }

    void clear_queue() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue = std::queue<MoveTask>();
    }

private:
    std::shared_ptr<SerialPort> serial;
    std::unique_ptr<damiao::Motor_Control> dm;
    damiao::Motor M2, M3, M4;

    std::function<void(int)> relay_callback = nullptr;

    std::queue<MoveTask> task_queue;
    std::mutex queue_mutex;
    std::atomic<bool> is_running;
    std::thread control_thread;

    // --- �ƾǻP�B�ʾǻ��U�禡 ---
    double clamp(double v) {
        if (v > 1.0) return 1.0;
        if (v < -1.0) return -1.0;
        return v;
    }

    IKResult palletizer_ik(double x, double y, double z) {
        IKResult result;
        result.theta = std::atan2(y, x);
        double q = std::sqrt(x * x + y * y);
        double Alpha_a1 = std::atan2(z, q);
        double a = std::sqrt(q * q + z * z);

        double Alpha_a2 = std::acos(clamp((L1 * L1 + a * a - L2 * L2) / (2 * L1 * a)));
        result.alpha = M_PI / 2.0 - (Alpha_a1 + Alpha_a2);

        double Beta_b1 = std::acos(clamp((L1 * L1 + L2 * L2 - a * a) / (2 * L1 * L2)));
        result.beta = Beta_b1 - M_PI / 2.0;
        return result;
    }

    void palletizer_fk(float alpha, float beta, float& out_x, float& out_z) {
        out_x = L1 * std::sin(alpha) + L2 * std::cos(beta - alpha);
        out_z = L1 * std::cos(alpha) + L2 * std::sin(beta - alpha);
    }

    void calc_feedforward_torque(float alpha, float beta, float L1_m, float L2_m, float Fx, float Fz, float& tau_alpha, float& tau_beta) {
        float J11 = L1_m * std::cos(alpha) + L2_m * std::sin(beta - alpha);
        float J12 = -L2_m * std::sin(beta - alpha);
        float J21 = -L1_m * std::sin(alpha) - L2_m * std::cos(beta - alpha);
        float J22 = L2_m * std::cos(beta - alpha);

        tau_alpha = J11 * Fx + J21 * Fz;
        tau_beta = J12 * Fx + J22 * Fz;
    }

    // --- 500Hz ����֤��޿� ---
    void motor_control_loop() {
        bool has_active_task = false;
        int task_stage = 0; // 0: Idle, 1: ���ʤ��u(X,Z), 2: ����M4
        MoveTask current_task;

        const float freq = 500.0f;
        const float dt = 1.0f / freq;

        float current_x, current_z;
        palletizer_fk(M2.Get_Position(), M3.Get_Position(), current_x, current_z);
        float current_m4 = M4.Get_Position();

        float last_target_alpha = M2.Get_Position();
        float last_target_beta = M3.Get_Position();
        float last_target_m4 = current_m4;

        float cur_linear_speed = 0.0f;
        const float MAX_ACCEL = 400.0f;

        while (is_running) {
            auto start_time = std::chrono::high_resolution_clock::now();

            // 1. �������
            if (!has_active_task) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (!task_queue.empty()) {
                    current_task = task_queue.front();
                    task_queue.pop();
                    has_active_task = true;
                    task_stage = 1;
                    std::cout << ">> ����s����: ���� (X:" << current_task.target_x << ", Z:" << current_task.target_z << ")\n";
                }
            }

            // 2. �y�񴡸�
            if (has_active_task) {
                if (task_stage == 1) {
                    float dx = current_task.target_x - current_x;
                    float dz = current_task.target_z - current_z;
                    float distance = std::sqrt(dx * dx + dz * dz);

                    if (cur_linear_speed < current_task.speed) {
                        cur_linear_speed = std::min(cur_linear_speed + MAX_ACCEL * dt, current_task.speed);
                    }
                    else {
                        cur_linear_speed = std::max(cur_linear_speed - MAX_ACCEL * dt, current_task.speed);
                    }

                    float stop_dist = (cur_linear_speed * cur_linear_speed) / (2.0f * MAX_ACCEL);
                    if (distance < stop_dist) {
                        cur_linear_speed = std::max(cur_linear_speed - MAX_ACCEL * dt, 5.0f);
                    }

                    float step = cur_linear_speed * dt;

                    if (distance <= step) {
                        current_x = current_task.target_x;
                        current_z = current_task.target_z;
                        cur_linear_speed = 0;
                        task_stage = 2;
                        std::cout << "   -> ���u���A�}�l���� M4 ��� " << current_task.target_m4 << " rad\n";
                    }
                    else {
                        current_x += (dx / distance) * step;
                        current_z += (dz / distance) * step;
                    }
                }
                else if (task_stage == 2) {
                    float d_m4 = current_task.target_m4 - current_m4;
                    float m4_step = current_task.m4_speed * dt;

                    if (std::abs(d_m4) <= m4_step) {
                        current_m4 = current_task.target_m4;
                        has_active_task = false;
                        task_stage = 0;
                        std::cout << "   -> M4 ���A���ȹ��������I\n\n";

                        if (current_task.relay_action != -1 && relay_callback != nullptr) {
                            relay_callback(current_task.relay_action);
                        }

                    }
                    else {
                        current_m4 += (d_m4 > 0) ? m4_step : -m4_step;
                    }
                }
            }

            // 3. �f�V�B�ʾǻP����
            IKResult res = palletizer_ik(current_x, 0.0f, current_z);

            float v_alpha = (res.alpha - last_target_alpha) / dt;
            float v_beta = (res.beta - last_target_beta) / dt;
            float v_m4 = (current_m4 - last_target_m4) / dt;

            float tau_m2_ff = 0.0f, tau_m3_ff = 0.0f;
            calc_feedforward_torque(res.alpha, res.beta, L1_M, L2_M, current_task.force_x, current_task.force_z, tau_m2_ff, tau_m3_ff);

            float kp = 30.0f, kd = 0.5f;
            dm->control_mit(M2, kp, kd, res.alpha, v_alpha, tau_m2_ff);
            dm->control_mit(M3, kp, kd, res.beta, v_beta, tau_m3_ff);
            dm->control_mit(M4, kp, kd, current_m4, v_m4, 0);

            last_target_alpha = res.alpha;
            last_target_beta = res.beta;
            last_target_m4 = current_m4;

            // 4. �W�v���� (2ms)
            auto end_time = std::chrono::high_resolution_clock::now();
            int elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            if (elapsed_us < 2000) {
                std::this_thread::sleep_for(std::chrono::microseconds(2000 - elapsed_us));
            }
        }
    }
};