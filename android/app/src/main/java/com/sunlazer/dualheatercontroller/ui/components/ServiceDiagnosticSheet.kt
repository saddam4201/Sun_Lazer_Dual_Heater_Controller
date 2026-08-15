package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.sunlazer.dualheatercontroller.model.SystemStatus
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun ServiceDiagnosticSheet(
    status: SystemStatus,
    logs: List<String>,
    onToggleStartMode: () -> Unit,
    onSyncRtcTime: () -> Unit,
    onResetAlarm: () -> Unit,
    onEmergencyStop: () -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.85f)
                .clip(RoundedCornerShape(16.dp))
                .background(IndustrialDarkBg)
                .border(1.5.dp, AccentAmber, RoundedCornerShape(16.dp))
                .padding(16.dp)
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "SERVICE & DIAGNOSTICS",
                        color = AccentAmber,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold
                    )
                    TextButton(onClick = onDismiss) {
                        Text("CLOSE", color = TextSecondary)
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                // Quick Service Action Buttons
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onToggleStartMode,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = IndustrialSurface),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text(
                            text = "MODE: ${if (status.startModeAuto) "AUTO" else "MANUAL"}",
                            color = AccentCyan,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }

                    Button(
                        onClick = onSyncRtcTime,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = IndustrialSurface),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("SYNC RTC CLOCK", color = AccentNeonGreen, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onResetAlarm,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF37474F)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("RESET ALARM", color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                    }

                    Button(
                        onClick = onEmergencyStop,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentRed),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("E-STOP TRIP", color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Black)
                    }
                }

                Spacer(modifier = Modifier.height(14.dp))

                Text(
                    text = "RECENT PROCESS HISTORY (SD / RAM)",
                    color = AccentCyan,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold
                )

                Spacer(modifier = Modifier.height(6.dp))

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color.Black)
                        .border(1.dp, Color(0xFF223040), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    if (logs.isEmpty()) {
                        Text("No process logs recorded yet.", color = Color.Gray, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    } else {
                        LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            items(logs) { logLine ->
                                Text(
                                    text = logLine,
                                    color = Color(0xFF00FF66),
                                    fontSize = 10.sp,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
