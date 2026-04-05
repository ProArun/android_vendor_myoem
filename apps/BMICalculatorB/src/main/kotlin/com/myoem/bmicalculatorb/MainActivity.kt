// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatorb

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import com.myoem.bmicalculatorb.ui.BmiCalScreen
import com.myoem.bmicalculatorb.ui.theme.BMICalculatorBTheme

/**
 * Single-activity host for BMICalculatorB.
 *
 * Compare with BMICalculatorA's MainActivity:
 *   A: calls ServiceManager.checkService() (@hide), passes IBinder to ViewModel
 *   B: no binder code at all — BmiCalManager handles everything internally
 *
 * This is the manager pattern in action. The activity is completely clean.
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val viewModel = ViewModelProvider(this)[BmiCalBViewModel::class.java]

        setContent {
            BMICalculatorBTheme {
                BmiCalScreen(viewModel = viewModel)
            }
        }
    }
}
