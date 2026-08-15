package com.sunlazer.dualheatercontroller.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.sunlazer.dualheatercontroller.model.ProgramRecipe
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun RecipeManagerDialog(
    recipes: List<ProgramRecipe>,
    activeIdx: Int,
    onSelectProgram: (Int) -> Unit,
    onSaveRecipe: (ProgramRecipe) -> Unit,
    onDismiss: () -> Unit
) {
    var selectedTab by remember { mutableIntStateOf(activeIdx.coerceIn(0, 9)) }
    val currentRecipe = recipes.getOrNull(selectedTab) ?: ProgramRecipe(idx = selectedTab, name = "P%02d".format(selectedTab + 1))

    var name by remember(currentRecipe) { mutableStateOf(currentRecipe.name) }
    var h1Setpoint by remember(currentRecipe) { mutableStateOf(currentRecipe.h1Setpoint.toString()) }
    var h2Setpoint by remember(currentRecipe) { mutableStateOf(currentRecipe.h2Setpoint.toString()) }
    var processTime by remember(currentRecipe) { mutableStateOf(currentRecipe.processTimeSec.toString()) }
    var torqueLimit by remember(currentRecipe) { mutableStateOf(currentRecipe.torqueLimit.toString()) }
    var tempTolerance by remember(currentRecipe) { mutableStateOf(currentRecipe.tempTolerance.toString()) }

    var h1Kp by remember(currentRecipe) { mutableStateOf(currentRecipe.h1Kp.toString()) }
    var h1Ki by remember(currentRecipe) { mutableStateOf(currentRecipe.h1Ki.toString()) }
    var h1Kd by remember(currentRecipe) { mutableStateOf(currentRecipe.h1Kd.toString()) }
    var h2Kp by remember(currentRecipe) { mutableStateOf(currentRecipe.h2Kp.toString()) }
    var h2Ki by remember(currentRecipe) { mutableStateOf(currentRecipe.h2Ki.toString()) }
    var h2Kd by remember(currentRecipe) { mutableStateOf(currentRecipe.h2Kd.toString()) }

    Dialog(onDismissRequest = onDismiss) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.85f)
                .clip(RoundedCornerShape(16.dp))
                .background(IndustrialDarkBg)
                .border(1.5.dp, AccentCyan, RoundedCornerShape(16.dp))
                .padding(16.dp)
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                // Dialog Title
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "PROGRAM RECIPE MANAGER",
                        color = AccentCyan,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold
                    )
                    TextButton(onClick = onDismiss) {
                        Text("CLOSE", color = TextSecondary)
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                // Program Selector Tabs (P01..P10)
                LazyRow(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    items(10) { idx ->
                        val isSelected = (selectedTab == idx)
                        val isActive = (activeIdx == idx)
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(
                                    when {
                                        isSelected -> AccentCyan
                                        isActive -> AccentNeonGreen.copy(alpha = 0.3f)
                                        else -> IndustrialSurface
                                    }
                                )
                                .clickable { selectedTab = idx }
                                .padding(horizontal = 10.dp, vertical = 6.dp)
                        ) {
                            Text(
                                text = "P%02d".format(idx + 1),
                                color = if (isSelected) IndustrialDarkBg else TextPrimary,
                                fontWeight = FontWeight.Bold,
                                fontSize = 12.sp
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Recipe Fields Editor
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    item {
                        OutlinedTextField(
                            value = name,
                            onValueChange = { name = it },
                            label = { Text("Program Name") },
                            modifier = Modifier.fillMaxWidth(),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = AccentCyan,
                                unfocusedBorderColor = IndustrialCardBorder,
                                focusedTextColor = TextPrimary,
                                unfocusedTextColor = TextPrimary
                            )
                        )
                    }

                    item {
                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                value = h1Setpoint,
                                onValueChange = { h1Setpoint = it },
                                label = { Text("H1 Setpoint (°C)") },
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                                modifier = Modifier.weight(1f),
                                colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentCyan, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary)
                            )
                            OutlinedTextField(
                                value = h2Setpoint,
                                onValueChange = { h2Setpoint = it },
                                label = { Text("H2 Setpoint (°C)") },
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                                modifier = Modifier.weight(1f),
                                colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentCyan, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary)
                            )
                        }
                    }

                    item {
                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                value = processTime,
                                onValueChange = { processTime = it },
                                label = { Text("Process Time (s)") },
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                                modifier = Modifier.weight(1f),
                                colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentCyan, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary)
                            )
                            OutlinedTextField(
                                value = torqueLimit,
                                onValueChange = { torqueLimit = it },
                                label = { Text("Torque Limit (Nm)") },
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                                modifier = Modifier.weight(1f),
                                colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentCyan, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary)
                            )
                        }
                    }

                    item {
                        OutlinedTextField(
                            value = tempTolerance,
                            onValueChange = { tempTolerance = it },
                            label = { Text("Temperature Tolerance (±°C)") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                            modifier = Modifier.fillMaxWidth(),
                            colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentCyan, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary)
                        )
                    }

                    item {
                        Text(
                            text = "PID TUNING (ADVANCED)",
                            color = AccentAmber,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(top = 8.dp)
                        )
                    }

                    item {
                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            OutlinedTextField(value = h1Kp, onValueChange = { h1Kp = it }, label = { Text("H1 Kp") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                            OutlinedTextField(value = h1Ki, onValueChange = { h1Ki = it }, label = { Text("H1 Ki") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                            OutlinedTextField(value = h1Kd, onValueChange = { h1Kd = it }, label = { Text("H1 Kd") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                        }
                    }

                    item {
                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            OutlinedTextField(value = h2Kp, onValueChange = { h2Kp = it }, label = { Text("H2 Kp") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                            OutlinedTextField(value = h2Ki, onValueChange = { h2Ki = it }, label = { Text("H2 Ki") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                            OutlinedTextField(value = h2Kd, onValueChange = { h2Kd = it }, label = { Text("H2 Kd") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.weight(1f), colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = AccentAmber, unfocusedBorderColor = IndustrialCardBorder, focusedTextColor = TextPrimary, unfocusedTextColor = TextPrimary))
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Action Buttons (Select as Active / Upload to NVS)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            onSelectProgram(selectedTab)
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E293B)),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("SET ACTIVE", color = AccentCyan, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }

                    Button(
                        onClick = {
                            val updated = ProgramRecipe(
                                idx = selectedTab,
                                name = name,
                                h1Setpoint = h1Setpoint.toFloatOrNull() ?: 100f,
                                h2Setpoint = h2Setpoint.toFloatOrNull() ?: 100f,
                                processTimeSec = processTime.toLongOrNull() ?: 60L,
                                torqueLimit = torqueLimit.toFloatOrNull() ?: 5f,
                                tempTolerance = tempTolerance.toFloatOrNull() ?: 2f,
                                h1Kp = h1Kp.toFloatOrNull() ?: 2f,
                                h1Ki = h1Ki.toFloatOrNull() ?: 0.1f,
                                h1Kd = h1Kd.toFloatOrNull() ?: 1f,
                                h2Kp = h2Kp.toFloatOrNull() ?: 2f,
                                h2Ki = h2Ki.toFloatOrNull() ?: 0.1f,
                                h2Kd = h2Kd.toFloatOrNull() ?: 1f
                            )
                            onSaveRecipe(updated)
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = AccentNeonGreen),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("SAVE TO NVS", color = IndustrialDarkBg, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                }
            }
        }
    }
}
