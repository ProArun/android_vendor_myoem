// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// build.gradle.kts — Gradle/Maven build for safemode_library AAR.
//
// This file is used for STANDALONE builds outside the AOSP tree:
//   - CI/CD pipelines that produce a versioned AAR artifact
//   - Publishing to a private Maven repository (Artifactory, GitHub Packages, etc.)
//   - Third-party app developers who consume the library via Gradle dependency
//
// Usage (from this directory):
//   ./gradlew assembleRelease          # build AAR
//   ./gradlew publishToMavenLocal      # publish to ~/.m2/repository
//   ./gradlew publish                  # publish to remote repository

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("maven-publish")
}

// ── Library coordinates ──────────────────────────────────────────────────────
// These become the Maven artifact coordinates:
//   implementation("com.myoem:safemode-library:1.0.0")
group   = "com.myoem"
version = "1.0.0"

android {
    namespace  = "com.myoem.safemode"
    compileSdk = 34   // Android 14 — compile against latest APIs

    defaultConfig {
        minSdk = 33   // Android 13 / AAOS Tiramisu minimum
        // No targetSdk for libraries — determined by consuming app

        // AAR artifact ID (used in Maven publish block below)
        // This sets the base name of the output AAR file.
        aarMetadata {
            minCompileSdk = 33
        }
    }

    // ── AIDL ────────────────────────────────────────────────────────────────
    // Enable AIDL processing for the ISafeModeService / ISafeModeCallback stubs.
    // The AIDL files in src/main/aidl/ are compiled into Java Binder classes.
    buildFeatures {
        aidl = true
    }

    // ── Source sets ─────────────────────────────────────────────────────────
    // Standard layout: src/main/java, src/main/aidl, src/main/res
    // No changes needed — AGP auto-detects the standard layout.

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildTypes {
        release {
            isMinifyEnabled = false  // Libraries should never minify themselves
        }
    }

    // ── Publishing variant ───────────────────────────────────────────────────
    publishing {
        // Publish the release variant — that's the AAR artifact app developers
        // will consume. The debug variant is not published.
        singleVariant("release") {
            withSourcesJar()   // include -sources.jar for IDE source attachment
            withJavadocJar()   // include -javadoc.jar for documentation
        }
    }
}

// ── Dependencies ─────────────────────────────────────────────────────────────
dependencies {
    // kotlin-stdlib is automatically added by the Kotlin plugin — no explicit dep needed.

    // ── IMPORTANT: Hidden API access ──────────────────────────────────────────
    // SafeModeManager uses android.os.ServiceManager (hidden API) via reflection.
    // For Gradle builds targeting standard SDK, the reflection approach in
    // SafeModeManager.getServiceBinder() avoids compile-time hidden API errors.
    //
    // If you have access to the Android SDK with hidden APIs (e.g., a custom SDK
    // built from AOSP source), you can replace the reflection path with a direct
    // compile dependency. For public distribution, reflection is the correct choice.
    //
    // No additional dependency needed — reflection is a runtime operation.
}

// ── Maven publishing ─────────────────────────────────────────────────────────
publishing {
    publications {
        create<MavenPublication>("release") {
            groupId    = "com.myoem"
            artifactId = "safemode-library"
            version    = project.version.toString()

            // afterEvaluate is required because the release component is only
            // available after the android {} block has been fully evaluated.
            afterEvaluate {
                from(components["release"])
            }

            pom {
                name.set("SafeMode Library")
                description.set(
                    "Android library for receiving vehicle SafeMode state from " +
                    "the safemoded system service. Subscribes to speed, gear, " +
                    "and fuel level via the AIDL Binder interface."
                )
                url.set("https://github.com/myoem/safemode-library")

                licenses {
                    license {
                        name.set("Apache License 2.0")
                        url.set("https://www.apache.org/licenses/LICENSE-2.0")
                    }
                }

                developers {
                    developer {
                        id.set("myoem")
                        name.set("MyOEM Android Team")
                        email.set("android@myoem.com")
                    }
                }
            }
        }
    }

    repositories {
        // ── Local Maven repository (~/.m2) ─────────────────────────────────
        // Run: ./gradlew publishToMavenLocal
        // Then add mavenLocal() to consuming app's settings.gradle.kts
        mavenLocal()

        // ── Remote repository ───────────────────────────────────────────────
        // Uncomment and configure for CI/CD publishing:
        // maven {
        //     name = "GitHubPackages"
        //     url = uri("https://maven.pkg.github.com/myoem/safemode-library")
        //     credentials {
        //         username = System.getenv("GITHUB_ACTOR")
        //         password = System.getenv("GITHUB_TOKEN")
        //     }
        // }
    }
}
