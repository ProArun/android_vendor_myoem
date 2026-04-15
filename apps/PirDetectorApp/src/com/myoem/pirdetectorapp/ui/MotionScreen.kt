// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetectorapp.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.myoem.pirdetectorapp.MotionUiState

/**
 * MotionScreen — stateless composable for the PIR motion detector screen.
 *
 * Receives UiState and a SnackbarHostState — does NOT access ViewModel directly.
 * All state flows down via parameters; events flow up via Scaffold callbacks.
 *
 * ── Visual design ─────────────────────────────────────────────────────────────
 *   • Large circular indicator in the center of the screen
 *   • IDLE:  surfaceVariant colour,  "NO MOTION" label,   scale = 1.0
 *   • MOTION: error/red colour,      "MOTION DETECTED",   scale = 1.1 (pulse)
 *   • All transitions animated with a 300ms tween
 *   • Service disconnected: dim overlay banner at the top
 *   • Timestamp shown in a monospace font below the indicator
 */
@Composable
fun MotionScreen(
    uiState: MotionUiState,
    snackbarHostState: SnackbarHostState,
) {
    // Animate indicator colour based on motion state
    val indicatorColor by animateColorAsState(
        targetValue = if (uiState.motionDetected)
            MaterialTheme.colorScheme.error
        else
            MaterialTheme.colorScheme.surfaceVariant,
        animationSpec = tween(durationMillis = 300),
        label = "indicatorColor"
    )

    // Subtle scale pulse when motion is active
    val indicatorScale by animateFloatAsState(
        targetValue = if (uiState.motionDetected) 1.1f else 1.0f,
        animationSpec = tween(durationMillis = 300),
        label = "indicatorScale"
    )

    Scaffold(
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) }
    ) { contentPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(contentPadding)
                .padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {

            // ── Service status banner ──────────────────────────────────────────
            if (!uiState.serviceConnected) {
                ServiceUnavailableBanner()
                Spacer(modifier = Modifier.height(32.dp))
            }

            // ── Motion indicator circle ────────────────────────────────────────
            Box(
                modifier = Modifier
                    .size(200.dp)
                    .scale(indicatorScale)
                    .clip(CircleShape)
                    .background(indicatorColor),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = if (uiState.motionDetected) "MOTION" else "IDLE",
                    style = MaterialTheme.typography.headlineLarge,
                    fontWeight = FontWeight.Bold,
                    color = if (uiState.motionDetected)
                        MaterialTheme.colorScheme.onError
                    else
                        MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            Spacer(modifier = Modifier.height(32.dp))

            // ── Status text ────────────────────────────────────────────────────
            Text(
                text = if (uiState.motionDetected)
                    "Object / Person Detected"
                else
                    "No Object Present",
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onBackground,
            )

            Spacer(modifier = Modifier.height(16.dp))

            // ── Timestamp ──────────────────────────────────────────────────────
            if (uiState.lastEventTimestampNs > 0L) {
                Text(
                    text = "Last event: ${uiState.lastEventTimestampNs / 1_000_000} ms",
                    style = MaterialTheme.typography.bodyMedium,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f),
                )
            }
        }
    }
}

/**
 * Banner shown when pirdetectord is not reachable.
 * Displayed above the indicator so the indicator still shows the last known state.
 */
@Composable
private fun ServiceUnavailableBanner() {
    Box(
        modifier = Modifier
            .clip(MaterialTheme.shapes.medium)
            .background(MaterialTheme.colorScheme.errorContainer)
            .padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        Text(
            text = "PIR service unavailable",
            color = MaterialTheme.colorScheme.onErrorContainer,
            fontSize = 14.sp,
            fontWeight = FontWeight.Medium,
        )
    }
}

// ── Previews ──────────────────────────────────────────────────────────────────

@Preview(showBackground = true, name = "Idle state")
@Composable
private fun MotionScreenIdlePreview() {
    MaterialTheme {
        MotionScreen(
            uiState = MotionUiState(motionDetected = false, serviceConnected = true),
            snackbarHostState = SnackbarHostState(),
        )
    }
}

@Preview(showBackground = true, name = "Motion detected")
@Composable
private fun MotionScreenActivePreview() {
    MaterialTheme {
        MotionScreen(
            uiState = MotionUiState(
                motionDetected = true,
                serviceConnected = true,
                lastEventTimestampNs = 123_456_789_000L,
            ),
            snackbarHostState = SnackbarHostState(),
        )
    }
}

@Preview(showBackground = true, name = "Service unavailable")
@Composable
private fun MotionScreenDisconnectedPreview() {
    MaterialTheme {
        MotionScreen(
            uiState = MotionUiState(serviceConnected = false),
            snackbarHostState = SnackbarHostState(),
        )
    }
}
