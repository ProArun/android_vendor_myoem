// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalmonitor.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkColorScheme = darkColorScheme(
    primary          = Color(0xFF4FC3F7),  // Light Blue 300
    onPrimary        = Color(0xFF003549),
    primaryContainer = Color(0xFF004D67),
    secondary        = Color(0xFF81C784),  // Green 300
    background       = Color(0xFF0D1117),  // Near-black
    surface          = Color(0xFF161B22),  // GitHub-dark card surface
    onBackground     = Color(0xFFE6EDF3),
    onSurface        = Color(0xFFE6EDF3),
    error            = Color(0xFFFF5252),
)

@Composable
fun ThermalMonitorTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        content = content
    )
}
