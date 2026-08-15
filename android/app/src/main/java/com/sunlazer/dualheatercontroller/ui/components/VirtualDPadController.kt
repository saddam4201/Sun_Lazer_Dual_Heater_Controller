package com.sunlazer.dualheatercontroller.ui.components

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sunlazer.dualheatercontroller.ui.theme.*

@Composable
fun VirtualDPadController(
    onButtonPress: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current

    fun performHaptic() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
                vibratorManager?.defaultVibrator?.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK))
            } else {
                @Suppress("DEPRECATION")
                val vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator?.vibrate(VibrationEffect.createOneShot(30, VibrationEffect.DEFAULT_AMPLITUDE))
                } else {
                    @Suppress("DEPRECATION")
                    vibrator?.vibrate(30)
                }
            }
        } catch (_: Exception) {}
    }

    Box(
        modifier = modifier
            .size(240.dp)
            .clip(CircleShape)
            .background(
                Brush.radialGradient(
                    colors = listOf(Color(0xFF222B36), Color(0xFF0E131A))
                )
            )
            .border(3.dp, Color(0xFF384656), CircleShape)
            .padding(12.dp),
        contentAlignment = Alignment.Center
    ) {
        // UP Button
        Box(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 4.dp)
        ) {
            DPadDirectionButton(
                icon = Icons.Default.KeyboardArrowUp,
                label = "UP",
                onClick = {
                    performHaptic()
                    onButtonPress("up")
                }
            )
        }

        // DOWN Button
        Box(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 4.dp)
        ) {
            DPadDirectionButton(
                icon = Icons.Default.KeyboardArrowDown,
                label = "DN",
                onClick = {
                    performHaptic()
                    onButtonPress("down")
                }
            )
        }

        // LEFT Button
        Box(
            modifier = Modifier
                .align(Alignment.CenterStart)
                .padding(start = 4.dp)
        ) {
            DPadDirectionButton(
                icon = Icons.Default.KeyboardArrowLeft,
                label = "LF",
                onClick = {
                    performHaptic()
                    onButtonPress("left")
                }
            )
        }

        // RIGHT Button
        Box(
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .padding(end = 4.dp)
        ) {
            DPadDirectionButton(
                icon = Icons.Default.KeyboardArrowRight,
                label = "RT",
                onClick = {
                    performHaptic()
                    onButtonPress("right")
                }
            )
        }

        // CENTER OK BUTTON
        Box(
            modifier = Modifier.align(Alignment.Center)
        ) {
            DPadCenterOkButton(
                onClick = {
                    performHaptic()
                    onButtonPress("ok")
                }
            )
        }
    }
}

@Composable
private fun DPadDirectionButton(
    icon: ImageVector,
    label: String,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    Box(
        modifier = Modifier
            .size(54.dp)
            .clip(RoundedCornerShape(14.dp))
            .background(
                if (isPressed) Color(0xFF00E5FF).copy(alpha = 0.3f)
                else Color(0xFF1B232E)
            )
            .border(
                1.5.dp,
                if (isPressed) AccentCyan else Color(0xFF334050),
                RoundedCornerShape(14.dp)
            )
            .clickable(interactionSource = interactionSource, indication = null) { onClick() },
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = if (isPressed) AccentCyan else TextPrimary,
                modifier = Modifier.size(28.dp)
            )
        }
    }
}

@Composable
private fun DPadCenterOkButton(
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    Box(
        modifier = Modifier
            .size(64.dp)
            .shadow(if (isPressed) 12.dp else 4.dp, CircleShape)
            .clip(CircleShape)
            .background(
                if (isPressed) Brush.radialGradient(listOf(Color(0xFF00E676), Color(0xFF00A352)))
                else Brush.radialGradient(listOf(Color(0xFF00C853), Color(0xFF007E33)))
            )
            .border(2.dp, if (isPressed) Color.White else Color(0xFF69F0AE), CircleShape)
            .clickable(interactionSource = interactionSource, indication = null) { onClick() },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = "OK",
            color = Color.White,
            fontWeight = FontWeight.Black,
            fontSize = 18.sp
        )
    }
}
