plugins {
    kotlin("jvm")
    `java-library`
}

dependencies {
    // JNA for native library access
    api("net.java.dev.jna:jna:5.14.0")

    // Logging
    implementation("org.slf4j:slf4j-api:2.0.9")

    // Reactive Streams
    implementation("io.projectreactor:reactor-core:3.5.11")

    // Test dependencies
    testImplementation("ch.qos.logback:logback-classic:1.5.18")
}

tasks.jar {
    manifest {
        attributes(
            "Implementation-Title" to "Gopher MCP Kotlin SDK",
            "Implementation-Version" to project.version
        )
    }
}

// Copy native libraries to resources
tasks.register<Copy>("copyNativeLibraries") {
    from("../../build/lib")
    into("src/main/resources/native")
    include("*.so", "*.dylib", "*.dll")
}

tasks.processResources {
    dependsOn("copyNativeLibraries")
}
