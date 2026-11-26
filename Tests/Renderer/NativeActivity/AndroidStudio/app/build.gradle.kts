import com.android.build.api.artifact.SingleArtifact
import javax.inject.Inject

plugins {
    id("com.android.application") version "9.4.1"
}

abstract class StageForgeAssets : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val pathStatement: RegularFileProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val gpuData: RegularFileProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val gpuConfig: RegularFileProperty

    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val compiledShaders: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @get:Inject
    abstract val files: FileSystemOperations

    @TaskAction
    fun stage() {
        files.sync {
            into(outputDirectory)
            from(pathStatement)
            into("GPUCfg") {
                from(gpuData) { rename { "gpu.data" } }
                from(gpuConfig)
            }
            into("CompiledShaders") {
                from(compiledShaders)
            }
        }
    }
}

val panguLinkMode = providers.gradleProperty("panguLinkMode").orElse("Monolithic").get()
require(panguLinkMode == "Monolithic" || panguLinkMode == "Modular") {
    "panguLinkMode must be Monolithic or Modular"
}

val nativeActivity = rootProject.layout.projectDirectory.dir("..")
val engine = rootProject.layout.projectDirectory.dir("../../../..")
val indexedMesh = providers.gradleProperty("panguIndexedMesh").orElse("false").get().toBooleanStrict()
val meshTransform = providers.gradleProperty("panguMeshTransform").orElse("false").get().toBooleanStrict()
require(!(meshTransform && indexedMesh)) { "Choose one indexed mesh fixture" }
val modeBuildRoot = engine.dir("Build/Android/Tests/NativeActivity/Gradle/$panguLinkMode")
val gradleBuildRoot = when {
    meshTransform -> modeBuildRoot.dir("MeshTransform")
    indexedMesh -> modeBuildRoot.dir("IndexedMesh")
    else -> modeBuildRoot
}
layout.buildDirectory.set(gradleBuildRoot.dir("app"))

val forge = engine.dir("Source/ThirdParty/TheForge/Repo")

android {
    namespace = "com.pangulabs.nativeactivitycheck.gradle"
    ndkVersion = "30.0.16248370"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = namespace
        minSdk = 29
        targetSdk = 29
        versionCode = 1
        versionName = "1.0"
        ndk.abiFilters += "arm64-v8a"
        externalNativeBuild.cmake {
            arguments += listOf(
                "-DANDROID_STL=c++_shared",
                "-DBUILD_TESTING=ON",
                "-DPANGU_BUILD_ENGINE_TESTS=ON",
                "-DPANGU_BUILD_FORGE_LINK_CHECK=ON",
                "-DPANGU_BUILD_NATIVE_ACTIVITY_TRIANGLE=ON",
                "-DPANGU_BUILD_NATIVE_ACTIVITY_INDEXED_MESH=${if (indexedMesh) "ON" else "OFF"}",
                "-DPANGU_BUILD_NATIVE_ACTIVITY_MESH_TRANSFORM=${if (meshTransform) "ON" else "OFF"}",
                "-DPANGU_BUILD_TRIANGLE_SHADER_CHECK=ON",
                "-DPANGU_FORGE_USE_PREFAB_SWAPPY=ON",
                "-DPANGU_LINK_MODE=$panguLinkMode",
            )
            targets += "PanguNativeActivityCheck"
        }
    }

    flavorDimensions += "swappy"
    productFlavors {
        create("agdk") {
            dimension = "swappy"
            applicationIdSuffix = ".agdk"
        }
    }

    externalNativeBuild.cmake {
        path = engine.file("CMakeLists.txt").asFile
        version = "4.1.2"
        buildStagingDirectory = gradleBuildRoot.dir("CMake").asFile
    }
    buildFeatures.prefab = true
    packaging.jniLibs.useLegacyPackaging = true
}

androidComponents.onVariants { variant ->
    val nativeShaderRoot = gradleBuildRoot.dir("NativeShaders/${variant.name}")
    checkNotNull(variant.externalNativeBuild).arguments.add(
        "-DPANGU_NATIVE_ACTIVITY_SHADER_OUTPUT_ROOT:PATH=${nativeShaderRoot.asFile.invariantSeparatorsPath}",
    )
    val stageForgeAssets = tasks.register<StageForgeAssets>(variant.computeTaskName("stage", "ForgeAssets")) {
        // 合并原生库的产物依赖该变体的 CMake 目标，后者同时生成着色器。
        dependsOn(variant.artifacts.get(SingleArtifact.MERGED_NATIVE_LIBS))
        pathStatement.set(nativeActivity.file("PathStatement.txt"))
        gpuData.set(forge.file("Common_3/OS/Android/android_gpu.data"))
        gpuConfig.set(nativeActivity.file("GPUCfg/gpu.cfg"))
        compiledShaders.set(nativeShaderRoot.dir("CompiledShaders"))
        outputDirectory.set(layout.buildDirectory.dir("generated/forgeAssets/${variant.name}"))
    }
    variant.sources.assets?.addGeneratedSourceDirectory(stageForgeAssets, StageForgeAssets::outputDirectory)
}

dependencies {
    val swappyAar = providers.gradleProperty("panguSwappyAar").orNull
    add("agdkImplementation", if (swappyAar == null) "androidx.games:games-frame-pacing:2.1.3" else files(swappyAar))
}
