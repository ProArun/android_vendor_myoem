// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalmonitor.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.myoem.thermalcontrol.ThermalControlManager
import com.myoem.thermalmonitor.R
import com.myoem.thermalmonitor.ThermalViewModel
import com.myoem.thermalmonitor.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ThermalScreen(viewModel: ThermalViewModel) {
    val uiState by viewModel.uiState.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    if (uiState.isAutoMode) {
                        AutoModeBadge()
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Spacer(modifier = Modifier.height(4.dp))

            if (!uiState.serviceAvailable) {
                ServiceUnavailableCard(uiState.errorMessage)
            } else {
                TemperatureCard(uiState)
                FanStatusCard(uiState)
                FanControlCard(uiState, viewModel)
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

// ── Auto mode badge shown in the top bar ─────────────────────────────────────

@Composable
private fun AutoModeBadge() {
    Surface(
        color = MaterialTheme.colorScheme.primary,
        shape = MaterialTheme.shapes.small,
        modifier = Modifier.padding(end = 8.dp)
    ) {
        Text(
            text = "AUTO",
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onPrimary
        )
    }
}

// ── Service unavailable card ──────────────────────────────────────────────────

@Composable
private fun ServiceUnavailableCard(message: String?) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = stringResource(R.string.service_unavailable),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.error
            )
            if (message != null) {
                Spacer(Modifier.height(4.dp))
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onErrorContainer
                )
            }
        }
    }
}

// ── Temperature card ──────────────────────────────────────────────────────────

@Composable
private fun TemperatureCard(state: UiState) {
    val tempColor = Color(ThermalControlManager.temperatureColor(state.cpuTempCelsius))
    val category  = ThermalControlManager.categorizeTemperature(state.cpuTempCelsius)

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier
                .padding(20.dp)
                .fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = stringResource(R.string.cpu_temperature),
                style = MaterialTheme.typography.titleMedium
            )
            Spacer(Modifier.height(8.dp))
            Text(
                text = "%.1f °C".format(state.cpuTempCelsius),
                fontSize = 48.sp,
                fontWeight = FontWeight.Bold,
                color = tempColor
            )
            Spacer(Modifier.height(4.dp))
            Text(
                text = "● $category",
                color = tempColor,
                style = MaterialTheme.typography.bodyMedium
            )
        }
    }
}

// ── Fan status card ───────────────────────────────────────────────────────────

@Composable
private fun FanStatusCard(state: UiState) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(20.dp)) {
            Text(
                text = stringResource(R.string.fan_status),
                style = MaterialTheme.typography.titleMedium
            )
            Spacer(Modifier.height(12.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly
            ) {
                StatusItem(
                    label = stringResource(R.string.fan_speed),
                    value = "${state.fanSpeedPercent}%"
                )
                StatusItem(
                    label = stringResource(R.string.fan_rpm),
                    value = if (state.fanRpm < 0) "N/A" else "${state.fanRpm} RPM"
                )
                StatusItem(
                    label = stringResource(R.string.fan_running),
                    value = if (state.isFanRunning) "Yes" else "No",
                    valueColor = if (state.isFanRunning)
                        MaterialTheme.colorScheme.primary
                    else
                        MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun StatusItem(
    label: String,
    value: String,
    valueColor: Color = MaterialTheme.colorScheme.onSurface
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
        )
        Spacer(Modifier.height(2.dp))
        Text(
            text = value,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.SemiBold,
            color = valueColor
        )
    }
}

// ── Fan control card ──────────────────────────────────────────────────────────

@Composable
private fun FanControlCard(state: UiState, viewModel: ThermalViewModel) {
    // Local slider/text state — separate from ViewModel so user edits aren't
    // overwritten by the 2-second polling refresh
    var sliderValue by remember { mutableFloatStateOf(state.fanSpeedPercent.toFloat()) }
    var textValue   by remember { mutableStateOf(state.fanSpeedPercent.toString()) }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(20.dp)) {
            Text(
                text = stringResource(R.string.fan_control),
                style = MaterialTheme.typography.titleMedium
            )
            Spacer(Modifier.height(12.dp))

            // ── On / Off / Auto buttons ───────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    onClick = { viewModel.turnFanOn() },
                    modifier = Modifier.weight(1f)
                ) {
                    Text(stringResource(R.string.turn_on))
                }
                Button(
                    onClick = { viewModel.turnFanOff() },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text(stringResource(R.string.turn_off))
                }
                OutlinedButton(
                    onClick = { viewModel.setAutoMode() },
                    modifier = Modifier.weight(1f)
                ) {
                    Text(stringResource(R.string.auto_mode))
                }
            }

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            // ── Speed slider ──────────────────────────────────────────────
            Text(
                text = stringResource(R.string.set_speed),
                style = MaterialTheme.typography.labelMedium
            )
            Spacer(Modifier.height(4.dp))
            Slider(
                value = sliderValue,
                onValueChange = { newVal ->
                    sliderValue = newVal
                    textValue   = newVal.toInt().toString()
                },
                valueRange = 0f..100f,
                steps = 99,          // 1% increments
                modifier = Modifier.fillMaxWidth()
            )

            // ── Text input + Apply ────────────────────────────────────────
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = textValue,
                    onValueChange = { raw ->
                        textValue = raw
                        raw.toIntOrNull()?.coerceIn(0, 100)?.let { pct ->
                            sliderValue = pct.toFloat()
                        }
                    },
                    label = { Text("%") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f)
                )
                Button(
                    onClick = {
                        val pct = textValue.toIntOrNull()?.coerceIn(0, 100) ?: 0
                        sliderValue = pct.toFloat()
                        textValue   = pct.toString()
                        viewModel.setFanSpeed(pct)
                    }
                ) {
                    Text(stringResource(R.string.apply))
                }
            }
        }
    }
}
