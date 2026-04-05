// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatorc

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import com.myoem.bmicalculatorc.ui.BmiCalScreen
import com.myoem.bmicalculatorc.ui.theme.BMICalculatorCTheme

/**
 * Single-activity host for BMICalculatorC.
 *
 * This file is IDENTICAL in structure to BMICalculatorB's MainActivity.
 * There is zero binder code here, and — unlike App B — there is also zero
 * JNI anywhere in this entire app. The ViewModel handles service lookup via
 * standard Java AIDL.
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val viewModel = ViewModelProvider(this)[BmiCalCViewModel::class.java]

        setContent {
            BMICalculatorCTheme {
                BmiCalScreen(viewModel = viewModel)
            }
        }
    }
}
