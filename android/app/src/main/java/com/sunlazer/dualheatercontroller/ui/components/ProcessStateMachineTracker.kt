package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sunlazer.dualheatercontroller.ui.theme.*

val ProcessStatesList = listOf(
    "IDLE",
    "SAFETY_CHECK",
    "MOVE_DOWN",
    "DOWN_LIMIT",
    "HEAT_TO_SETPOINT",
    "TEMP_READY",
    "PROCESS_TIMER",
    "TIMER_COMPLETE",
    "MOVE_UP",
    "HOME_LIMIT",
    "SAVE_RECORD",
    "PROCESS_COMPLETE",
    "READY"
)

@Composable
fun ProcessStateMachineTracker(
    currentState: String,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(IndustrialCardBg)
            .border(1.dp, IndustrialCardBorder, RoundedCornerShape(14.dp))
            .padding(12.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PROCESS STATE FLOW",
                    color = AccentCyan,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = currentState,
                    color = if (currentState == "ALARM_FAULT") AccentRed else AccentNeonGreen,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold
                )
            }

            Spacer(modifier = Modifier.height(10.dp))

            LazyRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                itemsIndexed(ProcessStatesList) { index, stateName ->
                    val isActive = (currentState == stateName)
                    val isPast = ProcessStatesList.indexOf(currentState) > index

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(
                                when {
                                    isActive -> AccentCyan.copy(alpha = 0.25f)
                                    isPast -> AccentNeonGreen.copy(alpha = 0.15f)
                                    else -> IndustrialSurface
                                }
                            )
                            .border(
                                1.dp,
                                when {
                                    isActive -> AccentCyan
                                    isPast -> AccentNeonGreen.copy(alpha = 0.4f)
                                    else -> Color.Transparent
                                },
                                RoundedCornerShape(8.dp)
                            )
                            .padding(horizontal = 8.dp, vertical = 6.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = "${index + 1}. ",
                                color = if (isActive) AccentCyan else TextMuted,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = stateName,
                                color = when {
                                    isActive -> AccentCyan
                                    isPast -> TextPrimary
                                    else -> TextSecondary
                                },
                                fontSize = 11.sp,
                                fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal
                            )
                        }
                    }
                }
            }
        }
    }
}
