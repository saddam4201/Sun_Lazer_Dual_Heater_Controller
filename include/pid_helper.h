#ifndef PID_HELPER_H
#define PID_HELPER_H

#include <Arduino.h>

// Minimal, header-only PID controller replacement for PID_v1.h
// API compatible for the uses in control_tasks.cpp

#define DIRECT 0
#define REVERSE 1

#define AUTOMATIC 1
#define MANUAL 0

class PID {
public:
    PID(double* input, double* output, double* setpoint, double Kp, double Ki, double Kd, int direction = DIRECT)
        : myInput(input), myOutput(output), mySetpoint(setpoint), kp(Kp), ki(Ki), kd(Kd), controllerDirection(direction)
    {
        inAuto = false;
        outMin = 0.0;
        outMax = 100.0;
        integral = 0.0;
        lastInput = *myInput;
        lastTime = millis();
    }

    void SetMode(int mode) {
        bool newAuto = (mode == AUTOMATIC);
        if (newAuto && !inAuto) { // going from manual to auto
            initialize();
        }
        inAuto = newAuto;
    }

    void SetOutputLimits(double min, double max) {
        if (min >= max) return;
        outMin = min;
        outMax = max;
        // clamp current output and integral
        if (*myOutput > outMax) *myOutput = outMax;
        else if (*myOutput < outMin) *myOutput = outMin;
        if (integral > outMax) integral = outMax;
        else if (integral < outMin) integral = outMin;
    }

    void SetTunings(double Kp, double Ki, double Kd) {
        if (Kp < 0 || Ki < 0 || Kd < 0) return;
        kp = Kp;
        ki = Ki;
        kd = Kd;
    }

    bool Compute() {
        if (!inAuto) return false;
        unsigned long now = millis();
        double timeChange = (now - lastTime) / 1000.0; // seconds
        if (timeChange <= 0.0) return false; // avoid div by zero

        double input = *myInput;
        double error = *mySetpoint - input;
        integral += (ki * error * timeChange);
        // Anti-windup: clamp integral to output limits
        if (integral > outMax) integral = outMax;
        else if (integral < outMin) integral = outMin;

        // derivative on measurement
        double dInput = (input - lastInput) / timeChange;

        double output = kp * error + integral - kd * dInput;

        // clamp output
        if (output > outMax) output = outMax;
        else if (output < outMin) output = outMin;

        *myOutput = output;

        // remember for next time
        lastInput = input;
        lastTime = now;
        return true;
    }

private:
    double kp, ki, kd;
    int controllerDirection;
    double *myInput, *myOutput, *mySetpoint;
    bool inAuto;
    double outMin, outMax;
    double integral;
    double lastInput;
    unsigned long lastTime;

    void initialize() {
        integral = *myOutput;
        lastInput = *myInput;
        if (integral > outMax) integral = outMax;
        else if (integral < outMin) integral = outMin;
    }
};

#endif // PID_HELPER_H
