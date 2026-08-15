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
fun TorqueMeterCard(
    currentTorque: Float,
    maxTorque: Float,
    torqueLimit: Float,
    modifier: Modifier = Modifier
) {
    val progress = if (torqueLimit > 0f) (currentTorque / torqueLimit).coerceIn(0f, 1f) else 0f
    val animatedProgress by animateFloatAsState(targetValue = progress, label = "torqueProgress")
    val isNearLimit = currentTorque >= (torqueLimit * 0.85f)

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(14.dp))
            .background(IndustrialCardBg)
            .border(
                1.dp,
                if (isNearLimit) AccentRed else IndustrialCardBorder,
                RoundedCornerShape(14.dp)
            )
            .padding(14.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "TORQUE SENSOR (TQ10)",
                    color = AccentCyan,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "LIMIT: %.1f Nm".format(torqueLimit),
                    color = AccentAmber,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.Bottom
            ) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = "%.3f".format(currentTorque),
                        color = if (isNearLimit) AccentRed else TextPrimary,
                        fontSize = 28.sp,
                        fontWeight = FontWeight.Black
                    )
                    Text(
                        text = " Nm",
                        color = TextSecondary,
                        fontSize = 16.sp,
                        modifier = Modifier.padding(bottom = 3.dp)
                    )
                }

                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        text = "PEAK: %.3f Nm".format(maxTorque),
                        color = AccentCyan,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold
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
                color = if (isNearLimit) AccentRed else AccentCyan,
                trackColor = IndustrialSurface
            )
        }
    }
}
