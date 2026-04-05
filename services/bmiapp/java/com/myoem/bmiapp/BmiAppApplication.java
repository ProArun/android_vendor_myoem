// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmiapp;

import android.app.Application;
import android.content.Intent;

/**
 * Application entry point for BmiSystemService app.
 *
 * Because android:persistent="true", this process is started by ActivityManager
 * shortly after boot and kept alive indefinitely. Starting BmiAppService from
 * here ensures the binder registration happens at boot without needing an RC
 * file or init.d script.
 */
public class BmiAppApplication extends Application {

    @Override
    public void onCreate() {
        super.onCreate();
        startService(new Intent(this, BmiAppService.class));
    }
}
