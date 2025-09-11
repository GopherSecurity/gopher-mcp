import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    kotlin("jvm") version "1.9.22" apply false
    id("com.diffplug.spotless") version "6.25.0"
}

allprojects {
    group = "com.gopher.mcp"
    version = "1.0.0-SNAPSHOT"

    repositories {
        mavenCentral()
    }
}

subprojects {
    apply(plugin = "org.jetbrains.kotlin.jvm")
    apply(plugin = "com.diffplug.spotless")

    dependencies {
        val implementation by configurations
        val testImplementation by configurations

        // Kotlin
        implementation(kotlin("stdlib"))
        implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
        implementation("org.jetbrains.kotlinx:kotlinx-coroutines-reactor:1.7.3")

        // Testing
        testImplementation("org.junit.jupiter:junit-jupiter:5.10.0")
        testImplementation("org.junit.jupiter:junit-jupiter-api:5.10.0")
        testImplementation("org.junit.jupiter:junit-jupiter-engine:5.10.0")
        testImplementation("io.mockk:mockk:1.13.8")
        testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.7.3")
    }

    tasks.withType<KotlinCompile> {
        kotlinOptions {
            jvmTarget = "17"
            freeCompilerArgs = listOf("-Xjsr305=strict")
        }
    }

    tasks.withType<JavaCompile> {
        sourceCompatibility = "17"
        targetCompatibility = "17"
    }

    tasks.withType<Test> {
        useJUnitPlatform()
    }

    configure<com.diffplug.gradle.spotless.SpotlessExtension> {
        kotlin {
            target("**/*.kt")
            ktlint("1.1.1")
        }
        kotlinGradle {
            target("**/*.gradle.kts")
            ktlint("1.1.1")
        }
    }
}
