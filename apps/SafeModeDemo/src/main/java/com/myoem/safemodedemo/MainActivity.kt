// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemodedemo

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.myoem.safemode.SafeModeListener
import com.myoem.safemode.SafeModeManager
import com.myoem.safemode.SafeModeState
import com.myoem.safemode.VehicleInfo

/**
 * MainActivity — single-screen SafeMode dashboard.
 *
 * Layout:
 *   ┌──────────────────────────────────────┐
 *   │  [Speed]     [Gear]      [Fuel]      │  ← 3 metric cards (top row)
 *   │                                      │
 *   │                                      │
 *   │         ┌──────────────┐             │
 *   │         │  SafeMode    │             │  ← big state card (bottom center)
 *   │         │   State      │             │
 *   │         │  NO / NORMAL │             │
 *   │         │  / HARD      │             │
 *   │         └──────────────┘             │
 *   └──────────────────────────────────────┘
 *
 * The SafeModeManager singleton is attached in onStart() and released in
 * onStop() — matching the Activity visible lifecycle so we don't waste
 * Binder callbacks while the app is in the background.
 */
class MainActivity : ComponentActivity() {

    // ── State holders (Compose-observable) ───────────────────────────────────
    // These live outside the composable so the Activity (not Compose) owns them.
    // mutableStateOf() makes Compose recompose automatically when they change.
    private var vehicleInfo by mutableStateOf(VehicleInfo())
    private var safeMode    by mutableStateOf(SafeModeState.NO_SAFE_MODE)

    // ── SafeModeListener ─────────────────────────────────────────────────────
    // fun interface — implement with a lambda.
    // Called on the main thread by SafeModeManager, so we can write to
    // mutableStateOf directly without post() or runOnUiThread().
    private val safeModeListener = SafeModeListener { state, info ->
        safeMode    = state
        vehicleInfo = info
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            MaterialTheme {
                // Pass state down as parameters — keeps composables stateless
                // (easier to preview and test in isolation).
                SafeModeDashboard(
                    info  = vehicleInfo,
                    state = safeMode,
                )
            }
        }
    }

    override fun onStart() {
        super.onStart()
        // attach() connects to SafeModeService (if not already connected) and
        // immediately delivers the current state before the first VHAL event.
        SafeModeManager.getInstance(this).attach(this, safeModeListener)
    }

    override fun onStop() {
        super.onStop()
        // dispose() removes our listener. If no other listeners remain,
        // SafeModeManager also unregisters its Binder callback from safemoded.
        SafeModeManager.getInstance(this).dispose(this)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Root screen
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun SafeModeDashboard(
    info:  VehicleInfo,
    state: SafeModeState,
    modifier: Modifier = Modifier,
) {
    // Smoothly animate the SafeMode card color on state transitions.
    // tween(600ms) gives a perceptible but snappy color shift.
    val targetColor = when (state) {
        SafeModeState.NO_SAFE_MODE     -> Color(0xFF4CAF50)  // Material Green 500
        SafeModeState.NORMAL_SAFE_MODE -> Color(0xFFFFC107)  // Material Amber 500
        SafeModeState.HARD_SAFE_MODE   -> Color(0xFFF44336)  // Material Red 500
    }
    val animatedColor by animateColorAsState(
        targetValue  = targetColor,
        animationSpec = tween(durationMillis = 600),
        label        = "safemode_color",
    )

    Surface(
        modifier = modifier.fillMaxSize(),
        color    = MaterialTheme.colorScheme.background,
    ) {
        Column(
            modifier            = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.SpaceBetween,
        ) {

            // ── Top row: three metric cards ───────────────────────────────
            MetricCardRow(info = info)

            // ── Bottom center: SafeMode state card ────────────────────────
            SafeModeStateCard(
                state         = state,
                cardColor     = animatedColor,
                modifier      = Modifier
                    .fillMaxWidth(0.75f)   // 75% of screen width
                    .align(Alignment.CenterHorizontally)
                    .padding(bottom = 24.dp),
            )
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Top row — three metric cards
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun MetricCardRow(info: VehicleInfo) {
    Row(
        modifier            = Modifier
            .fillMaxWidth()
            .padding(top = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Each card gets equal width via weight(1f).
        MetricCard(
            label    = "Speed",
            value    = "%.1f".format(info.speedKmh),
            unit     = "km/h",
            icon     = "🚗",
            modifier = Modifier.weight(1f),
        )
        MetricCard(
            label    = "Gear",
            // Show the short readable name from VehicleGear.name()
            value    = info.gearName,
            unit     = "",
            icon     = "⚙️",
            modifier = Modifier.weight(1f),
        )
        MetricCard(
            label    = "Fuel",
            value    = "%.1f".format(info.fuelLevelL),
            unit     = "L",
            icon     = "⛽",
            modifier = Modifier.weight(1f),
        )
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Individual metric card
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun MetricCard(
    label:    String,
    value:    String,
    unit:     String,
    icon:     String,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier  = modifier.height(120.dp),
        shape     = RoundedCornerShape(16.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
        colors    = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier            = Modifier
                .fillMaxSize()
                .padding(12.dp),
            verticalArrangement   = Arrangement.SpaceEvenly,
            horizontalAlignment   = Alignment.CenterHorizontally,
        ) {
            // Icon
            Text(
                text     = icon,
                fontSize = 22.sp,
            )
            // Label (e.g. "Speed")
            Text(
                text      = label,
                style     = MaterialTheme.typography.labelMedium,
                color     = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            // Value + unit on the same line
            Row(
                verticalAlignment       = Alignment.Bottom,
                horizontalArrangement   = Arrangement.Center,
            ) {
                Text(
                    text       = value,
                    fontSize   = 20.sp,
                    fontWeight = FontWeight.Bold,
                    color      = MaterialTheme.colorScheme.onSurface,
                )
                if (unit.isNotEmpty()) {
                    Spacer(modifier = Modifier.width(3.dp))
                    Text(
                        text     = unit,
                        fontSize = 12.sp,
                        color    = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(bottom = 2.dp),
                    )
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Big SafeMode state card (bottom center)
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun SafeModeStateCard(
    state:     SafeModeState,
    cardColor: Color,         // pre-animated color passed from parent
    modifier:  Modifier = Modifier,
) {
    // Readable title and subtitle per state
    val (title, subtitle) = when (state) {
        SafeModeState.NO_SAFE_MODE     -> "NO SAFE MODE"     to "All features available"
        SafeModeState.NORMAL_SAFE_MODE -> "NORMAL SAFE MODE" to "Reduced UI complexity"
        SafeModeState.HARD_SAFE_MODE   -> "HARD SAFE MODE"   to "Minimal UI — driving fast"
    }

    // Text on dark backgrounds should be white; on amber (yellow) use dark text.
    val contentColor = when (state) {
        SafeModeState.NORMAL_SAFE_MODE -> Color(0xFF212121)   // dark text on yellow
        else                           -> Color.White
    }

    Card(
        modifier  = modifier.height(220.dp),
        shape     = RoundedCornerShape(24.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 8.dp),
        colors    = CardDefaults.cardColors(containerColor = cardColor),
    ) {
        Column(
            modifier            = Modifier
                .fillMaxSize()
                .padding(24.dp),
            verticalArrangement   = Arrangement.Center,
            horizontalAlignment   = Alignment.CenterHorizontally,
        ) {
            // State icon
            Text(
                text     = when (state) {
                    SafeModeState.NO_SAFE_MODE     -> "✅"
                    SafeModeState.NORMAL_SAFE_MODE -> "⚠️"
                    SafeModeState.HARD_SAFE_MODE   -> "🛑"
                },
                fontSize  = 40.sp,
            )

            Spacer(modifier = Modifier.height(12.dp))

            // State name — large and bold
            Text(
                text       = title,
                fontSize   = 22.sp,
                fontWeight = FontWeight.ExtraBold,
                color      = contentColor,
                textAlign  = TextAlign.Center,
                letterSpacing = 1.sp,
            )

            Spacer(modifier = Modifier.height(6.dp))

            // Subtitle — describes the UI policy
            Text(
                text      = subtitle,
                fontSize  = 14.sp,
                color     = contentColor.copy(alpha = 0.85f),
                textAlign = TextAlign.Center,
            )
        }
    }
}
