// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// settings.gradle.kts — Gradle project settings for the standalone AAR build.
//
// This file makes vendor/myoem/libs/safemode/ a standalone Gradle project
// that can be opened directly in Android Studio or built via CI without
// the AOSP tree.

pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "safemode-library"
include(":safemode-library")

// Map the ":safemode-library" module to the current directory
// (where build.gradle.kts lives). This avoids a nested subdirectory.
project(":safemode-library").projectDir = rootDir
