// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatora

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import com.myoem.bmicalculatora.ui.BmiCalScreen
import com.myoem.bmicalculatora.ui.theme.BMICalculatorATheme

/**
 * Single-activity host for the Compose UI.
 *
 * Service resolution is now done inside NativeBinder (JNI) via
 * AServiceManager_checkService() on a background thread — this activity
 * no longer touches ServiceManager directly.
 *
 * BMICalculatorA calls bmid and calculatord through libbmicalculator_jni.so,
 * using the NDK binder path (no Java BinderProxy, no BinderProxyTransactListener).
 */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val viewModel = ViewModelProvider(this)[BmiCalViewModel::class.java]

        setContent {
            BMICalculatorATheme {
                BmiCalScreen(viewModel = viewModel)
            }
        }
    }
}
