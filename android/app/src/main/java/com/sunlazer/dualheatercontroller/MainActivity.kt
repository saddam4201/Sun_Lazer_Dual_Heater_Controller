package com.sunlazer.dualheatercontroller

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sunlazer.dualheatercontroller.network.ConnectionState
import com.sunlazer.dualheatercontroller.ui.components.*
import com.sunlazer.dualheatercontroller.ui.theme.*
import com.sunlazer.dualheatercontroller.ui.viewmodel.MainViewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            SunLazerTheme {
                MainScreen()
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(viewModel: MainViewModel = viewModel()) {
    val connectionState by viewModel.connectionState.collectAsState()
    val status by viewModel.systemStatus.collectAsState()
    val recipes by viewModel.recipes.collectAsState()
    val logs by viewModel.logs.collectAsState()
    val pingMs by viewModel.pingLatencyMs.collectAsState()
    val ipAddress by viewModel.ipAddress.collectAsState()
    val showRecipeDialog by viewModel.showRecipeDialog.collectAsState()
    val showServiceDialog by viewModel.showServiceDialog.collectAsState()

    val activeRecipe = recipes.find { it.idx == status.activeProgIdx }

    var ipInput by remember(ipAddress) { mutableStateOf(ipAddress) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = "SUN LAZER",
                            color = AccentCyan,
                            fontWeight = FontWeight.Black,
                            fontSize = 18.sp
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "HEATER CONTROLLER",
                            color = TextSecondary,
                            fontWeight = FontWeight.Medium,
                            fontSize = 12.sp
                        )
                    }
                },
                actions = {
                    // Start Mode indicator
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (status.startModeAuto) AccentNeonGreen.copy(alpha = 0.2f) else AccentAmber.copy(alpha = 0.2f))
                            .border(1.dp, if (status.startModeAuto) AccentNeonGreen else AccentAmber, RoundedCornerShape(6.dp))
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = if (status.startModeAuto) "AUTO" else "MANUAL",
                            color = if (status.startModeAuto) AccentNeonGreen else AccentAmber,
                            fontWeight = FontWeight.Bold,
                            fontSize = 10.sp
                        )
                    }

                    Spacer(modifier = Modifier.width(8.dp))

                    IconButton(onClick = { viewModel.openServiceDialog() }) {
                        Icon(Icons.Default.Build, contentDescription = "Service", tint = AccentAmber)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = IndustrialDarkBg)
            )
        },
        containerColor = IndustrialDarkBg
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 14.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(modifier = Modifier.height(6.dp))

            // IP & Connection Card
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(IndustrialCardBg)
                    .border(1.dp, IndustrialCardBorder, RoundedCornerShape(12.dp))
                    .padding(8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                OutlinedTextField(
                    value = ipInput,
                    onValueChange = {
                        ipInput = it
                        viewModel.setIpAddress(it)
                    },
                    label = { Text("ESP32 IP / AP") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = AccentCyan,
                        unfocusedBorderColor = IndustrialCardBorder,
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary,
                        focusedLabelColor = AccentCyan,
                        unfocusedLabelColor = TextSecondary
                    )
                )

                Spacer(modifier = Modifier.width(8.dp))

                Button(
                    onClick = {
                        if (connectionState == ConnectionState.CONNECTED) {
                            viewModel.disconnect()
                        } else {
                            viewModel.connect()
                        }
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (connectionState == ConnectionState.CONNECTED) Color(0xFF2E7D32) else Color(0xFF006699)
                    ),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(8.dp)
                                .clip(CircleShape)
                                .background(
                                    when (connectionState) {
                                        ConnectionState.CONNECTED -> AccentNeonGreen
                                        ConnectionState.CONNECTING -> AccentAmber
                                        else -> AccentRed
                                    }
                                )
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = when (connectionState) {
                                ConnectionState.CONNECTED -> "${pingMs}ms"
                                ConnectionState.CONNECTING -> "..."
                                else -> "CONNECT"
                            },
                            fontWeight = FontWeight.Bold,
                            fontSize = 11.sp
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Section 1: TFT Digital Twin Screen Mirror (Exact replica of 2.4" display)
            TftDigitalTwinScreen(status = status, activeRecipe = activeRecipe)

            Spacer(modifier = Modifier.height(14.dp))

            // Section 2: Virtual D-Pad Controller (Tactile 5-button deck)
            VirtualDPadController(
                onButtonPress = { key -> viewModel.sendButton(key) }
            )

            Spacer(modifier = Modifier.height(14.dp))

            // Quick Process Action Deck
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = { viewModel.openRecipeDialog() },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E293B)),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(Icons.Default.Tune, contentDescription = null, tint = AccentCyan, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(4.dp))
                    Text("RECIPES", color = AccentCyan, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                }

                if (status.forceStartPending) {
                    Button(
                        onClick = { viewModel.forceStart() },
                        modifier = Modifier.weight(1.2f),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentAmber),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("FORCE START", color = IndustrialDarkBg, fontWeight = FontWeight.Black, fontSize = 12.sp)
                    }
                } else if (status.stateName == "IDLE") {
                    Button(
                        onClick = { viewModel.startProcess() },
                        modifier = Modifier.weight(1.2f),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentNeonGreen),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Icon(Icons.Default.PlayArrow, contentDescription = null, tint = IndustrialDarkBg, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("START", color = IndustrialDarkBg, fontWeight = FontWeight.Black, fontSize = 13.sp)
                    }
                } else {
                    Button(
                        onClick = { viewModel.emergencyStop() },
                        modifier = Modifier.weight(1.2f),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentRed),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null, tint = Color.White, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("STOP", color = Color.White, fontWeight = FontWeight.Black, fontSize = 13.sp)
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            // Section 3: Dual Temperature Gauges
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                TemperatureGaugeCard(
                    title = "HEATER 1 (CH1)",
                    actualTemp = status.h1Actual,
                    setpointTemp = activeRecipe?.h1Setpoint ?: status.h1Setpoint,
                    tolerance = activeRecipe?.tempTolerance ?: status.tempTolerance,
                    isHeating = status.ssr1,
                    modifier = Modifier.weight(1f)
                )

                TemperatureGaugeCard(
                    title = "HEATER 2 (CH2)",
                    actualTemp = status.h2Actual,
                    setpointTemp = activeRecipe?.h2Setpoint ?: status.h2Setpoint,
                    tolerance = activeRecipe?.tempTolerance ?: status.tempTolerance,
                    isHeating = status.ssr2,
                    modifier = Modifier.weight(1f)
                )
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Section 4: Torque Meter Card
            TorqueMeterCard(
                currentTorque = status.torque,
                maxTorque = status.maxTorque,
                torqueLimit = activeRecipe?.torqueLimit ?: status.torqueLimit
            )

            Spacer(modifier = Modifier.height(10.dp))

            // Section 5: 13-State Process Machine Stepper
            ProcessStateMachineTracker(currentState = status.stateName)

            Spacer(modifier = Modifier.height(10.dp))

            // Section 6: Actuator & Limit Safety Hub
            ActuatorStatusCard(
                status = status,
                onJogUp = { viewModel.jogUp() },
                onJogDown = { viewModel.jogDown() },
                onToggleSsr1 = { viewModel.toggleSsr1() },
                onToggleSsr2 = { viewModel.toggleSsr2() },
                onResetFails = { viewModel.resetLimitFails() }
            )

            Spacer(modifier = Modifier.height(20.dp))
        }
    }

    // Recipe Dialog
    if (showRecipeDialog) {
        RecipeManagerDialog(
            recipes = recipes,
            activeIdx = status.activeProgIdx,
            onSelectProgram = { idx ->
                viewModel.selectProgram(idx)
                viewModel.closeRecipeDialog()
            },
            onSaveRecipe = { recipe ->
                viewModel.saveRecipe(recipe)
                viewModel.closeRecipeDialog()
            },
            onDismiss = { viewModel.closeRecipeDialog() }
        )
    }

    // Service & Diagnostic Sheet
    if (showServiceDialog) {
        ServiceDiagnosticSheet(
            status = status,
            logs = logs,
            onToggleStartMode = { viewModel.toggleStartMode() },
            onSyncRtcTime = { viewModel.syncRtcTime() },
            onResetAlarm = { viewModel.resetAlarm() },
            onEmergencyStop = { viewModel.emergencyStop() },
            onDismiss = { viewModel.closeServiceDialog() }
        )
    }
}
