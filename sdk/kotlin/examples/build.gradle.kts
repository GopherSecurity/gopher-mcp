plugins {
    kotlin("jvm")
    application
}

dependencies {
    implementation(project(":gopher-mcp-kotlin"))

    // MCP SDK
    implementation("io.modelcontextprotocol.sdk:mcp:0.12.1")

    // Logging
    implementation("ch.qos.logback:logback-classic:1.5.18")

    // Reactive Streams
    implementation("io.projectreactor:reactor-core:3.5.11")
    implementation("io.projectreactor:reactor-kotlin-extensions:1.2.2")
}

application {
    mainClass.set("com.gopher.mcp.example.MainKt")
}
