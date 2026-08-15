package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sunlazer.dualheatercontroller.model.SystemStatus
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun ActuatorStatusCard(
    status: SystemStatus,
    onJogUp: () -> Unit,
    onJogDown: () -> Unit,
    onToggleSsr1: () -> Unit,
    onToggleSsr2: () -> Unit,
    onResetFails: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
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
                    text = "ACTUATOR & LIMIT STATUS",
                    color = AccentCyan,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold
                )
                if (status.downFailCount > 0 || status.homeFailCount > 0) {
                    Text(
                        text = "FAILS: DN=${status.downFailCount} HM=${status.homeFailCount}",
                        color = AccentRed,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Actuator indicator grid
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                StatusPill(label = "SSR 1", isActive = status.ssr1, activeColor = AccentOrange)
                StatusPill(label = "SSR 2", isActive = status.ssr2, activeColor = AccentOrange)
                StatusPill(label = "DN LIM", isActive = status.downLimit, activeColor = AccentNeonGreen)
                StatusPill(label = "HM LIM", isActive = status.homeLimit, activeColor = AccentNeonGreen)
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Manual Service Controls
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = onJogUp,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF263238)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("JOG UP", fontSize = 11.sp, color = AccentCyan, fontWeight = FontWeight.Bold)
                }

                Button(
                    onClick = onJogDown,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF263238)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("JOG DN", fontSize = 11.sp, color = AccentCyan, fontWeight = FontWeight.Bold)
                }

                Button(
                    onClick = onResetFails,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF37474F)),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("RST FAIL", fontSize = 11.sp, color = AccentAmber, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

@Composable
private fun StatusPill(label: String, isActive: Boolean, activeColor: Color) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (isActive) activeColor.copy(alpha = 0.2f) else IndustrialSurface)
            .border(1.dp, if (isActive) activeColor else Color.Transparent, RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            color = if (isActive) activeColor else TextSecondary,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
    }
}
