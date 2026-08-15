package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sunlazer.dualheatercontroller.model.ProgramRecipe
import com.sunlazer.dualheatercontroller.model.SystemStatus
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun TftDigitalTwinScreen(
    status: SystemStatus,
    activeRecipe: ProgramRecipe?,
    modifier: Modifier = Modifier
) {
    // 4:3 Aspect Ratio TFT Screen Bezel
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF05070A))
            .border(2.dp, Color(0xFF3B4856), RoundedCornerShape(12.dp))
            .padding(8.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(TftBackground)
                .border(1.dp, Color(0xFF1E2836))
                .padding(10.dp)
        ) {
            // Screen Header Bar
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF112233))
                    .padding(horizontal = 8.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "SUN LAZER DUAL HEATER",
                    color = TftCyanText,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
                Text(
                    text = "SCREEN: ${status.screenName}",
                    color = TftYellowText,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(modifier = Modifier.height(6.dp))

            // Screen Body matching ESP32 screens
            when (status.screen) {
                0 -> TftHomeScreenView(status, activeRecipe)
                1 -> TftProgramSelectView(status)
                2 -> TftProgramEditView(status, activeRecipe)
                3 -> TftTimerEditView(status, activeRecipe)
                4 -> TftPidTuningView(status, activeRecipe)
                5 -> TftServiceView(status)
                6 -> TftRtcSetView(status)
                else -> TftHomeScreenView(status, activeRecipe)
            }
        }
    }
}

@Composable
private fun TftHomeScreenView(status: SystemStatus, activeRecipe: ProgramRecipe?) {
    val stateColor by animateColorAsState(
        targetValue = when (status.stateName) {
            "IDLE" -> Color(0xFF4FC3F7)
            "SAFETY_CHECK" -> Color(0xFFFFB74D)
            "MOVE_DOWN", "MOVE_UP" -> Color(0xFF81C784)
            "HEAT_TO_SETPOINT", "PROCESS_TIMER" -> Color(0xFFFF8A65)
            "PROCESS_COMPLETE", "READY" -> Color(0xFF66BB6A)
            "ALARM_FAULT" -> Color(0xFFE53935)
            else -> Color(0xFF90A4AE)
        }, label = "stateColor"
    )

    Column(modifier = Modifier.fillMaxWidth()) {
        // State Banner
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(stateColor.copy(alpha = 0.25f))
                .border(1.dp, stateColor)
                .padding(vertical = 4.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "STATE: ${status.stateName}",
                color = stateColor,
                fontWeight = FontWeight.Bold,
                fontSize = 13.sp,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Process Temperatures
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text("HEATER 1 (PT100)", color = Color.Gray, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = "%.1f".format(status.h1Actual),
                        color = if (status.h1Actual > (activeRecipe?.h1Setpoint ?: status.h1Setpoint)) TftRedText else TftGreenText,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(" / %.0f °C".format(activeRecipe?.h1Setpoint ?: status.h1Setpoint), color = Color.LightGray, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                }
            }

            Column(modifier = Modifier.weight(1f)) {
                Text("HEATER 2 (PT100)", color = Color.Gray, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = "%.1f".format(status.h2Actual),
                        color = if (status.h2Actual > (activeRecipe?.h2Setpoint ?: status.h2Setpoint)) TftRedText else TftGreenText,
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(" / %.0f °C".format(activeRecipe?.h2Setpoint ?: status.h2Setpoint), color = Color.LightGray, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Torque & Timer
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text("TORQUE (TQ10)", color = Color.Gray, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Text(
                    text = "%.2f / %.1f Nm".format(status.torque, activeRecipe?.torqueLimit ?: status.torqueLimit),
                    color = TftCyanText,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                Text("PROCESS TIMER", color = Color.Gray, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                val hrs = status.remainingTimeSec / 3600
                val mins = (status.remainingTimeSec % 3600) / 60
                val secs = status.remainingTimeSec % 60
                Text(
                    text = "%02d:%02d:%02d".format(hrs, mins, secs),
                    color = TftYellowText,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Hardware Status LEDs Row
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF0B1015))
                .padding(4.dp),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            LedIndicator(label = "SSR1", active = status.ssr1, activeColor = Color.Red)
            LedIndicator(label = "SSR2", active = status.ssr2, activeColor = Color.Red)
            LedIndicator(label = "DN LIM", active = status.downLimit, activeColor = Color.Green)
            LedIndicator(label = "HOME LIM", active = status.homeLimit, activeColor = Color.Green)
            LedIndicator(label = "M-DN", active = status.motorDown, activeColor = Color.Cyan)
            LedIndicator(label = "M-UP", active = status.motorUp, activeColor = Color.Cyan)
        }

        // Force Start or Alarm Banner
        if (status.forceStartPending) {
            Spacer(modifier = Modifier.height(4.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF7A4100))
                    .padding(3.dp),
                contentAlignment = Alignment.Center
            ) {
                Text("AUTO HOLD: Press OK to Force Start, LEFT to Cancel", color = Color.Yellow, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
            }
        }

        if (status.alarmMsg.isNotEmpty()) {
            Spacer(modifier = Modifier.height(4.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF660000))
                    .padding(3.dp),
                contentAlignment = Alignment.Center
            ) {
                Text("ALARM: ${status.alarmMsg}", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            }
        }
    }
}

@Composable
private fun TftProgramSelectView(status: SystemStatus) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text("SELECT PROGRAM (P01 - P10)", color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))
        for (i in 0..4) {
            val isSelected = (status.activeProgIdx == i)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(if (isSelected) TftHighlightBox else Color.Transparent)
                    .padding(vertical = 2.dp, horizontal = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    text = "${if (isSelected) ">" else " "} P%02d".format(i + 1),
                    color = if (isSelected) TftYellowText else Color.White,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace
                )
                Text(
                    text = if (isSelected) "[ACTIVE - Press OK to Edit]" else "",
                    color = TftCyanText,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
        }
        Text("Press [UP]/[DN] to select, [OK] to Edit, [LEFT] for Home", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun TftProgramEditView(status: SystemStatus, recipe: ProgramRecipe?) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text("EDIT PROGRAM: P%02d".format(status.activeProgIdx + 1), color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))

        EditFieldRow(label = "1. H1 Temp Setpoint", value = "%.0f °C".format(recipe?.h1Setpoint ?: 0f), isSelected = status.selectedEditField == 0)
        EditFieldRow(label = "2. H2 Temp Setpoint", value = "%.0f °C".format(recipe?.h2Setpoint ?: 0f), isSelected = status.selectedEditField == 1)
        EditFieldRow(label = "3. Process Timer", value = "%ds".format(recipe?.processTimeSec ?: 0), isSelected = status.selectedEditField == 2)
        EditFieldRow(label = "4. Torque Limit", value = "%.1f Nm".format(recipe?.torqueLimit ?: 0f), isSelected = status.selectedEditField == 3)

        Spacer(modifier = Modifier.height(4.dp))
        Text("[UP]/[DN]: Adjust | [->]: Next Field | [OK]: Save", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun TftTimerEditView(status: SystemStatus, recipe: ProgramRecipe?) {
    val sec = recipe?.processTimeSec ?: status.remainingTimeSec
    val h = sec / 3600
    val m = (sec % 3600) / 60
    val s = sec % 60

    Column(modifier = Modifier.fillMaxWidth()) {
        Text("PROCESS TIMER EDITOR (HH:MM:SS)", color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(10.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            TimerDigitBox(digits = "%02d".format(h), label = "HRS", isSelected = status.timerEditField == 0)
            Text(" : ", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            TimerDigitBox(digits = "%02d".format(m), label = "MIN", isSelected = status.timerEditField == 1)
            Text(" : ", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            TimerDigitBox(digits = "%02d".format(s), label = "SEC", isSelected = status.timerEditField == 2)
        }

        Spacer(modifier = Modifier.height(10.dp))
        Text("[UP]/[DN]: +/- 1s | [->]: Switch Field | [OK]: Save", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun TftPidTuningView(status: SystemStatus, recipe: ProgramRecipe?) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text("PID TUNING: P%02d".format(status.activeProgIdx + 1), color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))
        EditFieldRow(label = "H1 Kp", value = "%.3f".format(recipe?.h1Kp ?: 2f), isSelected = status.pidEditField == 0)
        EditFieldRow(label = "H1 Ki", value = "%.3f".format(recipe?.h1Ki ?: 0.1f), isSelected = status.pidEditField == 1)
        EditFieldRow(label = "H1 Kd", value = "%.3f".format(recipe?.h1Kd ?: 1f), isSelected = status.pidEditField == 2)
        EditFieldRow(label = "H2 Kp", value = "%.3f".format(recipe?.h2Kp ?: 2f), isSelected = status.pidEditField == 3)
        EditFieldRow(label = "H2 Ki", value = "%.3f".format(recipe?.h2Ki ?: 0.1f), isSelected = status.pidEditField == 4)
        EditFieldRow(label = "H2 Kd", value = "%.3f".format(recipe?.h2Kd ?: 1f), isSelected = status.pidEditField == 5)
    }
}

@Composable
private fun TftServiceView(status: SystemStatus) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text("SERVICE & DIAGNOSTICS", color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))
        Text("Down Limit Failures: ${status.downFailCount}", color = if (status.downFailCount > 0) TftRedText else Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Text("Home Limit Failures: ${status.homeFailCount}", color = if (status.homeFailCount > 0) TftRedText else Color.White, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Text("Peak Torque: %.3f Nm".format(status.maxTorque), color = TftCyanText, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Text("RTC Clock: ${status.rtcTime}", color = Color.LightGray, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))
        Text("Reset Limit Failures: [DN]+[->] | RTC Set: [DN]+[OK]", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun TftRtcSetView(status: SystemStatus) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text("RTC TIME & DATE EDITOR", color = TftYellowText, fontWeight = FontWeight.Bold, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(6.dp))
        Text("Current Time: ${status.rtcTime}", color = TftCyanText, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Spacer(modifier = Modifier.height(4.dp))
        Text("[UP]/[DN]: Adjust | [->]: Next | [OK]: Save", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun EditFieldRow(label: String, value: String, isSelected: Boolean) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(if (isSelected) TftHighlightBox else Color.Transparent)
            .border(if (isSelected) 1.dp else 0.dp, if (isSelected) TftCyanText else Color.Transparent)
            .padding(horizontal = 6.dp, vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, color = if (isSelected) TftYellowText else Color.LightGray, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        Text(value, color = if (isSelected) Color.White else TftCyanText, fontWeight = FontWeight.Bold, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun TimerDigitBox(digits: String, label: String, isSelected: Boolean) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .background(if (isSelected) TftHighlightBox else Color(0xFF111822))
            .border(1.dp, if (isSelected) TftCyanText else Color(0xFF223040))
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Text(digits, color = if (isSelected) TftYellowText else Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        Text(label, color = Color.Gray, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun LedIndicator(label: String, active: Boolean, activeColor: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier
                .size(10.dp)
                .clip(RoundedCornerShape(5.dp))
                .background(if (active) activeColor else Color(0xFF222830))
                .border(1.dp, if (active) Color.White else Color(0xFF445060), RoundedCornerShape(5.dp))
        )
        Text(label, color = if (active) Color.White else Color.Gray, fontSize = 7.sp, fontFamily = FontFamily.Monospace)
    }
}
