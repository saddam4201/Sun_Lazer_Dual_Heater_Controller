package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun TemperatureGaugeCard(
    title: String,
    actualTemp: Float,
    setpointTemp: Float,
    tolerance: Float,
    isHeating: Boolean,
    modifier: Modifier = Modifier
) {
    val progress = if (setpointTemp > 0f) (actualTemp / (setpointTemp * 1.2f)).coerceIn(0f, 1f) else 0f
    val animatedProgress by animateFloatAsState(targetValue = progress, label = "tempProgress")

    val tempDiff = kotlin.math.abs(actualTemp - setpointTemp)
    val inTolerance = tempDiff <= tolerance

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(14.dp))
            .background(IndustrialCardBg)
            .border(1.dp, IndustrialCardBorder, RoundedCornerShape(14.dp))
            .padding(14.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = title,
                    color = AccentCyan,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold
                )
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (isHeating) AccentOrange.copy(alpha = 0.2f) else IndustrialSurface)
                        .border(1.dp, if (isHeating) AccentOrange else Color.Gray.copy(alpha = 0.3f), RoundedCornerShape(6.dp))
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = if (isHeating) "HEATING" else "IDLE",
                        color = if (isHeating) AccentOrange else TextSecondary,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.Bottom
            ) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = "%.1f".format(actualTemp),
                        color = if (inTolerance) AccentNeonGreen else TextPrimary,
                        fontSize = 28.sp,
                        fontWeight = FontWeight.Black
                    )
                    Text(
                        text = " °C",
                        color = TextSecondary,
                        fontSize = 16.sp,
                        modifier = Modifier.padding(bottom = 3.dp)
                    )
                }

                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        text = "SET: %.0f °C".format(setpointTemp),
                        color = TextSecondary,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = "±%.1f °C".format(tolerance),
                        color = if (inTolerance) AccentNeonGreen else TextMuted,
                        fontSize = 10.sp
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            LinearProgressIndicator(
                progress = animatedProgress,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(6.dp)
                    .clip(RoundedCornerShape(3.dp)),
                color = if (inTolerance) AccentNeonGreen else AccentCyan,
                trackColor = IndustrialSurface
            )
        }
    }
}
