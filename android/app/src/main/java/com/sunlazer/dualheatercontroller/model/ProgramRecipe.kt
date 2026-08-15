package com.sunlazer.dualheatercontroller.model

import com.google.gson.annotations.SerializedName

data class ProgramRecipe(
    @SerializedName("idx") val idx: Int = 0,
    @SerializedName("name") var name: String = "P01",
    @SerializedName("h1_setpoint") var h1Setpoint: Float = 100.0f,
    @SerializedName("h2_setpoint") var h2Setpoint: Float = 100.0f,
    @SerializedName("process_time_sec") var processTimeSec: Long = 60L,
    @SerializedName("torque_limit") var torqueLimit: Float = 5.0f,
    @SerializedName("temp_tolerance") var tempTolerance: Float = 2.0f,
    @SerializedName("h1_Kp") var h1Kp: Float = 2.0f,
    @SerializedName("h1_Ki") var h1Ki: Float = 0.1f,
    @SerializedName("h1_Kd") var h1Kd: Float = 1.0f,
    @SerializedName("h2_Kp") var h2Kp: Float = 2.0f,
    @SerializedName("h2_Ki") var h2Ki: Float = 0.1f,
    @SerializedName("h2_Kd") var h2Kd: Float = 1.0f
)
