// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetectorapp

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.lifecycle.viewmodel.compose.viewModel
import com.myoem.pirdetectorapp.ui.MotionScreen

/**
 * MainActivity — entry point and composable host.
 *
 * ── Responsibilities ──────────────────────────────────────────────────────────
 * 1. Hosts the Compose UI tree.
 * 2. Provides MotionViewModel via viewModel() (survives configuration changes).
 * 3. Sends Connect / Disconnect events based on composition lifecycle.
 * 4. Collects UiEffect to show Snackbar messages.
 *
 * ── No business logic here ────────────────────────────────────────────────────
 * All logic lives in MotionViewModel. MainActivity is purely a bridge between
 * the Android Activity lifecycle and the composable UI tree.
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            MaterialTheme {
                val viewModel: MotionViewModel = viewModel()
                val uiState by viewModel.uiState.collectAsState()
                val snackbarHostState = remember { SnackbarHostState() }

                // ── Connect on enter, Disconnect on leave ─────────────────────
                // DisposableEffect(Unit) runs the block once on composition entry.
                // The onDispose lambda runs when the composable leaves composition
                // (Activity going to background / being destroyed).
                // This is the idiomatic Compose way to handle lifecycle-bound actions.
                DisposableEffect(Unit) {
                    viewModel.onEvent(MotionUiEvent.Connect)
                    onDispose {
                        viewModel.onEvent(MotionUiEvent.Disconnect)
                    }
                }

                // ── Collect UiEffect (Snackbar messages) ──────────────────────
                // SharedFlow is collected here — each emission shows a Snackbar.
                // LaunchedEffect on the effect flow key ensures collection restarts
                // if the flow object changes (it won't here, but it's correct Compose style).
                LaunchedEffect(viewModel.uiEffect) {
                    viewModel.uiEffect.collect { effect ->
                        when (effect) {
                            is MotionUiEffect.ShowError ->
                                snackbarHostState.showSnackbar(effect.message)
                        }
                    }
                }

                // ── Stateless screen composable ────────────────────────────────
                MotionScreen(
                    uiState = uiState,
                    snackbarHostState = snackbarHostState,
                )
            }
        }
    }

}
