// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatora.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.myoem.bmicalculatora.BmiCalViewModel
import com.myoem.bmicalculatora.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BmiCalScreen(viewModel: BmiCalViewModel) {
    val uiState by viewModel.uiState.collectAsState()

    // Local input state — owned by the UI, passed to ViewModel on button click
    var input1 by remember { mutableStateOf("") }
    var input2 by remember { mutableStateOf("") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.app_name),
                        fontWeight = FontWeight.Bold,
                    )
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Spacer(modifier = Modifier.height(4.dp))

            // ── Service status indicators ─────────────────────────────────────
            ServiceStatusRow(
                bmiAvailable  = uiState.bmiServiceAvailable,
                calcAvailable = uiState.calcServiceAvailable,
            )

            // ── Input fields ──────────────────────────────────────────────────
            InputCard(
                input1 = input1,
                input2 = input2,
                onInput1Change = { input1 = it },
                onInput2Change = { input2 = it },
            )

            // ── Operation buttons ─────────────────────────────────────────────
            OperationButtonsCard(
                onBmi      = { viewModel.computeBmi(input1, input2) },
                onAdd      = { viewModel.computeAdd(input1, input2) },
                onSubtract = { viewModel.computeSubtract(input1, input2) },
                onMultiply = { viewModel.computeMultiply(input1, input2) },
                onDivide   = { viewModel.computeDivide(input1, input2) },
            )

            // ── Result ────────────────────────────────────────────────────────
            ResultCard(
                result = uiState.result,
                error  = uiState.error,
            )

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

// ── Service status row ────────────────────────────────────────────────────────

@Composable
private fun ServiceStatusRow(bmiAvailable: Boolean, calcAvailable: Boolean) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        ServiceChip(label = "bmid", available = bmiAvailable, modifier = Modifier.weight(1f))
        ServiceChip(label = "calculatord", available = calcAvailable, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun ServiceChip(label: String, available: Boolean, modifier: Modifier = Modifier) {
    val containerColor = if (available)
        MaterialTheme.colorScheme.primaryContainer
    else
        MaterialTheme.colorScheme.errorContainer
    val contentColor = if (available)
        MaterialTheme.colorScheme.onPrimaryContainer
    else
        MaterialTheme.colorScheme.onErrorContainer

    Surface(
        modifier = modifier,
        shape = MaterialTheme.shapes.small,
        color = containerColor,
    ) {
        Text(
            text = if (available) "● $label" else "✕ $label",
            modifier = Modifier
                .padding(horizontal = 12.dp, vertical = 6.dp)
                .fillMaxWidth(),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.SemiBold,
            color = contentColor,
            textAlign = TextAlign.Center,
        )
    }
}

// ── Input card ────────────────────────────────────────────────────────────────

@Composable
private fun InputCard(
    input1: String,
    input2: String,
    onInput1Change: (String) -> Unit,
    onInput2Change: (String) -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = stringResource(R.string.label_inputs),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
            )
            OutlinedTextField(
                value = input1,
                onValueChange = onInput1Change,
                label = { Text(stringResource(R.string.hint_input1)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.fillMaxWidth(),
            )
            OutlinedTextField(
                value = input2,
                onValueChange = onInput2Change,
                label = { Text(stringResource(R.string.hint_input2)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

// ── Operation buttons card ────────────────────────────────────────────────────

@Composable
private fun OperationButtonsCard(
    onBmi: () -> Unit,
    onAdd: () -> Unit,
    onSubtract: () -> Unit,
    onMultiply: () -> Unit,
    onDivide: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = stringResource(R.string.label_operations),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.SemiBold,
            )

            // BMI button — full width, prominent
            Button(
                onClick = onBmi,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(stringResource(R.string.btn_bmi))
            }

            HorizontalDivider()

            // Arithmetic buttons — 2 x 2 grid
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    onClick = onAdd,
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.btn_add)) }

                OutlinedButton(
                    onClick = onSubtract,
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.btn_sub)) }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    onClick = onMultiply,
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.btn_mul)) }

                OutlinedButton(
                    onClick = onDivide,
                    modifier = Modifier.weight(1f),
                ) { Text(stringResource(R.string.btn_div)) }
            }
        }
    }
}

// ── Result card ───────────────────────────────────────────────────────────────

@Composable
private fun ResultCard(result: String, error: String?) {
    val isError = error != null

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (isError)
                MaterialTheme.colorScheme.errorContainer
            else
                MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier = Modifier
                .padding(20.dp)
                .fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (isError) {
                Text(
                    text = error!!,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                    textAlign = TextAlign.Center,
                )
            } else {
                Text(
                    text = result,
                    fontSize = 28.sp,
                    fontWeight = FontWeight.Bold,
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
