package com.sunlazer.dualheatercontroller.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val DarkColorScheme = darkColorScheme(
    primary = AccentCyan,
    secondary = AccentNeonGreen,
    tertiary = AccentAmber,
    background = IndustrialDarkBg,
    surface = IndustrialCardBg,
    onPrimary = IndustrialDarkBg,
    onSecondary = IndustrialDarkBg,
    onTertiary = IndustrialDarkBg,
    onBackground = TextPrimary,
    onSurface = TextPrimary
)

@Composable
fun SunLazerTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        typography = Typography,
        content = content
    )
}
